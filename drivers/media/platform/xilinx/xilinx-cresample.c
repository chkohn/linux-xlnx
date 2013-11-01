/*
 * Xilinx Chroma Resampler
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

#define XCRESAMPLE_MIN_WIDTH			32
#define XCRESAMPLE_MAX_WIDTH			7680
#define XCRESAMPLE_MIN_HEIGHT			32
#define XCRESAMPLE_MAX_HEIGHT			7680

#define XCRESAMPLE_PAD_SINK			0
#define XCRESAMPLE_PAD_SOURCE			1

#define XCRESAMPLE_ENCODING			0x100
#define XCRESAMPLE_FIELD_SHIFT			7
#define XCRESAMPLE_FIELD_MASK			(1 << XCRESAMPLE_FIELD_SHIFT)
#define XCRESAMPLE_CHROMA_SHIFT			8
#define XCRESAMPLE_CHROMA_MASK			(1 << XCRESAMPLE_CHROMA_SHIFT)

/**
 * struct xcresample_device - Xilinx CRESAMPLE device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: V4L2 media bus formats at the sink and source pads
 * @vip_formats: Xilinx Video IP formats
 * @ctrl_handler: control handler
 */
struct xcresample_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	const struct xvip_video_format *vip_formats[2];
	struct v4l2_ctrl_handler ctrl_handler;
};

static inline struct xcresample_device *to_cresample(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xcresample_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xcresample_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xcresample_device *xcresample = to_cresample(subdev);
	const u32 width = xcresample->formats[XCRESAMPLE_PAD_SINK].width;
	const u32 height = xcresample->formats[XCRESAMPLE_PAD_SINK].height;

	if (!enable) {
		xvip_write(&xcresample->xvip, XVIP_CTRL_CONTROL,
			   XVIP_CTRL_CONTROL_SW_RESET);
		xvip_write(&xcresample->xvip, XVIP_CTRL_CONTROL, 0);
		return 0;
	}

	xvip_write(&xcresample->xvip, XVIP_ACTIVE_SIZE,
		   (height << XVIP_ACTIVE_VSIZE_SHIFT) |
		   (width << XVIP_ACTIVE_HSIZE_SHIFT));

	xvip_write(&xcresample->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xcresample_enum_mbus_code(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_fh *fh,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *format;

	if (code->index)
		return -EINVAL;

	format = v4l2_subdev_get_try_format(fh, code->pad);

	code->code = format->code;

	return 0;
}

static int xcresample_enum_frame_size(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_fh *fh,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XCRESAMPLE_PAD_SINK) {
		fse->min_width = XCRESAMPLE_MIN_WIDTH;
		fse->max_width = XCRESAMPLE_MAX_WIDTH;
		fse->min_height = XCRESAMPLE_MIN_HEIGHT;
		fse->max_height = XCRESAMPLE_MAX_HEIGHT;
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
__xcresample_get_pad_format(struct xcresample_device *xcresample,
			    struct v4l2_subdev_fh *fh,
			    unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xcresample->formats[pad];
	default:
		return NULL;
	}
}

static int xcresample_get_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_format *fmt)
{
	struct xcresample_device *xcresample = to_cresample(subdev);

	fmt->format = *__xcresample_get_pad_format(xcresample, fh, fmt->pad,
						   fmt->which);

	return 0;
}

static int xcresample_set_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_format *fmt)
{
	struct xcresample_device *xcresample = to_cresample(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xcresample_get_pad_format(xcresample, fh, fmt->pad,
					       fmt->which);

	if (fmt->pad == XCRESAMPLE_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	__format->code = xcresample->vip_formats[XCRESAMPLE_PAD_SINK]->code;
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XCRESAMPLE_MIN_WIDTH, XCRESAMPLE_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XCRESAMPLE_MIN_HEIGHT,
				   XCRESAMPLE_MAX_HEIGHT);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = __xcresample_get_pad_format(xcresample, fh,
					       XCRESAMPLE_PAD_SOURCE,
					       fmt->which);
	__format->code = xcresample->vip_formats[XCRESAMPLE_PAD_SOURCE]->code;
	__format->width = fmt->format.width;
	__format->height = fmt->format.height;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xcresample_init_formats - Initialize formats on all pads
 * @subdev: cresampleper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xcresample_init_formats(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_fh *fh)
{
	struct xcresample_device *xcresample = to_cresample(subdev);
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));

	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.width = xvip_read(&xcresample->xvip, XVIP_ACTIVE_SIZE) &
			      XVIP_ACTIVE_HSIZE_MASK;
	format.format.height = (xvip_read(&xcresample->xvip, XVIP_ACTIVE_SIZE) &
				XVIP_ACTIVE_VSIZE_MASK) >>
			       XVIP_ACTIVE_VSIZE_SHIFT;
	format.format.field = V4L2_FIELD_NONE;
	format.format.colorspace = V4L2_COLORSPACE_SRGB;

	format.pad = XCRESAMPLE_PAD_SINK;

	xcresample_set_format(subdev, fh, &format);

	format.pad = XCRESAMPLE_PAD_SOURCE;

	xcresample_set_format(subdev, fh, &format);
}

static int xcresample_open(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh)
{
	xcresample_init_formats(subdev, fh);

	return 0;
}

static int xcresample_close(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int xcresample_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xcresample_device *xcresample =
		container_of(ctrl->handler, struct xcresample_device,
			     ctrl_handler);
	u32 reg;

	switch (ctrl->id) {
	case V4L2_CID_XILINX_CRESAMPLE_FIELD_PARITY:
		reg = xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING);
		reg |= (ctrl->val << XCRESAMPLE_FIELD_SHIFT);
		xvip_write(&xcresample->xvip, XCRESAMPLE_ENCODING, reg);
		return 0;
	case V4L2_CID_XILINX_CRESAMPLE_CHROMA_PARITY:
		reg = xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING);
		reg |= (ctrl->val << XCRESAMPLE_CHROMA_SHIFT);
		xvip_write(&xcresample->xvip, XCRESAMPLE_ENCODING, reg);
		return 0;
	}

	return -EINVAL;

}

static const struct v4l2_ctrl_ops xcresample_ctrl_ops = {
	.s_ctrl	= xcresample_s_ctrl,
};

static struct v4l2_subdev_core_ops xcresample_core_ops = {
	.queryctrl	= v4l2_subdev_queryctrl,
	.g_ctrl		= v4l2_subdev_g_ctrl,
	.s_ctrl		= v4l2_subdev_s_ctrl,
	.g_ext_ctrls	= v4l2_subdev_g_ext_ctrls,
	.s_ext_ctrls	= v4l2_subdev_s_ext_ctrls,
	.try_ext_ctrls	= v4l2_subdev_try_ext_ctrls,
	.querymenu	= v4l2_subdev_querymenu,
};

static struct v4l2_subdev_video_ops xcresample_video_ops = {
	.s_stream = xcresample_s_stream,
};

static struct v4l2_subdev_pad_ops xcresample_pad_ops = {
	.enum_mbus_code		= xcresample_enum_mbus_code,
	.enum_frame_size	= xcresample_enum_frame_size,
	.get_fmt		= xcresample_get_format,
	.set_fmt		= xcresample_set_format,
};

static struct v4l2_subdev_ops xcresample_ops = {
	.core   = &xcresample_core_ops,
	.video  = &xcresample_video_ops,
	.pad    = &xcresample_pad_ops,
};

static const struct v4l2_subdev_internal_ops xcresample_internal_ops = {
	.open	= xcresample_open,
	.close	= xcresample_close,
};

/*
 * Control Configs
 */

static const char *const xcresample_parity_string[] = {
	"Even",
	"Odd",
	NULL,
};

static struct v4l2_ctrl_config xcresample_field = {
	.ops	= &xcresample_ctrl_ops,
	.id	= V4L2_CID_XILINX_CRESAMPLE_FIELD_PARITY,
	.name	= "Chroma Resampler: Encoding Field Parity",
	.type	= V4L2_CTRL_TYPE_MENU,
	.min	= 0,
	.max	= 1,
	.qmenu	= xcresample_parity_string,
};

static struct v4l2_ctrl_config xcresample_chroma = {
	.ops	= &xcresample_ctrl_ops,
	.id	= V4L2_CID_XILINX_CRESAMPLE_CHROMA_PARITY,
	.name	= "Chroma Resampler: Encoding Chroma Parity",
	.type	= V4L2_CTRL_TYPE_MENU,
	.min	= 0,
	.max	= 1,
	.qmenu	= xcresample_parity_string,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xcresample_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Power Management
 */

#ifdef CONFIG_PM

static int xcresample_pm_suspend(struct device *dev)
{
	struct xcresample_device *xcresample = dev_get_drvdata(dev);

	xvip_write(&xcresample->xvip, XVIP_CTRL_CONTROL, 0);

	return 0;
}

static int xcresample_pm_resume(struct device *dev)
{
	struct xcresample_device *xcresample = dev_get_drvdata(dev);

	xvip_write(&xcresample->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

#else

#define xcresample_pm_suspend	NULL
#define xcresample_pm_resume	NULL

#endif /* CONFIG_PM */

/*
 * Platform Device Driver
 */

static int xcresample_parse_of(struct xcresample_device *xcresample)
{
	struct device_node *node = xcresample->xvip.dev->of_node;
	int ret;

	ret = xvip_of_get_formats(node,
			&xcresample->vip_formats[XCRESAMPLE_PAD_SINK],
			&xcresample->vip_formats[XCRESAMPLE_PAD_SOURCE]);
	if (ret < 0)
		dev_err(xcresample->xvip.dev, "invalid format in DT");

	return ret;
}

static int xcresample_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xcresample_device *xcresample;
	struct resource *res;
	u32 version;
	int ret;

	xcresample = devm_kzalloc(&pdev->dev, sizeof(*xcresample), GFP_KERNEL);
	if (!xcresample)
		return -ENOMEM;

	xcresample->xvip.dev = &pdev->dev;

	ret = xcresample_parse_of(xcresample);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	xcresample->xvip.iomem = devm_request_and_ioremap(&pdev->dev, res);
	if (xcresample->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xcresample->xvip.subdev;
	v4l2_subdev_init(subdev, &xcresample_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xcresample_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xcresample);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xcresample_init_formats(subdev, NULL);

	xcresample->pads[XCRESAMPLE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xcresample->pads[XCRESAMPLE_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xcresample_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xcresample->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xcresample->ctrl_handler, 2);
	xcresample_field.def =
		(xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING) &
		 XCRESAMPLE_FIELD_MASK) >> XCRESAMPLE_FIELD_SHIFT;
	v4l2_ctrl_new_custom(&xcresample->ctrl_handler, &xcresample_field,
			     NULL);
	xcresample_chroma.def =
		(xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING) &
		 XCRESAMPLE_CHROMA_MASK) >> XCRESAMPLE_CHROMA_SHIFT;
	v4l2_ctrl_new_custom(&xcresample->ctrl_handler, &xcresample_chroma,
			     NULL);
	if (xcresample->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xcresample->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xcresample->ctrl_handler;

	platform_set_drvdata(pdev, xcresample);

	version = xvip_read(&xcresample->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xcresample->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xcresample_remove(struct platform_device *pdev)
{
	struct xcresample_device *xcresample = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xcresample->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xcresample->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct dev_pm_ops xcresample_pm_ops = {
	.suspend	= xcresample_pm_suspend,
	.resume		= xcresample_pm_resume,
};

static const struct of_device_id xcresample_of_id_table[] = {
	{ .compatible = "xlnx,axi-cresample" },
	{ }
};
MODULE_DEVICE_TABLE(of, xcresample_of_id_table);

static struct platform_driver xcresample_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-cresample",
		.pm		= &xcresample_pm_ops,
		.of_match_table	= of_match_ptr(xcresample_of_id_table),
	},
	.probe			= xcresample_probe,
	.remove			= xcresample_remove,
};

module_platform_driver(xcresample_driver);

MODULE_DESCRIPTION("Xilinx Chroma Resampler Driver");
MODULE_LICENSE("GPL v2");
