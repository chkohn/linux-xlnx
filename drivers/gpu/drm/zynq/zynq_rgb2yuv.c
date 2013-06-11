/*
 * Xilinx rgb to yuv converter support for Zynq DRM KMS
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

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "zynq_drm_drv.h"

/* registers */
/* general control registers */
#define RGB_CONTROL        0x000    /* control        */
#define RGB_STATUS         0x004    /* status         */
#define RGB_ERROR          0x008    /* error          */
#define RGB_IRQ_EN         0x00C    /* irq enable     */
#define RGB_VERSION        0x010    /* version        */
#define RGB_SYSDEBUG0      0x014    /* system debug 0 */
#define RGB_SYSDEBUG1      0x018    /* system debug 1 */
#define RGB_SYSDEBUG2      0x01C    /* system debug 2 */
/* timing control registers */
#define RGB_ACTIVE_SIZE    0x020    /* active size (v x h)       */
#define RGB_TIMING_STATUS  0x024    /* timing measurement status */
/* core specific registers */
#define RGB_YMAX           0x100    /* luma clipping */
#define RGB_YMIN           0x104    /* luma clamping */
#define RGB_CBMAX          0x108    /* cb clipping   */
#define RGB_CBMIN          0x10c    /* cb clamping   */
#define RGB_CRMAX          0x110    /* cr clipping   */
#define RGB_CRMIN          0x114    /* cr clamping   */
#define RGB_YOFFSET        0x118    /* lumma offset  */
#define RGB_CBOFFSET       0x11c    /* cb offset     */
#define RGB_CROFFSET       0x120    /* cr offset     */
#define RGB_ACOEF          0x124    /* matrix coversion coefficient */
#define RGB_BCOEF          0x128    /* matrix coversion coefficient */
#define RGB_CCOEF          0x12c    /* matrix coversion coefficient */
#define RGB_DCOEF          0x130    /* matrix coversion coefficient */

/* ccm control register bit definition */
#define RGB_CTL_EN_MASK     0x00000001 /* ccm enable */
#define RGB_CTL_RUE_MASK    0x00000002 /* ccm register update enable */

/* ccm reset register bit definition */
#define RGB_RST_RESET      0x80000000 /* software reset -
					 instantaneous */
#define RGB_RST_AUTORESET  0x40000000 /* software reset -
					 auto-synchronize to sof */

struct zynq_rgb2yuv {
	void __iomem *base;		/* rgb2yuv base addr */
	struct device_node *node;	/* device node */
};

/* io write operations */
static inline void zynq_rgb2yuv_writel(struct zynq_rgb2yuv *rgb2yuv, int offset,
		u32 val)
{
	writel(val, rgb2yuv->base + offset);
}

/* io read operations */
static inline u32 zynq_rgb2yuv_readl(struct zynq_rgb2yuv *rgb2yuv, int offset)
{
	return readl(rgb2yuv->base + offset);
}

/* enable rgb2yuv */
void zynq_rgb2yuv_enable(struct zynq_rgb2yuv *rgb2yuv)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");

	reg = zynq_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg |= RGB_CTL_EN_MASK;
	zynq_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
}

/* disable rgb2yuv */
void zynq_rgb2yuv_disable(struct zynq_rgb2yuv *rgb2yuv)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");

	reg = zynq_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg &= ~RGB_CTL_EN_MASK;
	zynq_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
}

/* configure rgb2yuv */
void zynq_rgb2yuv_configure(struct zynq_rgb2yuv *rgb2yuv,
		int hactive, int vactive)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");

	reg = zynq_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg &= ~RGB_CTL_RUE_MASK;
	zynq_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	zynq_rgb2yuv_writel(rgb2yuv, RGB_ACTIVE_SIZE,
			(vactive << 16) | hactive);

	reg = zynq_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg |= RGB_CTL_RUE_MASK;
	zynq_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
}

/* reset rgb2yuv */
void zynq_rgb2yuv_reset(struct zynq_rgb2yuv *rgb2yuv)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
	zynq_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, RGB_RST_RESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
}

/* frame synced reset rgb2yuv */
void zynq_rgb2yuv_fsync_reset(struct zynq_rgb2yuv *rgb2yuv)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
	zynq_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, RGB_RST_AUTORESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
}

/* probe rgb2yuv */
struct zynq_rgb2yuv *zynq_rgb2yuv_probe(char *compatible)
{
	struct zynq_rgb2yuv *rgb2yuv;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");

	rgb2yuv = kzalloc(sizeof(*rgb2yuv), GFP_KERNEL);
	if (!rgb2yuv) {
		pr_err("failed to alloc rgb2yuv\n");
		goto err_rgb2yuv;
	}

	rgb2yuv->node = of_find_compatible_node(NULL, NULL, compatible);
	if (!rgb2yuv->node) {
		pr_err("failed to find a compatible node(%s)\n", compatible);
		goto err_node;
	}

	rgb2yuv->base = of_iomap(rgb2yuv->node, 0);
	if (!rgb2yuv->base) {
		pr_err("failed to ioremap rgb2yuv\n");
		goto err_iomap;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");

	return rgb2yuv;

err_iomap:
	of_node_put(rgb2yuv->node);
err_node:
	kfree(rgb2yuv);
err_rgb2yuv:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
	return NULL;
}

/* remove rgb2yuv */
void zynq_rgb2yuv_remove(struct zynq_rgb2yuv *rgb2yuv)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
	zynq_rgb2yuv_reset(rgb2yuv);
	iounmap(rgb2yuv->base);
	of_node_put(rgb2yuv->node);
	kfree(rgb2yuv);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_RGB2YUV, "\n");
}
