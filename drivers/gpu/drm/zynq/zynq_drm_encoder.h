/*
 * Xilinx DRM encoder header for Zynq
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

#ifndef _ZYNQ_DRM_ENCODER_H_
#define _ZYNQ_DRM_ENCODER_H_

struct drm_encoder *zynq_drm_encoder_create(struct drm_device *drm);
void zynq_drm_encoder_destroy(struct drm_encoder *encoder);

#endif
