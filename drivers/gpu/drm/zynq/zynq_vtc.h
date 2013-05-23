/*
 * Video Timing Controller Header for Zynq DRM KMS
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

struct zynq_vtc;

struct zynq_vtc_polarity {
	u8 active_chroma;
	u8 active_video;
	u8 field_id;
	u8 vblank;
	u8 vsync;
	u8 hblank;
	u8 hsync;
};

struct zynq_vtc_hori_offset {
	u16 v0blank_hori_start;
	u16 v0blank_hori_end;
	u16 v0sync_hori_start;
	u16 v0sync_hori_end;
	u16 v1blank_hori_start;
	u16 v1blank_hori_end;
	u16 v1sync_hori_start;
	u16 v1sync_hori_end;
};

struct zynq_vtc_src_config {
	u8 field_id_pol;
	u8 active_chroma_pol;
	u8 active_video_pol;
	u8 hsync_pol;
	u8 vsync_pol;
	u8 hblank_pol;
	u8 vblank_pol;

	u8 vchroma;
	u8 vactive;
	u8 vbackporch;
	u8 vsync;
	u8 vfrontporch;
	u8 vtotal;

	u8 hactive;
	u8 hbackporch;
	u8 hsync;
	u8 hfrontporch;
	u8 htotal;
};

struct zynq_vtc_sig_config {
	u16 htotal;
	u16 hfrontporch_start;
	u16 hsync_start;
	u16 hbackporch_start;
	u16 hactive_start;

	u16 v0total;
	u16 v0frontporch_start;
	u16 v0sync_start;
	u16 v0backporch_start;
	u16 v0active_start;
	u16 v0chroma_start;

	u16 v1total;
	u16 v1frontporch_start;
	u16 v1sync_start;
	u16 v1backporch_start;
	u16 v1active_start;
	u16 v1chroma_start;
};

void zynq_vtc_config_sig(struct zynq_vtc *vtc,
		struct zynq_vtc_sig_config *sig_config);
void zynq_vtc_reset(struct zynq_vtc *vtc);
void zynq_vtc_enable(struct zynq_vtc *vtc);
void zynq_vtc_disable(struct zynq_vtc *vtc);

struct zynq_vtc *zynq_vtc_probe(char *compatible);
void zynq_vtc_remove(struct zynq_vtc *vtc);

#endif
