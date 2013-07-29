/*
 * Xilinx DRM crtc header for Zynq
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

#ifndef _ZYNQ_DRM_CRTC_H_
#define _ZYNQ_DRM_CRTC_H_

struct drm_device;
struct drm_crtc;

struct drm_crtc *zynq_drm_crtc_create(struct drm_device *drm);
void zynq_drm_crtc_destroy(struct drm_crtc *crtc);

#endif
