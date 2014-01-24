/*
 * Xilinx Test Pattern Generator
 *
 * Copyright (C) 2013 Ideas on Board SPRL
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
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "xilinx-controls.h"
#include "xilinx-vip.h"
#include "xilinx-vtc.h"

#define XTPG_MIN_WIDTH				32
#define XTPG_MAX_WIDTH				7680
#define XTPG_MIN_HEIGHT				32
#define XTPG_MAX_HEIGHT				7680

#define XTPG_CTRL_STATUS_SLAVE_ERROR		(1 << 16)
#define XTPG_CTRL_IRQ_SLAVE_ERROR		(1 << 16)

#define XTPG_PATTERN_CONTROL			0x0100
#define XTPG_PATTERN_MASK			0xf
#define XTPG_CROSS_HAIRS_SHIFT			4
#define XTPG_CROSS_HAIRS_MASK			(0x1 << XTPG_CROSS_HAIRS_SHIFT)
#define XTPG_MOVING_BOX_SHIFT			5
#define XTPG_MOVING_BOX_MASK			(0x1 << XTPG_MOVING_BOX_SHIFT)
#define XTPG_COLOR_MASK_SHIFT			6
#define XTPG_COLOR_MASK_MASK			(0xf << XTPG_COLOR_MASK_SHIFT)
#define XTPG_STUCK_PIXEL_SHIFT			9
#define XTPG_STUCK_PIXEL_MASK			(0x1 << XTPG_STUCK_PIXEL_SHIFT)
#define XTPG_NOISE_SHIFT			10
#define XTPG_NOISE_MASK				(0x1 << XTPG_NOISE_SHIFT)
#define XTPG_MOTION_SHIFT			12
#define XTPG_MOTION_MASK			(0x1 << XTPG_MOTION_SHIFT)
#define XTPG_MOTION_SPEED			0x0104
#define XTPG_CROSS_HAIRS			0x0108
#define XTPG_CROSS_HAIR_COLUMN_SHIFT		16
#define XTPG_CROSS_HAIR_POS_MASK		0xfff
#define XTPG_ZPLATE_HOR_CONTROL			0x010c
#define XTPG_ZPLATE_VER_CONTROL			0x0110
#define XTPG_ZPLATE_SPEED_SHIFT		16
#define XTPG_ZPLATE_MASK			0xffff
#define XTPG_BOX_SIZE				0x0114
#define XTPG_BOX_COLOR				0x0118
#define XTPG_STUCK_PIXEL_THRESH			0x011c
#define XTPG_NOISE_GAIN				0x0120
#define XTPG_BAYER_PHASE			0x0124

/**
 * struct xtpg_device - Xilinx Test Pattern Generator device structure
 * @xvip: Xilinx Video IP device
 * @pads: media pads
 * @npads: number of pads (1 or 2)
 * @format: active V4L2 media bus format
 * @vip_format: format information corresponding to the active format
 * @ctrl_handler: control handler
 * @vtc: video timing controller
 */
struct xtpg_device {
	struct xvip_device xvip;

	struct media_pad pads[2];
	unsigned int npads;

	struct v4l2_mbus_framefmt format;
	const struct xvip_video_format *vip_format;
	struct v4l2_ctrl_handler ctrl_handler;

	struct xvtc_device *vtc;
};

static inline struct xtpg_device *to_tpg(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xtpg_device, xvip.subdev);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Video Operations
 */

static int xtpg_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	const u32 width = xtpg->format.width;
	const u32 height = xtpg->format.height;

	if (!enable) {
		xvip_stop(&xtpg->xvip);
		if (xtpg->vtc)
			xvtc_generator_stop(xtpg->vtc);
		return 0;
	}

	xvip_set_size(&xtpg->xvip, width, height);

	if (xtpg->vtc) {
		struct xvtc_config config = {
			.hblank_start = width,
			.hsync_start = width + 10,
			.hsync_end = width + 20,
			.hsize = width + 100,
			.vblank_start = height,
			.vsync_start = height + 10,
			.vsync_end = height + 20,
			.vsize = height + 100,
		};

		xvtc_generator_start(xtpg->vtc, &config);
	}

	xvip_start(&xtpg->xvip);

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int xtpg_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xtpg_device *xtpg = to_tpg(subdev);

	fmt->format = *xvip_get_pad_format(fh, &xtpg->format, fmt->pad,
					   fmt->which);

	return 0;
}

static int xtpg_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_format *fmt)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = xvip_get_pad_format(fh, &xtpg->format, fmt->pad, fmt->which);

	/* In two pads mode the source pad format is always identical to the
	 * sink pad format.
	 */
	if (xtpg->npads == 2 && fmt->pad == 1) {
		fmt->format = *__format;
		return 0;
	}

	xvip_set_format(__format, xtpg->vip_format, fmt);

	fmt->format = *__format;

	/* Propagate the format to the source pad. */
	if (xtpg->npads == 2) {
		__format = xvip_get_pad_format(fh, &xtpg->format, 1,
					       fmt->which);
		*__format = fmt->format;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/**
 * xtpg_init_formats - Initialize formats on all pads
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
static void xtpg_init_formats(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh)
{
	struct xtpg_device *xtpg = to_tpg(subdev);
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));

	format.pad = 0;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.width = xvip_read(&xtpg->xvip, XVIP_ACTIVE_SIZE) &
			      XVIP_ACTIVE_HSIZE_MASK;
	format.format.height = (xvip_read(&xtpg->xvip, XVIP_ACTIVE_SIZE) &
				XVIP_ACTIVE_VSIZE_MASK) >>
			       XVIP_ACTIVE_VSIZE_SHIFT;
	format.format.field = V4L2_FIELD_NONE;
	format.format.colorspace = V4L2_COLORSPACE_SRGB;

	xtpg_set_format(subdev, fh, &format);
}

static int xtpg_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	xtpg_init_formats(subdev, fh);

	return 0;
}

static int xtpg_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static void xtpg_set_test_pattern(struct xtpg_device *xtpg,
				  unsigned int pattern)
{
	u32 reg;

	reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
	xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
		   (reg & ~XTPG_PATTERN_MASK) | pattern);
}

static int xtpg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct xtpg_device *xtpg = container_of(ctrl->handler,
						struct xtpg_device,
						ctrl_handler);
	u32 reg;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		xtpg_set_test_pattern(xtpg, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIRS:
		reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			   (reg & ~XTPG_CROSS_HAIRS_MASK) |
			   (ctrl->val << XTPG_CROSS_HAIRS_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_MOVING_BOX:
		reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			   (reg & ~XTPG_MOVING_BOX_MASK) |
			   (ctrl->val << XTPG_MOVING_BOX_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_COLOR_MASK:
		reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			   (reg & ~XTPG_COLOR_MASK_MASK) |
			   (ctrl->val << XTPG_COLOR_MASK_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_STUCK_PIXEL:
		reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			   (reg & ~XTPG_STUCK_PIXEL_MASK) |
			   (ctrl->val << XTPG_STUCK_PIXEL_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_NOISE:
		reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			   (reg & ~XTPG_NOISE_MASK) |
			   (ctrl->val << XTPG_NOISE_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_MOTION:
		reg = xvip_read(&xtpg->xvip, XTPG_PATTERN_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_PATTERN_CONTROL,
			   (reg & ~XTPG_MOTION_MASK) |
			   (ctrl->val << XTPG_MOTION_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_MOTION_SPEED:
		xvip_write(&xtpg->xvip, XTPG_MOTION_SPEED, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW:
		reg = xvip_read(&xtpg->xvip, XTPG_CROSS_HAIRS);
		xvip_write(&xtpg->xvip, XTPG_CROSS_HAIRS,
			   (reg & ~XTPG_CROSS_HAIR_POS_MASK) | ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN:
		reg = xvip_read(&xtpg->xvip, XTPG_CROSS_HAIRS);
		xvip_write(&xtpg->xvip, XTPG_CROSS_HAIRS,
			   (reg & XTPG_CROSS_HAIR_POS_MASK) |
			   (ctrl->val << XTPG_CROSS_HAIR_COLUMN_SHIFT));
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_HOR_START:
		reg = xvip_read(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL,
			   (reg & ~XTPG_ZPLATE_MASK) | ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED:
		reg = xvip_read(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_ZPLATE_HOR_CONTROL,
			   (reg & ~XTPG_ZPLATE_MASK) | ctrl->val <<
			   XTPG_ZPLATE_SPEED_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_VER_START:
		reg = xvip_read(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL,
			   (reg & ~XTPG_ZPLATE_MASK) | ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED:
		reg = xvip_read(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL);
		xvip_write(&xtpg->xvip, XTPG_ZPLATE_VER_CONTROL,
			   (reg & ~XTPG_ZPLATE_MASK) | ctrl->val <<
			   XTPG_ZPLATE_SPEED_SHIFT);
		return 0;
	case V4L2_CID_XILINX_TPG_BOX_SIZE:
		xvip_write(&xtpg->xvip, XTPG_BOX_SIZE, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_BOX_COLOR:
		xvip_write(&xtpg->xvip, XTPG_BOX_COLOR, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH:
		xvip_write(&xtpg->xvip, XTPG_STUCK_PIXEL_THRESH, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_NOISE_GAIN:
		xvip_write(&xtpg->xvip, XTPG_NOISE_GAIN, ctrl->val);
		return 0;
	case V4L2_CID_XILINX_TPG_BAYER_PHASE:
		xvip_write(&xtpg->xvip, XTPG_BAYER_PHASE, ctrl->val);
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops xtpg_ctrl_ops = {
	.s_ctrl	= xtpg_s_ctrl,
};

static struct v4l2_subdev_core_ops xtpg_core_ops = {
};

static struct v4l2_subdev_video_ops xtpg_video_ops = {
	.s_stream = xtpg_s_stream,
};

static struct v4l2_subdev_pad_ops xtpg_pad_ops = {
	.enum_mbus_code		= xvip_enum_mbus_code,
	.enum_frame_size	= xvip_enum_frame_size,
	.get_fmt		= xtpg_get_format,
	.set_fmt		= xtpg_set_format,
};

static struct v4l2_subdev_ops xtpg_ops = {
	.core   = &xtpg_core_ops,
	.video  = &xtpg_video_ops,
	.pad    = &xtpg_pad_ops,
};

static const struct v4l2_subdev_internal_ops xtpg_internal_ops = {
	.open	= xtpg_open,
	.close	= xtpg_close,
};

/*
 * Control Config
 */

static const char *const xtpg_pattern_strings[] = {
	"Passthrough",
	"Horizontal Ramp",
	"Vertical Ramp",
	"Temporal Ramp",
	"Solid Red",
	"Solid Green",
	"Solid Blue",
	"Solid Black",
	"Solid White",
	"Color Bars",
	"Zone Plate",
	"Tartan Color Bars",
	"Cross Hatch",
	"None",
	"Vertical/Horizontal Ramps",
	"Black/White Checker Board",
};

static struct v4l2_ctrl_config xtpg_cross_hairs = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_CROSS_HAIRS,
	.name	= "Test Pattern: Cross Hairs",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= false,
	.max	= true,
	.step	= 1,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_moving_box = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_MOVING_BOX,
	.name	= "Test Pattern: Moving Box",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= false,
	.max	= true,
	.step	= 1,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_color_mask = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_COLOR_MASK,
	.name	= "Test Pattern: Color Mask",
	.type	= V4L2_CTRL_TYPE_BITMASK,
	.min	= 0,
	.max	= 0xf,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_stuck_pixel = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_STUCK_PIXEL,
	.name	= "Test Pattern: Stuck Pixel",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= false,
	.max	= true,
	.step	= 1,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_noise = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_NOISE,
	.name	= "Test Pattern: Noise",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= false,
	.max	= true,
	.step	= 1,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_motion = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_MOTION,
	.name	= "Test Pattern: Motion",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= false,
	.max	= true,
	.step	= 1,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_motion_speed = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_MOTION_SPEED,
	.name	= "Test Pattern: Motion Speed",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 8) - 1,
	.step	= 1,
	.def	= 4,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_cross_hair_row = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW,
	.name	= "Test Pattern: Cross Hairs Row",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 12) - 1,
	.step	= 1,
	.def	= 0x64,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_cross_hair_column = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN,
	.name	= "Test Pattern: Cross Hairs Column",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 12) - 1,
	.step	= 1,
	.def	= 0x64,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_hor_start = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_ZPLATE_HOR_START,
	.name	= "Test Pattern: Zplate Horizontal Start Pos",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.def	= 0x1e,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_hor_speed = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED,
	.name	= "Test Pattern: Zplate Horizontal Speed",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_ver_start = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_ZPLATE_VER_START,
	.name	= "Test Pattern: Zplate Vertical Start Pos",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.def	= 1,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_ver_speed = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED,
	.name	= "Test Pattern: Zplate Vertical Speed",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_box_size = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_BOX_SIZE,
	.name	= "Test Pattern: Box Size",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 12) - 1,
	.step	= 1,
	.def	= 0x32,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_box_color = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_BOX_COLOR,
	.name	= "Test Pattern: Box Color(RGB)",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 24) - 1,
	.step	= 1,
	.def	= 0,
};

static struct v4l2_ctrl_config xtpg_stuck_pixel_thresh = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH,
	.name	= "Test Pattern: Stuck Pixel threshhold",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 16) - 1,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static struct v4l2_ctrl_config xtpg_noise_gain = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_NOISE_GAIN,
	.name	= "Test Pattern: Noise Gain",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= (1 << 8) - 1,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_SLIDER,
};

static const char *const xtpg_bayer_phase_menu_strings[] = {
	"RGRG Bayer",
	"GRGR Bayer",
	"GBGB Bayer",
	"BGBG Bayer",
	"Off",
	NULL,
};

static struct v4l2_ctrl_config xtpg_bayer_phase = {
	.ops	= &xtpg_ctrl_ops,
	.id	= V4L2_CID_XILINX_TPG_BAYER_PHASE,
	.name	= "Test Pattern: Bayer Phase",
	.type	= V4L2_CTRL_TYPE_MENU,
	.min	= 0,
	.max	= 4,
	.def	= 4,
	.qmenu	= xtpg_bayer_phase_menu_strings,
};

/* -----------------------------------------------------------------------------
 * Media Operations
 */

static const struct media_entity_operations xtpg_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xtpg_parse_of(struct xtpg_device *xtpg)
{
	struct device_node *node = xtpg->xvip.dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	unsigned int nports = 0;

	/* Count the number of ports. */
	ports = of_get_child_by_name(node, "ports");
	if (ports == NULL)
		ports = node;

	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0))
			nports++;
	}

	if (nports != 1 && nports != 2) {
		dev_err(xtpg->xvip.dev, "invalid number of ports %u\n", nports);
		return -EINVAL;
	}

	xtpg->npads = nports;

	xtpg->vip_format = xvip_of_get_format(node);
	if (xtpg->vip_format == NULL) {
		dev_err(xtpg->xvip.dev, "invalid format in DT\n");
		return -EINVAL;
	}

	return 0;
}

static int xtpg_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev;
	struct xtpg_device *xtpg;
	struct resource *res;
	u32 version;
	int ret;

	xtpg = devm_kzalloc(&pdev->dev, sizeof(*xtpg), GFP_KERNEL);
	if (!xtpg)
		return -ENOMEM;

	xtpg->xvip.dev = &pdev->dev;

	ret = xtpg_parse_of(xtpg);
	if (ret < 0)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xtpg->xvip.iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xtpg->xvip.iomem))
		return PTR_ERR(xtpg->xvip.iomem);

	xtpg->vtc = xvtc_of_get(pdev->dev.of_node);
	if (IS_ERR(xtpg->vtc))
		return PTR_ERR(xtpg->vtc);

	/* Initialize V4L2 subdevice and media entity. Pad numbers depend on the
	 * number of pads.
	 */
	if (xtpg->npads == 2) {
		xtpg->pads[0].flags = MEDIA_PAD_FL_SINK;
		xtpg->pads[1].flags = MEDIA_PAD_FL_SOURCE;
	} else {
		xtpg->pads[0].flags = MEDIA_PAD_FL_SOURCE;
	}

	subdev = &xtpg->xvip.subdev;
	v4l2_subdev_init(subdev, &xtpg_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xtpg_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	v4l2_set_subdevdata(subdev, xtpg);
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->entity.ops = &xtpg_media_ops;

	xtpg_init_formats(subdev, NULL);

	ret = media_entity_init(&subdev->entity, xtpg->npads, xtpg->pads, 0);
	if (ret < 0)
		goto error_media_init;

	v4l2_ctrl_handler_init(&xtpg->ctrl_handler, 14);
	v4l2_ctrl_new_std_menu_items(&xtpg->ctrl_handler, &xtpg_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(xtpg_pattern_strings) - 1,
				     xtpg->npads == 2 ? 0 : 1,
				     xtpg->npads == 2 ? 0 : 1,
				     xtpg_pattern_strings);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_cross_hairs, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_moving_box, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_color_mask, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_stuck_pixel, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_noise, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_motion, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_motion_speed, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_cross_hair_row, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_cross_hair_column,
			     NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_hor_start, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_hor_speed, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_ver_start, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_ver_speed, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_box_size, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_box_color, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_stuck_pixel_thresh,
			     NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_noise_gain, NULL);
	v4l2_ctrl_new_custom(&xtpg->ctrl_handler, &xtpg_bayer_phase, NULL);
	if (xtpg->ctrl_handler.error) {
		dev_err(&pdev->dev, "failed to add controls\n");
		ret = xtpg->ctrl_handler.error;
		goto error;
	}
	subdev->ctrl_handler = &xtpg->ctrl_handler;

	platform_set_drvdata(pdev, xtpg);

	version = xvip_read(&xtpg->xvip, XVIP_CTRL_VERSION);

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
	v4l2_ctrl_handler_free(&xtpg->ctrl_handler);
	media_entity_cleanup(&subdev->entity);
error_media_init:
	xvtc_put(xtpg->vtc);
	return ret;
}

static int xtpg_remove(struct platform_device *pdev)
{
	struct xtpg_device *xtpg = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xtpg->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&xtpg->ctrl_handler);
	media_entity_cleanup(&subdev->entity);

	return 0;
}

static const struct of_device_id xtpg_of_id_table[] = {
	{ .compatible = "xlnx,axi-tpg" },
	{ }
};
MODULE_DEVICE_TABLE(of, xtpg_of_id_table);

static struct platform_driver xtpg_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "xilinx-axi-tpg",
		.of_match_table	= xtpg_of_id_table,
	},
	.probe			= xtpg_probe,
	.remove			= xtpg_remove,
};

module_platform_driver(xtpg_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Test Pattern Generator Driver");
MODULE_LICENSE("GPL v2");
