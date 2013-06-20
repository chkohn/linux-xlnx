/*
 * Xilinx DRM encoder driver for Zynq
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

#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/i2c/si570.h>
#include <linux/of.h>
#include <linux/of_i2c.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>

#include "../i2c/adv7511.h"

#include "zynq_drm_drv.h"

#include "zynq_vtc.h"

struct zynq_drm_encoder {
	struct drm_encoder_slave slave;	/* slave encoder */
	struct i2c_client *i2c_slave;	/* i2c slave encoder client */
	bool rgb;			/* rgb flag */
	struct i2c_client *si570;	/* si570 pixel clock */
	struct zynq_vtc *vtc;		/* video timing controller */
	int dpms;			/* dpms */
};

#define to_zynq_encoder(x)	container_of(x, struct zynq_drm_encoder, slave)

static const uint16_t adv7511_csc_ycbcr_to_rgb[] = {
	0x0734, 0x04ad, 0x0000, 0x1c1b,
	0x1ddc, 0x04ad, 0x1f24, 0x0135,
	0x0000, 0x04ad, 0x087c, 0x1b77,
};

/* set encoder dpms */
static void zynq_drm_encoder_dpms(struct drm_encoder *base_encoder, int dpms)
{
	struct zynq_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");

	encoder_slave = to_encoder_slave(base_encoder);
	encoder_sfuncs = encoder_slave->slave_funcs;
	encoder = to_zynq_encoder(encoder_slave);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "dpms: %d -> %d\n",
			encoder->dpms, dpms);

	if (encoder->dpms != dpms) {
		encoder->dpms = dpms;
		switch (dpms) {
		case DRM_MODE_DPMS_ON:
			zynq_vtc_enable(encoder->vtc);
			if (encoder_sfuncs->dpms)
				encoder_sfuncs->dpms(base_encoder, dpms);
			break;
		default:
			if (encoder_sfuncs->dpms)
				encoder_sfuncs->dpms(base_encoder, dpms);
			zynq_vtc_disable(encoder->vtc);
			zynq_vtc_reset(encoder->vtc);
			break;
		}
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
}

/* adjust a mode if needed */
static bool zynq_drm_encoder_mode_fixup(struct drm_encoder *base_encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs = NULL;
	bool ret = true;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");

	encoder_slave = to_encoder_slave(base_encoder);
	if (!encoder_slave) {
		ret = false;
		DRM_ERROR("failed to get encoder slave\n");
		goto out;
	}

	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->mode_fixup)
		ret = encoder_sfuncs->mode_fixup(base_encoder, mode,
				adjusted_mode);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");

out:
	return ret;
}

/* set mode to zynq encoder */
static void zynq_drm_encoder_mode_set(struct drm_encoder *base_encoder,
	struct drm_display_mode *mode, struct drm_display_mode *adjusted_mode)
{
	struct zynq_drm_encoder *encoder;
	struct zynq_vtc_sig_config vtc_sig_config;
	struct drm_device *dev = base_encoder->dev;
	struct drm_encoder_slave *encoder_slave;
	struct drm_encoder_slave_funcs *encoder_sfuncs;
	struct drm_connector *iter;
	struct drm_connector *connector = NULL;
	struct adv7511_video_config config;
	struct edid *edid;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "h: %d, v: %d, p clock: %d khz\n",
			mode->hdisplay, mode->vdisplay, mode->clock);

	encoder_slave = to_encoder_slave(base_encoder);
	encoder = to_zynq_encoder(encoder_slave);

	/* search for a connector for this encoder */
	/* assume there's only one connector for this encoder */
	list_for_each_entry(iter, &dev->mode_config.connector_list, head) {
		if (iter->encoder == base_encoder) {
			connector = iter;
			break;
		}
	}
	if (!connector) {
		DRM_ERROR("failed to find a connector\n");
		goto out;
	}

	/* set si570 pixel clock */
	set_frequency_si570(&encoder->si570->dev, mode->clock * 1000);

	/* set vtc */
	vtc_sig_config.htotal = mode->htotal;
	vtc_sig_config.hfrontporch_start = mode->hdisplay;
	vtc_sig_config.hsync_start = mode->hsync_start;
	vtc_sig_config.hbackporch_start = mode->hsync_end;
	vtc_sig_config.hactive_start = 0;

	vtc_sig_config.vtotal = mode->vtotal;
	vtc_sig_config.vfrontporch_start = mode->vdisplay;
	vtc_sig_config.vsync_start = mode->vsync_start;
	vtc_sig_config.vbackporch_start = mode->vsync_end;
	vtc_sig_config.vactive_start = 0;

	zynq_vtc_config_sig(encoder->vtc, &vtc_sig_config);

	if (connector->display_info.raw_edid) {
		edid = (struct edid *)connector->display_info.raw_edid;
		config.hdmi_mode = drm_detect_hdmi_monitor(edid);
	} else {
		config.hdmi_mode = false;
	}

	hdmi_avi_infoframe_init(&config.avi_infoframe);

	config.avi_infoframe.scan_mode = HDMI_SCAN_MODE_UNDERSCAN;

	if (encoder->rgb) {
		config.csc_enable = false;
		config.avi_infoframe.colorspace = HDMI_COLORSPACE_RGB;
	} else {
		config.csc_scaling_factor = ADV7511_CSC_SCALING_4;
		config.csc_coefficents = adv7511_csc_ycbcr_to_rgb;

		if ((connector->display_info.color_formats &
					DRM_COLOR_FORMAT_YCRCB422) &&
					config.hdmi_mode) {
			config.csc_enable = false;
			config.avi_infoframe.colorspace =
				HDMI_COLORSPACE_YUV422;
		} else {
			config.csc_enable = true;
			config.avi_infoframe.colorspace = HDMI_COLORSPACE_RGB;
		}
	}

	encoder_sfuncs = encoder_slave->slave_funcs;
	if (encoder_sfuncs->set_config)
		encoder_sfuncs->set_config(base_encoder, &config);

	if (encoder_sfuncs->mode_set)
		encoder_sfuncs->mode_set(base_encoder, mode, adjusted_mode);

out:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
}

/* apply mode to encoder pipe */
static void zynq_drm_encoder_commit(struct drm_encoder *base_encoder)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
	/* start encoder with new mode */
	zynq_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_ON);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
}

/* prepare encoder */
static void zynq_drm_encoder_prepare(struct drm_encoder *base_encoder)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
	zynq_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_OFF);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
}

/* get crtc */
static struct drm_crtc *zynq_drm_encoder_get_crtc(
		struct drm_encoder *base_encoder)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
	return base_encoder->crtc;
}

static struct drm_encoder_helper_funcs zynq_drm_encoder_helper_funcs = {
	.dpms		= zynq_drm_encoder_dpms,
	.mode_fixup	= zynq_drm_encoder_mode_fixup,
	.mode_set	= zynq_drm_encoder_mode_set,
	.prepare	= zynq_drm_encoder_prepare,
	.commit		= zynq_drm_encoder_commit,
	.get_crtc	= zynq_drm_encoder_get_crtc,
};

/* destroy encoder */
void zynq_drm_encoder_destroy(struct drm_encoder *base_encoder)
{
	struct zynq_drm_encoder *encoder;
	struct drm_encoder_slave *encoder_slave;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");

	encoder_slave = to_encoder_slave(base_encoder);
	encoder = to_zynq_encoder(encoder_slave);

	/* make sure encoder is off */
	zynq_drm_encoder_dpms(base_encoder, DRM_MODE_DPMS_OFF);

	drm_encoder_cleanup(base_encoder);
	put_device(&encoder->i2c_slave->dev);
	zynq_vtc_remove(encoder->vtc);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
}

static struct drm_encoder_funcs zynq_drm_encoder_funcs = {
	.destroy = zynq_drm_encoder_destroy,
};

/* create encoder */
struct drm_encoder *zynq_drm_encoder_create(struct drm_device *drm)
{
	struct zynq_drm_encoder *encoder;
	struct platform_device *pdev = drm->platformdev;
	struct device_node *slave_node;
	struct drm_i2c_encoder_driver *i2c_driver;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");

	encoder = devm_kzalloc(drm->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder) {
		DRM_ERROR("failed to allocate encoder\n");
		goto err_alloc;
	}
	encoder->dpms = DRM_MODE_DPMS_OFF;

	encoder->si570 = get_i2c_client_si570();
	if (!encoder->si570) {
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "failed to get si570 clock\n");
		goto err_si570;
	}

	encoder->vtc = zynq_vtc_probe("xlnx,vtc");
	if (!encoder->vtc) {
		DRM_ERROR("failed to probe video timing controller\n");
		goto err_vtc;
	}

	/* get slave encoder */
	slave_node = of_parse_phandle(pdev->dev.of_node, "encoder-slave", 0);
	if (!slave_node) {
		DRM_ERROR("failed to get encoder slave node\n");
		goto err_slave;
	}

	encoder->i2c_slave = of_find_i2c_device_by_node(slave_node);
	of_node_put(slave_node);
	if (!encoder->i2c_slave) {
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "failed to get encoder slv\n");
		goto err_slave;
	}

	/* initialize slave encoder */
	i2c_driver = to_drm_i2c_encoder_driver(encoder->i2c_slave->driver);
	if (i2c_driver->encoder_init(encoder->i2c_slave, drm,
				&encoder->slave)) {
		DRM_ERROR("failed to initialize encoder slave\n");
		goto err_slave_init;
	}

	if (!encoder->slave.slave_funcs) {
		DRM_ERROR("there's no encoder slave function\n");
		goto err_slave_func;
	}

	encoder->rgb = of_property_read_bool(pdev->dev.of_node, "adi,is-rgb");

	/* intiialize encoder */
	encoder->slave.base.possible_crtcs = 1;
	if (drm_encoder_init(drm, &encoder->slave.base, &zynq_drm_encoder_funcs,
			DRM_MODE_ENCODER_TMDS)) {
		DRM_ERROR("failed to initialize drm encoder\n");
		goto err_init;
	}

	drm_encoder_helper_add(&encoder->slave.base,
			&zynq_drm_encoder_helper_funcs);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");

	return &encoder->slave.base;

err_init:
err_slave_func:
err_slave_init:
	put_device(&encoder->i2c_slave->dev);
err_slave:
	zynq_vtc_remove(encoder->vtc);
err_vtc:
err_si570:
err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_ENCODER, "\n");
	return NULL;
}
