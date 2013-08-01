/*
 * Xilinx DRM crtc driver for Zynq
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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "zynq_drm_drv.h"
#include "zynq_drm_plane.h"

#include "zynq_cresample.h"
#include "zynq_rgb2yuv.h"

struct zynq_drm_crtc {
	struct drm_crtc base;			/* base drm crtc object */
	struct drm_plane *priv_plane;		/* crtc's private plane */
	struct zynq_cresample *cresample;	/* chroma resampler */
	struct zynq_rgb2yuv *rgb2yuv;		/* color space converter */
	struct zynq_drm_plane_manager *plane_manager;	/* plane manager */
	int dpms;				/* dpms */
};

#define to_zynq_crtc(x)	container_of(x, struct zynq_drm_crtc, base)

/* set crtc dpms */
static void zynq_drm_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "dpms: %d -> %d\n", crtc->dpms, dpms);

	if (crtc->dpms != dpms) {
		crtc->dpms = dpms;
		switch (dpms) {
		case DRM_MODE_DPMS_ON:
			zynq_drm_plane_dpms(crtc->priv_plane, dpms);
			if (crtc->rgb2yuv)
				zynq_rgb2yuv_enable(crtc->rgb2yuv);
			if (crtc->cresample)
				zynq_cresample_enable(crtc->cresample);
			break;
		default:
			if (crtc->cresample) {
				zynq_cresample_disable(crtc->cresample);
				zynq_cresample_reset(crtc->cresample);
			}
			if (crtc->rgb2yuv) {
				zynq_rgb2yuv_disable(crtc->rgb2yuv);
				zynq_rgb2yuv_reset(crtc->rgb2yuv);
			}
			zynq_drm_plane_dpms(crtc->priv_plane, dpms);
			break;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

/* prepare crtc */
static void zynq_drm_crtc_prepare(struct drm_crtc *base_crtc)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	zynq_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

/* apply mode to crtc pipe */
static void zynq_drm_crtc_commit(struct drm_crtc *base_crtc)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	zynq_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);
	zynq_drm_plane_commit(crtc->priv_plane);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

static bool zynq_drm_crtc_mode_fixup(struct drm_crtc *base_crtc,
	const struct drm_display_mode *mode,
	struct drm_display_mode *adjusted_mode)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return true;
}

static int _zynq_drm_crtc_mode_set(struct zynq_drm_crtc *crtc,
		struct drm_display_mode *mode, int x, int y)
{
	struct drm_crtc *base_crtc = &crtc->base;
	int ret = 0;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	/* configure cresample and rgb2yuv */
	if (crtc->cresample)
		zynq_cresample_configure(crtc->cresample,
				mode->hdisplay, mode->vdisplay);
	if (crtc->rgb2yuv)
		zynq_rgb2yuv_configure(crtc->rgb2yuv,
				mode->hdisplay, mode->vdisplay);

	/* configure a plane: vdma and osd layer */
	ret = zynq_drm_plane_mode_set(crtc->priv_plane, base_crtc,
		       	base_crtc->fb, 0, 0, mode->hdisplay, mode->vdisplay,
			x, y, mode->hdisplay, mode->vdisplay);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		ret = -EINVAL;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	return ret;
}

/* set new mode in crtc pipe */
static int zynq_drm_crtc_mode_set(struct drm_crtc *base_crtc,
	struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode,
	int x, int y, struct drm_framebuffer *old_fb)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	ret = _zynq_drm_crtc_mode_set(crtc, adjusted_mode, x, y);
	if (ret) {
		DRM_ERROR("failed to set mode\n");
		goto err_out;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	return 0;
err_out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return ret;
}

/* update address and information from fb */
static int zynq_drm_crtc_mode_set_base(struct drm_crtc *base_crtc, int x,
		int y, struct drm_framebuffer *old_fb)
{
	struct zynq_drm_crtc *crtc = to_zynq_crtc(base_crtc);
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	ret = _zynq_drm_crtc_mode_set(crtc, &base_crtc->mode, x, y);
	if (ret) {
		DRM_ERROR("failed to set mode\n");
		goto err_out;
	}

	/* apply the new fb addr */
	zynq_drm_crtc_commit(base_crtc);

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

	drm_crtc_cleanup(base_crtc);
	zynq_drm_plane_destroy_planes(crtc->plane_manager);
	zynq_drm_plane_destroy_private(crtc->plane_manager, crtc->priv_plane);
	zynq_drm_plane_remove_manager(crtc->plane_manager);
	if (crtc->rgb2yuv)
		zynq_rgb2yuv_remove(crtc->rgb2yuv);
	if (crtc->cresample)
		zynq_cresample_remove(crtc->cresample);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
}

static struct drm_crtc_funcs zynq_drm_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = zynq_drm_crtc_destroy,
};

/* create crtc */
struct drm_crtc *zynq_drm_crtc_create(struct drm_device *drm)
{
	struct zynq_drm_crtc *crtc;
	int possible_crtcs = 1;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	crtc = devm_kzalloc(drm->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		DRM_ERROR("failed to allocate crtc\n");
		goto err_alloc;
	}

	/* probe chroma resampler and enable */
	crtc->cresample = zynq_cresample_probe(drm->dev, "xlnx,vcresample");
	if (crtc->cresample)
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "cresample is probed\n");

	/* probe color space converter and enable */
	crtc->rgb2yuv = zynq_rgb2yuv_probe(drm->dev, "xlnx,vrgb2ycrcb");
	if (crtc->rgb2yuv)
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "rgb2yuv is probed\n");

	/* probe a plane manager */
	crtc->plane_manager = zynq_drm_plane_probe_manager(drm);
	if (!crtc->plane_manager) {
		DRM_ERROR("failed to probe a plane manager\n");
		goto err_plane_manager;
	}

	/* create a private plane */
	/* there's only one crtc now */
	crtc->priv_plane = zynq_drm_plane_create_private(crtc->plane_manager,
			possible_crtcs);
	if (!crtc->priv_plane) {
		DRM_ERROR("failed to create a private plane for crtc\n");
		goto err_plane;
	}

	/* create extra planes */
	zynq_drm_plane_create_planes(crtc->plane_manager, possible_crtcs);

	/* initialize drm crtc */
	if (drm_crtc_init(drm, &crtc->base, &zynq_drm_crtc_funcs)) {
		DRM_ERROR("failed to initialize crtc\n");
		goto err_init;
	}
	drm_crtc_helper_add(&crtc->base, &zynq_drm_crtc_helper_funcs);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");

	return &crtc->base;

err_init:
	zynq_drm_plane_destroy_planes(crtc->plane_manager);
	zynq_drm_plane_destroy_private(crtc->plane_manager, crtc->priv_plane);
err_plane:
	zynq_drm_plane_remove_manager(crtc->plane_manager);
err_plane_manager:
	if (crtc->rgb2yuv)
		zynq_rgb2yuv_remove(crtc->rgb2yuv);
	if (crtc->cresample)
		zynq_cresample_remove(crtc->cresample);
err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CRTC, "\n");
	return NULL;
}
