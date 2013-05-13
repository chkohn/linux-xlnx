/*
 * Xilinx Chroma Resampler support for Zynq DRM KMS
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

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "zynq_drm_drv.h"

/* registers */
/* general control registers */
#define CRESAMPLE_CONTROL		0x0000	/* control */
#define CRESAMPLE_STATUS		0x0004	/* status */
#define CRESAMPLE_ERROR			0x0008	/* error */
#define CRESAMPLE_IRQ_ENABLE		0x000C	/* irq enable */
#define CRESAMPLE_VERSION		0x0010	/* version */
#define CRESAMPLE_SYSDEBUG0		0x0014	/* system debug 0 */
#define CRESAMPLE_SYSDEBUG1		0x0018	/* system debug 1 */
#define CRESAMPLE_SYSDEBUG2		0x001C	/* system debug 2 */

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

/* io operations */
#define zynq_cresample_writel(device, offset, val)	\
	writel(val, device->base + offset)
#define zynq_cresample_readl(device, offset)	\
	readl(device->base + offset)

/* disable interrupt */
static inline void zynq_cresample_intr_disable(struct zynq_cresample *cresample)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
	zynq_cresample_writel(cresample, CRESAMPLE_IRQ_ENABLE, 0);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
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

struct zynq_cresample *zynq_cresample_probe(char *compatible)
{
	struct zynq_cresample *cresample;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	cresample = kzalloc(sizeof(struct zynq_cresample), GFP_KERNEL);
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

	zynq_cresample_reset(cresample);
	zynq_cresample_intr_disable(cresample);

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
