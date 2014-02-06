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

#define XCRESAMPLE_ENCODING			0x100
#define XCRESAMPLE_ENCODING_FIELD		(1 << 7)
#define XCRESAMPLE_ENCODING_CHROMA		(1 << 8)

/**
 * struct xcresample_device - Xilinx CRESAMPLE device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @default_formats: default V4L2 media bus formats
 * @formats: V4L2 media bus formats at the sink and source pads
 * @vip_formats: Xilinx Video IP formats
 * @ctrl_handler: control handler
 */
struct xcresample_device {
	struct xvip_device xvip;

	struct media_pad pads[2];

	struct v4l2_mbus_framefmt default_formats[2];
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
	const u32 width = xcresample->formats[XVIP_PAD_SINK].width;
	const u32 height = xcresample->formats[XVIP_PAD_SINK].height;

	if (!enable) {
		xvip_stop(&xcresample->xvip);
		return 0;
	}

	xvip_set_frame_size(&xcresample->xvip, width, height);

	xvip_start(&xcresample->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

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

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format_size(__format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	__format = __xcresample_get_pad_format(xcresample, fh, XVIP_PAD_SOURCE,
					       fmt->which);

	xvip_set_format_size(__format, fmt);

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xcresample_init_format - Initialize formats on all pads
 * @subdev: cresample V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xcresample_init_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh)
{
	struct xcresample_device *xcresample = to_cresample(subdev);
	struct v4l2_mbus_framefmt *__format;
	u32 which;

	which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;

	__format = __xcresample_get_pad_format(xcresample, fh, XVIP_PAD_SINK,
					       which);
	*__format = xcresample->default_formats[XVIP_PAD_SINK];

	__format = __xcresample_get_pad_format(xcresample, fh, XVIP_PAD_SOURCE,
					       which);
	*__format = xcresample->default_formats[XVIP_PAD_SOURCE];
}

static int xcresample_open(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh)
{
	xcresample_init_format(subdev, fh);

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
		reg = ctrl->val ? (reg | XCRESAMPLE_ENCODING_FIELD) :
			(reg & ~XCRESAMPLE_ENCODING_FIELD);
		xvip_write(&xcresample->xvip, XCRESAMPLE_ENCODING, reg);
		return 0;
	case V4L2_CID_XILINX_CRESAMPLE_CHROMA_PARITY:
		reg = xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING);
		reg = ctrl->val ? (reg | XCRESAMPLE_ENCODING_CHROMA) :
			(reg & ~XCRESAMPLE_ENCODING_CHROMA);
		xvip_write(&xcresample->xvip, XCRESAMPLE_ENCODING, reg);
		return 0;
	}

	return -EINVAL;

}

static const struct v4l2_ctrl_ops xcresample_ctrl_ops = {
	.s_ctrl	= xcresample_s_ctrl,
};

static struct v4l2_subdev_video_ops xcresample_video_ops = {
	.s_stream = xcresample_s_stream,
};

static struct v4l2_subdev_pad_ops xcresample_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xcresample_get_format,
	.set_fmt		= xcresample_set_format,
};

static struct v4l2_subdev_ops xcresample_ops = {
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
	struct device *dev = xcresample->xvip.dev;
	struct device_node *node = xcresample->xvip.dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	const struct xvip_video_format *vip_format;
	u32 port_id;
	int ret;

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

			ret = of_property_read_u32(port, "reg", &port_id);
			if (ret < 0) {
				dev_err(dev, "no reg in DT");
				return -EINVAL;
			}

			if (port_id != 0 && port_id != 1) {
				dev_err(dev, "invalid reg in DT");
				return -EINVAL;
			}

			xcresample->vip_formats[port_id] = vip_format;
		}
	}

	return 0;
}

static int xcresample_probe(struct platform_device *pdev)
{
	struct xcresample_device *xcresample;
	struct resource *res;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *default_format;
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

	xcresample->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xcresample->xvip.iomem))
		return PTR_ERR(xcresample->xvip.iomem);

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xcresample->xvip.subdev;
	v4l2_subdev_init(subdev, &xcresample_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xcresample_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xcresample);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Initialize default formats */
	default_format = &xcresample->default_formats[XVIP_PAD_SINK];
	default_format->code = xcresample->vip_formats[XVIP_PAD_SINK]->code;
	default_format->field = V4L2_FIELD_NONE;
	default_format->colorspace = V4L2_COLORSPACE_SRGB;
	xvip_get_frame_size(&xcresample->xvip, &default_format->width,
			    &default_format->height);

	default_format = &xcresample->default_formats[XVIP_PAD_SOURCE];
	*default_format = xcresample->default_formats[XVIP_PAD_SINK];
	default_format->code = xcresample->vip_formats[XVIP_PAD_SOURCE]->code;

	xcresample_init_format(subdev, NULL);

	xcresample->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xcresample->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xcresample_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xcresample->pads, 0);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&xcresample->ctrl_handler, 2);
	xcresample_field.def =
		(xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING) &
		 XCRESAMPLE_ENCODING_FIELD) ? 1 : 0;
	v4l2_ctrl_new_custom(&xcresample->ctrl_handler, &xcresample_field,
			     NULL);
	xcresample_chroma.def =
		(xvip_read(&xcresample->xvip, XCRESAMPLE_ENCODING) &
		 XCRESAMPLE_ENCODING_CHROMA) ? 1 : 0;
	v4l2_ctrl_new_custom(&xcresample->ctrl_handler, &xcresample_chroma,
			     NULL);
	if (xcresample->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xcresample->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xcresample->ctrl_handler;

	platform_set_drvdata(pdev, xcresample);

	xvip_print_version(&xcresample->xvip);

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
		.of_match_table	= xcresample_of_id_table,
	},
	.probe			= xcresample_probe,
	.remove			= xcresample_remove,
};

module_platform_driver(xcresample_driver);

MODULE_DESCRIPTION("Xilinx Chroma Resampler Driver");
MODULE_LICENSE("GPL v2");
