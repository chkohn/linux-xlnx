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
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XCFA_BAYER_PHASE			0x100
#define XCFA_BAYER_PHASE_RGGB			0
#define XCFA_BAYER_PHASE_GRBG			1
#define XCFA_BAYER_PHASE_GBRG			2
#define XCFA_BAYER_PHASE_BGGR			3

/**
 * struct xcfa_device - Xilinx CFA device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @default_formats: default V4L2 media bus formats
 * @formats: V4L2 media bus formats
 * @vip_formats: Xilinx Video IP formats
 */
struct xcfa_device {
	struct xvip_device xvip;

	struct media_pad pads[2];

	struct v4l2_mbus_framefmt default_formats[2];
	struct v4l2_mbus_framefmt formats[2];
	const struct xvip_video_format *vip_formats[2];
};

static inline struct xcfa_device *to_cfa(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xcfa_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static int xcfa_get_bayer_phase(const unsigned int code)
{
	switch (code) {
	case V4L2_MBUS_FMT_SRGGB8_1X8:
		return XCFA_BAYER_PHASE_RGGB;
	case V4L2_MBUS_FMT_SGRBG8_1X8:
		return XCFA_BAYER_PHASE_GRBG;
	case V4L2_MBUS_FMT_SGBRG8_1X8:
		return XCFA_BAYER_PHASE_GBRG;
	case V4L2_MBUS_FMT_SBGGR8_1X8:
		return XCFA_BAYER_PHASE_BGGR;
	}

	return -EINVAL;
}

static int xcfa_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xcfa_device *xcfa = to_cfa(subdev);
	const u32 width = xcfa->formats[XVIP_PAD_SINK].width;
	const u32 height = xcfa->formats[XVIP_PAD_SINK].height;
	const unsigned int code = xcfa->formats[XVIP_PAD_SINK].code;
	u32 bayer_phase;

	if (!enable) {
		xvip_stop(&xcfa->xvip);
		return 0;
	}

	bayer_phase = xcfa_get_bayer_phase(code);

	xvip_write(&xcfa->xvip, XCFA_BAYER_PHASE, bayer_phase);

	xvip_set_frame_size(&xcfa->xvip, width, height);

	xvip_start(&xcfa->xvip);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
__xcfa_get_pad_format(struct xcfa_device *xcfa,
		      struct v4l2_subdev_fh *fh,
		      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xcfa->formats[pad];
	default:
		return NULL;
	}
}

static int xcfa_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xcfa_device *xcfa = to_cfa(subdev);

	fmt->format = *__xcfa_get_pad_format(xcfa, fh, fmt->pad, fmt->which);

	return 0;
}

static int xcfa_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xcfa_device *xcfa = to_cfa(subdev);
	struct v4l2_mbus_framefmt *__format;
	int bayer_phase;

	__format = __xcfa_get_pad_format(xcfa, fh, fmt->pad, fmt->which);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		fmt->format = *__format;
		return 0;
	}

	bayer_phase = xcfa_get_bayer_phase(fmt->format.code);
	if (bayer_phase >= 0) {
		xcfa->vip_formats[XVIP_PAD_SINK] =
			xvip_get_format_by_code(fmt->format.code);
		__format->code = fmt->format.code;
	}

	xvip_set_format_size(__format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad */
	__format = __xcfa_get_pad_format(xcfa, fh, XVIP_PAD_SOURCE, fmt->which);

	xvip_set_format_size(__format, fmt);

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xcfa_init_format - Initialize formats on all pads
 * @subdev: cfa V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xcfa_init_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh)
{
	struct xcfa_device *xcfa = to_cfa(subdev);
	struct v4l2_mbus_framefmt *__format;
	u32 which;

	which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;

	__format = __xcfa_get_pad_format(xcfa, fh, XVIP_PAD_SINK, which);
	*__format = xcfa->default_formats[XVIP_PAD_SINK];

	__format = __xcfa_get_pad_format(xcfa, fh, XVIP_PAD_SOURCE, which);
	*__format = xcfa->default_formats[XVIP_PAD_SOURCE];
}

static int xcfa_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xcfa_init_format(subdev, fh);

	return 0;
}

static int xcfa_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

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
	.video  = &xcfa_video_ops,
	.pad    = &xcfa_pad_ops,
};

static const struct v4l2_subdev_internal_ops xcfa_internal_ops = {
	.open	= xcfa_open,
	.close	= xcfa_close,
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
	struct device *dev = xcfa->xvip.dev;
	struct device_node *node = xcfa->xvip.dev->of_node;
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

			xcfa->vip_formats[port_id] = vip_format;
		}
	}

	return 0;
}

static int xcfa_probe(struct platform_device *pdev)
{
	struct xcfa_device *xcfa;
	struct resource *res;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *default_format;
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

	xcfa->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xcfa->xvip.iomem))
		return PTR_ERR(xcfa->xvip.iomem);

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xcfa->xvip.subdev;
	v4l2_subdev_init(subdev, &xcfa_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xcfa_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xcfa);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Initialize default formats */
	default_format = &xcfa->default_formats[XVIP_PAD_SINK];
	default_format->code = xcfa->vip_formats[XVIP_PAD_SINK]->code;
	default_format->field = V4L2_FIELD_NONE;
	default_format->colorspace = V4L2_COLORSPACE_SRGB;
	xvip_get_frame_size(&xcfa->xvip, &default_format->width,
			    &default_format->height);

	default_format = &xcfa->default_formats[XVIP_PAD_SOURCE];
	*default_format = xcfa->default_formats[XVIP_PAD_SINK];
	default_format->code = xcfa->vip_formats[XVIP_PAD_SOURCE]->code;

	xcfa_init_format(subdev, NULL);

	xcfa->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xcfa->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xcfa_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xcfa->pads, 0);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, xcfa);

	xvip_print_version(&xcfa->xvip);

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register subdev\n");
		goto error;
	}

	return 0;

error:
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xcfa_remove(struct platform_device *pdev)
{
	struct xcfa_device *xcfa = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xcfa->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
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
		.of_match_table	= xcfa_of_id_table,
	},
	.probe			= xcfa_probe,
	.remove			= xcfa_remove,
};

module_platform_driver(xcfa_driver);

MODULE_DESCRIPTION("Xilinx Color Filter Array Driver");
MODULE_LICENSE("GPL v2");
