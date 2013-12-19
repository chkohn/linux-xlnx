/*
 * Xilinx Color Filter Array
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

#define XCFA_BAYER_PHASE			0x100

/**
 * struct xcfa_device - Xilinx CFA device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @vip_formats: Xilinx Video IP formats
 * @formats: V4L2 media bus formats
 * @ctrl_handler: control handler
 */
struct xcfa_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	const struct xvip_video_format *vip_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xcfa_device *to_cfa(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xcfa_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xcfa_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xcfa_device *xcfa = to_cfa(subdev);
	const u32 width = xcfa->formats[XVIP_PAD_SINK].width;
	const u32 height = xcfa->formats[XVIP_PAD_SINK].height;

	if (!enable) {
		xvip_stop(&xcfa->xvip);
		return 0;
	}

	xvip_set_size(&xcfa->xvip, width, height);

	xvip_start(&xcfa->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xcfa_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xcfa_device *xcfa = to_cfa(subdev);

	fmt->format = *xvip_get_pad_format(fh, &xcfa->formats[fmt->pad],
					   fmt->pad, fmt->which);

	return 0;
}

static int xcfa_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xcfa_device *xcfa = to_cfa(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = xvip_get_pad_format(fh, &xcfa->formats[fmt->pad], fmt->pad,
				       fmt->which);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format(__format, xcfa->vip_formats[XVIP_PAD_SINK], fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad */
	__format = xvip_get_pad_format(fh, &xcfa->formats[XVIP_PAD_SOURCE],
				       XVIP_PAD_SOURCE, fmt->which);

	xvip_set_format(__format, xcfa->vip_formats[XVIP_PAD_SOURCE], fmt);

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

static int xcfa_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xvip_init_formats(subdev, NULL);

	return 0;
}

static int xcfa_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xcfa_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xcfa_device *xcfa = container_of(ctrl->handler,
						struct xcfa_device,
						ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_CFA_BAYER:
		xvip_write(&xcfa->xvip, XCFA_BAYER_PHASE, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xcfa_ctrl_ops = {
	.s_ctrl	= xcfa_s_ctrl,
};

static struct v4l2_subdev_core_ops xcfa_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xcfa_video_ops = {
	.s_stream = xcfa_s_stream,
};

static struct v4l2_subdev_pad_ops xcfa_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xcfa_get_format,
	.set_fmt		= xcfa_set_format,
};

static struct v4l2_subdev_ops xcfa_ops = {
	.core   = &xcfa_core_ops,
	.video  = &xcfa_video_ops,
	.pad    = &xcfa_pad_ops,
};

static const struct v4l2_subdev_internal_ops xcfa_internal_ops = {
	.open	= xcfa_open,
	.close	= xcfa_close,
};

/*
 * Control Configs
 */

static const char *const xcfa_bayer_menu_strings[] = {
	"RGRG Bayer",
	"GRGR Bayer",
	"GBGB Bayer",
	"BGBG Bayer",
	NULL,
};

static struct v4l2_ctrl_config xcfa_bayer = {
	.ops	= &xcfa_ctrl_ops,
	.id	= V4L2_CID_XILINX_CFA_BAYER,
	.name	= "Color Filter: Bayer",
	.type	= V4L2_CTRL_TYPE_MENU,
	.min	= 0,
	.max	= 3,
	.qmenu	= xcfa_bayer_menu_strings,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xcfa_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xcfa_pm_suspend(struct device *dev)
{
	struct xcfa_device *xcfa = dev_get_drvdata(dev);

	xvip_write(&xcfa->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xcfa_pm_resume(struct device *dev)
{
	struct xcfa_device *xcfa = dev_get_drvdata(dev);

	xvip_write(&xcfa->xvip, XVIP_CTRL_CONTROL, XVIP_CTRL_CONTROL_SW_ENABLE |
		   XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xcfa_pm_suspend	NULL
#define xcfa_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xcfa_parse_of(struct xcfa_device *xcfa)
{
	struct device_node *node = xcfa->xvip.dev->of_node;
	int ret;

	ret = xvip_of_get_formats(node, &xcfa->vip_formats[XVIP_PAD_SINK],
				  &xcfa->vip_formats[XVIP_PAD_SOURCE]);
	if (ret < 0)
		dev_err(xcfa->xvip.dev, "invalid format in DT");

	return 0;
}

static int xcfa_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xcfa_device *xcfa;
	struct resource *res;
	u32 version;
	int ret;

	xcfa = devm_kzalloc(&pdev->dev, sizeof(*xcfa), GFP_KERNEL);
	if (!xcfa)
		return -ENOMEM;

	xcfa->xvip.dev = &pdev->dev;

	ret = xcfa_parse_of(xcfa);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xcfa->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xcfa->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xcfa->xvip.subdev;
	v4l2_subdev_init(subdev, &xcfa_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xcfa_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xcfa);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xvip_init_formats(subdev, NULL);

	xcfa->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xcfa->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xcfa_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xcfa->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xcfa->ctrl_handler, 1);
	xcfa_bayer.def = xvip_read(&xcfa->xvip, XCFA_BAYER_PHASE);
	v4l2_ctrl_new_custom(&xcfa->ctrl_handler, &xcfa_bayer, NULL);
	if (xcfa->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xcfa->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xcfa->ctrl_handler;

	platform_set_drvdata(pdev, xcfa);

	version = xvip_read(&xcfa->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xcfa->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xcfa_remove(struct platform_device *pdev)
{
	struct xcfa_device *xcfa = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xcfa->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xcfa->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xcfa_pm_ops = {
	.suspend	= xcfa_pm_suspend,
	.resume		= xcfa_pm_resume,
};

static const struct of_device_id xcfa_of_id_table[] = {
	{ .compatible = "xlnx,axi-cfa" },
	{ }
};
MODULE_DEVICE_TABLE(of, xcfa_of_id_table);

static struct platform_driver xcfa_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-cfa",
		.pm		= &xcfa_pm_ops,
		.of_match_table	= of_match_ptr(xcfa_of_id_table),
	},
	.probe			= xcfa_probe,
	.remove			= xcfa_remove,
};

module_platform_driver(xcfa_driver);

MODULE_DESCRIPTION("Xilinx Color Filter Array Driver");
MODULE_LICENSE("GPL v2");
