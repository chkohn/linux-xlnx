/*
 * Xilinx Color Correction Matrix
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

#define XCCM_K11				0x100
#define XCCM_K12				0x104
#define XCCM_K13				0x108
#define XCCM_K21				0x10c
#define XCCM_K22				0x110
#define XCCM_K23				0x114
#define XCCM_K31				0x118
#define XCCM_K32				0x11c
#define XCCM_K33				0x120
#define XCCM_ROFFSET				0x124
#define XCCM_GOFFSET				0x128
#define XCCM_BOFFSET				0x12c
#define XCCM_CLIP				0x130
#define XCCM_CLAMP				0x134

/**
 * struct xccm_device - Xilinx CCM device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @default_format: default V4L2 media bus format
 * @format: V4L2 media bus format
 * @vip_format: Xilinx Video IP format
 * @ctrl_handler: control handler
 */
struct xccm_device {
	struct xvip_device xvip;

	struct media_pad pads[2];

	struct v4l2_mbus_framefmt default_format;
	struct v4l2_mbus_framefmt format;
	const struct xvip_video_format *vip_format;

	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xccm_device *to_ccm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xccm_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xccm_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xccm_device *xccm = to_ccm(subdev);
	const u32 width = xccm->format.width;
	const u32 height = xccm->format.height;

	if (!enable) {
		xvip_stop(&xccm->xvip);
		return 0;
	}

	xvip_set_frame_size(&xccm->xvip, width, height);

	xvip_start(&xccm->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
__xccm_get_pad_format(struct xccm_device *xccm,
		      struct v4l2_subdev_fh *fh,
		      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xccm->format;
	default:
		return NULL;
	}
}

static int xccm_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xccm_device *xccm = to_ccm(subdev);

	fmt->format = *__xccm_get_pad_format(xccm, fh, fmt->pad, fmt->which);

	return 0;
}

static int xccm_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xccm_device *xccm = to_ccm(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xccm_get_pad_format(xccm, fh, fmt->pad, fmt->which);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format_size(__format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad */
	__format = __xccm_get_pad_format(xccm, fh, XVIP_PAD_SOURCE, fmt->which);

	*__format = fmt->format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xccm_init_format - Initialize formats on all pads
 * @subdev: ccm V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xccm_init_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh)
{
	struct xccm_device *xccm = to_ccm(subdev);
	struct v4l2_mbus_framefmt *__format;
	u32 which;

	which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;

	__format = __xccm_get_pad_format(xccm, fh, XVIP_PAD_SINK, which);
	*__format = xccm->default_format;

	__format = __xccm_get_pad_format(xccm, fh, XVIP_PAD_SOURCE, which);
	*__format = xccm->default_format;
}

static int xccm_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xccm_init_format(subdev, fh);

	return 0;
}

static int xccm_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xccm_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xccm_device *xccm = container_of(ctrl->handler,
						struct xccm_device,
						ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_CCM_COEFF11:
		xvip_write(&xccm->xvip, XCCM_K11, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF12:
		xvip_write(&xccm->xvip, XCCM_K12, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF13:
		xvip_write(&xccm->xvip, XCCM_K13, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF21:
		xvip_write(&xccm->xvip, XCCM_K21, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF22:
		xvip_write(&xccm->xvip, XCCM_K22, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF23:
		xvip_write(&xccm->xvip, XCCM_K23, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF31:
		xvip_write(&xccm->xvip, XCCM_K31, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF32:
		xvip_write(&xccm->xvip, XCCM_K32, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_COEFF33:
		xvip_write(&xccm->xvip, XCCM_K33, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_RED_OFFSET:
		xvip_write(&xccm->xvip, XCCM_ROFFSET, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_GREEN_OFFSET:
		xvip_write(&xccm->xvip, XCCM_GOFFSET, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_BLUE_OFFSET:
		xvip_write(&xccm->xvip, XCCM_BOFFSET, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_CLIP:
		xvip_write(&xccm->xvip, XCCM_CLIP, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_CCM_CLAMP:
		xvip_write(&xccm->xvip, XCCM_CLAMP, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xccm_ctrl_ops = {
	.s_ctrl	= xccm_s_ctrl,
};

static struct v4l2_subdev_video_ops xccm_video_ops = {
	.s_stream = xccm_s_stream,
};

static struct v4l2_subdev_pad_ops xccm_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xccm_get_format,
	.set_fmt		= xccm_set_format,
};

static struct v4l2_subdev_ops xccm_ops = {
	.video  = &xccm_video_ops,
	.pad    = &xccm_pad_ops,
};

static const struct v4l2_subdev_internal_ops xccm_internal_ops = {
	.open	= xccm_open,
	.close	= xccm_close,
};

/*
 * Control Configs
 */

static struct v4l2_ctrl_config xccm_coeff11 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF11,
	.name	= "Color Correction: Coefficient 11",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff12 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF12,
	.name	= "Color Correction: Coefficient 12",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff13 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF13,
	.name	= "Color Correction: Coefficient 13",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff21 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF21,
	.name	= "Color Correction: Coefficient 21",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff22 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF22,
	.name	= "Color Correction: Coefficient 22",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff23 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF23,
	.name	= "Color Correction: Coefficient 23",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff31 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF31,
	.name	= "Color Correction: Coefficient 31",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff32 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF32,
	.name	= "Color Correction: Coefficient 32",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_coeff33 = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_COEFF33,
	.name	= "Color Correction: Coefficient 33",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 18) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_red_offset = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_RED_OFFSET,
	.name	= "Color Correction: Red Offset",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_green_offset = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_GREEN_OFFSET,
	.name	= "Color Correction: Green Offset",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_blue_offset = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_BLUE_OFFSET,
	.name	= "Color Correction: Blue Offset",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_clip = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_CLIP,
	.name	= "Color Correction: Maximum Output",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.step	= 1,
};

static struct v4l2_ctrl_config xccm_clamp = {
	.ops	= &xccm_ctrl_ops,
	.id	= V4L2_CID_XILINX_CCM_CLAMP,
	.name	= "Color Correction: Minimum Output",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.step	= 1,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xccm_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xccm_pm_suspend(struct device *dev)
{
	struct xccm_device *xccm = dev_get_drvdata(dev);

	xvip_write(&xccm->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xccm_pm_resume(struct device *dev)
{
	struct xccm_device *xccm = dev_get_drvdata(dev);

	xvip_write(&xccm->xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_ENABLE |
		   XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xccm_pm_suspend	NULL
#define xccm_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xccm_parse_of(struct xccm_device *xccm)
{
	struct device *dev = xccm->xvip.dev;
	struct device_node *node = xccm->xvip.dev->of_node;
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

			if (!xccm->vip_format) {
				xccm->vip_format = vip_format;
			} else if (xccm->vip_format != vip_format) {
				dev_err(dev, "in/out format mismatch in DT");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int xccm_probe(struct platform_device *pdev)
{
	struct xccm_device *xccm;
	struct resource *res;
	struct v4l2_subdev *subdev;
	int ret;

	xccm = devm_kzalloc(&pdev->dev, sizeof(*xccm), GFP_KERNEL);
	if (!xccm)
		return -ENOMEM;

	xccm->xvip.dev = &pdev->dev;

	ret = xccm_parse_of(xccm);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xccm->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xccm->xvip.iomem))
		return PTR_ERR(xccm->xvip.iomem);

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xccm->xvip.subdev;
	v4l2_subdev_init(subdev, &xccm_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xccm_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xccm);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Initialize the default format */
	xccm->default_format.code = xccm->vip_format->code;
	xccm->default_format.field = V4L2_FIELD_NONE;
	xccm->default_format.colorspace = V4L2_COLORSPACE_SRGB;
	xvip_get_frame_size(&xccm->xvip, &xccm->default_format.width,
			    &xccm->default_format.height);

	xccm_init_format(subdev, NULL);

	xccm->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xccm->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xccm_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xccm->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xccm->ctrl_handler, 14);

	xccm_coeff11.def = xvip_read(&xccm->xvip, XCCM_K11);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff11, NULL);

	xccm_coeff12.def = xvip_read(&xccm->xvip, XCCM_K12);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff12, NULL);

	xccm_coeff13.def = xvip_read(&xccm->xvip, XCCM_K13);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff13, NULL);

	xccm_coeff21.def = xvip_read(&xccm->xvip, XCCM_K21);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff21, NULL);

	xccm_coeff22.def = xvip_read(&xccm->xvip, XCCM_K22);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff22, NULL);

	xccm_coeff23.def = xvip_read(&xccm->xvip, XCCM_K23);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff23, NULL);

	xccm_coeff31.def = xvip_read(&xccm->xvip, XCCM_K31);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff31, NULL);

	xccm_coeff32.def = xvip_read(&xccm->xvip, XCCM_K32);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff32, NULL);

	xccm_coeff33.def = xvip_read(&xccm->xvip, XCCM_K33);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_coeff33, NULL);

	xccm_red_offset.min = -(2 << xccm->vip_format->width) - 1;
	xccm_red_offset.max = (2 << xccm->vip_format->width) - 1;
	xccm_red_offset.def = xvip_read(&xccm->xvip, XCCM_ROFFSET);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_red_offset, NULL);

	xccm_green_offset.min = -(2 << xccm->vip_format->width) - 1;
	xccm_green_offset.max = (2 << xccm->vip_format->width) - 1;
	xccm_green_offset.def = xvip_read(&xccm->xvip, XCCM_GOFFSET);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_green_offset, NULL);

	xccm_blue_offset.min = -(2 << xccm->vip_format->width) - 1;
	xccm_blue_offset.max = (2 << xccm->vip_format->width) - 1;
	xccm_blue_offset.def = xvip_read(&xccm->xvip, XCCM_BOFFSET);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_blue_offset, NULL);

	xccm_clip.max = (2 << xccm->vip_format->width) - 1;
	xccm_clip.def = xvip_read(&xccm->xvip, XCCM_CLIP);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_clip, NULL);

	xccm_clamp.max = (2 << xccm->vip_format->width) - 1;
	xccm_clamp.def = xvip_read(&xccm->xvip, XCCM_CLAMP);
	v4l2_ctrl_new_custom(&xccm->ctrl_handler, &xccm_clamp, NULL);

	if (xccm->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xccm->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xccm->ctrl_handler;

	platform_set_drvdata(pdev, xccm);

	xvip_print_version(&xccm->xvip);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	v4l2_ctrl_handler_free(&xccm->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xccm_remove(struct platform_device *pdev)
{
	struct xccm_device *xccm = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xccm->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xccm->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xccm_pm_ops = {
	.suspend	= xccm_pm_suspend,
	.resume		= xccm_pm_resume,
};

static const struct of_device_id xccm_of_id_table[] = {
	{ .compatible = "xlnx,axi-ccm" },
	{ }
};
MODULE_DEVICE_TABLE(of, xccm_of_id_table);

static struct platform_driver xccm_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "xilinx-ccm",
		.pm		= &xccm_pm_ops,
		.of_match_table	= xccm_of_id_table,
	},
	.probe			= xccm_probe,
	.remove			= xccm_remove,
};

module_platform_driver(xccm_driver);

MODULE_DESCRIPTION("Xilinx Color Correction Matrix Driver");
MODULE_LICENSE("GPL v2");
