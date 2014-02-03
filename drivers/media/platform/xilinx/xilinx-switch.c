/*
 * Xilinx Video Switch
 *
 * Copyright (C) 2014 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XSW_CORE_CH_CTRL			0x0100
#define XSW_CORE_CH_CTRL_FORCE			(1 << 3)

#define XSW_SWITCH_STATUS			0x0104

/**
 * struct xswitch_device - Xilinx Video Switch device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @nsinks: number of sink pads (2 to 8)
 * @nsources: number of source pads (1 to 8)
 * @formats: active V4L2 media bus formats on sink pads
 */
struct xswitch_device {
	struct xvip_device xvip;

	struct media_pad *pads;
	unsigned int nsinks;
	unsigned int nsources;

	unsigned int routing[8];

	struct v4l2_mbus_framefmt *formats;
};

static inline struct xswitch_device *to_xsw(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xswitch_device, xvip.subdev);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xsw_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	unsigned int i;
	u32 routing = 0;

	if (!enable) {
		xvip_stop(&xsw->xvip);
		return 0;
	}

	for (i = 0; i < xsw->nsources; ++i)
		routing |= (XSW_CORE_CH_CTRL_FORCE | xsw->routing[i])
			<< (i * 4);

	xvip_write(&xsw->xvip, XSW_CORE_CH_CTRL, routing);

	xvip_write(&xsw->xvip, XVIP_CTRL_CONTROL,
		   (((1 << xsw->nsources) - 1) << 4) |
		   XVIP_CTRL_CONTROL_SW_ENABLE);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static struct v4l2_mbus_framefmt *
xsw_get_pad_format(struct xswitch_device *xsw, struct v4l2_subdev_fh *fh,
		   unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xsw->formats[pad];
	default:
		return NULL;
	}
}

static int xsw_get_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	unsigned int pad;

	pad = fmt->pad < xsw->nsinks ? fmt->pad
	    : xsw->routing[fmt->pad - xsw->nsinks];

	fmt->format = *xsw_get_pad_format(xsw, fh, pad, fmt->which);

	return 0;
}

static int xsw_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	struct v4l2_mbus_framefmt *__format;
	unsigned int pad;

	pad = fmt->pad < xsw->nsinks ? fmt->pad
	    : xsw->routing[fmt->pad - xsw->nsinks];

	__format = xsw_get_pad_format(xsw, fh, pad, fmt->which);

	/* The source pad format is always identical to the sink pad format. */
	if (fmt->pad >= xsw->nsinks) {
		fmt->format = *__format;
		return 0;
	}

	__format->code = fmt->format.code;
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XVIP_MIN_WIDTH, XVIP_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XVIP_MIN_HEIGHT, XVIP_MAX_HEIGHT);
	__format->field = V4L2_FIELD_NONE;
	__format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *__format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/**
 * xsw_init_formats - Initialize formats on all pads
 * @subdev: tpgper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 *
 * The function sets the format on pad 0 only. In two pads mode, this is the
 * sink pad and the set format handler will propagate the format to the source
 * pad. In one pad mode this is the source pad.
 */
static void xsw_init_formats(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh)
{
	struct xswitch_device *xsw = to_xsw(subdev);
	struct v4l2_subdev_format format;
	unsigned int i;

	for (i = 0; i < xsw->nsinks; ++i) {
		memset(&format, 0, sizeof(format));

		format.pad = 0;
		format.which = fh ? V4L2_SUBDEV_FORMAT_TRY
			     : V4L2_SUBDEV_FORMAT_ACTIVE;
		format.format.width = 1920;
		format.format.height = 1080;

		xsw_set_format(subdev, fh, &format);
	}
}

static int xsw_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xsw_init_formats(subdev, fh);

	return 0;
}

static int xsw_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_video_ops xsw_video_ops = {
	.s_stream = xsw_s_stream,
};

static struct v4l2_subdev_pad_ops xsw_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xsw_get_format,
	.set_fmt = xsw_set_format,
};

static struct v4l2_subdev_ops xsw_ops = {
	.video = &xsw_video_ops,
	.pad = &xsw_pad_ops,
};

static const struct v4l2_subdev_internal_ops xsw_internal_ops = {
	.open = xsw_open,
	.close = xsw_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xsw_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xsw_parse_of(struct xswitch_device *xsw)
{
	struct device_node *node = xsw->xvip.dev->of_node;
	int ret;

	ret = of_property_read_u32(node, "#xlnx,inputs", &xsw->nsinks);
	if (ret < 0) {
		dev_err(xsw->xvip.dev, "missing or invalid #xlnx,%s property\n",
			"inputs");
		return ret;
	}

	ret = of_property_read_u32(node, "#xlnx,outputs", &xsw->nsources);
	if (ret < 0) {
		dev_err(xsw->xvip.dev, "missing or invalid #xlnx,%s property\n",
			"outputs");
		return ret;
	}

	return 0;
}

static int xsw_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xswitch_device *xsw;
	struct resource *res;
	unsigned int npads;
	unsigned int i;
	u32 version;
	int ret;

	xsw = devm_kzalloc(&pdev->dev, sizeof(*xsw), GFP_KERNEL);
	if (!xsw)
		return -ENOMEM;

	xsw->xvip.dev = &pdev->dev;

	ret = xsw_parse_of(xsw);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xsw->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xsw->xvip.iomem))
		return PTR_ERR(xsw->xvip.iomem);

	/* Initialize V4L2 subdevice and media entity. Pad numbers depend on the
	 * number of pads.
	 */
	npads = xsw->nsinks + xsw->nsources;
	xsw->pads = devm_kzalloc(&pdev->dev, npads * sizeof(*xsw->pads),
				 GFP_KERNEL);
	if (!xsw->pads)
		return -ENOMEM;

	for (i = 0; i < xsw->nsinks; ++i)
		xsw->pads[i].flags = MEDIA_PAD_FL_SINK;
	for (; i < npads; ++i)
		xsw->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	xsw->formats = devm_kzalloc(&pdev->dev,
				    xsw->nsinks * sizeof(*xsw->formats),
				    GFP_KERNEL);
	if (!xsw->formats)
		return -ENOMEM;

	for (i = 0; i < xsw->nsources; ++i)
		xsw->routing[i] = min(i, xsw->nsinks - 1);

	subdev = &xsw->xvip.subdev;
	v4l2_subdev_init(subdev, &xsw_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xsw_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xsw);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &xsw_media_ops;

	xsw_init_formats(subdev, NULL);

	ret = media_entity_init(&subdev->entity, npads, xsw->pads, 0);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, xsw);

	version = xvip_read(&xsw->xvip, XVIP_CTRL_VERSION);

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
	media_entity_cleanup(&subdev->entity);
	return ret;
}

static int xsw_remove(struct platform_device *pdev)
{
	struct xswitch_device *xsw = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xsw->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xsw_of_id_table[] = {
	{ .compatible = "xlnx,axi-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, xsw_of_id_table);

static struct platform_driver xsw_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "xilinx-axi-switch",
		.of_match_table	= xsw_of_id_table,
	},
	.probe			= xsw_probe,
	.remove			= xsw_remove,
};

module_platform_driver(xsw_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Video Switch Driver");
MODULE_LICENSE("GPL v2");
