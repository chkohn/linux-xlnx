/*
 * Xilinx Defective(Stuck) Pixel Correction Driver
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

#define XSPC_THRESH_TEMPORAL_VAR		0x100
#define XSPC_THRESH_SPATIAL_VAR			0x104
#define XSPC_THRESH_PIXEL_AGE			0x108

/**
 * struct xspc_device - Xilinx SPC device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @vip_format: Xilinx Video IP format
 * @format: V4L2 media bus format at the source pad
 * @ctrl_handler: control handler
 */
struct xspc_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_mbus_framefmt format;
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xspc_device *to_spc(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xspc_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xspc_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xspc_device *xspc = to_spc(subdev);
	const u32 width = xspc->format.width;
	const u32 height = xspc->format.height;

	if (!enable) {
		xvip_stop(&xspc->xvip);
		return 0;
	}

	xvip_set_size(&xspc->xvip, width, height);

	xvip_start(&xspc->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xspc_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xspc_device *xspc = to_spc(subdev);

	fmt->format = *xvip_get_pad_format(fh, &xspc->format, fmt->pad,
					   fmt->which);

	return 0;
}

static int xspc_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xspc_device *xspc = to_spc(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = xvip_get_pad_format(fh, &xspc->format, fmt->pad, fmt->which);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format(__format, xspc->vip_format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = xvip_get_pad_format(fh, &xspc->format, XVIP_PAD_SOURCE,
				       fmt->which);

	*__format = fmt->format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

static int xspc_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xvip_init_formats(subdev, fh);

	return 0;
}

static int xspc_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xspc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xspc_device *xspc = container_of(ctrl->handler,
						struct xspc_device,
						ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_SPC_TEMPORAL:
		xvip_write(&xspc->xvip, XSPC_THRESH_TEMPORAL_VAR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_SPC_SPATIAL:
		xvip_write(&xspc->xvip, XSPC_THRESH_SPATIAL_VAR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_SPC_PIXEL_AGE:
		xvip_write(&xspc->xvip, XSPC_THRESH_PIXEL_AGE, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xspc_ctrl_ops = {
	.s_ctrl	= xspc_s_ctrl,
};

static struct v4l2_subdev_core_ops xspc_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xspc_video_ops = {
	.s_stream = xspc_s_stream,
};

static struct v4l2_subdev_pad_ops xspc_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xspc_get_format,
	.set_fmt		= xspc_set_format,
};

static struct v4l2_subdev_ops xspc_ops = {
	.core   = &xspc_core_ops,
	.video  = &xspc_video_ops,
	.pad    = &xspc_pad_ops,
};

static const struct v4l2_subdev_internal_ops xspc_internal_ops = {
	.open	= xspc_open,
	.close	= xspc_close,
};

/*
 * Control Configs
 */

static struct v4l2_ctrl_config xspc_temporal = {
	.ops	= &xspc_ctrl_ops,
	.id	= V4L2_CID_XILINX_SPC_TEMPORAL,
	.name	= "Pixel Correction: Temporal Variance Threshold",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 12) - 1,
	.step	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xspc_spatial = {
	.ops	= &xspc_ctrl_ops,
	.id	= V4L2_CID_XILINX_SPC_SPATIAL,
	.name	= "Pixel Correction: Spatial Variance Threshold",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xspc_pixel_age = {
	.ops	= &xspc_ctrl_ops,
	.id	= V4L2_CID_XILINX_SPC_PIXEL_AGE,
	.name	= "Pixel Correction: Pixel Age Threshold",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xspc_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xspc_pm_suspend(struct device *dev)
{
	struct xspc_device *xspc = dev_get_drvdata(dev);

	xvip_write(&xspc->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xspc_pm_resume(struct device *dev)
{
	struct xspc_device *xspc = dev_get_drvdata(dev);

	xvip_write(&xspc->xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_ENABLE |
		   XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xspc_pm_suspend	NULL
#define xspc_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xspc_parse_of(struct xspc_device *xspc)
{
	struct device_node *node = xspc->xvip.dev->of_node;

	/* Parse the DT properties. */
	xspc->vip_format = xvip_of_get_format(node);
	if (xspc->vip_format == NULL) {
		dev_err(xspc->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	return 0;
}

static int xspc_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xspc_device *xspc;
	struct resource *res;
	u32 version;
	int ret;

	xspc = devm_kzalloc(&pdev->dev, sizeof(*xspc), GFP_KERNEL);
	if (!xspc)
		return -ENOMEM;

	xspc->xvip.dev = &pdev->dev;

	ret = xspc_parse_of(xspc);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xspc->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xspc->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xspc->xvip.subdev;
	v4l2_subdev_init(subdev, &xspc_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xspc_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xspc);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xvip_init_formats(subdev, NULL);

	xspc->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xspc->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xspc_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xspc->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xspc->ctrl_handler, 3);
	xspc_temporal.def = xvip_read(&xspc->xvip, XSPC_THRESH_TEMPORAL_VAR);
	v4l2_ctrl_new_custom(&xspc->ctrl_handler, &xspc_temporal, NULL);
	xspc_spatial.def = xvip_read(&xspc->xvip, XSPC_THRESH_SPATIAL_VAR);
	v4l2_ctrl_new_custom(&xspc->ctrl_handler, &xspc_spatial, NULL);
	xspc_pixel_age.def = xvip_read(&xspc->xvip, XSPC_THRESH_PIXEL_AGE);
	v4l2_ctrl_new_custom(&xspc->ctrl_handler, &xspc_pixel_age, NULL);
	if (xspc->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xspc->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xspc->ctrl_handler;

	platform_set_drvdata(pdev, xspc);

	version = xvip_read(&xspc->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xspc->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xspc_remove(struct platform_device *pdev)
{
	struct xspc_device *xspc = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xspc->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xspc->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xspc_pm_ops = {
	.suspend	= xspc_pm_suspend,
	.resume		= xspc_pm_resume,
};

static const struct of_device_id xspc_of_id_table[] = {
	{ .compatible = "xlnx,axi-spc" },
	{ }
};
MODULE_DEVICE_TABLE(of, xspc_of_id_table);

static struct platform_driver xspc_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-spc",
		.pm		= &xspc_pm_ops,
		.of_match_table	= of_match_ptr(xspc_of_id_table),
	},
	.probe			= xspc_probe,
	.remove			= xspc_remove,
};

module_platform_driver(xspc_driver);

MODULE_DESCRIPTION("Xilinx Defective(Stuck) Pixel Correction Driver");
MODULE_LICENSE("GPL v2");
