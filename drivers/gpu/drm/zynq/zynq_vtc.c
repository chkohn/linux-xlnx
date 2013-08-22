/*
 * Video Timing Controller support for Zynq DRM KMS
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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include "zynq_drm_drv.h"

#include "zynq_vtc.h"

/* register offsets */
#define VTC_CTL		0x000	/* control */
#define VTC_STATS	0x004	/* status */
#define VTC_ERROR	0x008	/* error */

#define VTC_GASIZE	0x060	/* generator active size */
#define VTC_GPOL	0x06c	/* generator polarity */
#define VTC_GHSIZE	0x070	/* generator frame horizontal size */
#define VTC_GVSIZE	0x074	/* generator frame vertical size */
#define VTC_GHSYNC	0x078	/* generator horizontal sync */
#define VTC_GVBHOFF	0x07c	/* generator vblank horizontal offset */
#define VTC_GVSYNC	0x080	/* generator vertical sync */
#define VTC_GVSHOFF	0x084	/* generator vsync horizontal offset */

#define VTC_RESET	0x000	/* reset register */
#define VTC_ISR		0x004	/* interrupt status register */
#define VTC_IER		0x00c	/* interrupt enable register */

/* control register bit */
#define VTC_CTL_FIP	(1 << 6)	/* field id output polarity */
#define VTC_CTL_ACP	(1 << 5)	/* active chroma output polarity */
#define VTC_CTL_AVP	(1 << 4)	/* active video output polarity */
#define VTC_CTL_HSP	(1 << 3)	/* hori sync output polarity */
#define VTC_CTL_VSP	(1 << 2)	/* vert sync output polarity */
#define VTC_CTL_HBP	(1 << 1)	/* hori blank output polarity */
#define VTC_CTL_VBP	(1 << 0)	/* vert blank output polarity */

#define VTC_CTL_FIPSS	(1 << 26)	/* field id output polarity source */
#define VTC_CTL_ACPSS	(1 << 25)	/* active chroma out polarity source */
#define VTC_CTL_AVPSS	(1 << 24)	/* active video out polarity source */
#define VTC_CTL_HSPSS	(1 << 23)	/* hori sync out polarity source */
#define VTC_CTL_VSPSS	(1 << 22)	/* vert sync out polarity source */
#define VTC_CTL_HBPSS	(1 << 21)	/* hori blank out polarity source */
#define VTC_CTL_VBPSS	(1 << 20)	/* vert blank out polarity source */

#define VTC_CTL_VCSS	(1 << 18)	/* start of active chroma register
					   source select */
#define VTC_CTL_VASS	(1 << 17)	/* vertical active video start register
					   source select */
#define VTC_CTL_VBSS	(1 << 16)	/* vertical back porch start register
					   source select (sync end) */
#define VTC_CTL_VSSS	(1 << 15)	/* vertical sync start register source
					   select */
#define VTC_CTL_VFSS	(1 << 14)	/* vertical front porch start register
					   source select (active size) */
#define VTC_CTL_VTSS	(1 << 13)	/* vertical total register
					   source select (frame size) */

#define VTC_CTL_HBSS	(1 << 11)	/* horizontal back porch start register
					   source select (sync end) */
#define VTC_CTL_HSSS	(1 << 10)	/* horizontal sync start register
					   source select */
#define VTC_CTL_HFSS	(1 << 9)	/* horizontal front porch start register
					   source select (active size) */
#define VTC_CTL_HTSS	(1 << 8)	/* horizontal total register source
					   select (frame size) */

#define VTC_CTL_GE	(1 << 2)	/* vtc generator enable */
#define VTC_CTL_RU	(1 << 1)	/* vtc register update */

/* vtc generator horizontal 1 */
#define VTC_GH1_BPSTART_MASK   0x1fff0000	/* horizontal back porch start
						   cycle count */
#define VTC_GH1_BPSTART_SHIFT  16		/* bit shift for horizontal back
						   porch start cycle count */
#define VTC_GH1_SYNCSTART_MASK 0x00001fff	/* horizontal sync start cycle
						   count */

/* vtc generator vertical 1 (filed 0) */
#define VTC_GV1_BPSTART_MASK   0x1fff0000	/* vertical back porch start
						   cycle count */
#define VTC_GV1_BPSTART_SHIFT  16		/* bit shift for vertical back
						   porch start cycle count */
#define VTC_GV1_SYNCSTART_MASK 0x00001fff	/* vertical sync start cycle
						   count */

/* vtc generator/detector vblank/vsync horizontal offset registers */
#define VTC_XVXHOX_HEND_MASK	0x1fff0000	/* horizontal offset end */
#define VTC_XVXHOX_HEND_SHIFT	16		/* horizontal offset end
						   shift */
#define VTC_XVXHOX_HSTART_MASK	0x00001fff	/* horizontal offset start */

/* reset register bit definition */
#define VTC_RESET_RESET		(1 << 31)	/* Software Reset */

/* interrupt status/enable register bit definition */
#define VTC_IXR_FSYNC15		(1 << 31)	/* frame sync interrupt 15 */
#define VTC_IXR_FSYNC14		(1 << 30)	/* frame sync interrupt 14 */
#define VTC_IXR_FSYNC13		(1 << 29)	/* frame sync interrupt 13 */
#define VTC_IXR_FSYNC12		(1 << 28)	/* frame sync interrupt 12 */
#define VTC_IXR_FSYNC11		(1 << 27)	/* frame sync interrupt 11 */
#define VTC_IXR_FSYNC10		(1 << 26)	/* frame sync interrupt 10 */
#define VTC_IXR_FSYNC09		(1 << 25)	/* frame sync interrupt 09 */
#define VTC_IXR_FSYNC08		(1 << 24)	/* frame sync interrupt 08 */
#define VTC_IXR_FSYNC07		(1 << 23)	/* frame sync interrupt 07 */
#define VTC_IXR_FSYNC06		(1 << 22)	/* frame sync interrupt 06 */
#define VTC_IXR_FSYNC05		(1 << 21)	/* frame sync interrupt 05 */
#define VTC_IXR_FSYNC04		(1 << 20)	/* frame sync interrupt 04 */
#define VTC_IXR_FSYNC03		(1 << 19)	/* frame sync interrupt 03 */
#define VTC_IXR_FSYNC02		(1 << 18)	/* frame sync interrupt 02 */
#define VTC_IXR_FSYNC01		(1 << 17)	/* frame sync interrupt 01 */
#define VTC_IXR_FSYNC00		(1 << 16)	/* frame sync interrupt 00 */
#define VTC_IXR_FSYNCALL_MASK	(VTC_IXR_FSYNC00 |	\
		VTC_IXR_FSYNC01 | \
		VTC_IXR_FSYNC02 | \
		VTC_IXR_FSYNC03 | \
		VTC_IXR_FSYNC04 | \
		VTC_IXR_FSYNC05 | \
		VTC_IXR_FSYNC06 | \
		VTC_IXR_FSYNC07 | \
		VTC_IXR_FSYNC08 | \
		VTC_IXR_FSYNC09 | \
		VTC_IXR_FSYNC10 | \
		VTC_IXR_FSYNC11 | \
		VTC_IXR_FSYNC12 | \
		VTC_IXR_FSYNC13 | \
		VTC_IXR_FSYNC14 | \
		VTC_IXR_FSYNC15)	/* all frame sync intr */

#define VTC_IXR_G_AV		(1 << 13)	/* generator actv video intr */
#define VTC_IXR_G_VBLANK	(1 << 12)	/* generator vblank interrupt */
#define VTC_IXR_G_ALL_MASK	(VTC_IXR_G_AV | \
				VTC_IXR_G_VBLANK)	/* all generator intr */

#define VTC_IXR_D_AV		(1 << 11)	/* detector active video intr */
#define VTC_IXR_D_VBLANK	(1 << 10)	/* detector vblank interrupt */
#define VTC_IXR_D_ALL_MASK	(VTC_IXR_D_AV | \
				VTC_IXR_D_VBLANK)	/* all detector intr */

#define VTC_IXR_LOL		(1 << 9)	/* lock loss */
#define VTC_IXR_LO		(1 << 8)	/* lock  */
#define VTC_IXR_LOCKALL_MASK	(VTC_IXR_LOL | \
				VTC_IXR_LO)	/* all signal lock intr */

#define VTC_IXR_ACL	(1 << 21)	/* active chroma signal lock */
#define VTC_IXR_AVL	(1 << 20)	/* active video signal lock */
#define VTC_IXR_HSL	(1 << 19)	/* horizontal sync signal
						   lock */
#define VTC_IXR_VSL	(1 << 18)	/* vertical sync signal lock */
#define VTC_IXR_HBL	(1 << 17)	/* horizontal blank signal
						   lock */
#define VTC_IXR_VBL	(1 << 16)	/* vertical blank signal lock */

#define VTC_IXR_ALLINTR_MASK	(VTC_IXR_FSYNCALL_MASK | \
		VTC_IXR_G_ALL_MASK | \
		VTC_IXR_D_ALL_MASK | \
		VTC_IXR_LOCKALL_MASK)	/* mask for all interrupts */

struct zynq_vtc {
	void __iomem *base;		/* vtc base addr */
	int irq;			/* irq */
	void (*vblank_fn)(void *);	/* vblank handler func */
	void *vblank_data;		/* vblank handler private data */
};

struct zynq_vtc_polarity {
	u8 active_chroma;
	u8 active_video;
	u8 field_id;
	u8 vblank;
	u8 vsync;
	u8 hblank;
	u8 hsync;
};

struct zynq_vtc_hori_offset {
	u16 vblank_hori_start;
	u16 vblank_hori_end;
	u16 vsync_hori_start;
	u16 vsync_hori_end;
};

struct zynq_vtc_src_config {
	u8 field_id_pol;
	u8 active_chroma_pol;
	u8 active_video_pol;
	u8 hsync_pol;
	u8 vsync_pol;
	u8 hblank_pol;
	u8 vblank_pol;

	u8 vchroma;
	u8 vactive;
	u8 vbackporch;
	u8 vsync;
	u8 vfrontporch;
	u8 vtotal;

	u8 hactive;
	u8 hbackporch;
	u8 hsync;
	u8 hfrontporch;
	u8 htotal;
};

/* io write operations */
static inline void zynq_vtc_writel(struct zynq_vtc *vtc, int offset, u32 val)
{
	writel(val, vtc->base + offset);
}

/* io read operations */
static inline u32 zynq_vtc_readl(struct zynq_vtc *vtc, int offset)
{
	return readl(vtc->base + offset);
}

/* configure polarity of signals */
static void zynq_vtc_config_polarity(struct zynq_vtc *vtc,
		struct zynq_vtc_polarity *polarity)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	reg = zynq_vtc_readl(vtc, VTC_GPOL);

	if (polarity->active_chroma)
		reg |= VTC_CTL_ACP;
	if (polarity->active_video)
		reg |= VTC_CTL_AVP;
	if (polarity->field_id)
		reg |= VTC_CTL_FIP;
	if (polarity->vblank)
		reg |= VTC_CTL_VBP;
	if (polarity->vsync)
		reg |= VTC_CTL_VSP;
	if (polarity->hblank)
		reg |= VTC_CTL_HBP;
	if (polarity->hsync)
		reg |= VTC_CTL_HSP;

	zynq_vtc_writel(vtc, VTC_GPOL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* configure horizontal offset */
static void zynq_vtc_config_hori_offset(struct zynq_vtc *vtc,
		struct zynq_vtc_hori_offset *hori_offset)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	reg = (hori_offset->vblank_hori_start) & VTC_XVXHOX_HSTART_MASK;
	reg |= (hori_offset->vblank_hori_end << VTC_XVXHOX_HEND_SHIFT) &
		VTC_XVXHOX_HEND_MASK;
	zynq_vtc_writel(vtc, VTC_GVBHOFF, reg);

	reg = (hori_offset->vsync_hori_start) & VTC_XVXHOX_HSTART_MASK;
	reg |= (hori_offset->vsync_hori_end << VTC_XVXHOX_HEND_SHIFT) &
		VTC_XVXHOX_HEND_MASK;
	zynq_vtc_writel(vtc, VTC_GVSHOFF, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* configure source */
static void zynq_vtc_config_src(struct zynq_vtc *vtc,
		struct zynq_vtc_src_config *src_config)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	reg = zynq_vtc_readl(vtc, VTC_CTL);

	if (src_config->field_id_pol)
		reg |= VTC_CTL_FIPSS;
	if (src_config->active_chroma_pol)
		reg |= VTC_CTL_ACPSS;
	if (src_config->active_video_pol)
		reg |= VTC_CTL_AVPSS;
	if (src_config->hsync_pol)
		reg |= VTC_CTL_HSPSS;
	if (src_config->vsync_pol)
		reg |= VTC_CTL_VSPSS;
	if (src_config->hblank_pol)
		reg |= VTC_CTL_HBPSS;
	if (src_config->vblank_pol)
		reg |= VTC_CTL_VBPSS;

	if (src_config->vchroma)
		reg |= VTC_CTL_VCSS;
	if (src_config->vactive)
		reg |= VTC_CTL_VASS;
	if (src_config->vbackporch)
		reg |= VTC_CTL_VBSS;
	if (src_config->vsync)
		reg |= VTC_CTL_VSSS;
	if (src_config->vfrontporch)
		reg |= VTC_CTL_VFSS;
	if (src_config->vtotal)
		reg |= VTC_CTL_VTSS;

	if (src_config->hbackporch)
		reg |= VTC_CTL_HBSS;
	if (src_config->hsync)
		reg |= VTC_CTL_HSSS;
	if (src_config->hfrontporch)
		reg |= VTC_CTL_HFSS;
	if (src_config->htotal)
		reg |= VTC_CTL_HTSS;

	zynq_vtc_writel(vtc, VTC_CTL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* enable vtc */
void zynq_vtc_enable(struct zynq_vtc *vtc)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	/* enable a generator only for now */
	reg = zynq_vtc_readl(vtc, VTC_CTL);
	reg |= VTC_CTL_GE;
	zynq_vtc_writel(vtc, VTC_CTL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* disable vtc */
void zynq_vtc_disable(struct zynq_vtc *vtc)
{
	u32 reg;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	/* disable a generator only for now */
	reg = zynq_vtc_readl(vtc, VTC_CTL);
	reg &= ~VTC_CTL_GE;
	zynq_vtc_writel(vtc, VTC_CTL, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* configure vtc signals */
void zynq_vtc_config_sig(struct zynq_vtc *vtc,
		struct zynq_vtc_sig_config *sig_config)
{
	u32 reg;
	u32 htotal, vtotal, hactive, vactive;
	struct zynq_vtc_hori_offset hori_offset;
	struct zynq_vtc_polarity polarity;
	struct zynq_vtc_src_config src;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	reg = zynq_vtc_readl(vtc, VTC_CTL);
	zynq_vtc_writel(vtc, VTC_CTL, reg & ~VTC_CTL_RU);

	htotal = sig_config->htotal;
	vtotal = sig_config->vtotal;

	hactive = sig_config->hfrontporch_start;
	vactive = sig_config->vfrontporch_start;

	reg = htotal & 0x1fff;
	zynq_vtc_writel(vtc, VTC_GHSIZE, reg);

	reg = vtotal & 0x1fff;
	zynq_vtc_writel(vtc, VTC_GVSIZE, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "ht: %d, vt: %d\n", htotal, vtotal);

	reg = hactive & 0x1fff;
	reg |= (vactive & 0x1fff) << 16;
	zynq_vtc_writel(vtc, VTC_GASIZE, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "ha: %d, va: %d\n", hactive, vactive);

	reg = (sig_config->hsync_start) & VTC_GH1_SYNCSTART_MASK;
	reg |= (sig_config->hbackporch_start) << VTC_GH1_BPSTART_SHIFT &
		VTC_GH1_BPSTART_MASK;
	zynq_vtc_writel(vtc, VTC_GHSYNC, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "hs: %d, hb: %d\n",
			sig_config->hsync_start, sig_config->hbackporch_start);

	reg = (sig_config->vsync_start) & VTC_GV1_SYNCSTART_MASK;
	reg |= (sig_config->vbackporch_start) << VTC_GV1_BPSTART_SHIFT &
		VTC_GV1_BPSTART_MASK;
	zynq_vtc_writel(vtc, VTC_GVSYNC, reg);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "vs: %d, vb: %d\n",
			sig_config->vsync_start, sig_config->vbackporch_start);

	hori_offset.vblank_hori_start = hactive;
	hori_offset.vblank_hori_end = hactive;
	hori_offset.vsync_hori_start = hactive;
	hori_offset.vsync_hori_end = hactive;

	zynq_vtc_config_hori_offset(vtc, &hori_offset);

	/* set up polarity */
	memset(&polarity, 0x0, sizeof(polarity));
	polarity.hsync = 1;
	polarity.vsync = 1;
	polarity.hblank = 1;
	polarity.vblank = 1;
	polarity.active_video = 1;
	polarity.active_chroma = 1;
	polarity.field_id = 1;
	zynq_vtc_config_polarity(vtc, &polarity);

	/* set up src config */
	memset(&src, 0x0, sizeof(src));
	src.vchroma = 1;
	src.vactive = 1;
	src.vbackporch = 1;
	src.vsync = 1;
	src.vfrontporch = 1;
	src.vtotal = 1;
	src.hactive = 1;
	src.hbackporch = 1;
	src.hsync = 1;
	src.hfrontporch = 1;
	src.htotal = 1;
	zynq_vtc_config_src(vtc, &src);

	reg = zynq_vtc_readl(vtc, VTC_CTL);
	zynq_vtc_writel(vtc, VTC_CTL, reg | VTC_CTL_RU);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* reset vtc */
void zynq_vtc_reset(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	zynq_vtc_writel(vtc, VTC_RESET, VTC_RESET_RESET);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* enable interrupt */
static inline void zynq_vtc_intr_enable(struct zynq_vtc *vtc, u32 intr)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	zynq_vtc_writel(vtc, VTC_IER, (intr & VTC_IXR_ALLINTR_MASK) |
			zynq_vtc_readl(vtc, VTC_IER));
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* disable interrupt */
static inline void zynq_vtc_intr_disable(struct zynq_vtc *vtc, u32 intr)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	zynq_vtc_writel(vtc, VTC_IER, ~(intr & VTC_IXR_ALLINTR_MASK) &
			zynq_vtc_readl(vtc, VTC_IER));
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* get interrupt */
static inline u32 zynq_vtc_intr_get(struct zynq_vtc *vtc)
{
	return zynq_vtc_readl(vtc, VTC_IER) & zynq_vtc_readl(vtc, VTC_ISR) &
		VTC_IXR_ALLINTR_MASK;
}

/* clear interrupt */
static inline void zynq_vtc_intr_clear(struct zynq_vtc *vtc, u32 intr)
{
	zynq_vtc_writel(vtc, VTC_ISR, intr & VTC_IXR_ALLINTR_MASK);
}

/* interrupt handler */
static irqreturn_t zynq_vtc_intr_handler(int irq, void *data)
{
	struct zynq_vtc *vtc = data;
	u32 intr = zynq_vtc_intr_get(vtc);

	if ((intr & VTC_IXR_G_VBLANK) && (vtc->vblank_fn))
		vtc->vblank_fn(vtc->vblank_data);

	zynq_vtc_intr_clear(vtc, intr);

	return IRQ_HANDLED;
}

/* enable vblank interrupt */
void zynq_vtc_enable_vblank_intr(struct zynq_vtc *vtc,
		void (*vblank_fn)(void *), void *vblank_priv)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	vtc->vblank_fn = vblank_fn;
	vtc->vblank_data = vblank_priv;
	zynq_vtc_intr_enable(vtc, VTC_IXR_G_VBLANK);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* disable vblank interrupt */
void zynq_vtc_disable_vblank_intr(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	zynq_vtc_intr_disable(vtc, VTC_IXR_G_VBLANK);
	vtc->vblank_data = NULL;
	vtc->vblank_fn = NULL;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* probe vtc */
struct zynq_vtc *zynq_vtc_probe(struct device *dev, struct device_node *node)
{
	struct zynq_vtc *vtc;
	struct zynq_vtc *err_ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	vtc = devm_kzalloc(dev, sizeof(*vtc), GFP_KERNEL);
	if (!vtc) {
		pr_err("failed to alloc vtc\n");
		err_ret = ERR_PTR(-ENOMEM);
		goto err_vtc;
	}

	vtc->base = of_iomap(node, 0);
	if (!vtc->base) {
		pr_err("failed to iomap vtc\n");
		err_ret = ERR_PTR(-ENXIO);
		goto err_iomap;
	}

	zynq_vtc_intr_disable(vtc, VTC_IXR_ALLINTR_MASK);
	vtc->irq = irq_of_parse_and_map(node, 0);
	if (vtc->irq > 0) {
		if (devm_request_irq(dev, vtc->irq, zynq_vtc_intr_handler,
					IRQF_SHARED, "zynq_vtc", vtc)) {
			vtc->irq = 0;
			pr_warn("failed to requet_irq() for zynq_vtc\n");
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	return vtc;

err_iomap:
err_vtc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	return err_ret;
}

/* remove vtc */
void zynq_vtc_remove(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	zynq_vtc_reset(vtc);

	iounmap(vtc->base);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}
