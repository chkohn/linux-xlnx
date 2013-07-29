/*
 * Xilinx DRM plane header for Zynq
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

#ifndef _ZYNQ_DRM_PLANE_H_
#define _ZYNQ_DRM_PLANE_H_

struct drm_crtc;
struct drm_plane;

void zynq_drm_plane_dpms(struct drm_plane *base_plane, int dpms);
void zynq_drm_plane_commit(struct drm_plane *base_plane);
int zynq_drm_plane_mode_set(struct drm_plane *base_plane, struct drm_crtc *crtc,
		struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h);

struct zynq_drm_plane_manager;

struct drm_plane *zynq_drm_plane_create_private(
		struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs);
void zynq_drm_plane_destroy_private(struct zynq_drm_plane_manager *manager,
		struct drm_plane *base_plane);
int zynq_drm_plane_create_planes(struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs);
void zynq_drm_plane_destroy_planes(struct zynq_drm_plane_manager *manager);

struct zynq_drm_plane_manager *
zynq_drm_plane_probe_manager(struct drm_device *drm);
void zynq_drm_plane_remove_manager(struct zynq_drm_plane_manager *manager);

#endif
