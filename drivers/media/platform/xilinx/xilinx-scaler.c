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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-controls.h"
#include "xilinx-vip.h"

#define XSCALER_MIN_WIDTH			32
#define XSCALER_MAX_WIDTH			7680
#define XSCALER_MIN_HEIGHT			32
#define XSCALER_MAX_HEIGHT			7680

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
 */
struct xscaler_device {
	struct xvip_device xvip;
	struct media_pad pads[2];
	const struct xvip_video_format *vip_format;
	struct v4l2_mbus_framefmt formats[2];
};

static inline struct xscaler_device *to_scaler(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xscaler_device, xvip.subdev);
}

/*
 * V4L2 Subdevice Video Operations
 */

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

	/* FIXME: set aperture same as width/height for now */
	/* FIXME: use size mask for now */
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

static int xscaler_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
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

	xscaler->vip_format = xvip_of_get_format(node);
	if (xscaler->vip_format == NULL) {
		dev_err(xscaler->xvip.dev, "invalid format in DT");
		return -EINVAL;
	}

	return 0;
}

/* FIXME: temp coeff. taps = 4, phases = 4 */
static int16_t xscaler_coef[] = {
	0, 0, 0, 0, -104, 1018, 15364, 106, 0, 0, 0, 0,
	0, 0, 0, 0, -115, 1256, 15164, 79, 0, 0, 0, 0,
	0, 0, 0, 0, -125, 1494, 14962, 53, 0, 0, 0, 0,
	0, 0, 0, 0, -135, 1731, 14759, 29, 0, 0, 0, 0,
	0, 0, 0, 0, -145, 1968, 14555, 6, 0, 0, 0, 0,
	0, 0, 0, 0, -155, 2205, 14349, -16, 0, 0, 0, 0,
	0, 0, 0, 0, -164, 2442, 14143, -37, 0, 0, 0, 0,
	0, 0, 0, 0, -173, 2679, 13935, -57, 0, 0, 0, 0,
	0, 0, 0, 0, -182, 2915, 13726, -75, 0, 0, 0, 0,
	0, 0, 0, 0, -191, 3151, 13517, -93, 0, 0, 0, 0,
	0, 0, 0, 0, -199, 3386, 13306, -110, 0, 0, 0, 0,
	0, 0, 0, 0, -206, 3622, 13094, -125, 0, 0, 0, 0,
	0, 0, 0, 0, -214, 3857, 12882, -140, 0, 0, 0, 0,
	0, 0, 0, 0, -221, 4091, 12668, -154, 0, 0, 0, 0,
	0, 0, 0, 0, -228, 4326, 12454, -167, 0, 0, 0, 0,
	0, 0, 0, 0, -234, 4560, 12238, -180, 0, 0, 0, 0,
	0, 0, 0, 0, -240, 4793, 12022, -191, 0, 0, 0, 0,
	0, 0, 0, 0, -246, 5026, 11806, -202, 0, 0, 0, 0,
	0, 0, 0, 0, -251, 5259, 11588, -212, 0, 0, 0, 0,
	0, 0, 0, 0, -256, 5491, 11370, -221, 0, 0, 0, 0,
	0, 0, 0, 0, -261, 5723, 11151, -230, 0, 0, 0, 0,
	0, 0, 0, 0, -265, 5955, 10931, -237, 0, 0, 0, 0,
	0, 0, 0, 0, -269, 6186, 10711, -245, 0, 0, 0, 0,
	0, 0, 0, 0, -272, 6417, 10490, -251, 0, 0, 0, 0,
	0, 0, 0, 0, -275, 6648, 10268, -257, 0, 0, 0, 0,
	0, 0, 0, 0, -277, 6877, 10046, -262, 0, 0, 0, 0,
	0, 0, 0, 0, -279, 7107, 9823, -267, 0, 0, 0, 0,
	0, 0, 0, 0, -281, 7336, 9600, -271, 0, 0, 0, 0,
	0, 0, 0, 0, -282, 7565, 9376, -274, 0, 0, 0, 0,
	0, 0, 0, 0, -283, 7793, 9151, -277, 0, 0, 0, 0,
	0, 0, 0, 0, -283, 8020, 8926, -279, 0, 0, 0, 0,
	0, 0, 0, 0, -283, 8248, 8700, -281, 0, 0, 0, 0,
	0, 0, 0, 0, -282, 8474, 8474, -282, 0, 0, 0, 0,
	0, 0, 0, 0, -281, 8700, 8248, -283, 0, 0, 0, 0,
	0, 0, 0, 0, -279, 8926, 8020, -283, 0, 0, 0, 0,
	0, 0, 0, 0, -277, 9151, 7793, -283, 0, 0, 0, 0,
	0, 0, 0, 0, -274, 9376, 7565, -282, 0, 0, 0, 0,
	0, 0, 0, 0, -271, 9600, 7336, -281, 0, 0, 0, 0,
	0, 0, 0, 0, -267, 9823, 7107, -279, 0, 0, 0, 0,
	0, 0, 0, 0, -262, 10046, 6877, -277, 0, 0, 0, 0,
	0, 0, 0, 0, -257, 10268, 6648, -275, 0, 0, 0, 0,
	0, 0, 0, 0, -251, 10490, 6417, -272, 0, 0, 0, 0,
	0, 0, 0, 0, -245, 10711, 6186, -269, 0, 0, 0, 0,
	0, 0, 0, 0, -237, 10931, 5955, -265, 0, 0, 0, 0,
	0, 0, 0, 0, -230, 11151, 5723, -261, 0, 0, 0, 0,
	0, 0, 0, 0, -221, 11370, 5491, -256, 0, 0, 0, 0,
	0, 0, 0, 0, -212, 11588, 5259, -251, 0, 0, 0, 0,
	0, 0, 0, 0, -202, 11806, 5026, -246, 0, 0, 0, 0,
	0, 0, 0, 0, -191, 12022, 4793, -240, 0, 0, 0, 0,
	0, 0, 0, 0, -180, 12238, 4560, -234, 0, 0, 0, 0,
	0, 0, 0, 0, -167, 12454, 4326, -228, 0, 0, 0, 0,
	0, 0, 0, 0, -154, 12668, 4091, -221, 0, 0, 0, 0,
	0, 0, 0, 0, -140, 12882, 3857, -214, 0, 0, 0, 0,
	0, 0, 0, 0, -125, 13094, 3622, -206, 0, 0, 0, 0,
	0, 0, 0, 0, -110, 13306, 3386, -199, 0, 0, 0, 0,
	0, 0, 0, 0, -93, 13517, 3151, -191, 0, 0, 0, 0,
	0, 0, 0, 0, -75, 13726, 2915, -182, 0, 0, 0, 0,
	0, 0, 0, 0, -57, 13935, 2679, -173, 0, 0, 0, 0,
	0, 0, 0, 0, -37, 14143, 2442, -164, 0, 0, 0, 0,
	0, 0, 0, 0, -16, 14349, 2205, -155, 0, 0, 0, 0,
	0, 0, 0, 0, 6, 14555, 1968, -145, 0, 0, 0, 0,
	0, 0, 0, 0, 29, 14759, 1731, -135, 0, 0, 0, 0,
	0, 0, 0, 0, 53, 14962, 1494, -125, 0, 0, 0, 0,
	0, 0, 0, 0, 79, 15164, 1256, -115, 0, 0, 0, 0

};

static int16_t xscaler_coef23[] = {
	0, -141, 402, 0, -1940, 4527, 10949, 3881, -1397, 0, 176, -74,      // 0
	-2, -139, 411, -35, -1916, 4679, 10936, 3743, -1410, 20, 172, -75,  // 1
	-5, -136, 419, -71, -1889, 4830, 10919, 3606, -1421, 40, 168, -76,  // 2
	-7, -133, 427, -107, -1859, 4981, 10898, 3469, -1431, 60, 163, -77, // 3
	-9, -130, 434, -143, -1828, 5131, 10875, 3333, -1439, 79, 159, -77, // 4
	-12, -127, 441, -179, -1794, 5282, 10847, 3197, -1446, 98, 154, -78,// 5
	-14, -124, 448, -215, -1757, 5431, 10817, 3063, -1450, 117, 149, -79, // 6
	-16, -121, 454, -252, -1719, 5580, 10782, 2929, -1454, 135, 144, -79, // 7
	-19, -117, 460, -288, -1678, 5729, 10745, 2796, -1455, 153, 139, -80, // 8
	-21, -114, 465, -325, -1634, 5876, 10704, 2664, -1455, 170, 133, -80, // 9
	-23, -110, 470, -361, -1588, 6023, 10660, 2533, -1454, 188, 128, -80, // 10
	-26, -106, 474, -398, -1540, 6169, 10612, 2403, -1451, 204, 123, -81, // 11
	-28, -102, 478, -434, -1490, 6314, 10561, 2274, -1447, 221, 117, -81, // 12
	-30, -98, 482, -471, -1437, 6458, 10507, 2146, -1441, 237, 112, -81,  // 13
	-32, -94, 485, -507, -1382, 6601, 10449, 2019, -1434, 252, 106, -81,  // 14
	-34, -89, 487, -543, -1324, 6743, 10389, 1894, -1425, 268, 100, -80,  // 15
	-37, -85, 489, -579, -1264, 6883, 10325, 1770, -1416, 282, 95, -80,   // 16
	-39, -80, 491, -615, -1202, 7022, 10258, 1647, -1404, 297, 89, -80,   // 17
	-41, -75, 492, -650, -1138, 7160, 10188, 1526, -1392, 310, 83, -79,   // 18
	-43, -71, 493, -685, -1071, 7296, 10114, 1406, -1378, 324, 77, -79, 
	-48, -59, 492, -766, -906, 7608, 9932, 1133, -1341, 353, 63, -78,  // 20
	-48, -59, 492, -766, -906, 7608, 9932, 1133, -1341, 353, 63, -78,  // 21
	-48, -59, 492, -766, -906, 7608, 9932, 1133, -1341, 353, 63, -78,  // 22
	-48, -59, 492, -766, -906, 7608, 9932, 1133, -1341, 353, 63, -78,  // 23
	/////////////////////////////////////////////////////////////////////////////////

	-53, -45, 488, -855, -703, 7954, 9704, 830, -1291, 383, 47, -75,
	-55, -40, 485, -888, -622, 8080, 9613, 720, -1271, 394, 41, -74,
	-56, -34, 482, -920, -540, 8204, 9520, 612, -1249, 404, 36, -73,
	-58, -29, 478, -951, -455, 8326, 9424, 505, -1226, 413, 30, -72,
	-60, -23, 474, -982, -368, 8446, 9325, 400, -1203, 422, 24, -71,
	-61, -17, 469, -1012, -279, 8564, 9224, 297, -1178, 430, 18, -70,
	-63, -12, 464, -1042, -188, 8680, 9120, 196, -1152, 438, 12, -69,
	-65, -6, 459, -1071, -95, 8793, 9013, 97, -1126, 445, 6, -67,
	-66, 0, 452, -1099, 0, 8904, 8904, 0, -1099, 452, 0, -66,
	-67, 6, 445, -1126, 97, 9013, 8793, -95, -1071, 459, -6, -65,
	-69, 12, 438, -1152, 196, 9120, 8680, -188, -1042, 464, -12, -63,
	-70, 18, 430, -1178, 297, 9224, 8564, -279, -1012, 469, -17, -61,
	-71, 24, 422, -1203, 400, 9325, 8446, -368, -982, 474, -23, -60,
	-72, 30, 413, -1226, 505, 9424, 8326, -455, -951, 478, -29, -58,
	-73, 36, 404, -1249, 612, 9520, 8204, -540, -920, 482, -34, -56,
	-74, 41, 394, -1271, 720, 9613, 8080, -622, -888, 485, -40, -55,
	-75, 47, 383, -1291, 830, 9704, 7954, -703, -855, 488, -45, -53,
	-78, 63, 353, -1341, 1133, 9932, 7608, -906, -766, 492, -59, -48,
	-78, 63, 353, -1341, 1133, 9932, 7608, -906, -766, 492, -59, -48,
	-78, 63, 353, -1341, 1133, 9932, 7608, -906, -766, 492, -59, -48,
	-78, 63, 353, -1341, 1133, 9932, 7608, -906, -766, 492, -59, -48,
	/////////////////////////////////////////////////////////////////////////////////

	-79, 77, 324, -1378, 1406, 10114, 7296, -1071, -685, 493, -71, -43,
	-79, 83, 310, -1392, 1526, 10188, 7160, -1138, -650, 492, -75, -41,
	-80, 89, 297, -1404, 1647, 10258, 7022, -1202, -615, 491, -80, -39,
	-80, 95, 282, -1416, 1770, 10325, 6883, -1264, -579, 489, -85, -37,
	-80, 100, 268, -1425, 1894, 10389, 6743, -1324, -543, 487, -89, -34,
	-81, 106, 252, -1434, 2019, 10449, 6601, -1382, -507, 485, -94, -32,
	-81, 112, 237, -1441, 2146, 10507, 6458, -1437, -471, 482, -98, -30,
	-81, 117, 221, -1447, 2274, 10561, 6314, -1490, -434, 478, -102, -28,
	-81, 123, 204, -1451, 2403, 10612, 6169, -1540, -398, 474, -106, -26,
	-80, 128, 188, -1454, 2533, 10660, 6023, -1588, -361, 470, -110, -23,
	-80, 133, 170, -1455, 2664, 10704, 5876, -1634, -325, 465, -114, -21,
	-80, 139, 153, -1455, 2796, 10745, 5729, -1678, -288, 460, -117, -19,
	-79, 144, 135, -1454, 2929, 10782, 5580, -1719, -252, 454, -121, -16,
	-79, 149, 117, -1450, 3063, 10817, 5431, -1757, -215, 448, -124, -14,
	-78, 154, 98, -1446, 3197, 10847, 5282, -1794, -179, 441, -127, -12,
	-77, 159, 79, -1439, 3333, 10875, 5131, -1828, -143, 434, -130, -9,
	-77, 163, 60, -1431, 3469, 10898, 4981, -1859, -107, 427, -133, -7,
	-76, 168, 40, -1421, 3606, 10919, 4830, -1889, -71, 419, -136, -5,
	-75, 172, 20, -1410, 3743, 10936, 4679, -1916, -35, 411, -139, -2

};

static int16_t xscaler_coef23_t[] = {
	/* bin # 16; num_taps = 12; num_phases = 4 */
	-65, 135, -328, 596, -855, 1017, 15357, 872, -616, 343, -144, 71,
	-69, 167, -488, 1120, -2280, 5514, 13831, -1815, 476, -83, -9, 19,
	-36, 113, -403, 1116, -2804, 10206, 10206, -2804, 1116, -403, 113, -36,
	19, -9, -83, 476, -1815, 13831, 5514, -2280, 1120, -488, 167, -69
};

static int16_t xscaler_coef00[] = {
	/* bin # 16; num_taps = 12; num_phases = 4 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
};

static int16_t xscaler_coef0[] = {
	0, 0, 0, 0, 0, 0, 16384, 0, 0, 0, 0, 0,
	-52, 120, -346, 818, -1787, 4863, 14590, -2501, 1000, -399, 134, -57,
	-76, 178, -522, 1269, -2938, 10281, 10281, -2938, 1269, -522, 178, -76,
	-57, 134, -399, 1000, -2501, 14590, 4863, -1787, 818, -346, 120, -52
};

static int xscaler_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xscaler_device *xscaler;
	struct resource *res;
	u32 version;
	unsigned int i;
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

	xscaler->pads[XSCALER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xscaler->pads[XSCALER_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	subdev->entity.ops = &xscaler_media_ops;

	ret = media_entity_init(&subdev->entity, 2, xscaler->pads, 0);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, xscaler);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef0) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

#if 0
	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);

	for (i = 0; i < ARRAY_SIZE(xscaler_coef00) / 2; i += 2)
		xvip_write(&xscaler->xvip, XSCALER_COEF_DATA_IN,
			   xscaler_coef0[i + 1] << XSCALER_COEF_DATA_IN_SHIFT |
			   xscaler_coef0[i]);
#endif

	version = xvip_read(&xscaler->xvip, XVIP_CTRL_VERSION);

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
