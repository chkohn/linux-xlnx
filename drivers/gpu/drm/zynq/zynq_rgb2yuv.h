/*
 * Color Space Converter Header for Zynq DRM KMS
 *
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 * Author: hyun woo kwon <hyunk@xilinx.com>
 *
 * Description:
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef _ZYNQ_RGB2YUV_H_
#define _ZYNQ_RGB2YUV_H_

struct zynq_rgb2yuv;

void zynq_rgb2yuv_configure(struct zynq_rgb2yuv *rgb2yuv,
		int hactive, int vactive);
void zynq_rgb2yuv_reset(struct zynq_rgb2yuv *rgb2yuv);
void zynq_rgb2yuv_enable(struct zynq_rgb2yuv *rgb2yuv);
void zynq_rgb2yuv_disable(struct zynq_rgb2yuv *rgb2yuv);

struct zynq_rgb2yuv *zynq_rgb2yuv_probe(char *compatible);
void zynq_rgb2yuv_remove(struct zynq_rgb2yuv *rgb2yuv);

#endif
