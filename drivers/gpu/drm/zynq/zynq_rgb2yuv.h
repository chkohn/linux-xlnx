/*
 * Color Space Converter Header for Zynq DRM KMS
 *
 *  Copyright (C) 2013 Xilinx
 *
 *  Author: hyun woo kwon <hyunk@xilinx.com>
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

#ifndef _ZYNQ_RGB2YUV_H_
#define _ZYNQ_RGB2YUV_H_

struct zynq_rgb2yuv;

void zynq_rgb2yuv_configure(struct zynq_rgb2yuv *rgb2yuv,
		int hactive, int vactive);
void zynq_rgb2yuv_reset(struct zynq_rgb2yuv *rgb2yuv);
void zynq_rgb2yuv_fsync_reset(struct zynq_rgb2yuv *rgb2yuv);
void zynq_rgb2yuv_enable(struct zynq_rgb2yuv *rgb2yuv);
void zynq_rgb2yuv_disable(struct zynq_rgb2yuv *rgb2yuv);

struct zynq_rgb2yuv *zynq_rgb2yuv_probe(char *compatible);
void zynq_rgb2yuv_remove(struct zynq_rgb2yuv *rgb2yuv);

#endif
