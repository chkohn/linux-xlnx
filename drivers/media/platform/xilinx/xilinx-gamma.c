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

#define XGAMMA_GAMMA_TABLE_UPDATE			0x100
#define XGAMMA_GAMMA_ADDR_DATA				0x104

/**
 * struct xgamma_device - Xilinx GAMMA device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @default_format: default V4L2 media bus format
 * @format: V4L2 media bus format at the source pad
 * @vip_format: Xilinx Video IP format
 * @ctrl_handler: control handler
 */
struct xgamma_device {
	struct xvip_device xvip;

	struct media_pad pads[2];

	struct v4l2_mbus_framefmt default_format;
	struct v4l2_mbus_framefmt format;
	const struct xvip_video_format *vip_format;

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
		xvip_stop(&xgamma->xvip);
		return 0;
	}

	xvip_set_frame_size(&xgamma->xvip, width, height);

	xvip_start(&xgamma->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

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

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format_size(__format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = __xgamma_get_pad_format(xgamma, fh, XVIP_PAD_SOURCE,
					   fmt->which);

	*__format = fmt->format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xgamma_init_format - Initialize formats on all pads
 * @subdev: gamma V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xgamma_init_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh)
{
	struct xgamma_device *xgamma = to_gamma(subdev);
	struct v4l2_mbus_framefmt *__format;
	u32 which;

	which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;

	__format = __xgamma_get_pad_format(xgamma, fh, XVIP_PAD_SINK, which);
	*__format = xgamma->default_format;

	__format = __xgamma_get_pad_format(xgamma, fh, XVIP_PAD_SOURCE, which);
	*__format = xgamma->default_format;
}

static int xgamma_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xgamma_init_format(subdev, fh);

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

static struct v4l2_subdev_video_ops xgamma_video_ops = {
	.s_stream = xgamma_s_stream,
};

static struct v4l2_subdev_pad_ops xgamma_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xgamma_get_format,
	.set_fmt		= xgamma_set_format,
};

static struct v4l2_subdev_ops xgamma_ops = {
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
	struct device *dev = xgamma->xvip.dev;
	struct device_node *node = xgamma->xvip.dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	const struct xvip_video_format *vip_format;

	/* Count the number of ports. */
	ports = of_get_child_by_name(node, "ports");
	if (ports == NULL)
		ports = node;

	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			vip_format = xvip_of_get_format(port);
			if (vip_format == NULL) {
				dev_err(dev, "invalid format in DT");
				return -EINVAL;
			}

			if (!xgamma->vip_format) {
				xgamma->vip_format = vip_format;
			} else if (xgamma->vip_format != vip_format) {
				dev_err(dev, "in/out format mismatch in DT");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int xgamma_probe(struct platform_device *pdev)
{
	struct xgamma_device *xgamma;
	struct resource *res;
	struct v4l2_subdev *subdev;
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

	xgamma->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xgamma->xvip.iomem))
		return PTR_ERR(xgamma->xvip.iomem);

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xgamma->xvip.subdev;
	v4l2_subdev_init(subdev, &xgamma_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xgamma_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xgamma);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Initialize the default format */
	xgamma->default_format.code = xgamma->vip_format->code;
	xgamma->default_format.field = V4L2_FIELD_NONE;
	xgamma->default_format.colorspace = V4L2_COLORSPACE_SRGB;
	xvip_get_frame_size(&xgamma->xvip, &xgamma->default_format.width,
			    &xgamma->default_format.height);

	xgamma_init_format(subdev, NULL);

	xgamma->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xgamma->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
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

	xvip_print_version(&xgamma->xvip);

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
		.of_match_table	= xgamma_of_id_table,
	},
	.probe			= xgamma_probe,
	.remove			= xgamma_remove,
};

module_platform_driver(xgamma_driver);

MODULE_DESCRIPTION("Xilinx Gamma Correction Driver");
MODULE_LICENSE("GPL v2");
