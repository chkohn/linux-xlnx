/*
 * Xilinx RGB to YUV Convertor
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

#define XRGB2YUV_YMAX					0x100
#define XRGB2YUV_YMIN					0x104
#define XRGB2YUV_CBMAX					0x108
#define XRGB2YUV_CBMIN					0x10c
#define XRGB2YUV_CRMAX					0x110
#define XRGB2YUV_CRMIN					0x114
#define XRGB2YUV_YOFFSET				0x118
#define XRGB2YUV_CBOFFSET				0x11c
#define XRGB2YUV_CROFFSET				0x120
#define XRGB2YUV_ACOEF					0x124
#define XRGB2YUV_BCOEF					0x128
#define XRGB2YUV_CCOEF					0x12c
#define XRGB2YUV_DCOEF					0x130

/**
 * struct xrgb2yuv_device - Xilinx RGB2YUV device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats at the sink and source pads
 * @vip_formats: Xilinx Video IP formats
 * @ctrl_handler: control handler
 */
struct xrgb2yuv_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	const struct xvip_video_format *vip_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xrgb2yuv_device *to_rgb2yuv(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xrgb2yuv_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xrgb2yuv_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xrgb2yuv_device *xrgb2yuv = to_rgb2yuv(subdev);
	const u32 width = xrgb2yuv->formats[XVIP_PAD_SINK].width;
	const u32 height = xrgb2yuv->formats[XVIP_PAD_SINK].height;

	if (!enable) {
		xvip_stop(&xrgb2yuv->xvip);
		return 0;
	}

	xvip_set_size(&xrgb2yuv->xvip, width, height);

	xvip_start(&xrgb2yuv->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xrgb2yuv_get_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct xrgb2yuv_device *xrgb2yuv = to_rgb2yuv(subdev);

	fmt->format = *xvip_get_pad_format(fh, &xrgb2yuv->formats[fmt->pad],
					   fmt->pad, fmt->which);

	return 0;
}

static int xrgb2yuv_set_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct xrgb2yuv_device *xrgb2yuv = to_rgb2yuv(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = xvip_get_pad_format(fh, &xrgb2yuv->formats[fmt->pad],
				       fmt->pad, fmt->which);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format(__format, xrgb2yuv->vip_formats[XVIP_PAD_SINK], fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = xvip_get_pad_format(fh, &xrgb2yuv->formats[XVIP_PAD_SOURCE],
				       XVIP_PAD_SOURCE, fmt->which);

	xvip_set_format(__format, xrgb2yuv->vip_formats[XVIP_PAD_SOURCE], fmt);

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

static int xrgb2yuv_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xvip_init_formats(subdev, fh);

	return 0;
}

static int xrgb2yuv_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xrgb2yuv_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xrgb2yuv_device *xrgb2yuv =
		container_of(ctrl->handler, struct xrgb2yuv_device,
			     ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_RGB2YUV_YMAX:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_YMAX, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_YMIN:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_YMIN, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CBMAX:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CBMAX, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CBMIN:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CBMIN, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CRMAX:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CRMAX, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CRMIN:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CRMIN, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_YOFFSET:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_YOFFSET, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CBOFFSET:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CBOFFSET, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CROFFSET:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CROFFSET, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_ACOEF:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_ACOEF, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_BCOEF:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_BCOEF, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_CCOEF:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_CCOEF, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_RGB2YUV_DCOEF:
		xvip_write(&xrgb2yuv->xvip, XRGB2YUV_DCOEF, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xrgb2yuv_ctrl_ops = {
	.s_ctrl	= xrgb2yuv_s_ctrl,
};

static struct v4l2_subdev_core_ops xrgb2yuv_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xrgb2yuv_video_ops = {
	.s_stream = xrgb2yuv_s_stream,
};

static struct v4l2_subdev_pad_ops xrgb2yuv_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xrgb2yuv_get_format,
	.set_fmt		= xrgb2yuv_set_format,
};

static struct v4l2_subdev_ops xrgb2yuv_ops = {
	.core   = &xrgb2yuv_core_ops,
	.video  = &xrgb2yuv_video_ops,
	.pad    = &xrgb2yuv_pad_ops,
};

static const struct v4l2_subdev_internal_ops xrgb2yuv_internal_ops = {
	.open	= xrgb2yuv_open,
	.close	= xrgb2yuv_close,
};

/*
 * Control Configs
 */

static struct v4l2_ctrl_config xrgb2yuv_ymax = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_YMAX,
	.name	= "RGB to YUV: Maximum Y value",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_ymin = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_YMIN,
	.name	= "RGB to YUV: Minimum Y value",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_cbmax = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CBMAX,
	.name	= "RGB to YUV: Maximum Cb value",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_cbmin = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CBMIN,
	.name	= "RGB to YUV: Minimum Cb value",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_crmax = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CRMAX,
	.name	= "RGB to YUV: Maximum Cr value",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_crmin = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CRMIN,
	.name	= "RGB to YUV: Minimum Cr value",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_yoffset = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_YOFFSET,
	.name	= "RGB to YUV: Luma offset",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 17) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_cboffset = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CBOFFSET,
	.name	= "RGB to YUV: Chroma Cb offset",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 17) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_croffset = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CROFFSET,
	.name	= "RGB to YUV: Chroma Cr offset",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 17) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_acoef = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_ACOEF,
	.name	= "RGB to YUV: CA coefficient",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= -((1 << 17) - 1),
	.max	= (1 << 17) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_bcoef = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_BCOEF,
	.name	= "RGB to YUV: CB coefficient",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= -((1 << 17) - 1),
	.max	= (1 << 17) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_ccoef = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_CCOEF,
	.name	= "RGB to YUV: CC coefficient",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= -((1 << 17) - 1),
	.max	= (1 << 17) - 1,
	.step	= 1,
};

static struct v4l2_ctrl_config xrgb2yuv_dcoef = {
	.ops	= &xrgb2yuv_ctrl_ops,
	.id	= V4L2_CID_XILINX_RGB2YUV_DCOEF,
	.name	= "RGB to YUV: CD coefficient",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= -((1 << 17) - 1),
	.max	= (1 << 17) - 1,
	.step	= 1,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xrgb2yuv_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xrgb2yuv_pm_suspend(struct device *dev)
{
	struct xrgb2yuv_device *xrgb2yuv = dev_get_drvdata(dev);

	xvip_write(&xrgb2yuv->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xrgb2yuv_pm_resume(struct device *dev)
{
	struct xrgb2yuv_device *xrgb2yuv = dev_get_drvdata(dev);

	xvip_write(&xrgb2yuv->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xrgb2yuv_pm_suspend	NULL
#define xrgb2yuv_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xrgb2yuv_parse_of(struct xrgb2yuv_device *xrgb2yuv)
{
	struct device_node *node = xrgb2yuv->xvip.dev->of_node;
	int ret;

	ret = xvip_of_get_formats(node,
				  &xrgb2yuv->vip_formats[XVIP_PAD_SINK],
				  &xrgb2yuv->vip_formats[XVIP_PAD_SOURCE]);
	if (ret < 0)
		dev_err(xrgb2yuv->xvip.dev, "invalid format in DT");

	return ret;
}

static int xrgb2yuv_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xrgb2yuv_device *xrgb2yuv;
	struct resource *res;
	u32 version;
	int ret;

	xrgb2yuv = devm_kzalloc(&pdev->dev, sizeof(*xrgb2yuv), GFP_KERNEL);
	if (!xrgb2yuv)
		return -ENOMEM;

	xrgb2yuv->xvip.dev = &pdev->dev;

	ret = xrgb2yuv_parse_of(xrgb2yuv);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xrgb2yuv->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xrgb2yuv->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xrgb2yuv->xvip.subdev;
	v4l2_subdev_init(subdev, &xrgb2yuv_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xrgb2yuv_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xrgb2yuv);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xvip_init_formats(subdev, NULL);

	xrgb2yuv->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xrgb2yuv->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xrgb2yuv_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xrgb2yuv->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xrgb2yuv->ctrl_handler, 13);

	xrgb2yuv_ymax.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_YMAX);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_ymax, NULL);
	xrgb2yuv_ymin.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_YMIN);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_ymin, NULL);
	xrgb2yuv_crmax.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CRMAX);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_crmax, NULL);
	xrgb2yuv_crmin.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CRMIN);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_crmin, NULL);
	xrgb2yuv_cbmax.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CBMAX);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_cbmax, NULL);
	xrgb2yuv_cbmin.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CBMIN);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_cbmin, NULL);

	xrgb2yuv_yoffset.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_YOFFSET);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_yoffset, NULL);
	xrgb2yuv_cboffset.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CBOFFSET);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_cboffset, NULL);
	xrgb2yuv_croffset.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CROFFSET);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_croffset, NULL);

	xrgb2yuv_acoef.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_ACOEF);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_acoef, NULL);
	xrgb2yuv_bcoef.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_BCOEF);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_bcoef, NULL);
	xrgb2yuv_ccoef.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_CCOEF);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_ccoef, NULL);
	xrgb2yuv_dcoef.def = xvip_read(&xrgb2yuv->xvip, XRGB2YUV_DCOEF);
	v4l2_ctrl_new_custom(&xrgb2yuv->ctrl_handler, &xrgb2yuv_dcoef, NULL);

	if (xrgb2yuv->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xrgb2yuv->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xrgb2yuv->ctrl_handler;

	platform_set_drvdata(pdev, xrgb2yuv);

	version = xvip_read(&xrgb2yuv->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xrgb2yuv->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xrgb2yuv_remove(struct platform_device *pdev)
{
	struct xrgb2yuv_device *xrgb2yuv = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xrgb2yuv->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xrgb2yuv->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xrgb2yuv_pm_ops = {
	.suspend	= xrgb2yuv_pm_suspend,
	.resume		= xrgb2yuv_pm_resume,
};

static const struct of_device_id xrgb2yuv_of_id_table[] = {
	{ .compatible = "xlnx,axi-rgb2yuv" },
	{ }
};
MODULE_DEVICE_TABLE(of, xrgb2yuv_of_id_table);

static struct platform_driver xrgb2yuv_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-rgb2yuv",
		.pm		= &xrgb2yuv_pm_ops,
		.of_match_table	= of_match_ptr(xrgb2yuv_of_id_table),
	},
	.probe			= xrgb2yuv_probe,
	.remove			= xrgb2yuv_remove,
};

module_platform_driver(xrgb2yuv_driver);

MODULE_DESCRIPTION("Xilinx RGB to YUV Converter Driver");
MODULE_LICENSE("GPL v2");
