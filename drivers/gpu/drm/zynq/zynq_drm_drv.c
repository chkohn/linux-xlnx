/*
 * Xilinx DRM KMS support for Zynq
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

#include "zynq_drm_connector.h"
#include "zynq_drm_crtc.h"
#include "zynq_drm_drv.h"
#include "zynq_drm_encoder.h"

#define DRIVER_NAME	"zynq_drm"
#define DRIVER_DESC	"Xilinx DRM KMS support for Zynq"
#define DRIVER_DATE	"20130509"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#if ZYNQ_KMS_DEBUG

int zynq_kms_debug_enabled = ZYNQ_KMS_DEBUG_ALL;
module_param_named(zynq_kms_debug, zynq_kms_debug_enabled, int, 0600);

static char *zynq_kms_type[] = {"DRV",
				"CRT",
				"PLA",
				"ENC",
				"CON",
				"CRE",
				"OSD",
				"RGB",
				"VTC"};

void zynq_drm_debug(int type, const char *func, int line, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if ((1 << type) & zynq_kms_debug_enabled) {
		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		pr_info("[%s]%s:%d %pV", zynq_kms_type[type], func, line, &vaf);

		va_end(args);
	}
}
#endif

struct zynq_drm_private {
	struct drm_device *drm;			/* drm device */
	struct drm_crtc *crtc;			/* crtc */
	struct drm_encoder *encoder;		/* encoder */
	struct drm_connector *connector;	/* connector */
	struct drm_fbdev_cma *fbdev;		/* cma fbdev */
	struct platform_device *pdev;		/* platform device */
};

struct zynq_drm_format_info {
	u32 fourcc;
};

static const struct zynq_drm_format_info zynq_drm_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_XRGB8888,
	},
};

/* get supported format info */
const struct zynq_drm_format_info *zynq_drm_format_get(u32 fourcc)
{
	const struct zynq_drm_format_info *info = NULL;
	unsigned int i;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	for (i = 0; i < ARRAY_SIZE(zynq_drm_format_infos); ++i) {
		if (zynq_drm_format_infos[i].fourcc == fourcc)
			info = &zynq_drm_format_infos[i];
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return info;
}

/* create a fb */
static struct drm_framebuffer *zynq_drm_fb_create(struct drm_device *drm,
		struct drm_file *file_priv, struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct zynq_drm_format_info *format;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	format = zynq_drm_format_get(mode_cmd->pixel_format);
	if (format == NULL) {
		DRM_ERROR("unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return drm_fb_cma_create(drm, file_priv, mode_cmd);
}

/* poll changed handler */
static void zynq_drm_output_poll_changed(struct drm_device *drm)
{
	struct zynq_drm_private *private = drm->dev_private;
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	drm_fbdev_cma_hotplug_event(private->fbdev);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
}

static const struct drm_mode_config_funcs zynq_drm_mode_config_funcs = {
	.fb_create = zynq_drm_fb_create,
	.output_poll_changed = zynq_drm_output_poll_changed,
};

/* enable vblank */
static int zynq_drm_enable_vblank(struct drm_device *drm, int crtc)
{
	struct zynq_drm_private *private = drm->dev_private;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	zynq_drm_crtc_enable_vblank(private->crtc);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;
}

/* disable vblank */
static void zynq_drm_disable_vblank(struct drm_device *drm, int crtc)
{
	struct zynq_drm_private *private = drm->dev_private;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	zynq_drm_crtc_disable_vblank(private->crtc);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
}

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
	struct platform_device *pdev = drm->platformdev;
	int err;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	private = devm_kzalloc(drm->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate private\n");
		err = -ENOMEM;
		goto err_alloc;
	}

	drm_mode_config_init(drm);

	/* set up mode config for zynq */
	zynq_drm_mode_config_init(drm);

	/* create a zynq crtc */
	private->crtc = zynq_drm_crtc_create(drm);
	if (IS_ERR_OR_NULL(private->crtc)) {
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "failed to create zynq crtc\n");
		err = PTR_ERR(private->crtc);
		goto err_crtc;
	}

	/* create a zynq encoder */
	private->encoder = zynq_drm_encoder_create(drm);
	if (IS_ERR_OR_NULL(private->encoder)) {
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "failed to create zynq encoder\n");
		err = PTR_ERR(private->encoder);
		goto err_encoder;
	}

	/* create a zynq connector */
	private->connector = zynq_drm_connector_create(drm, private->encoder);
	if (!private->connector) {
		ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV,
				"failed to create zynq connector\n");
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

	/* initialize zynq cma framebuffer */
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

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;

err_fbdev:
	drm_vblank_cleanup(drm);
err_vblank:
	zynq_drm_connector_destroy(private->connector);
err_connector:
	zynq_drm_encoder_destroy(private->encoder);
err_encoder:
	zynq_drm_crtc_destroy(private->crtc);
err_crtc:
	drm_mode_config_cleanup(drm);
err_alloc:
	if (err == -EPROBE_DEFER) {
		DRM_INFO("load() is defered & will be called again\n");
	}
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	return err;
}

/* unload zynq drm */
static int zynq_drm_unload(struct drm_device *drm)
{
	struct zynq_drm_private *private = drm->dev_private;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	drm_vblank_cleanup(drm);

	drm_kms_helper_poll_fini(drm);

	drm_fbdev_cma_fini(private->fbdev);

	drm_mode_config_cleanup(drm);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;
}

/* preclose */
static void zynq_drm_preclose(struct drm_device *drm, struct drm_file *file)
{
	struct zynq_drm_private *private = drm->dev_private;

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	/* cancel pending page flip request */
	zynq_drm_crtc_cancel_page_flip(private->crtc, file);
	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
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
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load = zynq_drm_load,
	.unload = zynq_drm_unload,
	.preclose = zynq_drm_preclose,
	.lastclose = zynq_drm_lastclose,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = zynq_drm_enable_vblank,
	.disable_vblank = zynq_drm_disable_vblank,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_cma_dmabuf_export,
	.gem_prime_import = drm_gem_cma_dmabuf_import,
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

#if defined (CONFIG_PM_SLEEP) || defined (CONFIG_PM_RUNTIME)
/* suspend zynq drm */
static int zynq_drm_pm_suspend(struct device *dev)
{
	struct zynq_drm_private *private = dev_get_drvdata(dev);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");
	
	drm_kms_helper_poll_disable(private->drm);
	drm_helper_connector_dpms(private->connector, DRM_MODE_DPMS_SUSPEND);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;
}

/* resume zynq drm */
static int zynq_drm_pm_resume(struct device *dev)
{
	struct zynq_drm_private *private = dev_get_drvdata(dev);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	drm_helper_connector_dpms(private->connector, DRM_MODE_DPMS_ON);
	drm_kms_helper_poll_enable(private->drm);

	ZYNQ_DEBUG_KMS(ZYNQ_KMS_DRV, "\n");

	return 0;
}
#endif

static const struct dev_pm_ops zynq_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(zynq_drm_pm_suspend, zynq_drm_pm_resume)
	SET_RUNTIME_PM_OPS(zynq_drm_pm_suspend, zynq_drm_pm_resume, NULL)
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
		.pm	= &zynq_drm_pm_ops,
		.of_match_table = zynq_drm_of_match,
	},
};

module_platform_driver(zynq_drm_private_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx DRM KMS Driver");
MODULE_LICENSE("GPL v2");
