/*
 * Xilinx Image Enhancement
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

#define XENHANCE_MIN_WIDTH				32
#define XENHANCE_MAX_WIDTH				7680
#define XENHANCE_MIN_HEIGHT				32
#define XENHANCE_MAX_HEIGHT				7680

#define XENHANCE_PAD_SINK				0
#define XENHANCE_PAD_SOURCE				1

#define XENHANCE_NOISE_THRESHOLD			0x100
#define XENHANCE_ENHANCE_STRENGTH			0x104
#define XENHANCE_HALO_SUPPRESS				0x108

/**
 * struct xenhance_device - Xilinx ENHANCE device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @vip_format: Xilinx Video IP format
 * @format: V4L2 media bus format at the source pad
 * @ctrl_handler: control handler
 */
struct xenhance_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_mbus_framefmt format;
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xenhance_device *to_enhance(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xenhance_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xenhance_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xenhance_device *xenhance = to_enhance(subdev);
	const u32 width = xenhance->format.width;
	const u32 height = xenhance->format.height;

	if (!enable) {
		xvip_write(&xenhance->xvip, XVIP_CTRL_CONTROL,
			   XVIP_CTRL_CONTROL_SW_RESET);
		xvip_write(&xenhance->xvip, XVIP_CTRL_CONTROL, 0);
		return 0;
	}

	xvip_write(&xenhance->xvip, XVIP_ACTIVE_SIZE,
		   (height << XVIP_ACTIVE_VSIZE_SHIFT) |
		   (width << XVIP_ACTIVE_HSIZE_SHIFT));

	xvip_write(&xenhance->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xenhance_enum_mbus_code(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	struct xenhance_device *xenhance = to_enhance(subdev);

	if (code->index)
		return -EINVAL;

	code->code = xenhance->vip_format->code;

	return 0;
}

static int xenhance_enum_frame_size(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XENHANCE_PAD_SINK) {
		fse->min_width = XENHANCE_MIN_WIDTH;
		fse->max_width = XENHANCE_MAX_WIDTH;
		fse->min_height = XENHANCE_MIN_HEIGHT;
		fse->max_height = XENHANCE_MAX_HEIGHT;
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
__xenhance_get_pad_format(struct xenhance_device *xenhance,
			  struct v4l2_subdev_fh *fh,
			  unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xenhance->format;
	default:
		return NULL;
	}
}

static int xenhance_get_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct xenhance_device *xenhance = to_enhance(subdev);

	fmt->format = *__xenhance_get_pad_format(xenhance, fh, fmt->pad,
						 fmt->which);

	return 0;
}

static int xenhance_set_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct xenhance_device *xenhance = to_enhance(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xenhance_get_pad_format(xenhance, fh, fmt->pad,
					     fmt->which);

	if (fmt->pad == XENHANCE_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	__format->code = xenhance->vip_format->code;
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XENHANCE_MIN_WIDTH, XENHANCE_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XENHANCE_MIN_HEIGHT, XENHANCE_MAX_HEIGHT);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = __xenhance_get_pad_format(xenhance, fh, XENHANCE_PAD_SOURCE,
					     fmt->which);
	*__format = fmt->format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xenhance_init_formats - Initialize formats on all pads
 * @subdev: enhanceper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xenhance_init_formats(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh)
{
	struct xenhance_device *xenhance = to_enhance(subdev);
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));

	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.width = xvip_read(&xenhance->xvip, XVIP_ACTIVE_SIZE) &
			      XVIP_ACTIVE_HSIZE_MASK;
	format.format.height = (xvip_read(&xenhance->xvip, XVIP_ACTIVE_SIZE) &
				XVIP_ACTIVE_VSIZE_MASK) >>
			       XVIP_ACTIVE_VSIZE_SHIFT;
	format.format.field = V4L2_FIELD_NONE;
	format.format.colorspace = V4L2_COLORSPACE_SRGB;

	format.pad = XENHANCE_PAD_SINK;

	xenhance_set_format(subdev, fh, &format);

	format.pad = XENHANCE_PAD_SOURCE;

	xenhance_set_format(subdev, fh, &format);
}

static int xenhance_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xenhance_init_formats(subdev, fh);

	return 0;
}

static int xenhance_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xenhance_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xenhance_device *xenhance = container_of(ctrl->handler,
							struct xenhance_device,
							ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_XILINX_ENHANCE_NOISE_THRESHOLD:
		xvip_write(&xenhance->xvip, XENHANCE_NOISE_THRESHOLD,
			   ctrl->val);
		return 0;
	case V4L2_CID_XILINX_ENHANCE_STRENGTH:
		xvip_write(&xenhance->xvip, XENHANCE_ENHANCE_STRENGTH,
			   ctrl->val);
		return 0;
	case V4L2_CID_XILINX_ENHANCE_HALO_SUPPRESS:
		xvip_write(&xenhance->xvip, XENHANCE_HALO_SUPPRESS, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xenhance_ctrl_ops = {
	.s_ctrl	= xenhance_s_ctrl,
};

static struct v4l2_subdev_core_ops xenhance_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xenhance_video_ops = {
	.s_stream = xenhance_s_stream,
};

static struct v4l2_subdev_pad_ops xenhance_pad_ops = {
	.enum_mbus_code		= xenhance_enum_mbus_code,
	.enum_frame_size	= xenhance_enum_frame_size,
	.get_fmt		= xenhance_get_format,
	.set_fmt		= xenhance_set_format,
};

static struct v4l2_subdev_ops xenhance_ops = {
	.core   = &xenhance_core_ops,
	.video  = &xenhance_video_ops,
	.pad    = &xenhance_pad_ops,
};

static const struct v4l2_subdev_internal_ops xenhance_internal_ops = {
	.open	= xenhance_open,
	.close	= xenhance_close,
};

/*
 * Control Configs
 */

static struct v4l2_ctrl_config xenhance_noise_threshold = {
	.ops	= &xenhance_ctrl_ops,
	.id	= V4L2_CID_XILINX_ENHANCE_NOISE_THRESHOLD,
	.name	= "Image Enhancement: Noise Threshold",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.step	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xenhance_strength = {
	.ops	= &xenhance_ctrl_ops,
	.id	= V4L2_CID_XILINX_ENHANCE_STRENGTH,
	.name	= "Image Enhancement: Enhance Strength",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 15) - 1,
	.step	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xenhance_halo_suppress = {
	.ops	= &xenhance_ctrl_ops,
	.id	= V4L2_CID_XILINX_ENHANCE_HALO_SUPPRESS,
	.name	= "Image Enhancement: Halo Suppress",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 15),
	.step	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xenhance_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xenhance_pm_suspend(struct device *dev)
{
	struct xenhance_device *xenhance = dev_get_drvdata(dev);

	xvip_write(&xenhance->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xenhance_pm_resume(struct device *dev)
{
	struct xenhance_device *xenhance = dev_get_drvdata(dev);

	xvip_write(&xenhance->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xenhance_pm_suspend	NULL
#define xenhance_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xenhance_parse_of(struct xenhance_device *xenhance)
{
	struct device_node *node = xenhance->xvip.dev->of_node;

	xenhance->vip_format = xvip_of_get_format(node);
	if (xenhance->vip_format == NULL) {
		dev_err(xenhance->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	return 0;
}

static int xenhance_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xenhance_device *xenhance;
	struct resource *res;
	u32 version;
	int ret;

	xenhance = devm_kzalloc(&pdev->dev, sizeof(*xenhance), GFP_KERNEL);
	if (!xenhance)
		return -ENOMEM;

	xenhance->xvip.dev = &pdev->dev;

	ret = xenhance_parse_of(xenhance);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xenhance->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xenhance->xvip.iomem == NULL)
		return -ENODEV;

	xenhance->format.code = xenhance->vip_format->code;
	xenhance->format.width = xvip_read(&xenhance->xvip, XVIP_ACTIVE_SIZE) &
				 XVIP_ACTIVE_HSIZE_MASK;
	xenhance->format.height =
		(xvip_read(&xenhance->xvip, XVIP_ACTIVE_SIZE) &
		 XVIP_ACTIVE_VSIZE_MASK) >>
		XVIP_ACTIVE_VSIZE_SHIFT;
	xenhance->format.field = V4L2_FIELD_NONE;
	xenhance->format.colorspace = V4L2_COLORSPACE_SRGB;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xenhance->xvip.subdev;
	v4l2_subdev_init(subdev, &xenhance_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xenhance_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xenhance);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xenhance_init_formats(subdev, NULL);

	xenhance->pads[XENHANCE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xenhance->pads[XENHANCE_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xenhance_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xenhance->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xenhance->ctrl_handler, 3);
	xenhance_noise_threshold.max = (2 << xenhance->vip_format->width) - 1;
	xenhance_noise_threshold.def = xvip_read(&xenhance->xvip,
						 XENHANCE_NOISE_THRESHOLD);
	v4l2_ctrl_new_custom(&xenhance->ctrl_handler, &xenhance_noise_threshold,
			     NULL);
	xenhance_strength.def = xvip_read(&xenhance->xvip,
					   XENHANCE_ENHANCE_STRENGTH);
	v4l2_ctrl_new_custom(&xenhance->ctrl_handler, &xenhance_strength, NULL);
	xenhance_halo_suppress.def = xvip_read(&xenhance->xvip,
					       XENHANCE_HALO_SUPPRESS);
	v4l2_ctrl_new_custom(&xenhance->ctrl_handler, &xenhance_halo_suppress,
			     NULL);
	if (xenhance->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xenhance->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xenhance->ctrl_handler;

	platform_set_drvdata(pdev, xenhance);

	version = xvip_read(&xenhance->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xenhance->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xenhance_remove(struct platform_device *pdev)
{
	struct xenhance_device *xenhance = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xenhance->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xenhance->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xenhance_pm_ops = {
	.suspend	= xenhance_pm_suspend,
	.resume		= xenhance_pm_resume,
};

static const struct of_device_id xenhance_of_id_table[] = {
	{ .compatible = "xlnx,axi-enhance" },
	{ }
};
MODULE_DEVICE_TABLE(of, xenhance_of_id_table);

static struct platform_driver xenhance_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "xilinx-enhance",
		.pm		= &xenhance_pm_ops,
		.of_match_table	= of_match_ptr(xenhance_of_id_table),
	},
	.probe			= xenhance_probe,
	.remove			= xenhance_remove,
};

module_platform_driver(xenhance_driver);

MODULE_DESCRIPTION("Xilinx ENHANCE Driver");
MODULE_LICENSE("GPL v2");
