/*
 * Xilinx Video IP Core
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>

#include "xilinx-vip.h"

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static const struct xvip_video_format xvip_video_formats[] = {
	{ "rbg", 8, 3, V4L2_MBUS_FMT_RBG888_1X24, 0 },
	{ "xrgb", 8, 4, V4L2_MBUS_FMT_RGB888_1X32_PADHI, V4L2_PIX_FMT_BGR32 },
	{ "yuv422", 8, 2, V4L2_MBUS_FMT_UYVY8_1X16, V4L2_PIX_FMT_YUYV },
	{ "yuv444", 8, 3, V4L2_MBUS_FMT_VUY888_1X24, V4L2_PIX_FMT_YUV444 },
	{ "bayer", 8, 1, V4L2_MBUS_FMT_SRGGB8_1X8, V4L2_PIX_FMT_SGRBG8 },
};

/**
 * xvip_get_format_by_code - Retrieve format information for a media bus code
 * @code: the format media bus code
 *
 * Return: a pointer to the format information structure corresponding to the
 * given V4L2 media bus format @code, or %NULL if no corresponding format can be
 * found.
 */
const struct xvip_video_format *xvip_get_format_by_code(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->code == code)
			return format;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(xvip_get_format_by_code);

/**
 * xvip_get_format_by_fourcc - Retrieve format information for a 4CC
 * @fourcc: the format 4CC
 *
 * Return: a pointer to the format information structure corresponding to the
 * given V4L2 format @fourcc, or %NULL if no corresponding format can be found.
 */
const struct xvip_video_format *xvip_get_format_by_fourcc(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (format->fourcc == fourcc)
			return format;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(xvip_get_format_by_fourcc);

/**
 * xvip_get_format - Retrieve format information for name and width
 * @name: the format name string
 * @width: the format width in bits per component
 *
 * Return: a pointer to the format information structure corresponding to the
 * format name and width, or %NULL if no corresponding format can be found.
 */
const struct xvip_video_format *xvip_get_format(const char *name, u32 width)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(xvip_video_formats); ++i) {
		const struct xvip_video_format *format = &xvip_video_formats[i];

		if (strcmp(format->name, name) == 0 && format->width == width)
			return format;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(xvip_get_format);

/**
 * xvip_of_get_format - Parse a device tree node and return format information
 * @node: the device tree node
 *
 * Read the xlnx,axi-video-format and xlnx,axi-video-width properties from the
 * device tree @node passed as an argument and return the corresponding format
 * information.
 *
 * Return: a pointer to the format information structure corresponding to the
 * format name and width, or %NULL if no corresponding format can be found.
 */
const struct xvip_video_format *xvip_of_get_format(struct device_node *node)
{
	const char *name;
	u32 width;
	int ret;

	ret = of_property_read_string(node, "xlnx,axi-video-format", &name);
	if (ret < 0)
		return NULL;

	ret = of_property_read_u32(node, "xlnx,axi-video-width", &width);
	if (ret < 0)
		return NULL;

	return xvip_get_format(name, width);
}
EXPORT_SYMBOL_GPL(xvip_of_get_format);

/**
 * xvip_of_get_formats - Parse a device tree node and return format information
 * @node: the device tree node
 * @input_format: the returning input format
 * @output_format: the returning output format
 *
 * Read the xlnx,axi-video-input-format, xlnx,axi-video-output-format,  and
 * xlnx,axi-video-width properties from the device tree @node passed as
 * an argument and return the corresponding format information through
 * the passed arguments input_format and output_format.
 *
 * Return: 0 if the format is found, and the return code if no corresponding
 * format can be found.
 */
int xvip_of_get_formats(struct device_node *node,
			const struct xvip_video_format **input_format,
			const struct xvip_video_format **output_format)
{
	const char *name;
	u32 width;
	int ret;

	ret = of_property_read_u32(node, "xlnx,axi-video-width", &width);
	if (ret < 0)
		return ret;

	ret = of_property_read_string(node, "xlnx,axi-input-video-format",
				      &name);
	if (ret < 0)
		return ret;

	*input_format = xvip_get_format(name, width);
	if (*input_format == NULL)
		return -EINVAL;

	ret = of_property_read_string(node, "xlnx,axi-output-video-format",
				      &name);
	if (ret < 0)
		return ret;

	*output_format = xvip_get_format(name, width);
	if (*output_format == NULL)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(xvip_of_get_formats);

/**
 * xvip_enum_mbus_code - Enumerate the media format code
 * @subdev: V4L2 subdevice
 * @fh: V4L2 subdevice file handle
 * @code: returning media bus code
 *
 * Enumerate the media bus code of the subdevice. Return the corresponding
 * pad format code.
 *
 * Return: 0 if the media bus code is found, or -EINVAL if the format index
 * is not valid.
 */
int xvip_enum_mbus_code(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct v4l2_mbus_framefmt *format;

	if (code->index)
		return -EINVAL;

	format = v4l2_subdev_get_try_format(fh, code->pad);

	code->code = format->code;

	return 0;
}
EXPORT_SYMBOL_GPL(xvip_enum_mbus_code);

/**
 * xvip_enum_frame_size - Enumerate the media bus frame size
 * @subdev: V4L2 subdevice
 * @fh: V4L2 subdevice file handle
 * @fse: returning media bus frame size
 *
 * Enumerate the media bus frame size of the subdevice, such as min/max
 * width and height.
 *
 * Return: 0 if the media bus frame size is found, or -EINVAL
 * if the index or the code is not valid.
 */
int xvip_enum_frame_size(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh,
			 struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	if (fse->pad == XVIP_PAD_SINK) {
		fse->min_width = XVIP_MIN_WIDTH;
		fse->max_width = XVIP_MAX_WIDTH;
		fse->min_height = XVIP_MIN_HEIGHT;
		fse->max_height = XVIP_MAX_HEIGHT;
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
EXPORT_SYMBOL_GPL(xvip_enum_frame_size);
