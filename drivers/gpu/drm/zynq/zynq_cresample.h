/*
 * Xilinx Chroma Resampler Header for Zynq DRM KMS
 *
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 * Author: hyun woo kwon<hyunk@xilinx.com>
 *
 * Description:
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef _ZYNQ_CRESAMPLE_H_
#define _ZYNQ_CRESAMPLE_H_

struct zynq_cresample;

void zynq_cresample_configure(struct zynq_cresample *cresample,
		int hactive, int vactive);
void zynq_cresample_reset(struct zynq_cresample *cresample);
void zynq_cresample_enable(struct zynq_cresample *cresample);
void zynq_cresample_disable(struct zynq_cresample *cresample);

struct zynq_cresample *zynq_cresample_probe(char *compatible);
void zynq_cresample_remove(struct zynq_cresample *cresample);

#endif
