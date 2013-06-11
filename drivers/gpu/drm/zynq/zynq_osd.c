/*
 * Xilinx OSD support
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
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include "zynq_drm_drv.h"

/*
 * registers
 */
#define OSD_CTL	0x000	/* control */
#define OSD_SS	0x020	/* screen size */
#define OSD_BC0	0x100	/* background color channel 0 */
#define OSD_BC1	0x104	/* background color channel 1 */
#define OSD_BC2	0x108	/* background color channel 2 */

#define OSD_L0C	0x110	/* layer 0 control */
#define OSD_L0P	0x114	/* layer 0 position */
#define OSD_L0S	0x118	/* layer 0 size */

#define OSD_L1C	0x120	/* layer 1 control */
#define OSD_L1P	0x124	/* layer 1 position */
#define OSD_L1S	0x128	/* layer 1 size */

#define OSD_L2C	0x130	/* layer 2 control */
#define OSD_L2P	0x134	/* layer 2 position */
#define OSD_L2S	0x138	/* layer 2 size */

#define OSD_L3C	0x140	/* layer 3 control */
#define OSD_L3P	0x144	/* layer 3 position */
#define OSD_L3S	0x148	/* layer 3 size */

#define OSD_L4C	0x150	/* layer 4 control */
#define OSD_L4P	0x154	/* layer 4 position */
#define OSD_L4S	0x158	/* layer 4 size */

#define OSD_L5C	0x160	/* layer 5 control */
#define OSD_L5P	0x164	/* layer 5 position */
#define OSD_L5S	0x168	/* layer 5 size */

#define OSD_L6C	0x170	/* layer 6 control */
#define OSD_L6P	0x174	/* layer 6 position */
#define OSD_L6S	0x178	/* layer 6 size */

#define OSD_L7C	0x180	/* layer 7 control */
#define OSD_L7P	0x184	/* layer 7 position */
#define OSD_L7S	0x188	/* layer 7 size */

#define OSD_GCWBA	0x190	/* gpu write bank address */
#define OSD_GCABA	0x194	/* gpu active bank address */
#define OSD_GCD		0x198	/* gpu data */

#define OSD_VER	0x010	/* version register */
#define OSD_RST	0x000	/* software reset */

#define OSD_GIER	0x010	/* global interrupt enable register */
#define OSD_ISR		0x004	/* interrupt status register */
#define OSD_IER		0x00c	/* interrupt enable register */

/*
 * memory footprint of layers
 */
#define OSD_LAYER_SIZE	0x10
#define OSD_LXC		0x00	/* layer control */
#define OSD_LXP		0x04	/* layer position */
#define OSD_LXS		0x08	/* layer size */

/*
 * graphics controller bank related constants
 */
#define OSD_GC_BANK_NUM	2	/* the number of banks for each */

/*
 * osd control register bit definition
 */
#define OSD_CTL_VBP_MASK	0x00000020	/* vertical blank polarity */
#define OSD_CTL_HBP_MASK	0x00000010	/* horizontal blank polarity */
#define OSD_CTL_RUE_MASK	0x00000002	/* osd reg update enable */
#define OSD_CTL_EN_MASK		0x00000001	/* osd enable */

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
#define OSD_LXC_GALPHAEN_MASK	0x00000002	/* global alpha enable */
#define OSD_LXC_EN_MASK		0x00000001	/* layer enable */

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
 * osd graphics controller write bank address
 */
#define OSD_GCWBA_GCNUM_MASK	0x00000700	/* GC Number */
#define OSD_GCWBA_GCNUM_SHIFT	8		/* bit shift of GC number */
#define OSD_GCWBA_BANK_MASK	0x00000007	/* controls which bank to write
						   GPU instructions and color
						   LUT data into. */

#define OSD_GCWBA_INS0		0x00000000	/* instruction RAM 0 */
#define OSD_GCWBA_INS1		0x00000001	/* instruction RAM 1 */
#define OSD_GCWBA_COL0		0x00000002	/* color LUT RAM 0 */
#define OSD_GCWBA_COL1		0x00000003	/* color LUT RAM 1 */
#define OSD_GCWBA_TXT0		0x00000004	/* text RAM 0 */
#define OSD_GCWBA_TXT1		0x00000005	/* text RAM 1 */
#define OSD_GCWBA_CHR0		0x00000006	/* character set RAM 0 */
#define OSD_GCWBA_CHR1		0x00000007	/* character set RAM 1 */

/*
 * osd graphics controller active bank address
 */
#define OSD_GCABA_CHR_MASK	0xff000000	/* set the active character
						   bank */
#define OSD_GCABA_CHR_SHIFT	24		/* bit shift of active
						   character bank */
#define OSD_GCABA_TXT_MASK	0x00ff0000	/* set the active text bank */
#define OSD_GCABA_TXT_SHIFT	16		/* bit shift of active text
						   bank */
#define OSD_GCABA_COL_MASK	0x0000ff00	/* set the active color table
						   bank */
#define OSD_GCABA_COL_SHIFT	8		/* bit shift of active color
						   table bank */
#define OSD_GCABA_INS_MASK	0x000000ff	/* set the active instruction
						   bank */

/*
 * version register bit definition
 */
#define OSD_VER_MAJOR_MASK	0xff000000	/* major version */
#define OSD_VER_MAJOR_SHIFT	24		/* major version bit shift*/
#define OSD_VER_MINOR_MASK	0x00ff0000	/* minor version */
#define OSD_VER_MINOR_SHIFT	16		/* minor version bit shift*/
#define OSD_VER_REV_MASK	0x0000f000	/* revision version */
#define OSD_VER_REV_SHIFT	12		/* revision bit shift*/

/*
 * osd software reset
 */
#define OSD_RST_RESET	0x80000000	/* software reset */
#define OSD_SYNC_RESET	0x40000000	/* frame sync reset */

/*
 * global interrupt enable register bit definition
 */
#define OSD_GIER_GIE_MASK	0x80000000	/* global interrupt enable */

/*
 * interrupt status/enable register bit definition
 */
#define OSD_IXR_GAO_MASK	0xff000000	/* GC instruction overflow */
#define OSD_IXR_GIE_MASK	0x00ff0000	/* GC instruction error */
#define OSD_IXR_OOE_MASK	0x00000010	/* OSD output overflow error */
#define OSD_IXR_IUE_MASK	0x00ff0000	/* OSD input underflow error */
#define OSD_IXR_VBIE_MASK	0x00000004	/* vert blank interval end */
#define OSD_IXR_VBIS_MASK	0x00000002	/* vert blank interval start*/
#define OSD_IXR_FE_MASK		0x00000008	/* OSD did not complete
						   processing frame before next
						   vblank */
#define OSD_IXR_FD_MASK	0x00000001	/* OSD completed processing frame */
#define OSD_IXR_ALLIERR_MASK (OSD_IXR_GAO_MASK | \
				OSD_IXR_GIE_MASK | \
				OSD_IXR_OOE_MASK | \
				OSD_IXR_IUE_MASK | \
				OSD_IXR_FE_MASK)

#define OSD_IXR_ALLINTR_MASK  (OSD_IXR_VBIE_MASK | \
				OSD_IXR_VBIS_MASK | \
				OSD_IXR_FD_MASK | \
				OSD_IXR_ALLIERR_MASK)

/*
 * layer types
 */
#define OSD_LAYER_TYPE_DISABLE	0	/* layer is disabled */
#define OSD_LAYER_TYPE_GPU	1	/* layer's type is GPU  */
#define OSD_LAYER_TYPE_VFBC	2	/* layer's type is VFBC */

/*
 * supported instruction numbers given an instruction memory size
 */
#define OSD_INS_MEM_SIZE_TO_NUM	1	/* conversion to the number of							   instructions from the instruction
					   memory size */

/*
 * GC instruction word offset definition
 */
#define OSD_INS0		0	/* instruction word 0 offset */
#define OSD_INS1		1	/* instruction word 1 offset */
#define OSD_INS2		2	/* instruction word 2 offset */
#define OSD_INS3		3	/* instruction word 3 offset */
#define OSD_INS_SIZE		4	/* size of an instruction in words */

/*
 * GC instruction word 0 definition
 */
#define OSD_INS0_OPCODE_MASK	0xf0000000	/* operation code (OpCode) */
#define OSD_INS0_OPCODE_SHIFT	28		/* bit shift of OpCode */
#define OSD_INS0_GCNUM_MASK	0x07000000	/* GC number */
#define OSD_INS0_GCNUM_SHIFT	24		/* bit shift of GC# */
#define OSD_INS0_XEND_MASK	0x00fff000	/* horizontal end pixel of
						   the object */
#define OSD_INS0_XEND_SHIFT	12		/* bit shift ofhorizontal end
						   pixel of the object */
#define OSD_INS0_XSTART_MASK	0x00000fff	/* horizontal start pixel of the
						   object */

/*
 * GC instruction word 1 definition
 */
#define OSD_INS1_TXTINDEX_MASK	0x0000000f	/* string index */

/*
 * GC instruction word 2 definition
 */
#define OSD_INS2_OBJSIZE_MASK	0xff000000	/* object size */
#define OSD_INS2_OBJSIZE_SHIFT	24		/* bit shift of object size */
#define OSD_INS2_YEND_MASK	0x00fff000	/* vertical end line of
						   the object */
#define OSD_INS2_YEND_SHIFT	12		/* bit shift of vertical end
						   line of the object */
#define OSD_INS2_YSTART_MASK	0x00000fff	/* vertical start line of the
						   object */

/*
 * GC instruction word 3 definition
 */
#define OSD_INS3_COL_MASK	0x0000000f	/* color index for box/text */

/*
 * GC instruction operation code definition (used in instruction word 0)
 */
#define OSD_INS_OPCODE_END	0x0	/* end of instruction list */
#define OSD_INS_OPCODE_NOP	0x8	/* nop */
#define OSD_INS_OPCODE_BOX	0xa	/* box */
#define OSD_INS_OPCODE_LINE	0xc	/* line */
#define OSD_INS_OPCODE_TXT	0xe	/* text */
#define OSD_INS_OPCODE_BOXTXT	0xf	/* box text */

/*
 * GC color size
 */
#define OSD_COLOR_ENTRY_SIZE	4	/* size of each color entry in bytes */

/*
 * GC font unit size
 */
#define OSD_FONT_BIT_TO_BYTE	8	/* ratio to convert font size to byte */

/*
 * layer priority
 */
#define OSD_LAYER_PRIORITY_0	0	/* priority 0 --- lowest */
#define OSD_LAYER_PRIORITY_1	1	/* priority 1 */
#define OSD_LAYER_PRIORITY_2	2	/* priority 2 */
#define OSD_LAYER_PRIORITY_3	3	/* priority 3 */
#define OSD_LAYER_PRIORITY_4	4	/* priority 4 */
#define OSD_LAYER_PRIORITY_5	5	/* priority 5 */
#define OSD_LAYER_PRIORITY_6	6	/* priority 6 */
#define OSD_LAYER_PRIORITY_7	7	/* priority 7 --- highest */

struct zynq_osd_layer {
	void __iomem *base;		/* layer base addr */
	int id;				/* layer id */
	struct zynq_osd *osd;		/* osd */
};

struct zynq_osd {
	void __iomem *base;		/* osd base addr */
	struct device_node *node;	/* device node */
	struct zynq_osd_layer *layers[OSD_MAX_NUM_OF_LAYERS];	/* layers */
	int num_layers;			/* num of layers */
	u32 width, height;		/* width / height */
};

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


static inline void zynq_osd_enable_rue(struct zynq_osd *osd);
static inline void zynq_osd_disable_rue(struct zynq_osd *osd);

/* set layer alpha */
void zynq_osd_layer_set_alpha(struct zynq_osd_layer *layer, u32 enable,
		u32 alpha)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "alpha: 0x%08x\n", alpha);
	zynq_osd_disable_rue(layer->osd);
	value = zynq_osd_layer_readl(layer, OSD_LXC);
	value = enable ? (value | OSD_LXC_GALPHAEN_MASK) :
		(value & ~OSD_LXC_GALPHAEN_MASK);
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
	value |= OSD_LXC_EN_MASK;
	zynq_osd_layer_writel(layer, OSD_LXC, value);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* disable layer */
void zynq_osd_layer_disable(struct zynq_osd_layer *layer)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "layer->id: %d\n", layer->id);
	value = zynq_osd_layer_readl(layer, OSD_LXC);
	value &= ~OSD_LXC_EN_MASK;
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
		if (!osd->layers[i]) {
			break;
		}
	}
	if (i >= osd->num_layers) {
		pr_err("no available osd layer\n");
		goto err_out;
	}

	layer = kzalloc(sizeof(*layer), GFP_KERNEL);
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
	kfree(layer);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	return;
}

/* set osd color*/
void zynq_osd_set_color(struct zynq_osd *osd, u8 r, u8 g, u8 b)
{
	u32 value;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_disable_rue(osd);
	value = g & OSD_BC0_YG_MASK;
	zynq_osd_writel(osd, OSD_BC0, value);
	value = b & OSD_BC1_UCBB_MASK;
	zynq_osd_writel(osd, OSD_BC1, value);
	value = r & OSD_BC2_VCRR_MASK;
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
	value = width;
	value |= (height << OSD_SS_YSIZE_SHIFT) & OSD_SS_YSIZE_MASK;
	zynq_osd_writel(osd, OSD_SS, value);
	zynq_osd_enable_rue(osd);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* reset osd */
void zynq_osd_reset(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_RST, OSD_RST_RESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* frame synced reset osd */
void zynq_osd_fsync_reset(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_RST, OSD_SYNC_RESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* enable osd */
void zynq_osd_enable(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) |
			OSD_CTL_EN_MASK);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* disable osd */
void zynq_osd_disable(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) &
			~OSD_CTL_EN_MASK);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* register-update-enable osd */
void zynq_osd_enable_rue(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) |
			OSD_CTL_RUE_MASK);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

/* register-update-enable osd */
static inline void zynq_osd_disable_rue(struct zynq_osd *osd)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
	zynq_osd_writel(osd, OSD_CTL, zynq_osd_readl(osd, OSD_CTL) &
			~OSD_CTL_RUE_MASK);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}

struct zynq_osd *zynq_osd_probe(char *compatible)
{
	struct zynq_osd *osd;
	u32 prop;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");

	osd = kzalloc(sizeof(*osd), GFP_KERNEL);
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

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");

	return osd;

err_prop:
	iounmap(osd->base);
err_iomap:
	of_node_put(osd->node);
err_node:
	kfree(osd);
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
	kfree(osd);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_OSD, "\n");
}
