/*
 * Xilinx DRM KMS Header for Zynq
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

#ifndef _ZYNQ_DRM_H_
#define _ZYNQ_DRM_H_

#define ZYNQ_KMS_DEBUG	0

#if ZYNQ_KMS_DEBUG

#define ZYNQ_KMS_DRV		(0)
#define ZYNQ_KMS_CRTC		(1)
#define ZYNQ_KMS_PLANE		(2)
#define ZYNQ_KMS_ENCODER	(3)
#define ZYNQ_KMS_CONNECTOR	(4)
#define ZYNQ_KMS_CRESAMPLE	(5)
#define ZYNQ_KMS_OSD		(6)
#define ZYNQ_KMS_RGB2YUV	(7)
#define ZYNQ_KMS_VTC		(8)
#define ZYNQ_KMS_DEBUG_ALL	(0x1ff)

void zynq_drm_debug(int type, const char *func, int line, const char *fmt, ...);

#define ZYNQ_DEBUG_KMS(type, fmt, ...)	\
	zynq_drm_debug(type, __func__, __LINE__, fmt, ##__VA_ARGS__);

#else /* ZYNQ_KMS_DEBUG */

#define ZYNQ_DEBUG_KMS(type, fmt, ...)	\
	do {				\
	} while (0)

#endif /* ZYNQ_KMS_DEBUG */

#endif /* _ZYNQ_DRM_H_ */
