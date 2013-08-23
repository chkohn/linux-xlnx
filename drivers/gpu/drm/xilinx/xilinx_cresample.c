/*
 * Xilinx Chroma Resampler support for Xilinx DRM KMS
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

#include "xilinx_drm_drv.h"

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

struct xilinx_cresample {
	void __iomem *base;		/* cresample base addr */
};

/* io write operations */
static inline void xilinx_cresample_writel(struct xilinx_cresample *cresample,
		int offset, u32 val)
{
	writel(val, cresample->base + offset);
}

/* io read operations */
static inline u32 xilinx_cresample_readl(struct xilinx_cresample *cresample,
		int offset)
{
	return readl(cresample->base + offset);
}

/* enable cresample */
void xilinx_cresample_enable(struct xilinx_cresample *cresample)
{
	u32 reg;

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");

	reg = xilinx_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg |= CRESAMPLE_CTL_EN;
	xilinx_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
}

/* disable cresample */
void xilinx_cresample_disable(struct xilinx_cresample *cresample)
{
	u32 reg;

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");

	reg = xilinx_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg &= ~CRESAMPLE_CTL_EN;
	xilinx_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
}

/* configure cresample */
void xilinx_cresample_configure(struct xilinx_cresample *cresample,
		int hactive, int vactive)
{
	u32 reg;

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");

	/* disable register update */
	reg = xilinx_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg &= ~CRESAMPLE_CTL_RU;
	xilinx_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	/* configure hsize and vsize */
	xilinx_cresample_writel(cresample, CRESAMPLE_ACTIVE_SIZE,
			(vactive << 16) | hactive);

	/* enable register update */
	reg = xilinx_cresample_readl(cresample, CRESAMPLE_CONTROL);
	reg |= CRESAMPLE_CTL_RU;
	xilinx_cresample_writel(cresample, CRESAMPLE_CONTROL, reg);

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
}

/* reset cresample */
void xilinx_cresample_reset(struct xilinx_cresample *cresample)
{
	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
	xilinx_cresample_writel(cresample, CRESAMPLE_CONTROL,
			CRESAMPLE_CTL_RESET);
	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
}

struct xilinx_cresample *xilinx_cresample_probe(struct device *dev,
		struct device_node *node)
{
	struct xilinx_cresample *cresample;
	struct xilinx_cresample *err_ret;

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");

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

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");

	return cresample;

err_iomap:
err_cresample:
	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
	return err_ret;
}

void xilinx_cresample_remove(struct xilinx_cresample *cresample)
{
	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");

	xilinx_cresample_reset(cresample);

	iounmap(cresample->base);

	XILINX_DEBUG_KMS(XILINX_KMS_CRESAMPLE, "\n");
}
