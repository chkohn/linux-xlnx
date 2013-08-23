/*
 * Xilinx DRM crtc driver for Xilinx
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
#include <linux/i2c.h>
#include <linux/i2c/si570.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_plane.h"

#include "xilinx_cresample.h"
#include "xilinx_rgb2yuv.h"
#include "xilinx_vtc.h"

struct xilinx_drm_crtc {
	struct drm_crtc base;			/* base drm crtc object */
	struct drm_plane *priv_plane;		/* crtc's private plane */
	struct xilinx_cresample *cresample;	/* chroma resampler */
	struct xilinx_rgb2yuv *rgb2yuv;		/* color space converter */
	struct i2c_client *si570;		/* si570 pixel clock */
	struct xilinx_vtc *vtc;			/* video timing controller */
	struct xilinx_drm_plane_manager *plane_manager;	/* plane manager */
	int dpms;				/* dpms */
	struct drm_pending_vblank_event *event;	/* vblank event */
};

#define to_xilinx_crtc(x)	container_of(x, struct xilinx_drm_crtc, base)

/* set crtc dpms */
static void xilinx_drm_crtc_dpms(struct drm_crtc *base_crtc, int dpms)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "dpms: %d -> %d\n", crtc->dpms, dpms);

	if (crtc->dpms == dpms)
		goto out;

	crtc->dpms = dpms;
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		xilinx_drm_plane_dpms(crtc->priv_plane, dpms);
		if (crtc->rgb2yuv)
			xilinx_rgb2yuv_enable(crtc->rgb2yuv);
		if (crtc->cresample)
			xilinx_cresample_enable(crtc->cresample);
		xilinx_vtc_enable(crtc->vtc);
		break;
	default:
		xilinx_vtc_disable(crtc->vtc);
		xilinx_vtc_reset(crtc->vtc);
		if (crtc->cresample) {
			xilinx_cresample_disable(crtc->cresample);
			xilinx_cresample_reset(crtc->cresample);
		}
		if (crtc->rgb2yuv) {
			xilinx_rgb2yuv_disable(crtc->rgb2yuv);
			xilinx_rgb2yuv_reset(crtc->rgb2yuv);
		}
		xilinx_drm_plane_dpms(crtc->priv_plane, dpms);
		break;
	}

out:
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

/* prepare crtc */
static void xilinx_drm_crtc_prepare(struct drm_crtc *base_crtc)
{
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

/* apply mode to crtc pipe */
static void xilinx_drm_crtc_commit(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_ON);
	xilinx_drm_plane_commit(crtc->priv_plane);
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

static bool xilinx_drm_crtc_mode_fixup(struct drm_crtc *base_crtc,
	const struct drm_display_mode *mode,
	struct drm_display_mode *adjusted_mode)
{
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	return true;
}

/* set new mode in crtc pipe */
static int xilinx_drm_crtc_mode_set(struct drm_crtc *base_crtc,
	struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode,
	int x, int y, struct drm_framebuffer *old_fb)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct xilinx_vtc_sig_config vtc_sig_config;
	int ret;

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	/* configure cresample and rgb2yuv */
	if (crtc->cresample)
		xilinx_cresample_configure(crtc->cresample,
				adjusted_mode->hdisplay,
				adjusted_mode->vdisplay);
	if (crtc->rgb2yuv)
		xilinx_rgb2yuv_configure(crtc->rgb2yuv,
				adjusted_mode->hdisplay,
				adjusted_mode->vdisplay);

	/* configure a plane: vdma and osd layer */
	ret = xilinx_drm_plane_mode_set(crtc->priv_plane, base_crtc,
			base_crtc->fb, 0, 0,
			adjusted_mode->hdisplay, adjusted_mode->vdisplay,
			x, y,
			adjusted_mode->hdisplay, adjusted_mode->vdisplay);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		ret = -EINVAL;
	}

	/* set vtc */
	vtc_sig_config.htotal = adjusted_mode->htotal;
	vtc_sig_config.hfrontporch_start = adjusted_mode->hdisplay;
	vtc_sig_config.hsync_start = adjusted_mode->hsync_start;
	vtc_sig_config.hbackporch_start = adjusted_mode->hsync_end;
	vtc_sig_config.hactive_start = 0;

	vtc_sig_config.vtotal = adjusted_mode->vtotal;
	vtc_sig_config.vfrontporch_start = adjusted_mode->vdisplay;
	vtc_sig_config.vsync_start = adjusted_mode->vsync_start;
	vtc_sig_config.vbackporch_start = adjusted_mode->vsync_end;
	vtc_sig_config.vactive_start = 0;

	xilinx_vtc_config_sig(crtc->vtc, &vtc_sig_config);

	/* set si570 pixel clock */
	set_frequency_si570(&crtc->si570->dev, adjusted_mode->clock * 1000);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	return ret;
}

static int _xilinx_drm_crtc_mode_set_base(struct drm_crtc *base_crtc,
		struct drm_framebuffer *fb, int x, int y)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	int ret;

	/* configure a plane */
	ret = xilinx_drm_plane_mode_set(crtc->priv_plane, base_crtc,
			fb, 0, 0,
			base_crtc->hwmode.hdisplay, base_crtc->hwmode.vdisplay,
			x, y,
			base_crtc->hwmode.hdisplay, base_crtc->hwmode.vdisplay);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		goto err_out;
	}

	/* apply the new fb addr */
	xilinx_drm_crtc_commit(base_crtc);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	return 0;

err_out:
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	return ret;
}

/* update address and information from fb */
static int xilinx_drm_crtc_mode_set_base(struct drm_crtc *base_crtc,
		int x, int y, struct drm_framebuffer *old_fb)
{
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	/* configure a plane */
	return _xilinx_drm_crtc_mode_set_base(base_crtc, base_crtc->fb, x, y);
}

/* load rgb LUT for crtc */
static void xilinx_drm_crtc_load_lut(struct drm_crtc *base_crtc)
{
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

static struct drm_crtc_helper_funcs xilinx_drm_crtc_helper_funcs = {
	.dpms = xilinx_drm_crtc_dpms,
	.prepare = xilinx_drm_crtc_prepare,
	.commit = xilinx_drm_crtc_commit,
	.mode_fixup = xilinx_drm_crtc_mode_fixup,
	.mode_set = xilinx_drm_crtc_mode_set,
	.mode_set_base = xilinx_drm_crtc_mode_set_base,
	.load_lut = xilinx_drm_crtc_load_lut,
};

/* destroy crtc */
void xilinx_drm_crtc_destroy(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	/* make sure crtc is off */
	xilinx_drm_crtc_dpms(base_crtc, DRM_MODE_DPMS_OFF);

	drm_crtc_cleanup(base_crtc);

	xilinx_vtc_remove(crtc->vtc);
	xilinx_drm_plane_destroy_planes(crtc->plane_manager);
	xilinx_drm_plane_destroy_private(crtc->plane_manager, crtc->priv_plane);
	xilinx_drm_plane_remove_manager(crtc->plane_manager);
	if (crtc->rgb2yuv)
		xilinx_rgb2yuv_remove(crtc->rgb2yuv);
	if (crtc->cresample)
		xilinx_cresample_remove(crtc->cresample);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

/* cancel page flip functions */
void xilinx_drm_crtc_cancel_page_flip(struct drm_crtc *base_crtc,
		struct drm_file *file)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	spin_lock_irqsave(&drm->event_lock, flags);
	event = crtc->event;
	if (event && (event->base.file_priv == file)) {
		crtc->event = NULL;
		event->base.destroy(&event->base);
		drm_vblank_put(drm, 0);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

/* finish page flip functions */
static void xilinx_drm_crtc_finish_page_flip(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);
	event = crtc->event;
	crtc->event = NULL;
	if (event) {
		drm_send_vblank_event(drm, 0, event);
		drm_vblank_put(drm, 0);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

/* page flip functions */
static int xilinx_drm_crtc_page_flip(struct drm_crtc *base_crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	struct drm_device *drm = base_crtc->dev;
	unsigned long flags;
	int ret;

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	spin_lock_irqsave(&drm->event_lock, flags);
	if (crtc->event != NULL) {
		spin_unlock_irqrestore(&drm->event_lock, flags);
		ret = -EBUSY;
		goto err_out;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	/* configure a plane */
	ret = _xilinx_drm_crtc_mode_set_base(base_crtc, fb,
			base_crtc->x, base_crtc->y);
	if (ret) {
		DRM_ERROR("failed to mode set a plane\n");
		goto err_out;
	}

	base_crtc->fb = fb;

	if (event) {
		event->pipe = 0;
		drm_vblank_get(drm, 0);
		spin_lock_irqsave(&drm->event_lock, flags);
		crtc->event = event;
		spin_unlock_irqrestore(&drm->event_lock, flags);
	}

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	return 0;

err_out:
	return ret;
}

/* vblank interrupt handler */
static void xilinx_drm_crtc_vblank_handler(void *data)
{
	struct drm_crtc *base_crtc = data;
	struct drm_device *drm;

	if (!base_crtc)
		return;

	drm = base_crtc->dev;

	drm_handle_vblank(drm, 0);
	xilinx_drm_crtc_finish_page_flip(base_crtc);
}

/* enable vblank interrupt */
void xilinx_drm_crtc_enable_vblank(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	xilinx_vtc_enable_vblank_intr(crtc->vtc,
			xilinx_drm_crtc_vblank_handler, base_crtc);
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

/* disable vblank interrupt */
void xilinx_drm_crtc_disable_vblank(struct drm_crtc *base_crtc)
{
	struct xilinx_drm_crtc *crtc = to_xilinx_crtc(base_crtc);
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	xilinx_vtc_disable_vblank_intr(crtc->vtc);
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
}

static struct drm_crtc_funcs xilinx_drm_crtc_funcs = {
	.destroy = xilinx_drm_crtc_destroy,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = xilinx_drm_crtc_page_flip,
};

/* create crtc */
struct drm_crtc *xilinx_drm_crtc_create(struct drm_device *drm)
{
	struct xilinx_drm_crtc *crtc;
	struct drm_crtc *err_ret;
	struct device_node *sub_node;
	int possible_crtcs = 1;
	int res;

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	crtc = devm_kzalloc(drm->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		DRM_ERROR("failed to allocate crtc\n");
		err_ret = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	/* probe chroma resampler and enable */
	sub_node = of_parse_phandle(drm->dev->of_node, "cresample", 0);
	if (sub_node) {
		crtc->cresample = xilinx_cresample_probe(drm->dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR_OR_NULL(crtc->cresample)) {
			DRM_ERROR("failed to probe a cresample\n");
			err_ret = (void *)crtc->cresample;
			goto err_cresample;
		}
	}

	/* probe color space converter and enable */
	sub_node = of_parse_phandle(drm->dev->of_node, "rgb2yuv", 0);
	if (sub_node) {
		crtc->rgb2yuv = xilinx_rgb2yuv_probe(drm->dev, sub_node);
		of_node_put(sub_node);
		if (IS_ERR_OR_NULL(crtc->rgb2yuv)) {
			DRM_ERROR("failed to probe a rgb2yuv\n");
			err_ret = (void *)crtc->rgb2yuv;
			goto err_rgb2yuv;
		}
	}

	/* probe a plane manager */
	crtc->plane_manager = xilinx_drm_plane_probe_manager(drm);
	if (IS_ERR(crtc->plane_manager)) {
		DRM_ERROR("failed to probe a plane manager\n");
		err_ret = ERR_PTR(-ENODEV);
		goto err_plane_manager;
	}

	/* create a private plane */
	/* there's only one crtc now */
	crtc->priv_plane = xilinx_drm_plane_create_private(crtc->plane_manager,
			possible_crtcs);
	if (IS_ERR_OR_NULL(crtc->plane_manager)) {
		DRM_ERROR("failed to create a private plane for crtc\n");
		err_ret = ERR_PTR(-ENODEV);
		goto err_plane;
	}

	/* create extra planes */
	xilinx_drm_plane_create_planes(crtc->plane_manager, possible_crtcs);

	crtc->si570 = get_i2c_client_si570();
	if (!crtc->si570) {
		XILINX_DEBUG_KMS(XILINX_KMS_ENCODER,
				"failed to get si570 clock\n");
		err_ret = ERR_PTR(-EPROBE_DEFER);
		goto err_si570;
	}

	sub_node = of_parse_phandle(drm->dev->of_node, "tc", 0);
	if (!sub_node) {
		DRM_ERROR("failed to get a video timing controller node\n");
		err_ret = ERR_PTR(-ENODEV);
		goto err_vtc;
	}

	crtc->vtc = xilinx_vtc_probe(drm->dev, sub_node);
	of_node_put(sub_node);
	if (IS_ERR_OR_NULL(crtc->vtc)) {
		DRM_ERROR("failed to probe video timing controller\n");
		err_ret = (void *)crtc->vtc;
		goto err_vtc;
	}

	/* initialize drm crtc */
	res = drm_crtc_init(drm, &crtc->base, &xilinx_drm_crtc_funcs);
	if (res) {
		DRM_ERROR("failed to initialize crtc\n");
		err_ret = ERR_PTR(res);
		goto err_init;
	}
	drm_crtc_helper_add(&crtc->base, &xilinx_drm_crtc_helper_funcs);

	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");

	return &crtc->base;

err_init:
	xilinx_vtc_remove(crtc->vtc);
err_si570:
err_vtc:
	xilinx_drm_plane_destroy_planes(crtc->plane_manager);
	xilinx_drm_plane_destroy_private(crtc->plane_manager, crtc->priv_plane);
err_plane:
	xilinx_drm_plane_remove_manager(crtc->plane_manager);
err_plane_manager:
	if (crtc->rgb2yuv)
		xilinx_rgb2yuv_remove(crtc->rgb2yuv);
err_rgb2yuv:
	if (crtc->cresample)
		xilinx_cresample_remove(crtc->cresample);
err_cresample:
err_alloc:
	XILINX_DEBUG_KMS(XILINX_KMS_CRTC, "\n");
	return err_ret;
}
