/*
 * Xilinx DRM KMS support for Zynq
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

#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_crtc_helper.h>

#include "zynq_drm_drv.h"
#include "zynq_drm_crtc.h"
#include "zynq_drm_encoder.h"
#include "zynq_drm_connector.h"

#define DRIVER_NAME	"zynq_drm"
#define DRIVER_DESC	"Xilinx DRM KMS support for Zynq"
#define DRIVER_DATE	"20130509"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

struct zynq_drm_private {
	struct drm_crtc *crtc;			/* crtc */
	struct drm_encoder *encoder;		/* encoder */
	struct drm_connector *connector;	/* connector */
	struct drm_fbdev_cma *fbdev;		/* cma fbdev */
	struct platform_device *pdev;		/* platform device */
};

static void zynq_drm_output_poll_changed(struct drm_device *drm)
{
	struct zynq_drm_private *private = drm->dev_private;
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	if (private && private->fbdev)
		drm_fbdev_cma_hotplug_event(private->fbdev);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
}

static const struct drm_mode_config_funcs zynq_drm_mode_config_funcs = {
	.fb_create = drm_fb_cma_create,
	.output_poll_changed = zynq_drm_output_poll_changed,
};

/* initialize mode config */
static void zynq_drm_mode_config_init(struct drm_device *drm)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	drm->mode_config.funcs = &zynq_drm_mode_config_funcs;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
}

/* load zynq drm */
static int zynq_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct zynq_drm_private *private;
	int err;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	private = kzalloc(sizeof(struct zynq_drm_private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate private\n");
		err = -ENOMEM;
		goto err_alloc;
	}

	drm_mode_config_init(drm);

	drm_kms_helper_poll_init(drm);

	/* set up mode config for zynq */
	zynq_drm_mode_config_init(drm);

	/* create a zynq crtc */
	private->crtc = zynq_drm_crtc_create(drm);
	if (!private->crtc) {
		DRM_ERROR("failed to create xilinx crtc\n");
		err = -ENODEV;
		goto err_crtc;
	}

	/* create a zynq encoder */
	private->encoder = zynq_drm_encoder_create(drm);
	if (!private->encoder) {
		DRM_ERROR("failed to create xilinx encoder\n");
		err = -ENODEV;
		goto err_encoder;
	}

	/* create a zynq connector */
	private->connector = zynq_drm_connector_create(drm, private->encoder);
	if (!private->connector) {
		DRM_ERROR("failed to create xilinx connector\n");
		err = -ENODEV;
		goto err_connector;
	}

	/* initialize zynq cma framebuffer */
	private->fbdev = drm_fbdev_cma_init(drm, 32, 1, 1);
	if (IS_ERR(private->fbdev)) {
		DRM_ERROR("failed to initialize drm cma fbdev\n");
		err = PTR_ERR(private->fbdev);
		goto err_fbdev;
	}

	drm->dev_private = private;

	/* call hotplug event to initialize pipelines */
	drm_fbdev_cma_hotplug_event(private->fbdev);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;

err_fbdev:
	zynq_drm_connector_destroy(private->connector);
err_connector:
	zynq_drm_encoder_destroy(private->encoder);
err_encoder:
	zynq_drm_crtc_destroy(private->crtc);
err_crtc:
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);
	kfree(private);
err_alloc:
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	return err;
}

/* unload zynq drm */
static int zynq_drm_unload(struct drm_device *drm)
{
	struct zynq_drm_private *private;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	private = drm->dev_private;

	drm_fbdev_cma_fini(private->fbdev);

	drm_kms_helper_poll_fini(drm);

	drm_mode_config_cleanup(drm);

	kfree(private);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;
}

/* restore the default mode when zynq drm is released */
static void zynq_drm_lastclose(struct drm_device *drm)
{
	struct zynq_drm_private *private = drm->dev_private;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	drm_fbdev_cma_restore_mode(private->fbdev);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
}

static const struct file_operations zynq_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.fasync = drm_fasync,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver zynq_drm_driver = {
	.driver_features = DRIVER_BUS_PLATFORM | DRIVER_MODESET | DRIVER_GEM,
	.load = zynq_drm_load,
	.unload = zynq_drm_unload,
	.lastclose = zynq_drm_lastclose,

	.gem_free_object = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_cma_dumb_destroy,

	.fops = &zynq_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

/* init zynq drm platform */
static int zynq_drm_platform_probe(struct platform_device *pdev)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	return drm_platform_init(&zynq_drm_driver, pdev);
}

/* exit zynq drm platform */
static int zynq_drm_platform_remove(struct platform_device *pdev)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	drm_platform_exit(&zynq_drm_driver, pdev);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	return 0;
}

static const struct of_device_id zynq_drm_of_match[] = {
	{ .compatible = "xlnx,zynq_drm", },
	{},
};
MODULE_DEVICE_TABLE(of, zynq_drm_of_match);

static struct platform_driver zynq_drm_private_driver = {
	.probe		= zynq_drm_platform_probe,
	.remove		= zynq_drm_platform_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "zynq-drm",
		.of_match_table = zynq_drm_of_match,
	},
};

#if ZYNQ_KMS_DEBUG
int zynq_kms_debug_enabled = ZYNQ_KMS_DEBUG_ALL;
module_param_named(zynq_kms_debug, zynq_kms_debug_enabled, int, 0600);
#endif

/* init zynq drm module */
static int __init zynq_drm_init(void)
{
	int ret;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	ret = platform_driver_register(&zynq_drm_private_driver);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return ret;
}

#ifdef MODULE
/* exit zynq drm module */
static void __exit zynq_drm_exit(void)
{
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	platform_driver_unregister(&zynq_drm_private_driver);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
}

/* XXX: with module_init() zynq_drm is loaded before si570 driver */
module_init(zynq_drm_init);
module_exit(zynq_drm_exit);
#else
late_initcall(zynq_drm_init);
#endif

MODULE_AUTHOR("hyun woo kwon, Xilinx, Inc. <hyunk@xilinx.com>");
MODULE_DESCRIPTION("Xilinx DRM KMS Driver");
MODULE_LICENSE("GPL");
