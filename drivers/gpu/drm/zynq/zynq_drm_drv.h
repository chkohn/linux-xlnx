/*
 * Xilinx DRM KMS Header for Zynq
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
#define ZYNQ_KMS_RGB2YUV	(6)
#define ZYNQ_KMS_VTC		(7)
#define ZYNQ_KMS_DEBUG_ALL	(0xff)

extern int zynq_kms_debug_enabled;

static char *zynq_kms_type[] = {"KMS DRV",
				"CRTC",
				"PLANE",
				"ENCODER",
				"CONNECTOR",
				"CRESMAPLE",
				"RGB2YUV",
				"VTC"};

#define ZYNQ_DEBUG_KMS(type, fmt, args...)				\
	do {								\
		if ((1 << type) & zynq_kms_debug_enabled)		\
			printk(KERN_INFO "[%s]%s:%d " fmt,		\
					zynq_kms_type[type],		\
					 __func__, __LINE__, ##args);	\
	} while (0)

#else /* ZYNQ_KMS_DEBUG */

#define ZYNQ_DEBUG_KMS(type, fmt, args...)

#endif /* ZYNQ_KMS_DEBUG */

#endif /* _ZYNQ_DRM_H_ */
