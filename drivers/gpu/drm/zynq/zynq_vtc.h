/*
 * Video Timing Controller Header for Zynq DRM KMS
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

struct zynq_vtc;

struct zynq_vtc_sig_config {
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

void zynq_vtc_config_sig(struct zynq_vtc *vtc,
		struct zynq_vtc_sig_config *sig_config);
void zynq_vtc_reset(struct zynq_vtc *vtc);
void zynq_vtc_enable(struct zynq_vtc *vtc);
void zynq_vtc_disable(struct zynq_vtc *vtc);

struct device;

struct zynq_vtc *zynq_vtc_probe(struct device *dev, char *compatible);
void zynq_vtc_remove(struct zynq_vtc *vtc);

#endif
