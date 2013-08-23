/*
 * Xilinx DRM crtc header for Xilinx
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

#ifndef _XILINX_DRM_CRTC_H_
#define _XILINX_DRM_CRTC_H_

struct drm_device;
struct drm_crtc;

void xilinx_drm_crtc_enable_vblank(struct drm_crtc *base_crtc);
void xilinx_drm_crtc_disable_vblank(struct drm_crtc *base_crtc);
void xilinx_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
		struct drm_file *file);

struct drm_crtc *xilinx_drm_crtc_create(struct drm_device *drm);
void xilinx_drm_crtc_destroy(struct drm_crtc *base_crtc);

#endif
