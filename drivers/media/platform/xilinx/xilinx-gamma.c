/*
 * Xilinx Gamma Correction
 *
 * Copyright (C) 2013 Xilinx, Inc.
 *
 * Author: Hyun Woo Kwon <hyunk@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-controls.h"
#include "xilinx-vip.h"

#define XGAMMA_MIN_WIDTH				32
#define XGAMMA_MAX_WIDTH				7680
#define XGAMMA_MIN_HEIGHT				32
#define XGAMMA_MAX_HEIGHT				7680

#define XGAMMA_PAD_SINK					0
#define XGAMMA_PAD_SOURCE				1

#define XGAMMA_GAMMA_TABLE_UPDATE			0x100
#define XGAMMA_GAMMA_ADDR_DATA				0x104

/**
 * struct xgamma_device - Xilinx GAMMA device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @vip_format: Xilinx Video IP format
 * @format: V4L2 media bus format at the source pad
 * @ctrl_handler: control handler
 */
struct xgamma_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_mbus_framefmt format;
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xgamma_device *to_gamma(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xgamma_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xgamma_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xgamma_device *xgamma = to_gamma(subdev);
	const u32 width = xgamma->format.width;
	const u32 height = xgamma->format.height;

	if (!enable) {
		xvip_write(&xgamma->xvip, XVIP_CTRL_CONTROL,
			   XVIP_CTRL_CONTROL_SW_RESET);
		xvip_write(&xgamma->xvip, XVIP_CTRL_CONTROL, 0);
		return 0;
	}

	xvip_write(&xgamma->xvip, XVIP_ACTIVE_SIZE,
		   (height << XVIP_ACTIVE_VSIZE_SHIFT) |
		   (width << XVIP_ACTIVE_HSIZE_SHIFT));

	xvip_write(&xgamma->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xgamma_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct xgamma_device *xgamma = to_gamma(subdev);

	if (code->index)
		return -EINVAL;

	code->code = xgamma->vip_format->code;

	return 0;
}

static int xgamma_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XGAMMA_PAD_SINK) {
		fse->min_width = XGAMMA_MIN_WIDTH;
		fse->max_width = XGAMMA_MAX_WIDTH;
		fse->min_height = XGAMMA_MIN_HEIGHT;
		fse->max_height = XGAMMA_MAX_HEIGHT;
	} else {
		/* The size on the source pad is fixed and always identical to
		 * the size on the sink pad.
		 */
		fse->min_width = format->width;
		fse->max_width = format->width;
		fse->min_height = format->height;
		fse->max_height = format->height;
	}

	return 0;
}

static struct v4l2_mbus_framefmt *
__xgamma_get_pad_format(struct xgamma_device *xgamma,
			struct v4l2_subdev_fh *fh,
			unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xgamma->format;
	default:
		return NULL;
	}
}

static int xgamma_get_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct xgamma_device *xgamma = to_gamma(subdev);

	fmt->format = *__xgamma_get_pad_format(xgamma, fh, fmt->pad,
					       fmt->which);

	return 0;
}

static int xgamma_set_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct xgamma_device *xgamma = to_gamma(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xgamma_get_pad_format(xgamma, fh, fmt->pad, fmt->which);

	if (fmt->pad == XGAMMA_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	__format->code = xgamma->vip_format->code;
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XGAMMA_MIN_WIDTH, XGAMMA_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XGAMMA_MIN_HEIGHT, XGAMMA_MAX_HEIGHT);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = __xgamma_get_pad_format(xgamma, fh, XGAMMA_PAD_SOURCE,
					   fmt->which);
	*__format = fmt->format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xgamma_init_formats - Initialize formats on all pads
 * @subdev: gammaper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xgamma_init_formats(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh)
{
	struct xgamma_device *xgamma = to_gamma(subdev);
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));

	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.width = xvip_read(&xgamma->xvip, XVIP_ACTIVE_SIZE) &
			      XVIP_ACTIVE_HSIZE_MASK;
	format.format.height = (xvip_read(&xgamma->xvip, XVIP_ACTIVE_SIZE) &
				XVIP_ACTIVE_VSIZE_MASK) >>
			       XVIP_ACTIVE_VSIZE_SHIFT;
	format.format.field = V4L2_FIELD_NONE;
	format.format.colorspace = V4L2_COLORSPACE_SRGB;

	format.pad = XGAMMA_PAD_SINK;

	xgamma_set_format(subdev, fh, &format);

	format.pad = XGAMMA_PAD_SOURCE;

	xgamma_set_format(subdev, fh, &format);
}

static int xgamma_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xgamma_init_formats(subdev, fh);

	return 0;
}

static int xgamma_close(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xgamma_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xgamma_device *xgamma = container_of(ctrl->handler,
						    struct xgamma_device,
						    ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_GAMMA_SWITCH_LUT:
		xvip_write(&xgamma->xvip, XGAMMA_GAMMA_TABLE_UPDATE, 1);
		return 0;
	case V4L2_CID_XILINX_GAMMA_UPDATE_LUT:
		xvip_write(&xgamma->xvip, XGAMMA_GAMMA_ADDR_DATA, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xgamma_ctrl_ops = {
	.s_ctrl	= xgamma_s_ctrl,
};

static struct v4l2_subdev_core_ops xgamma_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xgamma_video_ops = {
	.s_stream = xgamma_s_stream,
};

static struct v4l2_subdev_pad_ops xgamma_pad_ops = {
	.enum_mbus_code		= xgamma_enum_mbus_code,
	.enum_frame_size	= xgamma_enum_frame_size,
	.get_fmt		= xgamma_get_format,
	.set_fmt		= xgamma_set_format,
};

static struct v4l2_subdev_ops xgamma_ops = {
	.core   = &xgamma_core_ops,
	.video  = &xgamma_video_ops,
	.pad    = &xgamma_pad_ops,
};

static const struct v4l2_subdev_internal_ops xgamma_internal_ops = {
	.open	= xgamma_open,
	.close	= xgamma_close,
};

/*
 * Control Configs
 */

static const struct v4l2_ctrl_config xgamma_switch_lut = {
	.ops	= &xgamma_ctrl_ops,
	.id	= V4L2_CID_XILINX_GAMMA_SWITCH_LUT,
	.name	= "Gamma: Switch to the inactive LUT",
	.type	= V4L2_CTRL_TYPE_BUTTON,
};

static const struct v4l2_ctrl_config xgamma_update_lut = {
	.ops	= &xgamma_ctrl_ops,
	.id	= V4L2_CID_XILINX_GAMMA_UPDATE_LUT,
	.name	= "Gamma: Update the inactive LUT",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= 0x7fffffff,
	.step	= 1,
	.def	= 0,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xgamma_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xgamma_pm_suspend(struct device *dev)
{
	struct xgamma_device *xgamma = dev_get_drvdata(dev);

	xvip_write(&xgamma->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xgamma_pm_resume(struct device *dev)
{
	struct xgamma_device *xgamma = dev_get_drvdata(dev);

	xvip_write(&xgamma->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xgamma_pm_suspend	NULL
#define xgamma_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xgamma_parse_of(struct xgamma_device *xgamma)
{
	struct device_node *node = xgamma->xvip.dev->of_node;

	xgamma->vip_format = xvip_of_get_format(node);
	if (xgamma->vip_format == NULL) {
		dev_err(xgamma->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	return 0;
}

static int xgamma_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xgamma_device *xgamma;
	struct resource *res;
	u32 version;
	int ret;

	xgamma = devm_kzalloc(&pdev->dev, sizeof(*xgamma), GFP_KERNEL);
	if (!xgamma)
		return -ENOMEM;

	xgamma->xvip.dev = &pdev->dev;

	ret = xgamma_parse_of(xgamma);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xgamma->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xgamma->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xgamma->xvip.subdev;
	v4l2_subdev_init(subdev, &xgamma_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xgamma_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xgamma);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xgamma_init_formats(subdev, NULL);

	xgamma->pads[XGAMMA_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xgamma->pads[XGAMMA_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xgamma_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xgamma->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xgamma->ctrl_handler, 2);
	v4l2_ctrl_new_custom(&xgamma->ctrl_handler, &xgamma_switch_lut, NULL);
	v4l2_ctrl_new_custom(&xgamma->ctrl_handler, &xgamma_update_lut, NULL);
	if (xgamma->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xgamma->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xgamma->ctrl_handler;

	platform_set_drvdata(pdev, xgamma);

	version = xvip_read(&xgamma->xvip, XVIP_CTRL_VERSION);

	dev_info(&pdev->dev, "device found, version %u.%02x%x\n",
		 ((version & XVIP_CTRL_VERSION_MAJOR_MASK) >>
		  XVIP_CTRL_VERSION_MAJOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_MINOR_MASK) >>
		  XVIP_CTRL_VERSION_MINOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_REVISION_MASK) >>
		  XVIP_CTRL_VERSION_REVISION_SHIFT));

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	v4l2_ctrl_handler_free(&xgamma->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xgamma_remove(struct platform_device *pdev)
{
	struct xgamma_device *xgamma = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xgamma->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xgamma->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xgamma_pm_ops = {
	.suspend	= xgamma_pm_suspend,
	.resume		= xgamma_pm_resume,
};

static const struct of_device_id xgamma_of_id_table[] = {
	{ .compatible = "xlnx,axi-gamma" },
	{ }
};
MODULE_DEVICE_TABLE(of, xgamma_of_id_table);

static struct platform_driver xgamma_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-gamma",
		.pm		= &xgamma_pm_ops,
		.of_match_table	= of_match_ptr(xgamma_of_id_table),
	},
	.probe			= xgamma_probe,
	.remove			= xgamma_remove,
};

module_platform_driver(xgamma_driver);

MODULE_DESCRIPTION("Xilinx Gamma Correction Driver");
MODULE_LICENSE("GPL v2");
