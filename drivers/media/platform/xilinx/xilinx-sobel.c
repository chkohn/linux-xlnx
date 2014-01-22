/*
 * Xilinx Sobel Filter
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XSOBEL_MIN_WIDTH			32
#define XSOBEL_DEF_WIDTH			1920
#define XSOBEL_MAX_WIDTH			7680
#define XSOBEL_MIN_HEIGHT			32
#define XSOBEL_DEF_HEIGHT			1080
#define XSOBEL_MAX_HEIGHT			7680

#define XSOBEL_PAD_SINK				0
#define XSOBEL_PAD_SOURCE			1

#define XSOBEL_REG_CTRL				0x00
#define XSOBEL_REG_CTRL_START			(1 << 0)
#define XSOBEL_REG_CTRL_DONE			(1 << 1)
#define XSOBEL_REG_CTRL_IDLE			(1 << 2)
#define XSOBEL_REG_CTRL_READY			(1 << 3)
#define XSOBEL_REG_CTRL_AUTO_RESTART		(1 << 7)
#define XSOBEL_REG_GIE				0x04
#define XSOBEL_REG_GIE_GIE			(1 << 0)
#define XSOBEL_REG_IER				0x08
#define XSOBEL_REG_IER_DONE			(1 << 0)
#define XSOBEL_REG_IER_READY			(1 << 1)
#define XSOBEL_REG_ISR				0x0c
#define XSOBEL_REG_ISR_DONE			(1 << 0)
#define XSOBEL_REG_ISR_READY			(1 << 1)
#define XSOBEL_REG_ROWS				0x14
#define XSOBEL_REG_COLS				0x1c
#define XSOBEL_REG_INVERT			0xc4
#define XSOBEL_REG_XRnCm(r, c)			(0x24 + 8 * (3*(r) + (c)))
#define XSOBEL_REG_YRnCm(r, c)			(0x6c + 8 * (3*(r) + (c)))
#define XSOBEL_REG_HIGH_THRESH			0xb4
#define XSOBEL_REG_LOW_THRESH			0xbc
#define XSOBEL_REG_INVERT			0xc4

/**
 * struct xsobel_device - Xilinx Sobel Filter device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @formats: active V4L2 media bus formats at the sink and source pads
 * @vip_format: format information corresponding to the sink pad active format
 */
struct xsobel_device {
	struct xvip_device xvip;
	struct media_pad pads[2];

	struct v4l2_mbus_framefmt formats[2];
	const struct xvip_video_format *vip_format;
};

static inline struct xsobel_device *to_sobel(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xsobel_device, xvip.subdev);
}

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static irqreturn_t xsobel_irq_handler(int irq, void *data)
{
	struct xsobel_device *xsobel = data;
	u32 status;

	status = xvip_read(&xsobel->xvip, XSOBEL_REG_ISR);
	xvip_write(&xsobel->xvip, XSOBEL_REG_ISR, status);

	dev_dbg(xsobel->xvip.dev, "%s: status 0x%08x\n", __func__, status);

	return status ? IRQ_HANDLED : IRQ_NONE;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xsobel_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xsobel_device *xsobel = to_sobel(subdev);
	struct v4l2_mbus_framefmt *format = &xsobel->formats[XSOBEL_PAD_SINK];

	if (!enable) {
		xvip_write(&xsobel->xvip, XSOBEL_REG_GIE, 0);
		xvip_write(&xsobel->xvip, XSOBEL_REG_CTRL, 0);
		return 0;
	}

	xvip_write(&xsobel->xvip, XSOBEL_REG_COLS, format->width);
	xvip_write(&xsobel->xvip, XSOBEL_REG_ROWS, format->height);

	xvip_write(&xsobel->xvip, XSOBEL_REG_IER, XSOBEL_REG_IER_DONE);
	xvip_write(&xsobel->xvip, XSOBEL_REG_GIE, XSOBEL_REG_GIE_GIE);

	xvip_write(&xsobel->xvip, XSOBEL_REG_CTRL,
		   XSOBEL_REG_CTRL_AUTO_RESTART | XSOBEL_REG_CTRL_START);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int xsobel_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct xsobel_device *xsobel = to_sobel(subdev);

	if (code->index)
		return -EINVAL;

	code->code = xsobel->vip_format->code;

	return 0;
}

static int xsobel_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XSOBEL_PAD_SINK) {
		fse->min_width = XSOBEL_MIN_WIDTH;
		fse->max_width = XSOBEL_MAX_WIDTH;
		fse->min_height = XSOBEL_MIN_HEIGHT;
		fse->max_height = XSOBEL_MAX_HEIGHT;
	} else {
		/* The size on the source pad are fixed and always identical to
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
__xsobel_get_pad_format(struct xsobel_device *xsobel, struct v4l2_subdev_fh *fh,
			unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xsobel->formats[pad];
	default:
		return NULL;
	}
}

static int xsobel_get_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct xsobel_device *xsobel = to_sobel(subdev);

	fmt->format =
		*__xsobel_get_pad_format(xsobel, fh, fmt->pad, fmt->which);

	return 0;
}

static int xsobel_set_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *format)
{
	struct xsobel_device *xsobel = to_sobel(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xsobel_get_pad_format(xsobel, fh, format->pad,
					   format->which);

	if (format->pad == XSOBEL_PAD_SOURCE) {
		format->format = *__format;
		return 0;
	}

	__format->code = xsobel->vip_format->code;
	__format->width = clamp_t(unsigned int, format->format.width,
				  XSOBEL_MIN_WIDTH, XSOBEL_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, format->format.height,
				   XSOBEL_MIN_HEIGHT, XSOBEL_MAX_HEIGHT);

	format->format = *__format;

	/* Propagate the format to the source pad. */
	__format = __xsobel_get_pad_format(xsobel, fh, XSOBEL_PAD_SOURCE,
					   format->which);
	*__format = format->format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/*
 * xsobel_init_formats - Initialize formats on all pads
 * @subdev: sobel filter V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xsobel_init_formats(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh)
{
	struct xsobel_device *xsobel = to_sobel(subdev);
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));

	format.pad = XSOBEL_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = xsobel->vip_format->code;
	format.format.width = XSOBEL_DEF_WIDTH;
	format.format.height = XSOBEL_DEF_HEIGHT;

	xsobel_set_format(subdev, fh, &format);
}

static int xsobel_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xsobel_init_formats(subdev, fh);

	return 0;
}

static int xsobel_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_core_ops xsobel_core_ops = {
};

static struct v4l2_subdev_video_ops xsobel_video_ops = {
	.s_stream = xsobel_s_stream,
};

static struct v4l2_subdev_pad_ops xsobel_pad_ops = {
	.enum_mbus_code = xsobel_enum_mbus_code,
	.enum_frame_size = xsobel_enum_frame_size,
	.get_fmt = xsobel_get_format,
	.set_fmt = xsobel_set_format,
};

static struct v4l2_subdev_ops xsobel_ops = {
	.core   = &xsobel_core_ops,
	.video  = &xsobel_video_ops,
	.pad    = &xsobel_pad_ops,
};

static const struct v4l2_subdev_internal_ops xsobel_internal_ops = {
	.open = xsobel_open,
	.close = xsobel_close,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xsobel_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static void xsobel_configure(struct xsobel_device *xsobel)
{
	static const s32 x_coeffs[3][3] = {
		{ 1, 0, -1 },
		{ 2, 0, -2 },
		{ 1, 0, -1 },
	};
	static const s32 y_coeffs[3][3] = {
		{  1,  2,  1 },
		{  0,  0,  0 },
		{ -1, -2, -1 },
	};
	unsigned int x;
	unsigned int y;

	for (y = 0; y < 3; ++y) {
		for (x = 0; x < 3; ++x) {
			xvip_write(&xsobel->xvip, XSOBEL_REG_XRnCm(y, x),
				   (u32)x_coeffs[y][x]);
			xvip_write(&xsobel->xvip, XSOBEL_REG_YRnCm(y, x),
				   (u32)y_coeffs[y][x]);
		}
	}

	xvip_write(&xsobel->xvip, XSOBEL_REG_HIGH_THRESH, 200);
	xvip_write(&xsobel->xvip, XSOBEL_REG_LOW_THRESH, 100);
	xvip_write(&xsobel->xvip, XSOBEL_REG_INVERT, 0);
}

static int xsobel_parse_of(struct xsobel_device *xsobel)
{
	struct device_node *node = xsobel->xvip.dev->of_node;

	xsobel->vip_format = xvip_of_get_format(node);
	if (xsobel->vip_format == NULL) {
		dev_err(xsobel->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	return 0;
}

static int xsobel_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xsobel_device *xsobel;
	struct resource *mem;
	struct resource *irq;
	int ret;

	xsobel = devm_kzalloc(&pdev->dev, sizeof(*xsobel), GFP_KERNEL);
	if (!xsobel)
		return -ENOMEM;

	xsobel->xvip.dev = &pdev->dev;

	ret = xsobel_parse_of(xsobel);
	if (ret < 0)
		return ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL)
		return -ENODEV;

	xsobel->xvip.iomem = devm_request_and_ioremap(&pdev->dev, mem);
	if (xsobel->xvip.iomem == NULL)
		return -ENODEV;

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq == NULL)
		return -ENODEV;

	ret = devm_request_irq(&pdev->dev, irq->start, xsobel_irq_handler,
			       IRQF_SHARED, dev_name(&pdev->dev), xsobel);
	if (ret < 0)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xsobel->xvip.subdev;
	v4l2_subdev_init(subdev, &xsobel_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xsobel_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xsobel);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xsobel_init_formats(subdev, NULL);

	xsobel->pads[XSOBEL_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xsobel->pads[XSOBEL_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xsobel_media_ops;
	ret = media_entity_init(&subdev->entity, 2, xsobel->pads, 0);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, xsobel);

	xsobel_configure(xsobel);

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

static int xsobel_remove(struct platform_device *pdev)
{
	struct xsobel_device *xsobel = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xsobel->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xsobel_of_id_table[] = {
	{ .compatible = "xlnx,axi-sobel" },
	{ }
};
MODULE_DEVICE_TABLE(of, xsobel_of_id_table);

static struct platform_driver xsobel_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "xilinx-axi-sobel",
		.of_match_table = xsobel_of_id_table,
	},
	.probe = xsobel_probe,
	.remove = xsobel_remove,
};

module_platform_driver(xsobel_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Sobel Filter Driver");
MODULE_LICENSE("GPL v2");
