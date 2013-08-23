/*
 * Xilinx DRM KMS Header for Xilinx
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

#ifndef _XILINX_DRM_H_
#define _XILINX_DRM_H_

#define XILINX_KMS_DEBUG	0

#if XILINX_KMS_DEBUG

#define XILINX_KMS_DRV		(0)
#define XILINX_KMS_CRTC		(1)
#define XILINX_KMS_PLANE	(2)
#define XILINX_KMS_ENCODER	(3)
#define XILINX_KMS_CONNECTOR	(4)
#define XILINX_KMS_CRESAMPLE	(5)
#define XILINX_KMS_OSD		(6)
#define XILINX_KMS_RGB2YUV	(7)
#define XILINX_KMS_VTC		(8)
#define XILINX_KMS_DEBUG_ALL	(0x1ff)

void xilinx_drm_debug(int type, const char *func, int line,
		const char *fmt, ...);

#define XILINX_DEBUG_KMS(type, fmt, ...)	\
	xilinx_drm_debug(type, __func__, __LINE__, fmt, ##__VA_ARGS__);

#else /* XILINX_KMS_DEBUG */

#define XILINX_DEBUG_KMS(type, fmt, ...)	\
	do { } while (0)

#endif /* XILINX_KMS_DEBUG */

#endif /* _XILINX_DRM_H_ */
