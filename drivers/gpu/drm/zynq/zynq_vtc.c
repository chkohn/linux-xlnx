/*
 * Video Timing Controller support for Zynq DRM KMS
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
#define VTC_GTSTAT	0x064	/* generator timing status */
#define VTC_GFENC	0x068	/* generator encoding */
#define VTC_GPOL	0x06c	/* generator polarity */
#define VTC_GHSIZE	0x070	/* generator frame horizontal size */
#define VTC_GVSIZE	0x074	/* generator frame vertical size */
#define VTC_GHSYNC	0x078	/* generator horizontal sync */
#define VTC_GVBHOFF	0x07c	/* generator vblank horizontal offset */
#define VTC_GVSYNC	0x080	/* generator vertical sync */
#define VTC_GVSHOFF	0x084	/* generator vsync horizontal offset */

#define VTC_DVBHO0	0x0b0	/* detector vblank hori offset 0 register */
#define VTC_DVSHO0	0x0b4	/* detector vsync  hori offset 0 register */
#define VTC_DVBHO1	0x0b8	/* detector vblank hori offset 1 register */
#define VTC_DVSHO1	0x0bc	/* detector vsync  hori offset 1 register */

#define VTC_VER		0x010	/* version register */
#define VTC_RESET	0x000	/* reset register */
#define VTC_ISR		0x004	/* interrupt status register */
#define VTC_IER		0x00c	/* interrupt enable register */

/* control register bit */
#define VTC_CTL_FIP_MASK	0x00000040	/* field id output polarity */
#define VTC_CTL_ACP_MASK	0x00000020	/* active chroma output
						   polarity */
#define VTC_CTL_AVP_MASK	0x00000010	/* active video output
						   polarity */
#define VTC_CTL_HSP_MASK	0x00000008	/* horizontal sync output
						   polarity */
#define VTC_CTL_VSP_MASK	0x00000004	/* vertical sync output
						   polarity */
#define VTC_CTL_HBP_MASK	0x00000002	/* horizontal blank output
						   polarity */
#define VTC_CTL_VBP_MASK	0x00000001	/* vertical blank output
						   polarity */
#define VTC_CTL_ALLP_MASK	0x0000007f	/* bit mask for all polarity
						   bits */


#define VTC_CTL_FIPSS_MASK	0x04000000	/* field id output polarity
						   source */
#define VTC_CTL_ACPSS_MASK	0x02000000	/* active chroma output
						   polarity source */
#define VTC_CTL_AVPSS_MASK	0x01000000	/* active video output
						   polarity source */
#define VTC_CTL_HSPSS_MASK	0x00800000	/* horizontal sync output
						   polarity source */
#define VTC_CTL_VSPSS_MASK	0x00400000	/* vertical sync output
						   polarity source */
#define VTC_CTL_HBPSS_MASK	0x00200000	/* horizontal blank output
						   polarity source */
#define VTC_CTL_VBPSS_MASK	0x00100000	/* vertical blank output
						   polarity source */



#define VTC_CTL_VCSS_MASK	0x00040000	/* start of active chroma
						   register source select */
#define VTC_CTL_VASS_MASK	0x00020000	/* vertical active video start
						   register source select */
#define VTC_CTL_VBSS_MASK	0x00010000	/* vertical back porch start
						   register source select
						   (sync end) */
#define VTC_CTL_VSSS_MASK	0x00008000	/* vertical sync start register
						   source select */
#define VTC_CTL_VFSS_MASK	0x00004000	/* vertical front porch start
						   register source select
						   (active size) */
#define VTC_CTL_VTSS_MASK	0x00002000	/* vertical total register
						   source select (frame size) */


#define VTC_CTL_HBSS_MASK	0x00000800	/* horizontal back porch start
						   register source select
						   (sync end) */
#define VTC_CTL_HSSS_MASK	0x00000400	/* horizontal sync start
						   register source select */
#define VTC_CTL_HFSS_MASK	0x00000200	/* horizontal front porch start
						   register source select
						   (active size) */
#define VTC_CTL_HTSS_MASK	0x00000100	/* horizontal total register
						   source select (frame size) */


#define VTC_CTL_ALLSS_MASK	0x03f7ef00	/* bit mask for all source
						   select */
#define VTC_CTL_GACPS_MASK	0x00000200	/* generator active chroma
						   pixel skip */
#define VTC_CTL_GACLS_MASK	0x00000001	/* generator active chroma
						   line skip */
#define VTC_CTL_GE_MASK	0x00000004	/* vtc generator enable */
#define VTC_CTL_RU_MASK	0x00000002	/* vtc register update */
#define VTC_CTL_SW_MASK	0x00000001	/* vtc core enable */

/* vtc generator horizontal 0 */
#define VTC_GH0_FPSTART_MASK	0x1fff0000	/* horizontal front porch start
						   cycle count */
#define VTC_GH0_FPSTART_SHIFT	16		/* bit shift for horizontal
						   front porch start cycle
						   count */
#define VTC_GH0_TOTAL_MASK	0x00001fff	/* total clock cycles per
						   line */

/* vtc generator horizontal 1 */
#define VTC_GH1_BPSTART_MASK   0x1fff0000	/* horizontal back porch start
						   cycle count */
#define VTC_GH1_BPSTART_SHIFT  16		/* bit shift for horizontal back
						   porch start cycle count */
#define VTC_GH1_SYNCSTART_MASK 0x00001fff	/* horizontal sync start cycle
						   count */

/* vtc generator horizontal 2 */
#define VTC_GH2_ACTIVESTART_MASK	0x00001fff	/* Horizontal Active
							   Video Start Cycle
							   Count */

/* vtc generator vertical 0 (filed 0)*/
#define VTC_GV0_FPSTART_MASK   0x1fff0000	/* vertical front porch start
						   cycle count */
#define VTC_GV0_FPSTART_SHIFT  16		/* bit shift for vertical front
						   porch start cycle count */
#define VTC_GV0_TOTAL_MASK	0x00001fff	/* total lines per frame */

/* vtc generator vertical 1 (filed 0) */
#define VTC_GV1_BPSTART_MASK   0x1fff0000	/* vertical back porch start
						   cycle count */
#define VTC_GV1_BPSTART_SHIFT  16		/* bit shift for vertical back
						   porch start cycle count */
#define VTC_GV1_SYNCSTART_MASK 0x00001fff	/* vertical sync start cycle
						   count */

/* vtc generator vertical 2 (filed 0) */
#define VTC_GV2_CHROMASTART_MASK	0x00000100	/* active chroma start
							   line count */
#define VTC_GV2_CHROMASTART_SHIFT	8		/* bit shift for active
							   chroma start line
							   count */
#define VTC_GV2_ACTIVESTART_MASK	0x00001fff	/* vertical active
							   video start
							   cycle count */

/* vtc generator vertical 3 (filed 1) */
#define VTC_GV3_FPSTART_MASK	0x1fff0000	/* vertical front porch start
						   cycle count */
#define VTC_GV3_FPSTART_SHIFT	16		/* bit shift for vertical front
						   porch start cycle count */
#define VTC_GV3_TOTAL_MASK	0x00001fff	/* total lines per frame */

/* vtc generator vertical 4 (filed 1) */
#define VTC_GV4_BPSTART_MASK	0x1fff0000	/* vertical back porch start
						   cycle count */
#define VTC_GV4_BPSTART_SHIFT	16		/* bit shift for vertical back
						   porch start cycle count */
#define VTC_GV4_SYNCSTART_MASK	0x00001fff	/* vertical sync start cycle
						   count */

/* vtc generator vertical 5 (filed 1) */
#define VTC_GV5_CHROMASTART_MASK	0x1fff0000	/* active chroma start
							   line count */
#define VTC_GV5_CHROMASTART_SHIFT	16		/* bit shift for active
							   chroma start line
							   count */
#define VTC_GV5_ACTIVESTART_MASK	0x00001fff	/* vertical active
							   video start
							   cycle count */

/* vtc detector status */
#define VTC_DS_AC_POL_MASK	0x04000000	/* active chroma output
						   polarity */
#define VTC_DS_AV_POL_MASK	0x02000000	/* active video output
						   polarity */
#define VTC_DS_FID_POL_MASK	0x01000000	/* field id output polarity */
#define VTC_DS_VBLANK_POL_MASK	0x00800000	/* vertical blank output
						   polarity */
#define VTC_DS_VSYNC_POL_MASK	0x00400000	/* vertical sync output
						   polarity */
#define VTC_DS_HBLANK_POL_MASK	0x00200000	/* horizontal blank output
						   polarity */
#define VTC_DS_HSYNC_POL_MASK	0x00100000	/* horizontal sync output
						   polarity */
#define VTC_DS_ACSKIP_MASK	0x00000010	/* detector active chroma
						   skip */

/* vtc detector horizontal 0 */
#define VTC_DH0_FPSTART_MASK	0x1fff0000	/* detected horizontal front
						   porch start cycle count */
#define VTC_DH0_FPSTART_SHIFT	16		/* bit shift for detected
						   horizontal front porch start
						   cycle count */
#define VTC_DH0_TOTAL_MASK	0x00001fff	/* detected total clock cycles
						   per line */

/* vtc detector horizontal 1 */
#define VTC_DH1_BPSTART_MASK	0x1fff0000	/* detected horizontal back
						   porch start cycle count */
#define VTC_DH1_BPSTART_SHIFT	16		/* bit shift for detected
						   horizontal back porch start
						   cycle count */
#define VTC_DH1_SYNCSTART_MASK	0x00001fff	/* detected horizontal sync
						   start cycle count */

/* vtc detector horizontal 0 */
#define VTC_DH2_ACTIVESTART_MASK	0x00001fff	/* detected horizontal
							   active video start
							   cycle count */

/* vtc detector vertical 0 (field 0) */
#define VTC_DV0_FPSTART_MASK	0x1fff0000	/* detected vertical front
						   porch start cycle count */
#define VTC_DV0_FPSTART_SHIFT	16		/* bit shift for detected
						   vertical front porch start
						   cycle count */
#define VTC_DV0_TOTAL_MASK	0x00001fff	/* detected total lines per
						   frame */

/* vtc detector vertical 1 (field 0) */
#define VTC_DV1_BPSTART_MASK	0x1fff0000	/* detected vertical back porch
						   start cycle count */
#define VTC_DV1_BPSTART_SHIFT	16		/* bit shift for detected
						   vertical back porch start
						   cycle count */
#define VTC_DV1_SYNCSTART_MASK	0x00001fff	/* detected vertical sync start
						   cycle count */

/* vtc detector vertical 2 (field 0) */
#define VTC_DV2_CHROMASTART_MASK	0x1fff0000	/* detected active
							   chroma start
							   line count */
#define VTC_DV2_CHROMASTART_SHIFT	16		/* bit shift for
							   detected active
							   chroma start line
							   count */
#define VTC_DV2_ACTIVESTART_MASK	0x00001fff	/* detected vertical
							   active video start
							   cycle count */

/* vtc detector vertical 3 (field 1) */
#define VTC_DV3_FPSTART_MASK	0x1fff0000	/* detected vertical front
						   porch start cycle count */
#define VTC_DV3_FPSTART_SHIFT	16		/* bit shift for detected
						   vertical front porch start
						   cycle count */
#define VTC_DV3_TOTAL_MASK	0x00001fff	/* detected total lines per
						   frame */

/* vtc detector vertical 4 (field 1) */
#define VTC_DV4_BPSTART_MASK	0x1fff0000	/* detected vertical back porch
						   start cycle count */
#define VTC_DV4_BPSTART_SHIFT	16		/* bit shift for detected
						   vertical back porch start
						   cycle count */
#define VTC_DV4_SYNCSTART_MASK	0x00001fff	/* detected vertical sync start
						   cycle count */

/* vtc detector vertical 5 (field 1) */
#define VTC_DV5_CHROMASTART_MASK	0x1fff0000	/* detected active
							   chroma start
							   line count */
#define VTC_DV5_CHROMASTART_SHIFT	16		/* bit shift for
							   detected active
							   chroma start line
							   count */
#define VTC_DV5_ACTIVESTART_MASK	0x00001fff	/* detected vertical
							   active video start
							   cycle count */

/* vtc frame sync 00 --- 15 */
#define VTC_FSXX_VSTART_MASK	0x1fff0000	/* vertical line count during
						   which current frame sync is
						   active */
#define VTC_FSXX_VSTART_SHIFT	16		/* bit shift for the vertical
						   line count */
#define VTC_FSXX_HSTART_MASK	0x00001fff	/* horizontal cycle count
						   during which current frame
						   sync is active */

/* vtc generator global delay */
#define VTC_GGD_VDELAY_MASK	0x1fff0000	/* total lines per frame to
						   delay generator output */
#define VTC_GGD_VDELAY_SHIFT	16		/* bit shift for the total
						   lines */
#define VTC_GGD_HDELAY_MASK	0x00001fff	/* total clock cycles per line
						   to delay generator output */

/* vtc generator/detector vblank/vsync horizontal offset registers */
#define VTC_XVXHOX_HEND_MASK	0x1fff0000	/* horizontal offset end */
#define VTC_XVXHOX_HEND_SHIFT	16		/* horizontal offset end
						   shift */
#define VTC_XVXHOX_HSTART_MASK	0x00001fff	/* horizontal offset start */

/* reset register bit definition */
#define VTC_RESET_RESET_MASK	0x80000000	/* Software Reset */
#define VTC_SYNC_RESET_MASK	0x40000000	/* Frame Sync'ed Software
						   Reset */

/* version register bit definition */
#define VTC_VER_MAJOR_MASK	0xff000000	/* major version*/
#define VTC_VER_MAJOR_SHIFT	24		/* major version bit shift*/
#define VTC_VER_MINOR_MASK	0x00ff0000	/* minor version */
#define VTC_VER_MINOR_SHIFT	16		/* minor version bit shift*/
#define VTC_VER_REV_MASK	0x0000f000	/* revision version */
#define VTC_VER_REV_SHIFT	12		/* revision bit shift*/

/* interrupt status/enable register bit definition */
#define VTC_IXR_FSYNC15_MASK	0x80000000	/* frame sync interrupt 15 */
#define VTC_IXR_FSYNC14_MASK	0x40000000	/* frame sync interrupt 14 */
#define VTC_IXR_FSYNC13_MASK	0x20000000	/* frame sync interrupt 13 */
#define VTC_IXR_FSYNC12_MASK	0x10000000	/* frame sync interrupt 12 */
#define VTC_IXR_FSYNC11_MASK	0x08000000	/* frame sync interrupt 11 */
#define VTC_IXR_FSYNC10_MASK	0x04000000	/* frame sync interrupt 10 */
#define VTC_IXR_FSYNC09_MASK	0x02000000	/* frame sync interrupt 09 */
#define VTC_IXR_FSYNC08_MASK	0x01000000	/* frame sync interrupt 08 */
#define VTC_IXR_FSYNC07_MASK	0x00800000	/* frame sync interrupt 07 */
#define VTC_IXR_FSYNC06_MASK	0x00400000	/* frame sync interrupt 06 */
#define VTC_IXR_FSYNC05_MASK	0x00200000	/* frame sync interrupt 05 */
#define VTC_IXR_FSYNC04_MASK	0x00100000	/* frame sync interrupt 04 */
#define VTC_IXR_FSYNC03_MASK	0x00080000	/* frame sync interrupt 03 */
#define VTC_IXR_FSYNC02_MASK	0x00040000	/* frame sync interrupt 02 */
#define VTC_IXR_FSYNC01_MASK	0x00020000	/* frame sync interrupt 01 */
#define VTC_IXR_FSYNC00_MASK	0x00010000	/* frame sync interrupt 00 */
#define VTC_IXR_FSYNCALL_MASK	0xffff0000	/* all frame sync interrupts */

#define VTC_IXR_G_AV_MASK	0x00002000	/* generator actv video intr */
#define VTC_IXR_G_VBLANK_MASK	0x00001000	/* generator vblank interrupt */
#define VTC_IXR_G_ALL_MASK	0x00003000	/* all generator interrupts */

#define VTC_IXR_D_AV_MASK	0x00000800	/* detector active video intr */
#define VTC_IXR_D_VBLANK_MASK	0x00000400	/* detector vblank interrupt */
#define VTC_IXR_D_ALL_MASK	0x00000c00	/* all detector interrupts */

#define VTC_IXR_LOL_MASK	0x00000200	/* lock loss */
#define VTC_IXR_LO_MAS		0x00000100	/* lock  */
#define VTC_IXR_LOCKALL_MASK	0x00000300	/* all signal lock interrupt */

#define VTC_IXR_ACL_MASK	0x00200000	/* active chroma signal lock */
#define VTC_IXR_AVL_MASK	0x00100000	/* active video signal lock */
#define VTC_IXR_HSL_MASK	0x00080000	/* horizontal sync signal
						   lock */
#define VTC_IXR_VSL_MASK	0x00040000	/* vertical sync signal lock */
#define VTC_IXR_HBL_MASK	0x00020000	/* horizontal blank signal
						   lock */
#define VTC_IXR_VBL_MASK	0x00010000	/* vertical blank signal lock */

#define VTC_IXR_ALLINTR_MASK	(VTC_IXR_FSYNCALL_MASK | \
		VTC_IXR_G_ALL_MASK | \
		VTC_IXR_D_ALL_MASK | \
		VTC_IXR_LOCKALL_MASK)	/* mask for all interrupts */

struct zynq_vtc {
	void __iomem *base;		/* vtc base addr */
	int irq;			/* irq */
	struct device_node *node;	/* device node */
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
		reg |= VTC_CTL_ACP_MASK;
	if (polarity->active_video)
		reg |= VTC_CTL_AVP_MASK;
	if (polarity->field_id)
		reg |= VTC_CTL_FIP_MASK;
	if (polarity->vblank)
		reg |= VTC_CTL_VBP_MASK;
	if (polarity->vsync)
		reg |= VTC_CTL_VSP_MASK;
	if (polarity->hblank)
		reg |= VTC_CTL_HBP_MASK;
	if (polarity->hsync)
		reg |= VTC_CTL_HSP_MASK;

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
		VTC_XVXHOX_HSTART_MASK;
	zynq_vtc_writel(vtc, VTC_GVBHOFF, reg);

	reg = (hori_offset->vsync_hori_start) & VTC_XVXHOX_HSTART_MASK;
	reg |= (hori_offset->vsync_hori_end << VTC_XVXHOX_HEND_SHIFT) &
		VTC_XVXHOX_HSTART_MASK;
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
		reg |= VTC_CTL_FIPSS_MASK;
	if (src_config->active_chroma_pol)
		reg |= VTC_CTL_ACPSS_MASK;
	if (src_config->active_video_pol)
		reg |= VTC_CTL_AVPSS_MASK;
	if (src_config->hsync_pol)
		reg |= VTC_CTL_HSPSS_MASK;
	if (src_config->vsync_pol)
		reg |= VTC_CTL_VSPSS_MASK;
	if (src_config->hblank_pol)
		reg |= VTC_CTL_HBPSS_MASK;
	if (src_config->vblank_pol)
		reg |= VTC_CTL_VBPSS_MASK;

	if (src_config->vchroma)
		reg |= VTC_CTL_VCSS_MASK;
	if (src_config->vactive)
		reg |= VTC_CTL_VASS_MASK;
	if (src_config->vbackporch)
		reg |= VTC_CTL_VBSS_MASK;
	if (src_config->vsync)
		reg |= VTC_CTL_VSSS_MASK;
	if (src_config->vfrontporch)
		reg |= VTC_CTL_VFSS_MASK;
	if (src_config->vtotal)
		reg |= VTC_CTL_VTSS_MASK;

	if (src_config->hbackporch)
		reg |= VTC_CTL_HBSS_MASK;
	if (src_config->hsync)
		reg |= VTC_CTL_HSSS_MASK;
	if (src_config->hfrontporch)
		reg |= VTC_CTL_HFSS_MASK;
	if (src_config->htotal)
		reg |= VTC_CTL_HTSS_MASK;

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
	reg |= VTC_CTL_GE_MASK;
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
	reg &= ~VTC_CTL_GE_MASK;
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

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	reg = zynq_vtc_readl(vtc, VTC_CTL);
	zynq_vtc_writel(vtc, VTC_CTL, reg & ~VTC_CTL_RU_MASK);

	htotal = sig_config->htotal;
	vtotal = sig_config->vtotal;

	hactive = sig_config->hfrontporch_start;
	vactive = sig_config->vfrontporch_start;

	reg = htotal & 0x1fff;
	zynq_vtc_writel(vtc, VTC_GHSIZE, reg);

	reg = vtotal & 0x1fff;
	zynq_vtc_writel(vtc, VTC_GVSIZE, reg);

	reg = hactive & 0x1fff;
	reg |= (vactive & 0x1fff) << 16;
	zynq_vtc_writel(vtc, VTC_GASIZE, reg);

	reg = (sig_config->hsync_start) & VTC_GH1_SYNCSTART_MASK;
	reg |= (sig_config->hbackporch_start) << VTC_GH1_BPSTART_SHIFT &
		VTC_GH1_BPSTART_MASK;
	zynq_vtc_writel(vtc, VTC_GHSYNC, reg);

	reg = (sig_config->vsync_start) & VTC_GV1_SYNCSTART_MASK;
	reg |= (sig_config->vbackporch_start) << VTC_GV1_BPSTART_SHIFT &
		VTC_GV1_BPSTART_MASK;
	zynq_vtc_writel(vtc, VTC_GVSYNC, reg);

	hori_offset.vblank_hori_start = hactive;
	hori_offset.vblank_hori_end = hactive;
	hori_offset.vsync_hori_start = hactive;
	hori_offset.vsync_hori_end = hactive;

	zynq_vtc_config_hori_offset(vtc, &hori_offset);

	reg = zynq_vtc_readl(vtc, VTC_CTL);
	zynq_vtc_writel(vtc, VTC_CTL, reg | VTC_CTL_RU_MASK);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* reset vtc */
void zynq_vtc_reset(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	zynq_vtc_writel(vtc, VTC_RESET, VTC_RESET_RESET_MASK);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* frame synced reset vtc */
void zynq_vtc_fsync_reset(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	zynq_vtc_writel(vtc, VTC_RESET, VTC_SYNC_RESET_MASK);
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
	zynq_vtc_writel(vtc, VTC_IER, (~intr & VTC_IXR_ALLINTR_MASK) &
			zynq_vtc_readl(vtc, VTC_IER));
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}

/* get interrupt */
static inline u32 zynq_vtc_intr_get(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
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
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	return IRQ_HANDLED;
}

/* probe vtc */
struct zynq_vtc *zynq_vtc_probe(char *compatible)
{
	struct zynq_vtc *vtc;
	struct zynq_vtc_polarity polarity;
	struct zynq_vtc_src_config src;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	vtc = kzalloc(sizeof(*vtc), GFP_KERNEL);
	if (!vtc) {
		pr_err("failed to alloc vtc\n");
		goto err_vtc;
	}

	vtc->node = of_find_compatible_node(NULL, NULL, compatible);
	if (!vtc->node) {
		pr_err("failed to find a compatible node\n");
		goto err_node;
	}

	vtc->base = of_iomap(vtc->node, 0);
	if (!vtc->base) {
		pr_err("failed to iomap vtc\n");
		goto err_iomap;
	}

	zynq_vtc_intr_disable(vtc, VTC_IXR_ALLINTR_MASK);
	vtc->irq = irq_of_parse_and_map(vtc->node, 0);
	if (vtc->irq > 0) {
		if (request_irq(vtc->irq, zynq_vtc_intr_handler, IRQF_SHARED,
					"zynq_vtc", vtc)) {
			vtc->irq = 0;
			pr_warn("failed to requet_irq() for zynq_vtc\n");
		} else
			zynq_vtc_intr_enable(vtc, VTC_IXR_ALLINTR_MASK);
	}

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

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	return vtc;

err_iomap:
	of_node_put(vtc->node);
err_node:
	kfree(vtc);
err_vtc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
	return NULL;
}

/* remove vtc */
void zynq_vtc_remove(struct zynq_vtc *vtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");

	if (vtc->irq > 0)
		free_irq(vtc->irq, vtc);

	zynq_vtc_reset(vtc);

	iounmap(vtc->base);
	of_node_put(vtc->node);
	kfree(vtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_VTC, "\n");
}
