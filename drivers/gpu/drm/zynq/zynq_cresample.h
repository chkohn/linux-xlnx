/*
 * Xilinx Chroma Resampler Header for Zynq DRM KMS
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

#ifndef _ZYNQ_CRESAMPLE_H_
#define _ZYNQ_CRESAMPLE_H_

struct zynq_cresample;

void zynq_cresample_configure(struct zynq_cresample *cresample,
		int hactive, int vactive);
void zynq_cresample_reset(struct zynq_cresample *cresample);
void zynq_cresample_enable(struct zynq_cresample *cresample);
void zynq_cresample_disable(struct zynq_cresample *cresample);

struct device;
struct device_node;

struct zynq_cresample *zynq_cresample_probe(struct device *dev,
		struct device_node *node);
void zynq_cresample_remove(struct zynq_cresample *cresample);

#endif
