/*
 * Xilinx Chroma Resampler support for Zynq DRM KMS
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
#define CRESAMPLE_CONTROL		0x0000	/* control */
#define CRESAMPLE_STATUS		0x0004	/* status */
#define CRESAMPLE_ERROR			0x0008	/* error */
#define CRESAMPLE_IRQ_ENABLE		0x000c	/* irq enable */
#define CRESAMPLE_VERSION		0x0010	/* version */
#define CRESAMPLE_SYSDEBUG0		0x0014	/* system debug 0 */
#define CRESAMPLE_SYSDEBUG1		0x0018	/* system debug 1 */
#define CRESAMPLE_SYSDEBUG2		0x001c	/* system debug 2 */

/* timing control registers */
#define CRESAMPLE_ACTIVE_SIZE		0x0020	/* horizontal and vertical
						   active frame size */
#define CRESAMPLE_ENCODING		0x0028	/* frame encoding */

/* control register bit definition */
#define CRESAMPLE_CTL_EN_MASK		0x00000001	/* enable */
#define CRESAMPLE_CTL_RU_MASK		0x00000002	/* register update */
#define CRESAMPLE_CTL_AUTORESET		0x40000000	/* software reset -
							   auto-synchronize to
							   SOF */
#define CRESAMPLE_CTL_RESET		0x80000000	/* software reset -
							   instantaneous */

struct zynq_cresample {
	void __iomem *base;		/* cresample base addr */
	struct device_node *node;	/* device node */
};

/* io write operations */
static inline void zynq_cresample_writel(struct zynq_cresample *cresample,
		int offset, u32 val)
{
	writel(val, cresample->base + offset);
}

/* io read operations */
static inline u32 zynq_cresample_readl(struct zynq_cresample *cresample,
		int offset)
{
	return readl(cresample->base + offset);
}

/* enable cresample */
void zynq_cresample_enable(struct zynq_cresample *cresample)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	reg = zynq_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg |= CRESAMPLE_CTL_EN_MASK;
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}

/* disable cresample */
void zynq_cresample_disable(struct zynq_cresample *cresample)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	reg = zynq_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg &= ~CRESAMPLE_CTL_EN_MASK;
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}

/* configure cresample */
void zynq_cresample_configure(struct zynq_cresample *cresample,
		int hactive, int vactive)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	/* disable register update */
	reg = zynq_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg &= ~CRESAMPLE_CTL_RU_MASK;
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	/* configure hsize and vsize */
	zynq_cresample_writel(cresample, CRESAMPLE_ACTIVE_SIZE,
			(vactive << 16) | hactive);

	/* enable register update */
	reg = zynq_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg |= CRESAMPLE_CTL_RU_MASK;
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}

/* reset cresample */
void zynq_cresample_reset(struct zynq_cresample *cresample)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL,
			CRESAMPLE_CTL_RESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}

/* frame synced reset cresample */
void zynq_cresample_fsync_reset(struct zynq_cresample *cresample)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL,
			CRESAMPLE_CTL_AUTORESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}

struct zynq_cresample *zynq_cresample_probe(char *compatible)
{
	struct zynq_cresample *cresample;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	cresample = kzalloc(sizeof(*cresample), GFP_KERNEL);
	if (!cresample) {
		pr_err("failed to alloc cresample\n");
		goto err_cresample;
	}

	cresample->node = of_find_compatible_node(NULL, NULL, compatible);
	if (!cresample->node) {
		pr_err("failed to find a compatible node(%s)\n", compatible);
		goto err_node;
	}

	cresample->base = of_iomap(cresample->node, 0);
	if (!cresample->base) {
		pr_err("failed to ioremap cresample\n");
		goto err_iomap;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	return cresample;

err_iomap:
	of_node_put(cresample->node);
err_node:
	kfree(cresample);
err_cresample:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
	return NULL;
}

void zynq_cresample_remove(struct zynq_cresample *cresample)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
	zynq_cresample_reset(cresample);
	iounmap(cresample->base);
	of_node_put(cresample->node);
	kfree(cresample);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}
