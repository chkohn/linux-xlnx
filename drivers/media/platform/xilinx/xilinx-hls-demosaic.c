/*
 * Xilinx Demosaic IP
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
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"
#include "xilinx-demosaic.h"

#define XDEMOSAIC_MIN_HEIGHT	(32)
#define XDEMOSAIC_MAX_HEIGHT	(2160)
#define XDEMOSAIC_DEF_HEIGHT	(720)
#define XDEMOSAIC_MIN_WIDTH	(32)
#define XDEMOSAIC_MAX_WIDTH	(3840)
#define XDEMOSAIC_DEF_WIDTH	(1280)

enum xdmsc_video_format {
	XDEMOSAIC_RGB = 0,
};

enum xdmsc_bayer_format {
	XDEMOSAIC_RGGB = 0,
	XDEMOSAIC_GRBG,
	XDEMOSAIC_GBRG,
	XDEMOSAIC_BGGR,
};

struct xdmsc_dev {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];
	const struct xvip_video_format *vip_formats[2];

	enum xdmsc_video_format vid_fmt;
	enum xdmsc_bayer_format bayer_fmt;

	struct gpio_desc *rst_gpio;
};

static inline u32 xdmsc_read(struct xdmsc_dev *xdmsc, u32 reg)
{
	u32 data;

	data = xvip_read(&xdmsc->xvip, reg);
	dev_dbg(xdmsc->xvip.dev,
		"Reading 0x%x from reg offset 0x%x", data, reg);
	return data;
}

static inline void xdmsc_write(struct xdmsc_dev *xdmsc, u32 reg, u32 data)
{
	u32 rb;

	dev_dbg(xdmsc->xvip.dev,
		"Writing 0x%x to reg offset 0x%x", data, reg);
	xvip_write(&xdmsc->xvip, reg, data);
	rb = xdmsc_read(xdmsc, reg);
	if (rb != data) {
		dev_err(xdmsc->xvip.dev,
		"Wrote 0x%x does not match read back 0x%x", data, rb);
	}
}

static inline struct xdmsc_dev *to_xdmsc(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xdmsc_dev, xvip.subdev);
}

static struct v4l2_mbus_framefmt
*__xdmsc_get_pad_format(struct xdmsc_dev *xdmsc,
		struct v4l2_subdev_pad_config *cfg,
			unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xdmsc->xvip.subdev,
								cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xdmsc->formats[pad];
	default:
		return NULL;
	}
}



static int xdmsc_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);

	if (!enable) {
		dev_info(xdmsc->xvip.dev, "%s : Off", __func__);
		/* Reset the Global IP Reset through PS GPIO */
		gpiod_set_value_cansleep(xdmsc->rst_gpio, 0x1);
		udelay(100);
		gpiod_set_value_cansleep(xdmsc->rst_gpio, 0x0);
		udelay(100);
		return 0;
	}
	dev_info(xdmsc->xvip.dev, "%s : Started", __func__);

	dev_info(xdmsc->xvip.dev, "%s : Setting width %d and height %d",
		__func__, xdmsc->formats[XVIP_PAD_SINK].width,
		xdmsc->formats[XVIP_PAD_SINK].height);
	xdmsc_write(xdmsc, XDEMOSAIC_WIDTH,
			xdmsc->formats[XVIP_PAD_SINK].width);
	xdmsc_write(xdmsc, XDEMOSAIC_HEIGHT,
			xdmsc->formats[XVIP_PAD_SINK].height);
	xdmsc_write(xdmsc, XDEMOSAIC_OUTPUT_VIDEO_FORMAT, xdmsc->vid_fmt);
	xdmsc_write(xdmsc, XDEMOSAIC_INPUT_BAYER_FORMAT, xdmsc->bayer_fmt);

	/* Start DEMOSAIC Video IP */
	xdmsc_write(xdmsc, XDEMOSAIC_AP_CTRL, 0X81);
	return 0;
}

static const struct v4l2_subdev_video_ops xdmsc_video_ops = {
	.s_stream = xdmsc_s_stream,
};

static int xdmsc_get_format(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);

	fmt->format = *__xdmsc_get_pad_format(xdmsc, cfg, fmt->pad, fmt->which);
	return 0;
}

static bool
xdmsc_is_format_bayer(struct xdmsc_dev *xdmsc, u32 code)
{
	bool rval = false;

	switch (code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		xdmsc->bayer_fmt = XDEMOSAIC_RGGB;
		rval = true;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		xdmsc->bayer_fmt = XDEMOSAIC_GRBG;
		rval = true;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		xdmsc->bayer_fmt = XDEMOSAIC_GBRG;
		rval = true;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		xdmsc->bayer_fmt = XDEMOSAIC_BGGR;
		rval = true;
		break;
	default:
		dev_err(xdmsc->xvip.dev, "Unsupported format for Sink Pad");
	}
	return rval;
}

static int xdmsc_set_format(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *fmt)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xdmsc_get_pad_format(xdmsc, cfg, fmt->pad, fmt->which);
	*__format = fmt->format;

	__format->width = clamp_t(unsigned int, fmt->format.width,
				XDEMOSAIC_MIN_WIDTH, XDEMOSAIC_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				XDEMOSAIC_MIN_HEIGHT, XDEMOSAIC_MAX_HEIGHT);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		if (__format->code != MEDIA_BUS_FMT_RBG888_1X24) {
			dev_err(xdmsc->xvip.dev,
			"%s : Not a supported sink media bus code format",
			__func__);
			__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		}
	}

	if (fmt->pad == XVIP_PAD_SINK) {
		if (!xdmsc_is_format_bayer(xdmsc, __format->code)) {
			dev_info(xdmsc->xvip.dev, "Setting Sink Pad to default bayer code RGGB");
			__format->code = MEDIA_BUS_FMT_SRGGB8_1X8;
		}
	}

	fmt->format = *__format;
	return 0;
}

static int xdmsc_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xdmsc->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xdmsc->default_formats[XVIP_PAD_SOURCE];
	return 0;
}

static int xdmsc_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops xdmsc_internal_ops = {
	.open = xdmsc_open,
	.close = xdmsc_close,
};

static const struct v4l2_subdev_pad_ops xdmsc_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xdmsc_get_format,
	.set_fmt = xdmsc_set_format,
};

static const struct v4l2_subdev_ops xdmsc_ops = {
	.video = &xdmsc_video_ops,
	.pad = &xdmsc_pad_ops,
};



static const struct media_entity_operations xdmsc_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xdmsc_parse_of(struct xdmsc_dev *xdmsc)
{
	struct device *dev = xdmsc->xvip.dev;
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
			xdmsc->vip_formats[port_id] = vip_format;
		}
	}

	/* Reset GPIO */
	xdmsc->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xdmsc->rst_gpio)) {
		dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xdmsc->rst_gpio);
	}
	return 0;
}

static int xdmsc_probe(struct platform_device *pdev)
{
	struct xdmsc_dev *xdmsc;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *def_fmt;
	int rval;

	dev_info(&pdev->dev, "Video Demosaic Probe Started");
	xdmsc = devm_kzalloc(&pdev->dev, sizeof(*xdmsc), GFP_KERNEL);
	if (!xdmsc)
		return -ENOMEM;
	xdmsc->xvip.dev = &pdev->dev;
	rval = xdmsc_parse_of(xdmsc);
	if (rval < 0)
		return rval;

	dev_info(xdmsc->xvip.dev, "Reset Demosaic\n");
	/* Reset the Global IP Reset through a PS GPIO */
	gpiod_set_value_cansleep(xdmsc->rst_gpio, 0x0);
	udelay(100);

	rval = xvip_init_resources(&xdmsc->xvip);

	/* Init V4L2 subdev */
	subdev = &xdmsc->xvip.subdev;
	v4l2_subdev_init(subdev, &xdmsc_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xdmsc_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/*
	 * Default Formats Initialization
	 * DEMOSAIC IP only to be supported for
	 * Bayer In and RGB Out
	 */
	def_fmt = &xdmsc->default_formats[XVIP_PAD_SINK];
	def_fmt->field = V4L2_FIELD_NONE;
	def_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	def_fmt->width = XDEMOSAIC_DEF_WIDTH;
	def_fmt->height = XDEMOSAIC_DEF_HEIGHT;
	if (xdmsc_is_format_bayer(xdmsc,
				xdmsc->vip_formats[XVIP_PAD_SINK]->code)) {
		def_fmt->code = xdmsc->vip_formats[XVIP_PAD_SINK]->code;

	} else {
		dev_info(xdmsc->xvip.dev,
			"Setting Sink Pad to default bayer code RGGB");
		def_fmt->code = MEDIA_BUS_FMT_SRGGB8_1X8;
	}
	xdmsc->formats[XVIP_PAD_SINK] = *def_fmt;

	def_fmt = &xdmsc->default_formats[XVIP_PAD_SOURCE];
	*def_fmt = xdmsc->default_formats[XVIP_PAD_SINK];
	if (xdmsc->vip_formats[XVIP_PAD_SOURCE]->code ==
				MEDIA_BUS_FMT_RBG888_1X24)
		def_fmt->code = xdmsc->vip_formats[XVIP_PAD_SOURCE]->code;
	else
		def_fmt->code = MEDIA_BUS_FMT_RBG888_1X24;
	xdmsc->formats[XVIP_PAD_SOURCE] = *def_fmt;

	xdmsc->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xdmsc->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xdmsc_media_ops;
	rval = media_entity_pads_init(&subdev->entity, 2, xdmsc->pads);
	if (rval < 0)
		goto media_error;

	platform_set_drvdata(pdev, xdmsc);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev");
		goto v4l2_subdev_error;
	}
	dev_info(&pdev->dev,
		"%s : Demosaic Probe Successful", __func__);
	return 0;

v4l2_subdev_error:
	media_entity_cleanup(&subdev->entity);
media_error:
	xvip_cleanup_resources(&xdmsc->xvip);
	return rval;
}


static int xdmsc_remove(struct platform_device *pdev)
{
	struct xdmsc_dev *xdmsc = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xdmsc->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	/* Add entry to cleanup v4l2 control handle */
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xdmsc->xvip);
	return 0;
}

static const struct of_device_id xdmsc_of_id_table[] = {
	{.compatible = "xlnx,v-demosaic-v1.0"},
	{ }
};
MODULE_DEVICE_TABLE(of, xdmsc_of_id_table);

static struct platform_driver xdmsc_driver = {
	.driver = {
		.name = "xilinx-demosaic",
		.of_match_table = xdmsc_of_id_table,
	},
	.probe = xdmsc_probe,
	.remove = xdmsc_remove,

};

module_platform_driver(xdmsc_driver);
MODULE_DESCRIPTION("Xilinx Demosaic HLS IP Driver");
MODULE_LICENSE("GPL v2");
