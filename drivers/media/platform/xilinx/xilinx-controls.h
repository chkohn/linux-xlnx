/*
 * Xilinx Controls Header
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

#ifndef __XILINX_CONTROLS_H__
#define __XILINX_CONTROLS_H__

#include <linux/v4l2-controls.h>

#define V4L2_CID_XILINX_OFFSET	0xc000
#define V4L2_CID_XILINX_BASE	(V4L2_CID_USER_BASE + V4L2_CID_XILINX_OFFSET)

/*
 * Private Controls for Xilinx Video IPs
 */

/*
 * Xilinx TPG Video IP
 */

#define V4L2_CID_XILINX_TPG			(V4L2_CID_USER_BASE + 0xc000)

/* Draw cross hairs */
#define V4L2_CID_XILINX_TPG_CROSS_HAIRS		(V4L2_CID_XILINX_TPG + 1)
/* Enable a moving box */
#define V4L2_CID_XILINX_TPG_MOVING_BOX		(V4L2_CID_XILINX_TPG + 2)
/* Mask out a color component */
#define V4L2_CID_XILINX_TPG_COLOR_MASK		(V4L2_CID_XILINX_TPG + 3)
/* Enable a stuck pixel feature */
#define V4L2_CID_XILINX_TPG_STUCK_PIXEL		(V4L2_CID_XILINX_TPG + 4)
/* Enable a noisy output */
#define V4L2_CID_XILINX_TPG_NOISE		(V4L2_CID_XILINX_TPG + 5)
/* Enable the motion feature */
#define V4L2_CID_XILINX_TPG_MOTION		(V4L2_CID_XILINX_TPG + 6)
/* Configure the motion speed of moving patterns */
#define V4L2_CID_XILINX_TPG_MOTION_SPEED	(V4L2_CID_XILINX_TPG + 7)
/* The row of horizontal cross hair location */
#define V4L2_CID_XILINX_TPG_CROSS_HAIR_ROW	(V4L2_CID_XILINX_TPG + 8)
/* The colum of vertical cross hair location */
#define V4L2_CID_XILINX_TPG_CROSS_HAIR_COLUMN	(V4L2_CID_XILINX_TPG + 9)
/* Set starting point of sine wave for horizontal component */
#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_START	(V4L2_CID_XILINX_TPG + 10)
/* Set speed of the horizontal component */
#define V4L2_CID_XILINX_TPG_ZPLATE_HOR_SPEED	(V4L2_CID_XILINX_TPG + 11)
/* Set starting point of sine wave for vertical component */
#define V4L2_CID_XILINX_TPG_ZPLATE_VER_START	(V4L2_CID_XILINX_TPG + 12)
/* Set speed of the vertical component */
#define V4L2_CID_XILINX_TPG_ZPLATE_VER_SPEED	(V4L2_CID_XILINX_TPG + 13)
/* Moving box size */
#define V4L2_CID_XILINX_TPG_BOX_SIZE		(V4L2_CID_XILINX_TPG + 14)
/* Moving box color */
#define V4L2_CID_XILINX_TPG_BOX_COLOR		(V4L2_CID_XILINX_TPG + 15)
/* Upper limit count of generated stuck pixels */
#define V4L2_CID_XILINX_TPG_STUCK_PIXEL_THRESH	(V4L2_CID_XILINX_TPG + 16)
/* Noise level */
#define V4L2_CID_XILINX_TPG_NOISE_GAIN		(V4L2_CID_XILINX_TPG + 17)

/*
 * Xilinx CRESAMPLE Video IP
 */

#define V4L2_CID_XILINX_CRESAMPLE		(V4L2_CID_USER_BASE + 0xc020)

/* The field parity for interlaced video */
#define V4L2_CID_XILINX_CRESAMPLE_FIELD_PARITY	(V4L2_CID_XILINX_CRESAMPLE + 1)
/* Specify if the first line of video contains the Chroma infomation */
#define V4L2_CID_XILINX_CRESAMPLE_CHROMA_PARITY	(V4L2_CID_XILINX_CRESAMPLE + 2)

/*
 * Xilinx RGB2YUV Video IPs
 */

#define V4L2_CID_XILINX_RGB2YUV			(V4L2_CID_USER_BASE + 0xc040)

/* Maximum Luma(Y) value */
#define V4L2_CID_XILINX_RGB2YUV_YMAX		(V4L2_CID_XILINX_RGB2YUV + 1)
/* Minimum Luma(Y) value */
#define V4L2_CID_XILINX_RGB2YUV_YMIN		(V4L2_CID_XILINX_RGB2YUV + 2)
/* Maximum Cb Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CBMAX		(V4L2_CID_XILINX_RGB2YUV + 3)
/* Minimum Cb Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CBMIN		(V4L2_CID_XILINX_RGB2YUV + 4)
/* Maximum Cr Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CRMAX		(V4L2_CID_XILINX_RGB2YUV + 5)
/* Minimum Cr Chroma value */
#define V4L2_CID_XILINX_RGB2YUV_CRMIN		(V4L2_CID_XILINX_RGB2YUV + 6)
/* The offset compensation value for Luma(Y) */
#define V4L2_CID_XILINX_RGB2YUV_YOFFSET		(V4L2_CID_XILINX_RGB2YUV + 7)
/* The offset compensation value for Cb Chroma */
#define V4L2_CID_XILINX_RGB2YUV_CBOFFSET	(V4L2_CID_XILINX_RGB2YUV + 8)
/* The offset compensation value for Cr Chroma */
#define V4L2_CID_XILINX_RGB2YUV_CROFFSET	(V4L2_CID_XILINX_RGB2YUV + 9)

/* Y = CA * R + (1 - CA - CB) * G + CB * B */

/* CA coefficient */
#define V4L2_CID_XILINX_RGB2YUV_ACOEF		(V4L2_CID_XILINX_RGB2YUV + 10)
/* CB coefficient */
#define V4L2_CID_XILINX_RGB2YUV_BCOEF		(V4L2_CID_XILINX_RGB2YUV + 11)
/* CC coefficient */
#define V4L2_CID_XILINX_RGB2YUV_CCOEF		(V4L2_CID_XILINX_RGB2YUV + 12)
/* CD coefficient */
#define V4L2_CID_XILINX_RGB2YUV_DCOEF		(V4L2_CID_XILINX_RGB2YUV + 13)

/*
 * Xilinx CCM Video IP
 */

#define V4L2_CID_XILINX_CCM			(V4L2_CID_USER_BASE + 0xc060)

/*
 * The equation for color correction matrix:
 *
 * Rc = K11 * R + K12 * G + K13 * B + Roff
 * Gc = K21 * R + K22 * G + K23 * B + Goff
 * Bc = K31 * R + K32 * G + K33 * B + Boff
 */

/* K11 */
#define V4L2_CID_XILINX_CCM_COEFF11		(V4L2_CID_XILINX_CCM + 1)
/* K12 */
#define V4L2_CID_XILINX_CCM_COEFF12		(V4L2_CID_XILINX_CCM + 2)
/* K13 */
#define V4L2_CID_XILINX_CCM_COEFF13		(V4L2_CID_XILINX_CCM + 3)
/* K21 */
#define V4L2_CID_XILINX_CCM_COEFF21		(V4L2_CID_XILINX_CCM + 4)
/* K22 */
#define V4L2_CID_XILINX_CCM_COEFF22		(V4L2_CID_XILINX_CCM + 5)
/* K23 */
#define V4L2_CID_XILINX_CCM_COEFF23		(V4L2_CID_XILINX_CCM + 6)
/* K31 */
#define V4L2_CID_XILINX_CCM_COEFF31		(V4L2_CID_XILINX_CCM + 7)
/* K32 */
#define V4L2_CID_XILINX_CCM_COEFF32		(V4L2_CID_XILINX_CCM + 8)
/* K33 */
#define V4L2_CID_XILINX_CCM_COEFF33		(V4L2_CID_XILINX_CCM + 9)
/* Roff */
#define V4L2_CID_XILINX_CCM_RED_OFFSET		(V4L2_CID_XILINX_CCM + 10)
/* Goff */
#define V4L2_CID_XILINX_CCM_GREEN_OFFSET	(V4L2_CID_XILINX_CCM + 11)
/* Boff */
#define V4L2_CID_XILINX_CCM_BLUE_OFFSET		(V4L2_CID_XILINX_CCM + 12)
/* Maximum output value */
#define V4L2_CID_XILINX_CCM_CLIP		(V4L2_CID_XILINX_CCM + 13)
/* Minimum output value */
#define V4L2_CID_XILINX_CCM_CLAMP		(V4L2_CID_XILINX_CCM + 14)

/*
 * Xilinx ENHANCE Video IP
 */

#define V4L2_CID_XILINX_ENHANCE			(V4L2_CID_USER_BASE + 0xc080)

/* The amount of noise reduction applied by the low pass filter */
#define V4L2_CID_XILINX_ENHANCE_NOISE_THRESHOLD	(V4L2_CID_XILINX_ENHANCE + 1)
/* The amount of edge enhancement applyed by the high pass filter */
#define V4L2_CID_XILINX_ENHANCE_STRENGTH	(V4L2_CID_XILINX_ENHANCE + 2)
/* The amount of halo suppression */
#define V4L2_CID_XILINX_ENHANCE_HALO_SUPPRESS	(V4L2_CID_XILINX_ENHANCE + 3)

/*
 * Xilinx GAMMA Video IP
 */

#define V4L2_CID_XILINX_GAMMA			(V4L2_CID_USER_BASE + 0xc0a0)

/* Switch to the new gamma LUT */
#define V4L2_CID_XILINX_GAMMA_SWITCH_LUT	(V4L2_CID_XILINX_GAMMA + 1)
/* Update the inactive gamma LUT */
#define V4L2_CID_XILINX_GAMMA_UPDATE_LUT	(V4L2_CID_XILINX_GAMMA + 2)

#endif /* __XILINX_CONTROLS_H__ */
