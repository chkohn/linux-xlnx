/*
 * Xilinx Gamma Correction IP
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/xilinx-v4l2-controls.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"
#include "xilinx-hls-gamma.h"
#include "xilinx-gamma-coeff.h"

#define XGAMMA_LUT_LENGTH	(128)
#define XGAMMA_MIN_HEIGHT	(32)
#define XGAMMA_MAX_HEIGHT	(2160)
#define XGAMMA_DEF_HEIGHT	(720)
#define XGAMMA_MIN_WIDTH	(32)
#define XGAMMA_MAX_WIDTH	(3840)
#define XGAMMA_DEF_WIDTH	(1280)

enum xgamma_video_format {
	XGAMMA_RGB = 0,
};

struct xgamma_dev {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	const struct xvip_video_format *vip_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;

	enum xgamma_video_format vid_fmt;
	const uint8_t *red_lut;
	const uint8_t *green_lut;
	const uint8_t *blue_lut;
	bool  probe_done;

	struct gpio_desc *rst_gpio;
};

static inline u32 xg_read(struct xgamma_dev *xg, u32 reg)
{
	u32 data;

	data = xvip_read(&xg->xvip, reg);
	dev_dbg(xg->xvip.dev,
		"Reading 0x%x from reg offset 0x%x", data, reg);
	return data;
}

static inline void xg_write(struct xgamma_dev *xg, u32 reg, u32 data)
{
	u32 rb;

	dev_dbg(xg->xvip.dev,
		"Writing 0x%x to reg offset 0x%x", data, reg);
	xvip_write(&xg->xvip, reg, data);
	rb = xg_read(xg, reg);
	if (rb != data) {
		dev_dbg(xg->xvip.dev,
		"Wrote 0x%x does not match read back 0x%x", data, rb);
	}
}

static inline struct xgamma_dev *to_xg(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xgamma_dev, xvip.subdev);
}

static struct v4l2_mbus_framefmt *
__xg_get_pad_format(struct xgamma_dev *xg,
			struct v4l2_subdev_pad_config *cfg,
			unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(
					&xg->xvip.subdev, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xg->formats[pad];
	default:
		return NULL;
	}
}

static void xg_set_default_state(struct xgamma_dev *xg)
{
	xg->vid_fmt = XGAMMA_RGB;
}

static void xg_set_lut_entries(struct xgamma_dev *xg,
	const uint8_t *lut,
	const u32 lut_base, const char *component)
{
	int itr;
	u32 lut_offset, lut_data;

	if (!xg->probe_done)
		return;

	lut_offset = lut_base;
	/* Write LUT Entries */
	for (itr = 0; itr < XGAMMA_LUT_LENGTH; itr++) {
		lut_data = (lut[2*itr+1] << 16) | lut[2*itr];
		xg_write(xg, lut_offset, lut_data);
		lut_offset += 4;

	}
}

static int xg_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xgamma_dev *xg = to_xg(subdev);

	if (!enable) {
		dev_info(xg->xvip.dev, "%s : Off", __func__);
		/* Reset the Global IP Reset through PS GPIO */
		gpiod_set_value_cansleep(xg->rst_gpio, 0x1);
		udelay(100);
		gpiod_set_value_cansleep(xg->rst_gpio, 0x0);
		udelay(100);
		return 0;
	}
	dev_info(xg->xvip.dev, "%s : Started", __func__);

	dev_info(xg->xvip.dev, "%s : Setting width %d and height %d",
		__func__, xg->formats[XVIP_PAD_SINK].width,
		xg->formats[XVIP_PAD_SINK].height);
	xg_write(xg, XGAMMA_WIDTH, xg->formats[XVIP_PAD_SINK].width);
	xg_write(xg, XGAMMA_HEIGHT, xg->formats[XVIP_PAD_SINK].height);
	xg_write(xg, XGAMMA_VIDEO_FORMAT, xg->vid_fmt);
	xg_set_lut_entries(xg, xg->red_lut,
				XGAMMA_GAMMA_LUT_0_BASE, "Red");
	xg_set_lut_entries(xg, xg->green_lut,
				XGAMMA_GAMMA_LUT_1_BASE, "Green");
	xg_set_lut_entries(xg, xg->blue_lut,
				XGAMMA_GAMMA_LUT_2_BASE, "Blue");

	/* Start GAMMA Correction LUT Video IP */
	xg_write(xg, XGAMMA_AP_CTRL, 0X81);
	return 0;
}

static const struct v4l2_subdev_video_ops xg_video_ops = {
	.s_stream = xg_s_stream,
};

static int xg_get_format(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	struct xgamma_dev *xg = to_xg(subdev);

	fmt->format = *__xg_get_pad_format(xg, cfg, fmt->pad, fmt->which);
	return 0;
}

static int xg_set_format(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	struct xgamma_dev *xg = to_xg(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xg_get_pad_format(xg, cfg, fmt->pad, fmt->which);
	*__format = fmt->format;

	if (fmt->pad == XVIP_PAD_SINK) {
		if (__format->code != MEDIA_BUS_FMT_RBG888_1X24) {
			dev_err(xg->xvip.dev,
			"%s : Not a supported sink media bus code format",
			__func__);
			__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		}
	}
	__format->width = clamp_t(unsigned int, fmt->format.width,
					XGAMMA_MIN_WIDTH, XGAMMA_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
					XGAMMA_MIN_HEIGHT, XGAMMA_MAX_HEIGHT);

	fmt->format = *__format;
	/* Propogate to Source Pad */
	__format = __xg_get_pad_format(xg, cfg, XVIP_PAD_SOURCE, fmt->which);
	*__format = fmt->format;
	return 0;
}

static int xg_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xgamma_dev *xg = to_xg(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xg->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xg->default_formats[XVIP_PAD_SOURCE];
	return 0;
}

static int xg_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops xg_internal_ops = {
	.open = xg_open,
	.close = xg_close,
};

static const struct v4l2_subdev_pad_ops xg_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xg_get_format,
	.set_fmt = xg_set_format,
};

static const struct v4l2_subdev_ops xg_ops = {
	.video = &xg_video_ops,
	.pad = &xg_pad_ops,
};

static int
select_gamma(s32 value, const uint8_t **coeff)
{

	if (!coeff)
		return -EINVAL;
	if (value <= 0 || value > GAMMA_CURVE_LENGTH)
		return -EINVAL;

	*coeff = *(xgamma_curves + value - 1);
	return 0;
}

static int xg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int rval;
	struct xgamma_dev *xg = container_of(ctrl->handler,
					struct xgamma_dev,
					ctrl_handler);
	dev_info(xg->xvip.dev, "%s called", __func__);
	switch (ctrl->id) {
	case V4L2_CID_XILINX_GAMMA_CORR_RED_GAMMA:
		rval = select_gamma(ctrl->val, &xg->red_lut);
		if (rval != 0) {
			dev_err(xg->xvip.dev, "Invalid Red Gamma");
			return rval;
		}
		dev_info(xg->xvip.dev, "%s: Setting Red Gamma to %d.%d",
					__func__, ctrl->val/10, ctrl->val%10);
		xg_set_lut_entries(xg, xg->red_lut,
			XGAMMA_GAMMA_LUT_0_BASE, "Red");
		break;
	case V4L2_CID_XILINX_GAMMA_CORR_BLUE_GAMMA:
		rval = select_gamma(ctrl->val, &xg->blue_lut);
		if (rval != 0) {
			dev_err(xg->xvip.dev, "Invalid Blue Gamma");
			return rval;
		}
		dev_info(xg->xvip.dev, "%s: Setting Blue Gamma to %d.%d",
					__func__, ctrl->val/10, ctrl->val%10);
		xg_set_lut_entries(xg, xg->blue_lut,
			XGAMMA_GAMMA_LUT_1_BASE, "Blue");
		break;
	case V4L2_CID_XILINX_GAMMA_CORR_GREEN_GAMMA:
		rval = select_gamma(ctrl->val, &xg->green_lut);
		if (rval != 0) {
			dev_err(xg->xvip.dev, "Invalid Green Gamma");
			return -EINVAL;
		}
		dev_info(xg->xvip.dev, "%s: Setting Green Gamma to %d.%d",
					__func__, ctrl->val/10, ctrl->val%10);
		xg_set_lut_entries(xg, xg->green_lut,
			XGAMMA_GAMMA_LUT_2_BASE, "Green");
			break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops xg_ctrl_ops = {
	.s_ctrl = xg_s_ctrl,
};

static struct v4l2_ctrl_config xg_ctrls[] = {
	/* Red Gamma */
	{
		.ops = &xg_ctrl_ops,
		.id = V4L2_CID_XILINX_GAMMA_CORR_RED_GAMMA,
		.name = "Red Gamma Correction(1 = 0.1 & 10 = 1.0)",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 10,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Blue Gamma */
	{
		.ops = &xg_ctrl_ops,
		.id = V4L2_CID_XILINX_GAMMA_CORR_BLUE_GAMMA,
		.name = "Blue Gamma Correction(1 = 0.1 & 10 = 1.0)",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 10,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
	/* Green Gamma */
	{
		.ops = &xg_ctrl_ops,
		.id = V4L2_CID_XILINX_GAMMA_CORR_GREEN_GAMMA,
		.name = "Green Gamma Correction(1 = 0.1 & 10 = 1.0)",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 40,
		.step = 1,
		.def = 10,
		.flags = V4L2_CTRL_FLAG_SLIDER,
	},
};

static const struct media_entity_operations xg_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xg_parse_of(struct xgamma_dev *xg)
{
	struct device *dev = xg->xvip.dev;
	struct device_node *node = dev->of_node;
	const struct xvip_video_format *vip_format;
	struct device_node *ports;
	struct device_node *port;
	u32 port_id = 0;
	int rval;

	ports = of_get_child_by_name(node, "ports");
	if (ports == NULL)
		ports = node;
	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			vip_format = xvip_of_get_format(port);
			if (IS_ERR(vip_format)) {
				dev_err(dev, "Invalid format in DT");
				return PTR_ERR(vip_format);
			}
			rval = of_property_read_u32(port, "reg", &port_id);
			if (rval < 0) {
				dev_err(dev, "No reg in DT");
				return rval;
			}

			if (port_id != 0 && port_id != 1) {
				dev_err(dev, "Invalid reg in DT");
				return -EINVAL;
			}
			xg->vip_formats[port_id] = vip_format;
		}
	}

	/* Reset GPIO */
	xg->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xg->rst_gpio)) {
		dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xg->rst_gpio);
	}
	return 0;
}

static int xg_probe(struct platform_device *pdev)
{
	struct xgamma_dev *xg;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *def_fmt;
	int rval, itr;

	dev_info(&pdev->dev, "Gamma LUT Probe Started");
	xg = devm_kzalloc(&pdev->dev, sizeof(*xg), GFP_KERNEL);
	if (!xg)
		return -ENOMEM;
	xg->xvip.dev = &pdev->dev;
	rval = xg_parse_of(xg);
	if (rval < 0)
		return rval;

	/* Reset and initialize the core */
	dev_info(xg->xvip.dev, "Reset Gamma\n");
	/* Reset the Global IP Reset through a PS GPIO */
	gpiod_set_value_cansleep(xg->rst_gpio, 0x0);
	udelay(100);
	rval = xvip_init_resources(&xg->xvip);

	/* Init V4L2 subdev */
	subdev = &xg->xvip.subdev;
	v4l2_subdev_init(subdev, &xg_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xg_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Default Formats Initialization */
	xg_set_default_state(xg);
	def_fmt = &xg->default_formats[XVIP_PAD_SINK];
	/* GAMMA LUT IP only to be supported for RGB */
	if (xg->vip_formats[XVIP_PAD_SINK]->code ==
				MEDIA_BUS_FMT_RBG888_1X24)
		def_fmt->code = xg->vip_formats[XVIP_PAD_SINK]->code;
	else
		def_fmt->code = MEDIA_BUS_FMT_RBG888_1X24;
	def_fmt->field = V4L2_FIELD_NONE;
	def_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	def_fmt->width = XGAMMA_DEF_WIDTH;
	def_fmt->height = XGAMMA_DEF_HEIGHT;
	xg->formats[XVIP_PAD_SINK] = *def_fmt;

	def_fmt = &xg->default_formats[XVIP_PAD_SOURCE];
	*def_fmt = xg->default_formats[XVIP_PAD_SINK];
	xg->formats[XVIP_PAD_SOURCE] = *def_fmt;

	xg->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xg->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xg_media_ops;
	rval = media_entity_pads_init(&subdev->entity, 2, xg->pads);
	if (rval < 0)
		goto media_error;

	/* V4L2 Controls */
	v4l2_ctrl_handler_init(&xg->ctrl_handler, ARRAY_SIZE(xg_ctrls));
	for (itr = 0; itr < ARRAY_SIZE(xg_ctrls); itr++) {
		v4l2_ctrl_new_custom(&xg->ctrl_handler,
			&xg_ctrls[itr], NULL);
	}
	if (xg->ctrl_handler.error) {
		dev_err(&pdev->dev, "Failed to add V4L2 controls");
		rval = xg->ctrl_handler.error;
		goto ctrl_error;
	}
	subdev->ctrl_handler = &xg->ctrl_handler;
	rval = v4l2_ctrl_handler_setup(&xg->ctrl_handler);
	if (rval < 0) {
		dev_err(&pdev->dev, "Failed to setup control handler");
		goto  ctrl_error;
	}

	platform_set_drvdata(pdev, xg);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev");
		goto v4l2_subdev_error;
	}
	xg->probe_done = true;
	dev_info(&pdev->dev,
		"%s : GAMMA Correction LUT Successful", __func__);
	return 0;
ctrl_error:
	v4l2_ctrl_handler_free(&xg->ctrl_handler);
v4l2_subdev_error:
	media_entity_cleanup(&subdev->entity);
media_error:
	xvip_cleanup_resources(&xg->xvip);
	return rval;
}


static int xg_remove(struct platform_device *pdev)
{
	struct xgamma_dev *xg = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xg->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	/* Add entry to cleanup v4l2 control handle */
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xg->xvip);
	return 0;
}

static const struct of_device_id xg_of_id_table[] = {
	{.compatible = "xlnx,v-gamma-lut-v1.0"},
	{ }
};
MODULE_DEVICE_TABLE(of, xg_of_id_table);

static struct platform_driver xg_driver = {
	.driver = {
		.name = "xilinx-gamma-lut-v1.0",
		.of_match_table = xg_of_id_table,
	},
	.probe = xg_probe,
	.remove = xg_remove,

};

module_platform_driver(xg_driver);
MODULE_DESCRIPTION("Xilinx Gamma Correction LUT Driver");
MODULE_LICENSE("GPL v2");
