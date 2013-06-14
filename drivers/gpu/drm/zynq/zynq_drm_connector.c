/*
 * Xilinx DRM connector driver for Zynq
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

#include <linux/device.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>

#include "zynq_drm_drv.h"

struct zynq_drm_connector {
	struct drm_connector base;
	struct drm_encoder *encoder;
};

#define to_zynq_connector(x)	container_of(x, struct zynq_drm_connector, base)

/* get mode list */
static int zynq_drm_connector_get_modes(struct drm_connector *base_connector)
{
	struct zynq_drm_connector *connector =
		to_zynq_connector(base_connector);
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;
	int count = 0;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	kfree(base_connector->display_info.raw_edid);
	base_connector->display_info.raw_edid = NULL;

	if (encoder_sfuncs->get_modes)
		count += encoder_sfuncs->get_modes(encoder, base_connector);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	return count;
}

/* check if mode is valid */
static int zynq_drm_connector_mode_valid(struct drm_connector *base_connector,
	struct drm_display_mode *mode)
{
	int ret = MODE_OK;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	if (mode->clock > 165000) {
		ret = MODE_CLOCK_HIGH;
		goto out;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		ret = MODE_NO_INTERLACE;
		goto out;
	}

out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "ret: %d\n", ret);

	return ret;
}

/* find best encoder: return stored encoder */
static struct drm_encoder *zynq_drm_connector_best_encoder(
		struct drm_connector *base_connector)
{
	struct zynq_drm_connector *connector =
		to_zynq_connector(base_connector);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");
	return connector->encoder;
}

static struct drm_connector_helper_funcs zynq_drm_connector_helper_funcs = {
	.get_modes = zynq_drm_connector_get_modes,
	.mode_valid = zynq_drm_connector_mode_valid,
	.best_encoder = zynq_drm_connector_best_encoder,
};

static enum drm_connector_status zynq_drm_connector_detect(
	struct drm_connector *base_connector, bool force)
{
	struct zynq_drm_connector *connector =
		to_zynq_connector(base_connector);
	enum drm_connector_status status = connector_status_unknown;
	struct drm_encoder *encoder = connector->encoder;
	struct drm_encoder_slave *encoder_slave = to_encoder_slave(encoder);
	struct drm_encoder_slave_funcs *encoder_sfuncs =
		encoder_slave->slave_funcs;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	if (encoder_sfuncs->detect)
		status = encoder_sfuncs->detect(encoder, base_connector);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "status: %d\n", status);

	return status;
}

/* destroy connector */
void zynq_drm_connector_destroy(struct drm_connector *base_connector)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	drm_sysfs_connector_remove(base_connector);
	drm_connector_cleanup(base_connector);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");
}

static struct drm_connector_funcs zynq_drm_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = zynq_drm_connector_detect,
	.destroy = zynq_drm_connector_destroy,
};

/* create connector */
struct drm_connector *zynq_drm_connector_create(struct drm_device *drm,
		struct drm_encoder *base_encoder)
{
	struct zynq_drm_connector *connector;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	connector = devm_kzalloc(drm->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector) {
		DRM_ERROR("failed to allocate connector\n");
		goto err_alloc;
	}

	connector->base.polled = DRM_CONNECTOR_POLL_CONNECT |
				DRM_CONNECTOR_POLL_DISCONNECT;

	if (drm_connector_init(drm, &connector->base, &zynq_drm_connector_funcs,
			DRM_MODE_CONNECTOR_HDMIA)) {
		DRM_ERROR("failed to initialize connector\n");
		goto err_init;
	}

	drm_connector_helper_add(&connector->base,
			&zynq_drm_connector_helper_funcs);

	/* add sysfs entry for connector */
	if (drm_sysfs_connector_add(&connector->base)) {
		DRM_ERROR("failed to add to sysfs\n");
		goto err_sysfs;
	}

	/* connect connector and encoder */
	connector->base.encoder = base_encoder;
	if (drm_mode_connector_attach_encoder(&connector->base, base_encoder)) {
		DRM_ERROR("failed to attach connector to encoder\n");
		goto err_attach;
	}
	connector->encoder = base_encoder;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");

	return &connector->base;

err_attach:
	drm_sysfs_connector_remove(&connector->base);
err_sysfs:
	drm_connector_cleanup(&connector->base);
err_init:
err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_CONNECTOR, "\n");
	return NULL;
}
