/*
 * Xilinx DRM plane driver for Zynq
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

#include <linux/amba/xilinx_dma.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "zynq_drm_drv.h"

#include "zynq_osd.h"

struct zynq_drm_plane_vdma {
	struct device_node *node;		/* device node */
	int chan_id;				/* channel id */
	struct dma_chan *chan;			/* dma channel */
	struct xilinx_vdma_config dma_config;	/* dma config */
};

struct zynq_drm_plane {
	struct drm_plane base;			/* base drm plane object */
	int id;					/* plane id(= fb id) */
	int zorder;				/* z-plane order */
	int dpms;				/* dpms */
	bool priv;				/* private flag */
	struct zynq_drm_plane_vdma vdma;	/* vdma */
	struct zynq_osd_layer *osd_layer;	/* osd layer */
	struct zynq_drm_plane_manager* manager;	/* plane manager */
};

#define MAX_PLANES 8

struct zynq_drm_plane_manager {
	struct drm_device *drm;			/* drm device */
	struct zynq_osd *osd;			/* osd */
	int num_planes;				/* num of planes */
	struct zynq_drm_plane *planes[MAX_PLANES]; /* planes */
	/* TODO: list to manage z order of planes */
};

static const uint32_t zynq_drm_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
};

#define to_zynq_plane(x)	container_of(x, struct zynq_drm_plane, base)

/* set plane dpms */
void zynq_drm_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct zynq_drm_plane *plane = to_zynq_plane(base_plane);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "dpms: %d -> %d\n", plane->dpms, dpms);

	if (plane->dpms != dpms) {
		plane->dpms = dpms;
		switch (dpms) {
		case DRM_MODE_DPMS_ON:
			/* start vdma engine */
			dma_async_issue_pending(plane->vdma.chan);
			break;
		default:
			/* stop vdma engine */
			dmaengine_terminate_all(plane->vdma.chan);
			break;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* prepare plane */
void zynq_drm_plane_prepare(struct drm_plane *base_plane)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	/* turn off the pipeline before set mode */
	zynq_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* apply mode to plane pipe */
void zynq_drm_plane_commit(struct drm_plane *base_plane)
{
	struct zynq_drm_plane *plane = to_zynq_plane(base_plane);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);
	/* start vdma engine with new mode*/
	zynq_drm_plane_dpms(base_plane, DRM_MODE_DPMS_ON);
	/* make sure that vdma starts with new mode */
	dma_async_issue_pending(plane->vdma.chan);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* mode set a plane */
int zynq_drm_plane_mode_set(struct drm_plane *base_plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	struct zynq_drm_plane *plane = to_zynq_plane(base_plane);
	struct drm_gem_cma_object *obj;
	struct dma_async_tx_descriptor *desc;
	int bpp = fb->bits_per_pixel / 8;
	int pitch = fb->pitches[0];
	size_t offset;
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);

	obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!obj) {
		DRM_ERROR("failed to get a gem obj for fb\n");
		ret = -EINVAL;
		goto err_out;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "h: %d(%d), v: %d(%d), paddr: %p\n",
			src_w, src_x, src_h, src_y,
			(void *)obj->paddr);

	offset = src_x * fb->bits_per_pixel / 8 + src_y * fb->pitches[0];

	/* configure vdma desc */
	plane->vdma.dma_config.hsize = src_w * bpp;
	plane->vdma.dma_config.vsize = src_h;
	plane->vdma.dma_config.stride = pitch;

	dmaengine_device_control(plane->vdma.chan, DMA_SLAVE_CONFIG,
			(unsigned long)&plane->vdma.dma_config);

	desc = dmaengine_prep_slave_single(plane->vdma.chan,
			obj->paddr + offset, src_h * pitch,
			DMA_MEM_TO_DEV, 0);
	if (!desc) {
		DRM_ERROR("failed to prepare DMA descriptor\n");
		ret = -EINVAL;
		goto err_out;
	}

	/* submit vdma desc */
	dmaengine_submit(desc);

	/* set OSD dimentions */
	if (plane->manager->osd) {
		/* if a plane is private, it's for crtc */
		if (plane->priv) {
			zynq_osd_set_dimension(plane->manager->osd,
					crtc_w, crtc_h);
		}

		zynq_osd_layer_set_dimension(plane->osd_layer, src_x, src_y,
				src_w, src_h);
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return 0;

err_out:
	return ret;
}

/* update a plane. just call mode_set() with bit-shifted values */
int zynq_drm_plane_update(struct drm_plane *base_plane, struct drm_crtc *crtc,
		struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	ret = zynq_drm_plane_mode_set(base_plane, crtc, fb, crtc_x, crtc_y,
			crtc_w, crtc_h, src_x >> 16, src_y >> 16,
			src_w >> 16, src_h >> 16);
	if (ret) {
		DRM_ERROR("failed to mode-set a plane\n");
		goto err_out;
	}

	/* apply the new fb addr */
	zynq_drm_plane_commit(base_plane);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return 0;

err_out:
	return ret;
}

/* disable a plane */
static int zynq_drm_plane_disable(struct drm_plane *base_plane)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	zynq_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return 0;
}

/*destroy a plane */
static void zynq_drm_plane_destroy(struct drm_plane *base_plane)
{
	struct zynq_drm_plane *plane = to_zynq_plane(base_plane);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);

	zynq_drm_plane_dpms(base_plane, DRM_MODE_DPMS_OFF);

	plane->manager->planes[plane->id] = NULL;
	drm_plane_cleanup(base_plane);
	dma_release_channel(plane->vdma.chan);
	if (plane->manager->osd) {
		zynq_osd_layer_disable(plane->osd_layer);
		zynq_osd_layer_destroy(plane->osd_layer);
	}
	kfree(plane);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* set property of a plane */
static int zynq_drm_plane_set_property(struct drm_plane *base_plane,
				     struct drm_property *property,
				     uint64_t val)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	/* TODO: set zorder, etc */
	return -EINVAL;
}

static struct drm_plane_funcs zynq_drm_plane_funcs = {
	.update_plane = zynq_drm_plane_update,
	.disable_plane = zynq_drm_plane_disable,
	.destroy = zynq_drm_plane_destroy,
	.set_property = zynq_drm_plane_set_property,
};

/* xilinx vdma filter */
static bool zynq_drm_plane_xvdma_filter(struct dma_chan *chan, void *param)
{
	struct zynq_drm_plane_vdma *vdma = param;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "node match: %d\n",
			chan->device->dev->of_node == vdma->node);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "chan->device->dev->of_node: %p\n",
			chan->device->dev->of_node);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "vdma->node: %p\n", vdma->node);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "chan->chan_id: %d\n", chan->chan_id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "vdma->chan_id: %d\n", vdma->chan_id);

	return (chan->device->dev->of_node == vdma->node) &&
		(chan->chan_id == vdma->chan_id);
}

/* create a plane */
static struct zynq_drm_plane *_zynq_drm_plane_create(
		struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs, bool priv)
{
	struct zynq_drm_plane *plane;
	struct platform_device *pdev = manager->drm->platformdev;
	char node_name[16];
	struct of_phandle_args dma_spec;
	dma_cap_mask_t mask;
	int i;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	for (i = 0; i < manager->num_planes; i++) {
		if (!manager->planes[i]) {
			break;
		}
	}
	if (i >= manager->num_planes) {
		DRM_ERROR("failed to allocate plane\n");
		goto err_plane;
	}

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane) {
		DRM_ERROR("failed to allocate plane\n");
		goto err_alloc;
	}

	plane->priv = priv;
	plane->id = i;
	plane->zorder = i;
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);
	/* TODO: add to the manager's zorder list */

	/* get a vdma node */
	snprintf(node_name, sizeof(node_name), "dma-request%d", i);
	if (of_parse_phandle_with_args(pdev->dev.of_node, node_name,
			"#dma-cells", 0, &dma_spec)) {
		DRM_ERROR("failed to get vdma node\n");
		goto err_dma_node;
	}
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "vdma name: %s\n",
			dma_spec.np->full_name);
	plane->vdma.node = dma_spec.np;
	plane->vdma.chan_id = dma_spec.args[0];

	/* configure and request dma channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);
	plane->vdma.chan = dma_request_channel(mask,
			zynq_drm_plane_xvdma_filter, &plane->vdma);
	if (!plane->vdma.chan) {
		DRM_ERROR("failed to request dma channel\n");
		goto err_dma_request;
	}

	/* create an OSD layer when OSD is available */
	if (manager->osd) {
		/* create an osd layer */
		plane->osd_layer = zynq_osd_layer_create(manager->osd);
		if (!plane->osd_layer) {
			DRM_ERROR("failed to create a osd layer\n");
			goto err_osd_layer;
		}

		/* set zorder */
		zynq_osd_layer_set_priority(plane->osd_layer, plane->zorder);
		zynq_osd_layer_enable(plane->osd_layer);

		zynq_osd_layer_set_alpha(plane->osd_layer, 1, 0xff);
	}

	/* initialize drm plane */
	if (drm_plane_init(manager->drm, &plane->base, possible_crtcs,
				&zynq_drm_plane_funcs, zynq_drm_plane_formats,
				ARRAY_SIZE(zynq_drm_plane_formats), priv)) {
		DRM_ERROR("failed to initialize plane\n");
		goto err_init;
	}
	plane->manager = manager;
	manager->planes[i] = plane;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return plane;

err_init:
	if (plane->manager->osd) {
		zynq_osd_layer_disable(plane->osd_layer);
		zynq_osd_layer_destroy(plane->osd_layer);
	}
err_osd_layer:
	dma_release_channel(plane->vdma.chan);
err_dma_request:
err_dma_node:
	kfree(plane);
err_alloc:
err_plane:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return NULL;
}

/* create a private plane */
struct drm_plane *zynq_drm_plane_create_private(
		struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs)
{
	struct zynq_drm_plane *plane;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	plane = _zynq_drm_plane_create(manager, possible_crtcs, true);
	if (!plane) {
		DRM_ERROR("failed to allocate a private plane\n");
		goto err_out;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return &plane->base;

err_out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return NULL;
}

void zynq_drm_plane_destroy_private(struct zynq_drm_plane_manager *manager,
		struct drm_plane *base_plane)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	zynq_drm_plane_destroy(base_plane);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* destroy planes */
void zynq_drm_plane_destroy_planes(struct zynq_drm_plane_manager *manager)
{
	struct zynq_drm_plane *plane;
	int i;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	for (i = 0; i < manager->num_planes; i++) {
		plane = manager->planes[i];
		if (plane && !plane->priv) {
			zynq_drm_plane_destroy(&plane->base);
			manager->planes[i] = NULL;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* create planes */
int zynq_drm_plane_create_planes(struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs)
{
	int i;
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	/* find if there any available plane */
	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i])
			continue;
		manager->planes[i] = _zynq_drm_plane_create(manager,
				possible_crtcs, false);
		if (!manager->planes[i]) {
			DRM_ERROR("failed to allocate a plane\n");
			ret = -ENOMEM;
			goto err_out;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return 0;

err_out:
	zynq_drm_plane_destroy_planes(manager);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return ret;
}

struct zynq_drm_plane_manager *
zynq_drm_plane_probe_manager(struct drm_device *drm)
{
	struct zynq_drm_plane_manager *manager;
	struct platform_device *pdev = drm->platformdev;
	u32 prop;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	manager = kzalloc(sizeof(*manager), GFP_KERNEL);
	if (!manager) {
		DRM_ERROR("failed to allocate a plane manager\n");
		goto err_alloc;
	}
	manager->drm = drm;
	/* TODO: duplicate get_prop in osd, consider clean up */
	if (of_property_read_u32(pdev->dev.of_node, "xlnx,num-planes", &prop)) {
		pr_err("failed to get num of planes prop\n");
		goto err_prop;
	}
	manager->num_planes = prop;

	/* probe an OSD. proceed even if there's no OSD */
	manager->osd = zynq_osd_probe("xlnx,vosd");
	if (manager->osd) {
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "OSD is probed\n");
		/* set background color as black */
		zynq_osd_set_color(manager->osd, 0x0, 0x0, 0x0);
		zynq_osd_enable(manager->osd);
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return manager;

err_prop:
	kfree(manager);
err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return NULL;
}

void zynq_drm_plane_remove_manager(struct zynq_drm_plane_manager *manager)
{
	int i;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i] && !manager->planes[i]->priv) {
			zynq_drm_plane_dpms(&manager->planes[i]->base,
					DRM_MODE_DPMS_OFF);
			zynq_drm_plane_destroy(&manager->planes[i]->base);
			manager->planes[i] = NULL;
		}
	}
	if (manager->osd) {
		zynq_osd_disable(manager->osd);
		zynq_osd_remove(manager->osd);
	}
	kfree(manager);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}
