/*
 * Xilinx DRM plane driver for Zynq
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

#include <linux/amba/xilinx_dma.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "zynq_drm_drv.h"

#include "zynq_osd.h"

struct zynq_drm_plane_vdma {
	struct dma_chan *chan;			/* dma channel */
	struct xilinx_vdma_config dma_config;	/* dma config */
};

struct zynq_drm_plane {
	struct drm_plane base;			/* base drm plane object */
	int id;					/* plane id */
	int dpms;				/* dpms */
	bool priv;				/* private flag */
	uint32_t x;				/* x position */
	uint32_t y;				/* y position */
	dma_addr_t paddr;			/* phys addr of frame buffer */
	int bpp;				/* bytes per pixel */
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
	DRM_FORMAT_YUYV,
};

#define to_zynq_plane(x)	container_of(x, struct zynq_drm_plane, base)

/* set plane dpms */
void zynq_drm_plane_dpms(struct drm_plane *base_plane, int dpms)
{
	struct zynq_drm_plane *plane = to_zynq_plane(base_plane);
	struct zynq_drm_plane_manager *manager = plane->manager;
	struct xilinx_vdma_config dma_config;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "dpms: %d -> %d\n", plane->dpms, dpms);

	if (plane->dpms == dpms)
		goto out;

	plane->dpms = dpms;
	switch (dpms) {
	case DRM_MODE_DPMS_ON:
		/* start vdma engine */
		dma_async_issue_pending(plane->vdma.chan);

		/* enable osd */
		if (manager->osd) {
			zynq_osd_disable_rue(manager->osd);

			/* set zorder(= id for now) */
			zynq_osd_layer_set_priority(plane->osd_layer,
					plane->id);
			/* FIXME: set global alpha for now */
			zynq_osd_layer_set_alpha(plane->osd_layer, 1,
					0xff);
			zynq_osd_layer_enable(plane->osd_layer);
			if (plane->priv) {
				/* set background color as black */
				zynq_osd_set_color(manager->osd, 0x0,
						0x0, 0x0);
				zynq_osd_enable(manager->osd);
			}

			zynq_osd_enable_rue(manager->osd);
		}

		break;
	default:
		/* disable/reset osd */
		if (manager->osd) {
			zynq_osd_disable_rue(manager->osd);

			zynq_osd_layer_set_dimension(plane->osd_layer,
					0, 0, 0, 0);
			zynq_osd_layer_disable(plane->osd_layer);
			if (plane->priv)
				zynq_osd_reset(manager->osd);

			zynq_osd_enable_rue(manager->osd);
		}

		/* reset vdma */
		dma_config.reset = 1;
		dmaengine_device_control(plane->vdma.chan,
				DMA_SLAVE_CONFIG,
				(unsigned long)&dma_config);

		/* stop vdma engine and release descriptors */
		dmaengine_terminate_all(plane->vdma.chan);
		break;
	}

out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}

/* apply mode to plane pipe */
void zynq_drm_plane_commit(struct drm_plane *base_plane)
{
	struct zynq_drm_plane *plane = to_zynq_plane(base_plane);
	struct dma_async_tx_descriptor *desc;
	uint32_t height = plane->vdma.dma_config.hsize;
	int pitch = plane->vdma.dma_config.stride;
	size_t offset;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);

	offset = plane->x * plane->bpp + plane->y * pitch;
	desc = dmaengine_prep_slave_single(plane->vdma.chan,
			plane->paddr + offset, height * pitch,
			DMA_MEM_TO_DEV, 0);
	if (!desc) {
		DRM_ERROR("failed to prepare DMA descriptor\n");
		goto out;
	}

	/* submit vdma desc */
	dmaengine_submit(desc);

	/* start vdma with new mode */
	dma_async_issue_pending(plane->vdma.chan);

out:
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
	int err_ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);

	obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!obj) {
		DRM_ERROR("failed to get a gem obj for fb\n");
		err_ret = -EINVAL;
		goto err_out;
	}

	plane->x = src_x;
	plane->y = src_y;
	plane->bpp = fb->bits_per_pixel / 8;
	plane->paddr = obj->paddr;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "h: %d(%d), v: %d(%d), paddr: %p\n",
			src_w, crtc_x, src_h, crtc_y, (void *)obj->paddr);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "bpp: %d\n", plane->bpp);

	/* configure vdma desc */
	plane->vdma.dma_config.hsize = src_w * plane->bpp;
	plane->vdma.dma_config.vsize = src_h;
	plane->vdma.dma_config.stride = fb->pitches[0];
	plane->vdma.dma_config.park = 1;
	plane->vdma.dma_config.park_frm = 0;

	dmaengine_device_control(plane->vdma.chan, DMA_SLAVE_CONFIG,
			(unsigned long)&plane->vdma.dma_config);

	/* set OSD dimensions */
	if (plane->manager->osd) {
		zynq_osd_disable_rue(plane->manager->osd);

		/* if a plane is private, it's for crtc */
		if (plane->priv)
			zynq_osd_set_dimension(plane->manager->osd,
					crtc_w, crtc_h);

		zynq_osd_layer_set_dimension(plane->osd_layer, crtc_x, crtc_y,
				src_w, src_h);

		zynq_osd_enable_rue(plane->manager->osd);
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return 0;

err_out:
	return err_ret;
}

/* update a plane. just call mode_set() with bit-shifted values */
static int zynq_drm_plane_update(struct drm_plane *base_plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	int err_ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	err_ret = zynq_drm_plane_mode_set(base_plane, crtc, fb, crtc_x, crtc_y,
			crtc_w, crtc_h, src_x >> 16, src_y >> 16,
			src_w >> 16, src_h >> 16);
	if (err_ret) {
		DRM_ERROR("failed to mode-set a plane\n");
		goto err_out;
	}

	/* make sure a plane is on */
	zynq_drm_plane_dpms(base_plane, DRM_MODE_DPMS_ON);
	/* apply the new fb addr */
	zynq_drm_plane_commit(base_plane);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return 0;

err_out:
	return err_ret;
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
		zynq_osd_layer_put(plane->osd_layer);
	}

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

/* create a plane */
static struct zynq_drm_plane *_zynq_drm_plane_create(
		struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs, bool priv)
{
	struct zynq_drm_plane *plane;
	struct zynq_drm_plane *err_ret;
	struct device *dev = manager->drm->dev;
	char dma_name[16];
	int i;
	int res;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	for (i = 0; i < manager->num_planes; i++) {
		if (!manager->planes[i]) {
			break;
		}
	}
	if (i >= manager->num_planes) {
		DRM_ERROR("failed to allocate plane\n");
		err_ret = ERR_PTR(-ENODEV);
		goto err_plane;
	}

	plane = devm_kzalloc(dev, sizeof(*plane), GFP_KERNEL);
	if (!plane) {
		DRM_ERROR("failed to allocate plane\n");
		err_ret = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}

	plane->priv = priv;
	plane->id = i;
	plane->dpms = DRM_MODE_DPMS_OFF;
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "plane->id: %d\n", plane->id);
	/* TODO: add to the manager's zorder list */

	snprintf(dma_name, sizeof(dma_name), "vdma%d", i);
	plane->vdma.chan = dma_request_slave_channel(dev, dma_name);
	if (!plane->vdma.chan) {
		DRM_ERROR("failed to request dma channel\n");
		err_ret = ERR_PTR(-ENODEV);
		goto err_dma_request;
	}

	/* create an OSD layer when OSD is available */
	if (manager->osd) {
		/* create an osd layer */
		plane->osd_layer = zynq_osd_layer_get(manager->osd);
		if (IS_ERR(plane->osd_layer)) {
			DRM_ERROR("failed to create a osd layer\n");
			err_ret = ERR_PTR(-ENODEV);
			plane->osd_layer = NULL;
			goto err_osd_layer;
		}

	
	}

	/* initialize drm plane */
	res = drm_plane_init(manager->drm, &plane->base, possible_crtcs,
			&zynq_drm_plane_funcs, zynq_drm_plane_formats,
			ARRAY_SIZE(zynq_drm_plane_formats), priv);
	if (res) {
		DRM_ERROR("failed to initialize plane\n");
		err_ret = ERR_PTR(res);
		goto err_init;
	}
	plane->manager = manager;
	manager->planes[i] = plane;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return plane;

err_init:
	if (plane->manager->osd) {
		zynq_osd_layer_disable(plane->osd_layer);
		zynq_osd_layer_put(plane->osd_layer);
	}
err_osd_layer:
	dma_release_channel(plane->vdma.chan);
err_dma_request:
err_alloc:
err_plane:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return err_ret;
}

/* create a private plane */
struct drm_plane *zynq_drm_plane_create_private(
		struct zynq_drm_plane_manager *manager,
		unsigned int possible_crtcs)
{
	struct zynq_drm_plane *plane;
	struct drm_plane *err_ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	plane = _zynq_drm_plane_create(manager, possible_crtcs, true);
	if (IS_ERR(plane)) {
		DRM_ERROR("failed to allocate a private plane\n");
		err_ret = ERR_PTR(-ENODEV);;
		goto err_out;
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return &plane->base;

err_out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return err_ret;
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
	int err_ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	/* find if there any available plane */
	for (i = 0; i < manager->num_planes; i++) {
		if (manager->planes[i])
			continue;
		manager->planes[i] = _zynq_drm_plane_create(manager,
				possible_crtcs, false);
		if (!manager->planes[i]) {
			DRM_ERROR("failed to allocate a plane\n");
			err_ret = PTR_ERR(manager->planes[i]);
			manager->planes[i] = NULL;
			goto err_out;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return 0;

err_out:
	zynq_drm_plane_destroy_planes(manager);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return err_ret;
}

struct zynq_drm_plane_manager *
zynq_drm_plane_probe_manager(struct drm_device *drm)
{
	struct zynq_drm_plane_manager *manager;
	struct zynq_drm_plane_manager *err_ret;
	struct device *dev = drm->dev;
	u32 prop;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	manager = devm_kzalloc(drm->dev, sizeof(*manager), GFP_KERNEL);
	if (!manager) {
		DRM_ERROR("failed to allocate a plane manager\n");
		err_ret = ERR_PTR(-ENOMEM);
		goto err_alloc;
	}
	manager->drm = drm;
	/* TODO: duplicate get_prop in osd, consider clean up */
	if (of_property_read_u32(dev->of_node, "xlnx,num-planes", &prop)) {
		DRM_ERROR("failed to get num of planes prop, set to 1\n");
		prop = 1;
	}
	manager->num_planes = prop;

	/* probe an OSD. proceed even if there's no OSD */
	manager->osd = zynq_osd_probe(dev, "xlnx,vosd");
	if (manager->osd)
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "OSD is probed\n");

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");

	return manager;

err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
	return err_ret;
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

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_PLANE, "\n");
}
