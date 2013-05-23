/*
 * Xilinx DRM connector header for Zynq
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

#ifndef _ZYNQ_DRM_CONNECTOR_H_
#define _ZYNQ_DRM_CONNECTOR_H_

struct drm_connector *zynq_drm_connector_create(struct drm_device *drm,
		struct drm_encoder *base_encoder);
void zynq_drm_connector_destroy(struct drm_connector *connector);

#endif
