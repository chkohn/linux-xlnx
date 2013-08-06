/*
 * Xilinx OSD Header for Zynq DRM KMS
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

#ifndef _ZYNQ_VTC_H_
#define _ZYNQ_VTC_H_

struct zynq_osd;
struct zynq_osd_layer;

/*
 * osd layer configuration
 */
void zynq_osd_layer_set_alpha(struct zynq_osd_layer *layer, u32 enable,
		u32 alpha);
void zynq_osd_layer_set_priority(struct zynq_osd_layer *layer, u32 prio);
void zynq_osd_layer_set_dimension(struct zynq_osd_layer *layer,
		u16 xstart, u16 ystart, u16 xsize, u16 ysize);

/*
 * osd layer operation
 */
void zynq_osd_layer_enable(struct zynq_osd_layer *layer);
void zynq_osd_layer_disable(struct zynq_osd_layer *layer);
struct zynq_osd_layer *zynq_osd_layer_get(struct zynq_osd *osd);
void zynq_osd_layer_put(struct zynq_osd_layer *layer);

/*
 * osd configuration
 */
void zynq_osd_set_color(struct zynq_osd *osd, u8 r, u8 g, u8 b);
void zynq_osd_set_dimension(struct zynq_osd *osd, u32 width, u32 height);

/*
 * osd operation
 */
void zynq_osd_reset(struct zynq_osd *osd);
void zynq_osd_enable(struct zynq_osd *osd);
void zynq_osd_disable(struct zynq_osd *osd);
void zynq_osd_enable_rue(struct zynq_osd *osd);
void zynq_osd_disable_rue(struct zynq_osd *osd);


struct device;
struct device_node;

struct zynq_osd *zynq_osd_probe(struct device *dev, struct device_node *node);
void zynq_osd_remove(struct zynq_osd *osd);

#endif
