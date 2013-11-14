/*
 * Xilinx Image Characterization Statistics
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

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-controls.h"
#include "xilinx-vip.h"

#define XSTATS_MIN_WIDTH				32
#define XSTATS_DEF_WIDTH				1920
#define XSTATS_MAX_WIDTH				7680
#define XSTATS_MIN_HEIGHT				32
#define XSTATS_DEF_HEIGHT				1080
#define XSTATS_MAX_HEIGHT				7680

#define XSTATS_PAD_SINK					0
#define XSTATS_PAD_SOURCE				1

#define XSTATS_HMAX0					0x100
#define XSTATS_HMAX1					0x104
#define XSTATS_HMAX2					0x108
#define XSTATS_VMAX0					0x10c
#define XSTATS_VMAX1					0x110
#define XSTATS_VMAX2					0x114
#define XSTATS_HIST_ZOOM_FACTOR				0x118
#define XSTATS_RGB_HIST_ZONE_EN				0x11c
#define XSTATS_YCC_HIST_ZONE_EN				0x120
#define XSTATS_ZONE_ADDR				0x124
#define XSTATS_COLOR_ADDR				0x128
#define XSTATS_HIST_ADDR				0x12c
#define XSTATS_ADDR_VALID				0x130

/**
 * struct xstats_device - Xilinx STATS device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @vip_format: Xilinx Video IP format
 * @format: V4L2 media bus format at the source pad
 * @ctrl_handler: control handler
 */
struct xstats_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_mbus_framefmt format;
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xstats_device *to_stats(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xstats_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xstats_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xstats_device *xstats = to_stats(subdev);
	const u32 width = xstats->format.width;
	const u32 height = xstats->format.height;

	if (!enable) {
		xvip_write(&xstats->xvip, XVIP_CTRL_CONTROL,
			   XVIP_CTRL_CONTROL_SW_RESET);
		xvip_write(&xstats->xvip, XVIP_CTRL_CONTROL, 0);
		return 0;
	}

	xvip_write(&xstats->xvip, XVIP_TIMING_ACTIVE_SIZE,
		   (height << XVIP_TIMING_ACTIVE_VSIZE_SHIFT) |
		   (width << XVIP_TIMING_ACTIVE_HSIZE_SHIFT));

	xvip_write(&xstats->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xstats_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct xstats_device *xstats = to_stats(subdev);

	if (code->index)
		return -EINVAL;

	code->code = xstats->vip_format->code;

	return 0;
}

static int xstats_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct xstats_device *xstats = to_stats(subdev);

	if (fse->index || fse->code != xstats->vip_format->code)
		return -EINVAL;

	fse->min_width = XSTATS_MIN_WIDTH;
	fse->max_width = XSTATS_MAX_WIDTH;
	fse->min_height = XSTATS_MIN_HEIGHT;
	fse->max_height = XSTATS_MAX_HEIGHT;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xstats_get_pad_format(struct xstats_device *xstats,
			struct v4l2_subdev_fh *fh,
			unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xstats->format;
	default:
		return NULL;
	}
}

static int xstats_get_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct xstats_device *xstats = to_stats(subdev);

	fmt->format = *__xstats_get_pad_format(xstats, fh, fmt->pad,
					       fmt->which);

	return 0;
}

static int xstats_set_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct xstats_device *xstats = to_stats(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xstats_get_pad_format(xstats, fh, fmt->pad, fmt->which);
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XSTATS_MIN_WIDTH, XSTATS_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XSTATS_MIN_HEIGHT, XSTATS_MAX_HEIGHT);

	fmt->format = *__format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

static int xstats_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xstats_device *xstats = to_stats(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, 0);

	format->code = xstats->vip_format->code;
	format->width = XSTATS_DEF_WIDTH;
	format->height = XSTATS_DEF_HEIGHT;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int xstats_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xstats_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xstats_device *xstats = container_of(ctrl->handler,
						    struct xstats_device,
						    ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_STATS_HMAX0:
		xvip_write(&xstats->xvip, XSTATS_HMAX0, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_HMAX1:
		xvip_write(&xstats->xvip, XSTATS_HMAX1, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_HMAX2:
		xvip_write(&xstats->xvip, XSTATS_HMAX2, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_VMAX0:
		xvip_write(&xstats->xvip, XSTATS_VMAX0, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_VMAX1:
		xvip_write(&xstats->xvip, XSTATS_VMAX1, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_VMAX2:
		xvip_write(&xstats->xvip, XSTATS_VMAX2, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_HIST_ZOOM_FACTOR:
		xvip_write(&xstats->xvip, XSTATS_HIST_ZOOM_FACTOR,
			   ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_RGB_HIST_ZONE_EN:
		xvip_write(&xstats->xvip, XSTATS_RGB_HIST_ZONE_EN,
			   ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_YCC_HIST_ZONE_EN:
		xvip_write(&xstats->xvip, XSTATS_YCC_HIST_ZONE_EN,
			   ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_ZONE_ADDR:
		xvip_write(&xstats->xvip, XSTATS_ZONE_ADDR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_COLOR_ADDR:
		xvip_write(&xstats->xvip, XSTATS_COLOR_ADDR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_HIST_ADDR:
		xvip_write(&xstats->xvip, XSTATS_HIST_ADDR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_STATS_ADDR_VALID:
		xvip_write(&xstats->xvip, XSTATS_ADDR_VALID, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xstats_ctrl_ops = {
	.s_ctrl	= xstats_s_ctrl,
};

static struct v4l2_subdev_core_ops xstats_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xstats_video_ops = {
	.s_stream = xstats_s_stream,
};

static struct v4l2_subdev_pad_ops xstats_pad_ops = {
	.enum_mbus_code		= xstats_enum_mbus_code,
	.enum_frame_size	= xstats_enum_frame_size,
	.get_fmt		= xstats_get_format,
	.set_fmt		= xstats_set_format,
};

static struct v4l2_subdev_ops xstats_ops = {
	.core   = &xstats_core_ops,
	.video  = &xstats_video_ops,
	.pad    = &xstats_pad_ops,
};

static const struct v4l2_subdev_internal_ops xstats_internal_ops = {
	.open	= xstats_open,
	.close	= xstats_close,
};

/*
 * Control Configs
 */

static struct v4l2_ctrl_config xstats_hmax0 = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_HMAX0,
	.name = "Image Statistics: vertical zone delemeter 0",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 7680,
	.step = 1,
};

static struct v4l2_ctrl_config xstats_hmax1 = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_HMAX1,
	.name = "Image Statistics: vertical zone delemeter 1",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 7680,
	.step = 1,
};

static struct v4l2_ctrl_config xstats_hmax2 = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_VMAX2,
	.name = "Image Statistics: vertical zone delemeter 2",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 7680,
	.step = 1,
};

static struct v4l2_ctrl_config xstats_vmax0 = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_VMAX0,
	.name = "Image Statistics: horizontal zone delemeter 0",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 7680,
	.step = 1,
};

static struct v4l2_ctrl_config xstats_vmax1 = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_VMAX1,
	.name = "Image Statistics: horizontal zone delemeter 1",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 7680,
	.step = 1,
};

static struct v4l2_ctrl_config xstats_vmax2 = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_HMAX2,
	.name = "Image Statistics: horizontal zone delemeter 2",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 7680,
	.step = 1,
};

static struct v4l2_ctrl_config xstats_hist_zoom_factor = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_HIST_ZOOM_FACTOR,
	.name = "Image Statistics: Histogram Zomm Factor",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 2) - 1,
	.step = 1,
	.def = 0,
};

static struct v4l2_ctrl_config xstats_rgb_hist_zone_en = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_RGB_HIST_ZONE_EN,
	.name = "Image Statistics: RGB Histogram Zone Enable",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 16) - 1,
	.step = 1,
	.def = 0xffff,
};

static struct v4l2_ctrl_config xstats_ycc_hist_zone_en = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_YCC_HIST_ZONE_EN,
	.name = "Image Statistics: YCC Histogram Zone Enable",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 16) - 1,
	.step = 1,
	.def = 0xffff,
};

static struct v4l2_ctrl_config xstats_zone_addr = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_ZONE_ADDR,
	.name = "Image Statistics: Zone Readout Select",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 4) - 1,
	.step = 1,
	.def = 0,
};

static struct v4l2_ctrl_config xstats_color_addr = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_COLOR_ADDR,
	.name = "Image Statistics: Color Readout Select",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 2) - 1,
	.step = 1,
	.def = 0,
};

static struct v4l2_ctrl_config xstats_hist_addr = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_HIST_ADDR,
	.name = "Image Statistics: Histogram Data Address",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.step = 1,
	.def = 0,
};

static struct v4l2_ctrl_config xstats_addr_valid = {
	.ops = &xstats_ctrl_ops,
	.id = V4L2_CID_XILINX_STATS_ADDR_VALID,
	.name = "Image Statistics: Address Validation",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = false,
	.max = true,
	.step = 1,
	.def = 0,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xstats_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xstats_pm_suspend(struct device *dev)
{
	struct xstats_device *xstats = dev_get_drvdata(dev);

	xvip_write(&xstats->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xstats_pm_resume(struct device *dev)
{
	struct xstats_device *xstats = dev_get_drvdata(dev);

	xvip_write(&xstats->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xstats_pm_suspend	NULL
#define xstats_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xstats_parse_of(struct xstats_device *xstats)
{
	struct device_node *node = xstats->xvip.dev->of_node;

	xstats->vip_format = xvip_of_get_format(node);
	if (xstats->vip_format == NULL) {
		dev_err(xstats->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	return 0;
}

static int xstats_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xstats_device *xstats;
	struct resource *res;
	u32 version;
	int ret;

	xstats = devm_kzalloc(&pdev->dev, sizeof(*xstats), GFP_KERNEL);
	if (!xstats)
		return -ENOMEM;

	xstats->xvip.dev = &pdev->dev;

	ret = xstats_parse_of(xstats);
	if (ret < 0)
		return ret;

	xstats->format.code = xstats->vip_format->code;
	xstats->format.width = XSTATS_DEF_WIDTH;
	xstats->format.height = XSTATS_DEF_HEIGHT;
	xstats->format.field = V4L2_FIELD_NONE;
	xstats->format.colorspace = V4L2_COLORSPACE_SRGB;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xstats->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xstats->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xstats->xvip.subdev;
	v4l2_subdev_init(subdev, &xstats_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xstats_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xstats);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xstats->pads[XSTATS_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xstats->pads[XSTATS_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xstats_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xstats->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xstats->ctrl_handler, 13);

	xstats_hmax0.def = xvip_read(&xstats->xvip, XSTATS_HMAX0);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_hmax0, NULL);
	xstats_hmax1.def = xvip_read(&xstats->xvip, XSTATS_HMAX1);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_hmax1, NULL);
	xstats_hmax2.def = xvip_read(&xstats->xvip, XSTATS_HMAX2);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_hmax2, NULL);
	xstats_vmax0.def = xvip_read(&xstats->xvip, XSTATS_VMAX0);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_vmax0, NULL);
	xstats_vmax1.def = xvip_read(&xstats->xvip, XSTATS_VMAX1);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_vmax1, NULL);
	xstats_vmax2.def = xvip_read(&xstats->xvip, XSTATS_VMAX2);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_vmax2, NULL);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_hist_zoom_factor,
			     NULL);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_rgb_hist_zone_en,
			     NULL);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_ycc_hist_zone_en,
			     NULL);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_zone_addr, NULL);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_color_addr, NULL);
	xstats_hist_addr.max = (1 << xstats->vip_format->width) - 1;
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_hist_addr, NULL);
	v4l2_ctrl_new_custom(&xstats->ctrl_handler, &xstats_addr_valid, NULL);

	if (xstats->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xstats->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xstats->ctrl_handler;

	platform_set_drvdata(pdev, xstats);

	version = xvip_read(&xstats->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xstats->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xstats_remove(struct platform_device *pdev)
{
	struct xstats_device *xstats = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xstats->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xstats->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xstats_pm_ops = {
	.suspend	= xstats_pm_suspend,
	.resume		= xstats_pm_resume,
};

static const struct of_device_id xstats_of_id_table[] = {
	{ .compatible = "xlnx,axi-stats" },
	{ }
};
MODULE_DEVICE_TABLE(of, xstats_of_id_table);

static struct platform_driver xstats_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-stats",
		.pm		= &xstats_pm_ops,
		.of_match_table	= of_match_ptr(xstats_of_id_table),
	},
	.probe			= xstats_probe,
	.remove			= xstats_remove,
};

module_platform_driver(xstats_driver);

MODULE_DESCRIPTION("Xilinx STATS Driver");
MODULE_LICENSE("GPL v2");
