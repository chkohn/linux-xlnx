/*
 * Xilinx OSD support
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
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include "zynq_drm_drv.h"

/*
 * registers
 */
#define OSD_CTL	0x000	/* control */
#define OSD_VER	0x010	/* version register */
#define OSD_SS	0x020	/* screen size */
#define OSD_BC0	0x100	/* background color channel 0 */
#define OSD_BC1	0x104	/* background color channel 1 */
#define OSD_BC2	0x108	/* background color channel 2 */

#define OSD_L0C	0x110	/* layer 0 control */
#define OSD_L0P	0x114	/* layer 0 position */
#define OSD_L0S	0x118	/* layer 0 size */

/*
 * register offset of layers
 */
#define OSD_LAYER_SIZE	0x10
#define OSD_LXC		0x00	/* layer control */
#define OSD_LXP		0x04	/* layer position */
#define OSD_LXS		0x08	/* layer size */

/*
 * osd control register bit definition
 */
#define OSD_CTL_RUE		(1 << 1)	/* osd reg update enable */
#define OSD_CTL_EN		(1 << 0)	/* osd enable */

/*
 * osd screen size register bit definition
 */
#define OSD_SS_YSIZE_MASK   0x0fff0000	/* vertical height of OSD output */
#define OSD_SS_YSIZE_SHIFT  16		/* bit shift of OSD_SS_YSIZE_MASK */
#define OSD_SS_XSIZE_MASK   0x00000fff	/* horizontal width of OSD output */

/*
 * osd background color channel 0
 */
#define OSD_BC0_YG_MASK		0x000000ff	/* Y (luma) or Green */

/*
 * osd background color channel 1
 */
#define OSD_BC1_UCBB_MASK	0x000000ff	/* U (Cb) or Blue */

/*
 * osd background color channel 2
 */
#define OSD_BC2_VCRR_MASK	0x000000ff	/* V(Cr) or Red */

/*
 * maximum number of the layers
 */
#define OSD_MAX_NUM_OF_LAYERS	8	/* the max number of layers */

/*
 * osd layer control (layer 0 through (OSD_MAX_NUM_OF_LAYERS - 1))
 */
#define OSD_LXC_ALPHA_MASK	0x0fff0000	/* global alpha value */
#define OSD_LXC_ALPHA_SHIFT	16	  	/* bit shift of alpha value */
#define OSD_LXC_PRIORITY_MASK	0x00000700	/* layer priority */
#define OSD_LXC_PRIORITY_SHIFT	8	  	/* bit shift of priority */
#define OSD_LXC_GALPHAEN	(1 << 1)	/* global alpha enable */
#define OSD_LXC_EN		(1 << 0)	/* layer enable */

/*
 * osd layer position (layer 0 through (OSD_MAX_NUM_OF_LAYERS - 1))
 */
#define OSD_LXP_YSTART_MASK	0x0fff0000	/* vertical start line */
#define OSD_LXP_YSTART_SHIFT	16		/* bit shift of vertical start
						   line */
#define OSD_LXP_XSTART_MASK	0x00000fff	/* horizontal start pixel */

/*
 * osd layer size (layer 0 through (OSD_MAX_NUM_OF_LAYERS - 1))
 */
#define OSD_LXS_YSIZE_MASK	0x0fff0000	/* vertical size of layer */
#define OSD_LXS_YSIZE_SHIFT	16	  	/* bit shift of vertical size */
#define OSD_LXS_XSIZE_MASK	0x00000fff	/* horizontal size of layer */

/*
 * osd software reset
 */
#define OSD_RST_RESET	(1 << 31)	/* software reset */

struct zynq_osd_layer {
	void __iomem *base;		/* layer base addr */
	int id;				/* layer id */
	struct zynq_osd *osd;		/* osd */
};

struct zynq_osd {
	void __iomem *base;		/* osd base addr */
	struct device *dev;		/* (parent) device */
	struct device_node *node;	/* device node */
	struct zynq_osd_layer *layers[OSD_MAX_NUM_OF_LAYERS];	/* layers */
	int num_layers;			/* num of layers */
	u32 width;			/* width */
	u32 height;			/* height */
};


static inline void zynq_osd_enable_rue(struct zynq_osd *osd);
static inline void zynq_osd_disable_rue(struct zynq_osd *osd);

/*
 * osd layer operation
 */
/* layer io write operations */
static inline void zynq_osd_layer_writel(struct zynq_osd_layer *layer,
		int offset, u32 val)
{
	writel(val, layer->base + offset);
}

/* layer io read operations */
static inline u32 zynq_osd_layer_readl(struct zynq_osd_layer *layer,
		int offset)
{
	return readl(layer->base + offset);
}

/* set layer alpha */
void zynq_osd_layer_set_alpha(struct zynq_osd_layer *layer, u32 enable,
		u32 alpha)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "alpha: 0x%08x\n", alpha);

	zynq_osd_disable_rue(layer->osd);
	value = zynq_osd_layer_readl(layer, OSD_LXC);
	value = enable ? (value | OSD_LXC_GALPHAEN) :
		(value & ~OSD_LXC_GALPHAEN);
	value &= ~OSD_LXC_ALPHA_MASK;
	value |= (alpha << OSD_LXC_ALPHA_SHIFT) & OSD_LXC_ALPHA_MASK;
	zynq_osd_layer_writel(layer, OSD_LXC, value);
	zynq_osd_enable_rue(layer->osd);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* set layer priority */
void zynq_osd_layer_set_priority(struct zynq_osd_layer *layer, u32 prio)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "prio: %d\n", prio);

	zynq_osd_disable_rue(layer->osd);
	value = zynq_osd_layer_readl(layer, OSD_LXC);
	value &= ~OSD_LXC_PRIORITY_MASK;
	value |= (prio << OSD_LXC_PRIORITY_SHIFT) & OSD_LXC_PRIORITY_MASK;
	zynq_osd_layer_writel(layer, OSD_LXC, value);
	zynq_osd_enable_rue(layer->osd);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* set layer dimension */
void zynq_osd_layer_set_dimension(struct zynq_osd_layer *layer,
		u16 xstart, u16 ystart, u16 xsize, u16 ysize)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "w: %d(%d), h: %d(%d)\n", xsize, xstart,
			ysize, ystart);

	zynq_osd_disable_rue(layer->osd);

	value = xstart & OSD_LXP_XSTART_MASK;
	value |= (ystart << OSD_LXP_YSTART_SHIFT) & OSD_LXP_YSTART_MASK;

	zynq_osd_layer_writel(layer, OSD_LXP, value);

	value = xsize & OSD_LXS_XSIZE_MASK;
	value |= (ysize << OSD_LXS_YSIZE_SHIFT) & OSD_LXS_YSIZE_MASK;

	zynq_osd_layer_writel(layer, OSD_LXS, value);

	zynq_osd_enable_rue(layer->osd);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* enable layer */
void zynq_osd_layer_enable(struct zynq_osd_layer *layer)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	value = zynq_osd_layer_readl(layer, OSD_LXC);
	value |= OSD_LXC_EN;
	zynq_osd_layer_writel(layer, OSD_LXC, value);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* disable layer */
void zynq_osd_layer_disable(struct zynq_osd_layer *layer)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	value = zynq_osd_layer_readl(layer, OSD_LXC);
	value &= ~OSD_LXC_EN;
	zynq_osd_layer_writel(layer, OSD_LXC, value);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* create layer */
struct zynq_osd_layer *zynq_osd_layer_create(struct zynq_osd *osd)
{
	struct zynq_osd_layer *layer;
	int i;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");

	for (i = 0; i < osd->num_layers; i++) {
		if (!osd->layers[i])
			break;
	}
	if (i >= osd->num_layers) {
		pr_err("no available osd layer\n");
		goto err_out;
	}

	layer = devm_kzalloc(osd->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer) {
		pr_err("failed to allocate layer\n");
		goto err_out;
	}

	layer->base = osd->base + OSD_L0C + OSD_LAYER_SIZE * i;
	layer->id = i;
	layer->osd = osd;
	osd->layers[i] = layer;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer id: %d\n", i);

	return layer;

err_out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	return NULL;
}

/* destroy layer */
void zynq_osd_layer_destroy(struct zynq_osd_layer *layer)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	layer->osd->layers[layer->id] = NULL;
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	return;
}

/*
 * osd operation
 */
/* io write operations */
static inline void zynq_osd_writel(struct zynq_osd *osd, int offset, u32 val)
{
	writel(val, osd->base + offset);
}

/* io read operations */
static inline u32 zynq_osd_readl(struct zynq_osd *osd, int offset)
{
	return readl(osd->base + offset);
}

/* set osd color*/
void zynq_osd_set_color(struct zynq_osd *osd, u8 r, u8 g, u8 b)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_disable_rue(osd);
	value = g;
	zynq_osd_writel(osd, OSD_BC0, value);
	value = b;
	zynq_osd_writel(osd, OSD_BC1, value);
	value = r;
	zynq_osd_writel(osd, OSD_BC2, value);
	zynq_osd_enable_rue(osd);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* set osd dimension*/
void zynq_osd_set_dimension(struct zynq_osd *osd, u32 width, u32 height)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "w: %d, h: %d\n", width, height);
	zynq_osd_disable_rue(osd);
	value = width | ((height << OSD_SS_YSIZE_SHIFT) & OSD_SS_YSIZE_MASK);
	zynq_osd_writel(osd, OSD_SS, value);
	zynq_osd_enable_rue(osd);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* reset osd */
void zynq_osd_reset(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, OSD_RST_RESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* enable osd */
void zynq_osd_enable(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) |
			OSD_CTL_EN);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* disable osd */
void zynq_osd_disable(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) &
			~OSD_CTL_EN);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* register-update-enable osd */
static inline void zynq_osd_enable_rue(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) |
			OSD_CTL_RUE);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* register-update-enable osd */
static inline void zynq_osd_disable_rue(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) &
			~OSD_CTL_RUE);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

struct zynq_osd *zynq_osd_probe(struct device *dev, char *compatible)
{
	struct zynq_osd *osd;
	u32 prop;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");

	osd = devm_kzalloc(dev, sizeof(*osd), GFP_KERNEL);
	if (!osd) {
		pr_err("failed to alloc osd\n");
		goto err_osd;
	}

	osd->node = of_find_compatible_node(NULL, NULL, compatible);
	if (!osd->node) {
		pr_err("failed to find a compatible node(%s)\n", compatible);
		goto err_node;
	}

	osd->base = of_iomap(osd->node, 0);
	if (!osd->base) {
		pr_err("failed to ioremap osd\n");
		goto err_iomap;
	}

	/* TODO: duplicate get prop in plane. consider clean up */
	if (of_property_read_u32(osd->node, "xlnx,num-layers", &prop)) {
		pr_err("failed to get num of layers prop\n");
		goto err_prop;
	}
	osd->num_layers = prop;

	osd->dev = dev;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");

	return osd;

err_prop:
	iounmap(osd->base);
err_iomap:
	of_node_put(osd->node);
err_node:
err_osd:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	return NULL;
}

void zynq_osd_remove(struct zynq_osd *osd)
{
	int i;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");

	zynq_osd_reset(osd);
	for (i = 0; i < osd->num_layers; i++) {
		if (!osd->layers[i])
			continue;
		zynq_osd_layer_destroy(osd->layers[i]);
	}
	iounmap(osd->base);
	of_node_put(osd->node);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}
