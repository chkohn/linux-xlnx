/*
 * Xilinx Chroma Resampler support for Zynq DRM KMS
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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "zynq_drm_drv.h"

/* registers */
/* general control registers */
#define CRESAMPLE_CONTROL		0x0000	/* control */

/* timing control registers */
#define CRESAMPLE_ACTIVE_SIZE		0x0020	/* horizontal and vertical
						   active frame size */

/* control register bit definition */
#define CRESAMPLE_CTL_EN		(1 << 0)	/* enable */
#define CRESAMPLE_CTL_RU		(1 << 1)	/* register update */
#define CRESAMPLE_CTL_RESET		(1 << 31)	/* software reset -
							   instantaneous */

struct zynq_cresample {
	void __iomem *base;		/* cresample base addr */
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
	reg |= CRESAMPLE_CTL_EN;
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}

/* disable cresample */
void zynq_cresample_disable(struct zynq_cresample *cresample)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	reg = zynq_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg &= ~CRESAMPLE_CTL_EN;
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
	reg &= ~CRESAMPLE_CTL_RU;
	zynq_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	/* configure hsize and vsize */
	zynq_cresample_writel(cresample, CRESAMPLE_ACTIVE_SIZE,
			(vactive << 16) | hactive);

	/* enable register update */
	reg = zynq_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg |= CRESAMPLE_CTL_RU;
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

struct zynq_cresample *zynq_cresample_probe(struct device *dev,
		struct device_node *node)
{
	struct zynq_cresample *cresample;
	struct zynq_cresample *err_ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	cresample = devm_kzalloc(dev, sizeof(*cresample), GFP_KERNEL);
	if (!cresample) {
		pr_err("failed to alloc cresample\n");
		err_ret = ERR_PTR(-ENOMEM);
		goto err_cresample;
	}

	cresample->base = of_iomap(node, 0);
	if (!cresample->base) {
		pr_err("failed to ioremap cresample\n");
		err_ret = ERR_PTR(-ENXIO);
		goto err_iomap;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	return cresample;

err_iomap:
err_cresample:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
	return err_ret;
}

void zynq_cresample_remove(struct zynq_cresample *cresample)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");

	zynq_cresample_reset(cresample);

	iounmap(cresample->base);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRESAMPLE, "\n");
}
