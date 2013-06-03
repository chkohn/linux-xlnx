/*
 * Xilinx OSD Header for Zynq DRM KMS
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

#ifndef _ZYNQ_VTC_H_
#define _ZYNQ_VTC_H_

struct zynq_osd;
struct zynq_osd_layer;

/* osd layer configuration */
void zynq_osd_layer_set_alpha(struct zynq_osd_layer *layer, u32 enable,
		u32 alpha);
void zynq_osd_layer_set_priority(struct zynq_osd_layer *layer, u32 prio);
void zynq_osd_layer_set_dimension(struct zynq_osd_layer *layer,
		u16 xstart, u16 ystart, u16 xsize, u16 ysize);

/* osd layer operation */
void zynq_osd_layer_enable(struct zynq_osd_layer *layer);
void zynq_osd_layer_disable(struct zynq_osd_layer *layer);
struct zynq_osd_layer *zynq_osd_layer_create(struct zynq_osd *osd);
void zynq_osd_layer_destroy(struct zynq_osd_layer *layer);

/* osd configuration */
void zynq_osd_set_color(struct zynq_osd *osd, u8 r, u8 g, u8 b);
void zynq_osd_set_dimension(struct zynq_osd *osd, u32 width, u32 height);

/* osd operation */
void zynq_osd_reset(struct zynq_osd *osd);
void zynq_osd_enable(struct zynq_osd *osd);
void zynq_osd_disable(struct zynq_osd *osd);

struct zynq_osd *zynq_osd_probe(char *compatible);
void zynq_osd_remove(struct zynq_osd *osd);

#endif
