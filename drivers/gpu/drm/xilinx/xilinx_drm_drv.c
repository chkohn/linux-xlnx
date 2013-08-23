/*
 * Xilinx DRM KMS support for Xilinx
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
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "xilinx_drm_connector.h"
#include "xilinx_drm_crtc.h"
#include "xilinx_drm_drv.h"
#include "xilinx_drm_encoder.h"

#define DRIVER_NAME	"xilinx_drm"
#define DRIVER_DESC	"Xilinx DRM KMS support for Xilinx"
#define DRIVER_DATE	"20130509"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#if XILINX_KMS_DEBUG

int xilinx_kms_debug_enabled = XILINX_KMS_DEBUG_ALL;
module_param_named(xilinx_kms_debug, xilinx_kms_debug_enabled, int, 0600);

static char *xilinx_kms_type[] = {"DRV",
				"CRT",
				"PLA",
				"ENC",
				"CON",
				"CRE",
				"OSD",
				"RGB",
				"VTC"};

void xilinx_drm_debug(int type, const char *func, int line,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if ((1 << type) & xilinx_kms_debug_enabled) {
		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		pr_info("[%s]%s:%d %pV", xilinx_kms_type[type], func, line,
				&vaf);

		va_end(args);
	}
}
#endif

struct xilinx_drm_private {
	struct drm_device *drm;			/* drm device */
	struct drm_crtc *crtc;			/* crtc */
	struct drm_encoder *encoder;		/* encoder */
	struct drm_connector *connector;	/* connector */
	struct drm_fbdev_cma *fbdev;		/* cma fbdev */
	struct platform_device *pdev;		/* platform device */
};

struct xilinx_drm_format_info {
	u32 fourcc;
};

static const struct xilinx_drm_format_info xilinx_drm_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_XRGB8888,
	},
};

/* get supported format info */
const struct xilinx_drm_format_info *xilinx_drm_format_get(u32 fourcc)
{
	const struct xilinx_drm_format_info *info = NULL;
	unsigned int i;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	for (i = 0; i < ARRAY_SIZE(xilinx_drm_format_infos); ++i) {
		if (xilinx_drm_format_infos[i].fourcc == fourcc)
			info = &xilinx_drm_format_infos[i];
	}

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return info;
}

/* create a fb */
static struct drm_framebuffer *xilinx_drm_fb_create(struct drm_device *drm,
		struct drm_file *file_priv, struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct xilinx_drm_format_info *format;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	format = xilinx_drm_format_get(mode_cmd->pixel_format);
	if (format == NULL) {
		DRM_ERROR("unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return drm_fb_cma_create(drm, file_priv, mode_cmd);
}

/* poll changed handler */
static void xilinx_drm_output_poll_changed(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	drm_fbdev_cma_hotplug_event(private->fbdev);
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
}

static const struct drm_mode_config_funcs xilinx_drm_mode_config_funcs = {
	.fb_create = xilinx_drm_fb_create,
	.output_poll_changed = xilinx_drm_output_poll_changed,
};

/* enable vblank */
static int xilinx_drm_enable_vblank(struct drm_device *drm, int crtc)
{
	struct xilinx_drm_private *private = drm->dev_private;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	xilinx_drm_crtc_enable_vblank(private->crtc);
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return 0;
}

/* disable vblank */
static void xilinx_drm_disable_vblank(struct drm_device *drm, int crtc)
{
	struct xilinx_drm_private *private = drm->dev_private;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	xilinx_drm_crtc_disable_vblank(private->crtc);
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
}

/* initialize mode config */
static void xilinx_drm_mode_config_init(struct drm_device *drm)
{
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	drm->mode_config.funcs = &xilinx_drm_mode_config_funcs;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
}

/* load xilinx drm */
static int xilinx_drm_load(struct drm_device *drm, unsigned long flags)
{
	struct xilinx_drm_private *private;
	struct platform_device *pdev = drm->platformdev;
	int err;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	private = devm_kzalloc(drm->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate private\n");
		err = -ENOMEM;
		goto err_alloc;
	}

	drm_mode_config_init(drm);

	/* set up mode config for xilinx */
	xilinx_drm_mode_config_init(drm);

	/* create a xilinx crtc */
	private->crtc = xilinx_drm_crtc_create(drm);
	if (IS_ERR_OR_NULL(private->crtc)) {
		XILINX_DEBUG_KMS(XILINX_KMS_DRV,
				"failed to create xilinx crtc\n");
		err = PTR_ERR(private->crtc);
		goto err_crtc;
	}

	/* create a xilinx encoder */
	private->encoder = xilinx_drm_encoder_create(drm);
	if (IS_ERR_OR_NULL(private->encoder)) {
		XILINX_DEBUG_KMS(XILINX_KMS_DRV,
				"failed to create xilinx encoder\n");
		err = PTR_ERR(private->encoder);
		goto err_encoder;
	}

	/* create a xilinx connector */
	private->connector = xilinx_drm_connector_create(drm, private->encoder);
	if (!private->connector) {
		XILINX_DEBUG_KMS(XILINX_KMS_DRV,
				"failed to create xilinx connector\n");
		err = -EPROBE_DEFER;
		goto err_connector;
	}

	err = drm_vblank_init(drm, 1);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto err_vblank;
	}

	/* enable irq to enable vblank feature */
	drm->irq_enabled = 1;

	/* allow disable vblank */
	drm->vblank_disable_allowed = 1;

	/* initialize xilinx cma framebuffer */
	private->fbdev = drm_fbdev_cma_init(drm, 32, 1, 1);
	if (IS_ERR_OR_NULL(private->fbdev)) {
		DRM_ERROR("failed to initialize drm cma fbdev\n");
		err = PTR_ERR(private->fbdev);
		goto err_fbdev;
	}

	drm->dev_private = private;
	private->drm = drm;

	drm_kms_helper_poll_init(drm);

	drm_helper_disable_unused_functions(drm);

	platform_set_drvdata(pdev, private);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return 0;

err_fbdev:
	drm_vblank_cleanup(drm);
err_vblank:
	xilinx_drm_connector_destroy(private->connector);
err_connector:
	xilinx_drm_encoder_destroy(private->encoder);
err_encoder:
	xilinx_drm_crtc_destroy(private->crtc);
err_crtc:
	drm_mode_config_cleanup(drm);
err_alloc:
	if (err == -EPROBE_DEFER)
		DRM_INFO("load() is defered & will be called again\n");
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	return err;
}

/* unload xilinx drm */
static int xilinx_drm_unload(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	drm_vblank_cleanup(drm);

	drm_kms_helper_poll_fini(drm);

	drm_fbdev_cma_fini(private->fbdev);

	drm_mode_config_cleanup(drm);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return 0;
}

/* preclose */
static void xilinx_drm_preclose(struct drm_device *drm, struct drm_file *file)
{
	struct xilinx_drm_private *private = drm->dev_private;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	/* cancel pending page flip request */
	xilinx_drm_crtc_cancel_page_flip(private->crtc, file);
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
}

/* restore the default mode when xilinx drm is released */
static void xilinx_drm_lastclose(struct drm_device *drm)
{
	struct xilinx_drm_private *private = drm->dev_private;

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	drm_fbdev_cma_restore_mode(private->fbdev);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
}

static const struct file_operations xilinx_drm_fops = {
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

static struct drm_driver xilinx_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load = xilinx_drm_load,
	.unload = xilinx_drm_unload,
	.preclose = xilinx_drm_preclose,
	.lastclose = xilinx_drm_lastclose,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = xilinx_drm_enable_vblank,
	.disable_vblank = xilinx_drm_disable_vblank,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_cma_dmabuf_export,
	.gem_prime_import = drm_gem_cma_dmabuf_import,
	.gem_free_object = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_cma_dumb_destroy,

	.fops = &xilinx_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM_RUNTIME)
/* suspend xilinx drm */
static int xilinx_drm_pm_suspend(struct device *dev)
{
	struct xilinx_drm_private *private = dev_get_drvdata(dev);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	drm_kms_helper_poll_disable(private->drm);
	drm_helper_connector_dpms(private->connector, DRM_MODE_DPMS_SUSPEND);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return 0;
}

/* resume xilinx drm */
static int xilinx_drm_pm_resume(struct device *dev)
{
	struct xilinx_drm_private *private = dev_get_drvdata(dev);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	drm_helper_connector_dpms(private->connector, DRM_MODE_DPMS_ON);
	drm_kms_helper_poll_enable(private->drm);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	return 0;
}
#endif

static const struct dev_pm_ops xilinx_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xilinx_drm_pm_suspend, xilinx_drm_pm_resume)
	SET_RUNTIME_PM_OPS(xilinx_drm_pm_suspend, xilinx_drm_pm_resume, NULL)
};

/* init xilinx drm platform */
static int xilinx_drm_platform_probe(struct platform_device *pdev)
{
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	return drm_platform_init(&xilinx_drm_driver, pdev);
}

/* exit xilinx drm platform */
static int xilinx_drm_platform_remove(struct platform_device *pdev)
{
	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");

	drm_platform_exit(&xilinx_drm_driver, pdev);

	XILINX_DEBUG_KMS(XILINX_KMS_DRV, "\n");
	return 0;
}

static const struct of_device_id xilinx_drm_of_match[] = {
	{ .compatible = "xlnx,drm", },
	{},
};
MODULE_DEVICE_TABLE(of, xilinx_drm_of_match);

static struct platform_driver xilinx_drm_private_driver = {
	.probe		= xilinx_drm_platform_probe,
	.remove		= xilinx_drm_platform_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "xilinx-drm",
		.pm	= &xilinx_drm_pm_ops,
		.of_match_table = xilinx_drm_of_match,
	},
};

module_platform_driver(xilinx_drm_private_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DRM KMS Driver");
MODULE_LICENSE("GPL v2");
