/*
 * Xilinx DRM crtc driver for Zynq
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

#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/amba/xilinx_dma.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "zynq_drm_drv.h"
#include "zynq_cresample.h"
#include "zynq_rgb2yuv.h"

struct zynq_drm_crtc_vdma {
	struct device_node *node;		/* device node */
	int chan_id;				/* channel id */
	struct dma_chan *chan;			/* dma channel */
	struct xilinx_vdma_config dma_config;	/* dma config */
};

struct zynq_drm_crtc {
	struct drm_crtc base;			/* base drm crtc object */
	struct zynq_drm_crtc_vdma vdma;		/* vdma */
	int dpms;
	struct zynq_cresample *cresample;	/* chroma resampler */
	struct zynq_rgb2yuv *rgb2yuv;		/* color space converter */

	/* framebuffer information */
	int hsize;				/* horizontal size */
	int vsize;				/* vertical size */
	int bpp;				/* byte per pixel */
	int pitch;				/* ptich */
	int x;					/* x position */
	int y;					/* y position */
	dma_addr_t paddr;			/* physical address */
};

#define to_zynq_crtc(x)	container_of(x, struct zynq_drm_crtc, base)

/* set crtc dpms */
static void zynq_drm_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	if (crtc->dpms != dpms) {
		crtc->dpms = dpms;
		switch (dpms) {
		case DRM_MODE_DPMS_ON:
			break;
		default:
			/* stop vdma engine */
			dmaengine_terminate_all(crtc->vdma.chan);
			break;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

/* prepare crtc */
static void zynq_drm_crtc_prepare(struct drm_crtc *base_crtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	/* nothing has to be done here */
}

/* commit mode in crtc object to crtc hardwares */
static void zynq_drm_crtc_commit(struct drm_crtc *base_crtc)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);
	struct dma_async_tx_descriptor *desc;
	size_t offset;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	/* make sure crtc is on */
	zynq_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);

	/* configure cresample and rgb2yuv */
	zynq_cresample_configure(crtc->cresample, crtc->hsize, crtc->vsize);
	zynq_rgb2yuv_configure(crtc->rgb2yuv, crtc->hsize, crtc->vsize);

	/* configure vdma desc */
	crtc->vdma.dma_config.hsize = (crtc->hsize - crtc->x) * crtc->bpp;
	crtc->vdma.dma_config.vsize = crtc->vsize - crtc->y;
	crtc->vdma.dma_config.stride = crtc->pitch;

	dmaengine_device_control(crtc->vdma.chan, DMA_SLAVE_CONFIG,
			(unsigned long)&crtc->vdma.dma_config);

	offset = crtc->x * crtc->bpp + crtc->y * crtc->pitch;

	desc = dmaengine_prep_slave_single(crtc->vdma.chan,
			crtc->paddr + offset,
			crtc->vsize * crtc->pitch,
			DMA_MEM_TO_DEV, 0);
	if (!desc) {
		DRM_ERROR("failed to prepare DMA descriptor\n");
		goto out;
	}

	/* submit vdma desc */
	dmaengine_submit(desc);

	/* start vdma engine */
	dma_async_issue_pending(crtc->vdma.chan);

out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return;
}

static bool zynq_drm_crtc_mode_fixup(struct drm_crtc *base_crtc,
	const struct drm_display_mode *mode,
	struct drm_display_mode *adjusted_mode)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return true;
}

/* store mode in crtc object */
static int zynq_drm_crtc_mode_set(struct drm_crtc *base_crtc,
	struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode,
	int x, int y, struct drm_framebuffer *old_fb)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);
	struct drm_framebuffer *fb = base_crtc->fb;
	struct drm_gem_cma_object *obj;
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	crtc->hsize = mode->hdisplay;
	crtc->vsize = mode->vdisplay;
	crtc->bpp = fb->bits_per_pixel / 8;
	crtc->pitch = fb->pitches[0];
	crtc->x = x;
	crtc->y = y;

	obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!obj) {
		DRM_ERROR("failed to get a gem obj for fb\n");
		ret = -EINVAL;
		goto err_out;
	}

	crtc->paddr =  obj->paddr;

	return 0;
err_out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return ret;
}

/* store fb information in crtc object */
static int zynq_drm_crtc_mode_set_base(struct drm_crtc *base_crtc, int x,
		int y, struct drm_framebuffer *old_fb)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);
	struct drm_framebuffer *fb = base_crtc->fb;
	struct drm_gem_cma_object *obj;
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	crtc->hsize = fb->width;
	crtc->vsize = fb->height;
	crtc->bpp = fb->bits_per_pixel / 8;
	crtc->pitch = fb->pitches[0];
	crtc->x = x;
	crtc->y = y;

	obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!obj) {
		DRM_ERROR("failed to get a gem obj for fb\n");
		ret = -EINVAL;
		goto err_out;
	}

	crtc->paddr =  obj->paddr;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	return 0;
err_out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return ret;
}

/* load rgb LUT for crtc */
static void zynq_drm_crtc_load_lut(struct drm_crtc *base_crtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

static struct drm_crtc_helper_funcs zynq_drm_crtc_helper_funcs = {
	.dpms = zynq_drm_crtc_dpms,
	.prepare = zynq_drm_crtc_prepare,
	.commit = zynq_drm_crtc_commit,
	.mode_fixup = zynq_drm_crtc_mode_fixup,
	.mode_set = zynq_drm_crtc_mode_set,
	.mode_set_base = zynq_drm_crtc_mode_set_base,
	.load_lut = zynq_drm_crtc_load_lut,
};

/* destroy crtc */
void zynq_drm_crtc_destroy(struct drm_crtc *base_crtc)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	/* make sure crtc is off */
	zynq_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);

	zynq_rgb2yuv_remove(crtc->rgb2yuv);
	zynq_cresample_remove(crtc->cresample);
	dma_release_channel(crtc->vdma.chan);
	drm_crtc_cleanup(base_crtc);
	kfree(crtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

static struct drm_crtc_funcs zynq_drm_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = zynq_drm_crtc_destroy,
};

/* xilinx vdma filter */
static bool zynq_drm_crtc_xvdma_filter(struct dma_chan *chan, void *param)
{
	struct zynq_drm_crtc_vdma *vdma = param;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	return (chan->device->dev->of_node == vdma->node) &&
		(chan->chan_id == vdma->chan_id);
}

/* create crtc */
struct drm_crtc *zynq_drm_crtc_create(struct drm_device *drm)
{
	struct zynq_drm_crtc *crtc;
	struct platform_device *pdev = drm->platformdev;
	struct of_phandle_args dma_spec;
	dma_cap_mask_t mask;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		DRM_ERROR("failed to allocate crtc\n");
		goto err_alloc;
	}
	crtc->dpms = DRM_MODE_DPMS_OFF;

	/* get vdma node */
	if (of_parse_phandle_with_args(pdev->dev.of_node, "dma-request",
			"#dma-cells", 0, &dma_spec)) {
		DRM_ERROR("failed to initialize crtc\n");
		goto err_dma_node;
	}
	crtc->vdma.node = dma_spec.np;
	crtc->vdma.chan_id = dma_spec.args[0];

	/* configure and request dma channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);
	crtc->vdma.chan = dma_request_channel(mask, zynq_drm_crtc_xvdma_filter,
			&crtc->vdma);
	if (!crtc->vdma.chan) {
		DRM_ERROR("failed to request dma channel\n");
		goto err_dma_request;
	}

	/* probe chroma resampler and enable */
	crtc->cresample = zynq_cresample_probe("xlnx,vcresample");
	if (!crtc->cresample) {
		DRM_ERROR("failed to probe cresample\n");
		goto err_cresample;
	}
	zynq_cresample_enable(crtc->cresample);

	/* probe color space converter and enable */
	crtc->rgb2yuv = zynq_rgb2yuv_probe("xlnx,vrgb2ycrcb");
	if (!crtc->rgb2yuv) {
		DRM_ERROR("failed to probe vrgb2yuv\n");
		goto err_rgb2yuv;
	}
	zynq_rgb2yuv_enable(crtc->rgb2yuv);

	/* initialize drm crtc */
	if (drm_crtc_init(drm, &crtc->base, &zynq_drm_crtc_funcs)) {
		DRM_ERROR("failed to initialize crtc\n");
		goto err_init;
	}
	drm_crtc_helper_add(&crtc->base, &zynq_drm_crtc_helper_funcs);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	return &crtc->base;

err_init:
	zynq_rgb2yuv_remove(crtc->rgb2yuv);
err_rgb2yuv:
	zynq_cresample_remove(crtc->cresample);
err_cresample:
	dma_release_channel(crtc->vdma.chan);
err_dma_request:
err_dma_node:
	kfree(crtc);
err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return NULL;
}
