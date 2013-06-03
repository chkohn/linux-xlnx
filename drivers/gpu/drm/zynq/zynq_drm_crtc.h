/*
 * Xilinx DRM crtc header for Zynq
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

#ifndef _ZYNQ_DRM_CRTC_H_
#define _ZYNQ_DRM_CRTC_H_

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

struct drm_crtc *zynq_drm_crtc_create(struct drm_device *drm);
void zynq_drm_crtc_destroy(struct drm_crtc *crtc);

#endif
