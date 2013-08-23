/*
 * Video Timing Controller Header for Xilinx DRM KMS
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

#ifndef _XILINX_VTC_H_
#define _XILINX_VTC_H_

struct xilinx_vtc;

struct xilinx_vtc_sig_config {
	u16 htotal;
	u16 hfrontporch_start;
	u16 hsync_start;
	u16 hbackporch_start;
	u16 hactive_start;

	u16 vtotal;
	u16 vfrontporch_start;
	u16 vsync_start;
	u16 vbackporch_start;
	u16 vactive_start;
};

void xilinx_vtc_config_sig(struct xilinx_vtc *vtc,
		struct xilinx_vtc_sig_config *sig_config);
void xilinx_vtc_enable_vblank_intr(struct xilinx_vtc *vtc,
		void (*fn)(void *), void *data);
void xilinx_vtc_disable_vblank_intr(struct xilinx_vtc *vtc);
void xilinx_vtc_reset(struct xilinx_vtc *vtc);
void xilinx_vtc_enable(struct xilinx_vtc *vtc);
void xilinx_vtc_disable(struct xilinx_vtc *vtc);

struct device;
struct device_node;

struct xilinx_vtc *xilinx_vtc_probe(struct device *dev,
		struct device_node *node);
void xilinx_vtc_remove(struct xilinx_vtc *vtc);

#endif
