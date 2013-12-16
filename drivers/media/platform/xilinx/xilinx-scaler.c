/*
 * Xilinx Scaler
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
#include <linux/fixp-arith.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-controls.h"
#include "xilinx-vip.h"

#define XSCALER_MIN_WIDTH			32
#define XSCALER_MAX_WIDTH			4096
#define XSCALER_MIN_HEIGHT			32
#define XSCALER_MAX_HEIGHT			4096

#define XSCALER_PAD_SINK			0
#define XSCALER_PAD_SOURCE			1

#define XSCALER_HSF				0x0100
#define XSCALER_VSF				0x0104
#define XSCALER_SF_MASK				0xffffff
#define XSCALER_SIZE_SHIFT			16
#define XSCALER_SIZE_MASK			0xfff
#define XSCALER_SOURCE_SIZE			0x0108
#define XSCALER_APERTURE_SHIFT			16
#define XSCALER_HAPERTURE			0x010c
#define XSCALER_VAPERTURE			0x0110
#define XSCALER_OUTPUT_SIZE			0x0114
#define XSCALER_COEF_DATA_IN			0x0134
#define XSCALER_COEF_DATA_IN_SHIFT		16

/**
 * struct xscaler_device - Xilinx Test Pattern Generator device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @vip_format: Xilinx Video IP format
 * @formats: V4L2 media bus formats at the sink and source pads
 * @num_hori_taps: number of vertical taps
 * @num_vert_taps: number of vertical taps
 * @max_num_phases: maximum number of phases
 * @separate_yc_coef: separate coefficients for Luma(y) and Chroma(c)
 * @separate_hv_coef: separate coefficients for Horizontal(h) and Vertical(v)
 */
struct xscaler_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_mbus_framefmt formats[2];
	u32 num_hori_taps;
	u32 num_vert_taps;
	u32 max_num_phases;
	bool separate_yc_coef;
	bool separate_hv_coef;
};

static inline struct xscaler_device *to_scaler(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xscaler_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

static inline fixp_t lanczos(fixp_t x, fixp_t a)
{
	fixp_t pi;
	fixp_t numerator;
	fixp_t denominator;
	fixp_t temp;

	if (x < -a || x > a) {
		return 0;
	} else if (x == 0) {
		return 1;
	} else {
		/* a * sin(pi * x) * sin(pi * x / a) / (pi * pi * x * x) */
		pi = (fixp_new(31459) << FRAC_N) / fixp_new(10000);

		if (x < 0)
			x = -x;

		/* sin(pi * x) */
		temp = fixp_mult(fixp_new(180), x);
		temp = fixp_sin(temp >> FRAC_N);

		/* a * sin(pi * x) */
		numerator = fixp_mult(temp , a);

		/* sin(pi * x / a) */
		temp = (fixp_mult(fixp_new(180), x) << FRAC_N) / a;
		temp = fixp_sin(temp >> FRAC_N);

		/* a * sin(pi * x) * sin(pi * x / a) */
		numerator = fixp_mult(temp, numerator);

		/* pi * pi * x * x */
		denominator = fixp_mult(pi, pi);
		temp = fixp_mult(x, x);
		denominator = fixp_mult(temp, denominator);

		return ((numerator << FRAC_N) / denominator);
	}
}

/**
 * xscaler_gen_coefs - generate the coefficient table
 * @xscaler: scaler device
 * @taps: maximum coefficient tap index
 */
static inline int xscaler_gen_coefs(struct xscaler_device *xscaler, s16 taps)
{
	fixp_t *coef;
	fixp_t sum;
	fixp_t dy;
	u32 coef_val;
	s16 phases = (s16)xscaler->max_num_phases;
	s16 i;
	s16 j;

	coef = kcalloc(phases, sizeof(*coef), GFP_KERNEL);
	if (!coef)
		return -ENOMEM;

	for (i = 0; i < phases; i++) {
		dy = ((fixp_new(i) << FRAC_N) / fixp_new(phases));

		/* Generate Lanczos coefficients */
		for (j = 0; j < taps; j++) {
			coef[j] = lanczos(fixp_new(j - (taps >> 1)) + dy,
					  fixp_new(taps >> 1));
			sum += coef[j];
		}

		/* Program coefficients */
		for (j = 0; j < taps; j += 2) {
			/* Normalize and multiply coefficients */
			coef_val = (((coef[j] << FRAC_N) << (FRAC_N - 2)) /
				    sum) & 0xffff;
			if (j < taps)
				coef_val |= ((((coef[j + 1] << FRAC_N) <<
					      (FRAC_N - 2)) / sum) & 0xffff) <<
					    16;

			xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
				   coef_val);
		}

		sum = 0;
	}

	kfree(coef);

	return 0;
}

static int xscaler_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	u32 in_width;
	u32 in_height;
	u32 out_width;
	u32 out_height;
	u32 scale_factor;

	if (!enable) {
		xvip_write(&xscaler->xvip, XVIP_CTRL_CONTROL,
			   XVIP_CTRL_CONTROL_SW_RESET);
		xvip_write(&xscaler->xvip, XVIP_CTRL_CONTROL, 0);
		return 0;
	}

	in_width = xscaler->formats[XSCALER_PAD_SINK].width & XSCALER_SIZE_MASK;
	in_height = xscaler->formats[XSCALER_PAD_SINK].height &
		    XSCALER_SIZE_MASK;
	xvip_write(&xscaler->xvip, XSCALER_SOURCE_SIZE,
		   (in_height << XSCALER_SIZE_SHIFT) | in_width);

	/* TODO: aperture is fixed to input width/height for now */
	xvip_write(&xscaler->xvip, XSCALER_HAPERTURE,
		   ((in_width - 1) << XSCALER_APERTURE_SHIFT));
	xvip_write(&xscaler->xvip, XSCALER_VAPERTURE,
		   ((in_height - 1) << XSCALER_APERTURE_SHIFT));

	out_width = xscaler->formats[XSCALER_PAD_SOURCE].width &
		   XSCALER_SIZE_MASK;
	out_height = xscaler->formats[XSCALER_PAD_SOURCE].height &
		    XSCALER_SIZE_MASK;
	xvip_write(&xscaler->xvip, XSCALER_OUTPUT_SIZE,
		   (out_height << XSCALER_SIZE_SHIFT) | out_width);

	scale_factor = ((in_width << 20) / out_width) & XSCALER_SF_MASK;
	xvip_write(&xscaler->xvip, XSCALER_HSF, scale_factor);

	scale_factor = ((in_height << 20) / out_height) & XSCALER_SF_MASK;
	xvip_write(&xscaler->xvip, XSCALER_VSF, scale_factor);

	xvip_write(&xscaler->xvip, XVIP_CTRL_CONTROL,
		   XVIP_CTRL_CONTROL_SW_ENABLE | XVIP_CTRL_CONTROL_REG_UPDATE);

	return 0;
}

/*
 * V4L2 Subdevice Pad Operations
 */

static int xscaler_enum_mbus_code(struct v4l2_subdev *subdev,
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

static int xscaler_enum_frame_size(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, fse->pad);

	if (fse->index || fse->code != format->code)
		return -EINVAL;

	fse->min_width = XSCALER_MIN_WIDTH;
	fse->max_width = XSCALER_MAX_WIDTH;
	fse->min_height = XSCALER_MIN_HEIGHT;
	fse->max_height = XSCALER_MAX_HEIGHT;

	return 0;
}

static struct v4l2_mbus_framefmt *
__xscaler_get_pad_format(struct xscaler_device *xscaler,
			 struct v4l2_subdev_fh *fh,
			 unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xscaler->formats[pad];
	default:
		return NULL;
	}
}

static int xscaler_get_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct xscaler_device *xscaler = to_scaler(subdev);

	fmt->format = *__xscaler_get_pad_format(xscaler, fh, fmt->pad,
						fmt->which);

	return 0;
}

static int xscaler_set_format(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xscaler_get_pad_format(xscaler, fh, fmt->pad, fmt->which);

	__format->code = xscaler->vip_format->code;
	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XSCALER_MIN_WIDTH, XSCALER_MAX_WIDTH);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XSCALER_MIN_HEIGHT, XSCALER_MAX_HEIGHT);

	fmt->format = *__format;

	return 0;
}

/*
 * V4L2 Subdevice Operations
 */

/**
 * xscaler_init_formats - Initialize formats on all pads
 * @subdev: scalerper V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static void xscaler_init_formats(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh)
{
	struct xscaler_device *xscaler = to_scaler(subdev);
	struct v4l2_subdev_format format;
	u32 size;

	memset(&format, 0, sizeof(format));

	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;

	size = xvip_read(&xscaler->xvip, XSCALER_SOURCE_SIZE);
	format.format.width = size & XSCALER_SIZE_MASK;
	format.format.height = (size >> XSCALER_SIZE_SHIFT) & XSCALER_SIZE_MASK;
	format.format.field = V4L2_FIELD_NONE;
	format.format.colorspace = V4L2_COLORSPACE_SRGB;

	format.pad = XSCALER_PAD_SINK;

	xscaler_set_format(subdev, fh, &format);

	size = xvip_read(&xscaler->xvip, XSCALER_OUTPUT_SIZE);
	format.format.width = size & XSCALER_SIZE_MASK;
	format.format.height = (size >> XSCALER_SIZE_SHIFT) & XSCALER_SIZE_MASK;

	format.pad = XSCALER_PAD_SOURCE;

	xscaler_set_format(subdev, fh, &format);
}

static int xscaler_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xscaler_init_formats(subdev, fh);

	return 0;
}

static int xscaler_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static struct v4l2_subdev_core_ops xscaler_core_ops = {
};

static struct v4l2_subdev_video_ops xscaler_video_ops = {
	.s_stream = xscaler_s_stream,
};

static struct v4l2_subdev_pad_ops xscaler_pad_ops = {
	.enum_mbus_code		= xscaler_enum_mbus_code,
	.enum_frame_size	= xscaler_enum_frame_size,
	.get_fmt		= xscaler_get_format,
	.set_fmt		= xscaler_set_format,
};

static struct v4l2_subdev_ops xscaler_ops = {
	.core   = &xscaler_core_ops,
	.video  = &xscaler_video_ops,
	.pad    = &xscaler_pad_ops,
};

static const struct v4l2_subdev_internal_ops xscaler_internal_ops = {
	.open	= xscaler_open,
	.close	= xscaler_close,
};

/*
 * Media Operations
 */

static const struct media_entity_operations xscaler_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * Platform Device Driver
 */

static int xscaler_parse_of(struct xscaler_device *xscaler)
{
	struct device_node *node = xscaler->xvip.dev->of_node;
	int ret;

	xscaler->vip_format = xvip_of_get_format(node);
	if (xscaler->vip_format == NULL) {
		dev_err(xscaler->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "xlnx,num-hori-taps",
				   &xscaler->num_hori_taps);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,num-vert-taps",
				   &xscaler->num_vert_taps);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "xlnx,max-num-phases",
				   &xscaler->max_num_phases);
	if (ret < 0)
		return ret;

	xscaler->separate_yc_coef =
		of_property_read_bool(node, "xlnx,separate-yc-coef");

	xscaler->separate_hv_coef =
		of_property_read_bool(node, "xlnx,separate-hv-coef");

	return 0;
}

static int xscaler_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xscaler_device *xscaler;
	struct resource *res;
	u32 version;
	int ret;

	xscaler = devm_kzalloc(&pdev->dev, sizeof(*xscaler), GFP_KERNEL);
	if (!xscaler)
		return -ENOMEM;

	xscaler->xvip.dev = &pdev->dev;

	ret = xscaler_parse_of(xscaler);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xscaler->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (xscaler->xvip.iomem == NULL)
		return -ENODEV;

	/* Initialize V4L2 subdevice and media entity */
	subdev = &xscaler->xvip.subdev;
	v4l2_subdev_init(subdev, &xscaler_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xscaler_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xscaler);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	xscaler_init_formats(subdev, NULL);

	xscaler->pads[XSCALER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xscaler->pads[XSCALER_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xscaler_media_ops;

	ret = media_entity_init(&subdev->entity, 2, xscaler->pads, 0);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, xscaler);

	version = xvip_read(&xscaler->xvip, XVIP_CTRL_VERSION);

	dev_info(&pdev->dev, "device found, version %u.%02x%x\n",
		 ((version & XVIP_CTRL_VERSION_MAJOR_MASK) >>
		  XVIP_CTRL_VERSION_MAJOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_MINOR_MASK) >>
		  XVIP_CTRL_VERSION_MINOR_SHIFT),
		 ((version & XVIP_CTRL_VERSION_REVISION_MASK) >>
		  XVIP_CTRL_VERSION_REVISION_SHIFT));

	ret = xscaler_gen_coefs(xscaler, (s16)xscaler->num_hori_taps);
	if (ret < 0)
		goto error;

	if (xscaler->separate_hv_coef) {
		ret = xscaler_gen_coefs(xscaler, (s16)xscaler->num_vert_taps);
		if (ret < 0)
			goto error;
	}

	if (xscaler->separate_yc_coef) {
		ret = xscaler_gen_coefs(xscaler, (s16)xscaler->num_hori_taps);
		if (ret < 0)
			goto error;

		if (xscaler->separate_hv_coef) {
			ret = xscaler_gen_coefs(xscaler,
						(s16)xscaler->num_vert_taps);
			if (ret < 0)
				goto error;
		}
	}

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

static int xscaler_remove(struct platform_device *pdev)
{
	struct xscaler_device *xscaler = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xscaler->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xscaler_of_id_table[] = {
	{ .compatible = "xlnx,axi-scaler" },
	{ }
};
MODULE_DEVICE_TABLE(of, xscaler_of_id_table);

static struct platform_driver xscaler_driver = {
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= "xilinx-scaler",
		.of_match_table	= of_match_ptr(xscaler_of_id_table),
	},
	.probe			= xscaler_probe,
	.remove			= xscaler_remove,
};

module_platform_driver(xscaler_driver);

MODULE_DESCRIPTION("Xilinx Scaler Driver");
MODULE_LICENSE("GPL v2");
