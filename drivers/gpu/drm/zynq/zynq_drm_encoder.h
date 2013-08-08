/*
 * Xilinx DRM encoder header for Zynq
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

#ifndef _ZYNQ_DRM_ENCODER_H_
#define _ZYNQ_DRM_ENCODER_H_

struct drm_device;
struct drm_encoder;

struct drm_encoder *zynq_drm_encoder_create(struct drm_device *drm);
void zynq_drm_encoder_destroy(struct drm_encoder *base_encoder);

#endif
