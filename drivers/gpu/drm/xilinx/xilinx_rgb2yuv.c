/*
 * Xilinx rgb to yuv converter support for Xilinx DRM KMS
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
#define RGB_CONTROL	0x000	/* control        */
/* timing control registers */
#define RGB_ACTIVE_SIZE	0x020	/* active size (v x h)       */

/* ccm control register bit definition */
#define RGB_CTL_EN	(1 << 0)	/* ccm enable */
#define RGB_CTL_RUE	(1 << 1)	/* ccm register update enable */

/* ccm reset register bit definition */
#define RGB_RST_RESET	(1 << 31)	/* software reset - instantaneous */

struct xilinx_rgb2yuv {
	void __iomem *base;		/* rgb2yuv base addr */
};

/* io write operations */
static inline void xilinx_rgb2yuv_writel(struct xilinx_rgb2yuv *rgb2yuv,
		int offset, u32 val)
{
	writel(val, rgb2yuv->base + offset);
}

/* io read operations */
static inline u32 xilinx_rgb2yuv_readl(struct xilinx_rgb2yuv *rgb2yuv,
		int offset)
{
	return readl(rgb2yuv->base + offset);
}

/* enable rgb2yuv */
void xilinx_rgb2yuv_enable(struct xilinx_rgb2yuv *rgb2yuv)
{
	u32 reg;

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");

	reg = xilinx_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg |= RGB_CTL_EN;
	xilinx_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
}

/* disable rgb2yuv */
void xilinx_rgb2yuv_disable(struct xilinx_rgb2yuv *rgb2yuv)
{
	u32 reg;

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");

	reg = xilinx_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg &= ~RGB_CTL_EN;
	xilinx_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
}

/* configure rgb2yuv */
void xilinx_rgb2yuv_configure(struct xilinx_rgb2yuv *rgb2yuv,
		int hactive, int vactive)
{
	u32 reg;

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");

	reg = xilinx_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg &= ~RGB_CTL_RUE;
	xilinx_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	xilinx_rgb2yuv_writel(rgb2yuv, RGB_ACTIVE_SIZE,
			(vactive << 16) | hactive);

	reg = xilinx_rgb2yuv_readl(rgb2yuv, RGB_CONTROL);
	reg |= RGB_CTL_RUE;
	xilinx_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, reg);

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
}

/* reset rgb2yuv */
void xilinx_rgb2yuv_reset(struct xilinx_rgb2yuv *rgb2yuv)
{
	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
	xilinx_rgb2yuv_writel(rgb2yuv, RGB_CONTROL, RGB_RST_RESET);
	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
}

/* probe rgb2yuv */
struct xilinx_rgb2yuv *xilinx_rgb2yuv_probe(struct device *dev,
		struct device_node *node)
{
	struct xilinx_rgb2yuv *rgb2yuv;
	struct xilinx_rgb2yuv *err_ret;

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");

	rgb2yuv = devm_kzalloc(dev, sizeof(*rgb2yuv), GFP_KERNEL);
	if (!rgb2yuv) {
		pr_err("failed to alloc rgb2yuv\n");
		err_ret = ERR_PTR(-ENOMEM);
		goto err_rgb2yuv;
	}

	rgb2yuv->base = of_iomap(node, 0);
	if (!rgb2yuv->base) {
		pr_err("failed to ioremap rgb2yuv\n");
		err_ret = ERR_PTR(-ENXIO);
		goto err_iomap;
	}

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");

	return rgb2yuv;

err_iomap:
err_rgb2yuv:
	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
	return err_ret;
}

/* remove rgb2yuv */
void xilinx_rgb2yuv_remove(struct xilinx_rgb2yuv *rgb2yuv)
{
	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");

	xilinx_rgb2yuv_reset(rgb2yuv);

	iounmap(rgb2yuv->base);

	XILINX_DEBUG_KMS(XILINX_KMS_RGB2YUV, "\n");
}
