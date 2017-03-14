/******************************************************************************
 *
 * Copyright (C) 2016,2017 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Use of the Software is limited solely to applications:
 * (a) running on a Xilinx device, or
 * (b) that interact with a Xilinx device through a bus or interconnect.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
******************************************************************************/

#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/gpio/consumer.h>

#include <drm/drm_crtc.h>
#include <drm/drm_gem_cma_helper.h>

#include "xilinx_drm_drv.h"
#include "xilinx_drm_fb.h"

#include "crtc/mixer/drm/xilinx_drm_mixer.h"

#include "crtc/mixer/hw/xilinx_mixer_regs.h"
#include "crtc/mixer/hw/xilinx_mixer_data.h"

#define COLOR_NAME_SIZE 10


struct color_fmt_tbl {
	char			name[COLOR_NAME_SIZE+1];
	xv_comm_color_fmt_id	fmt_id;
	u32			drm_format;
};

/*************************** STATIC DATA  ************************************/
static struct color_fmt_tbl color_table[] = {
	{"bgr888",    XVIDC_CSF_BGR,         DRM_FORMAT_BGR888},
	{"rgb888",    XVIDC_CSF_RGB,         DRM_FORMAT_RGB888},
	/* JPM - add to drm_fourcc.h for packed yuv444?? */
	/*{"yuv444",    XVIDC_CSF_YCRCB_444,   DRM_FORMAT_??????},*/
	{"yuv422",    XVIDC_CSF_YCRCB_422,   DRM_FORMAT_YUYV},
	{"nv12",      XVIDC_CSF_Y_CRCB8_420, DRM_FORMAT_NV12},
	{"nv16",      XVIDC_CSF_Y_CRCB8,     DRM_FORMAT_NV16},
	{"rgba8888",  XVIDC_CSF_RGBA8,       DRM_FORMAT_RGBA8888},
	{"abgr8888",  XVIDC_CSF_ABGR8,       DRM_FORMAT_ABGR8888},
	{"argb8888",  XVIDC_CSF_ARGB8,       DRM_FORMAT_ARGB8888},
	{"xbgr8888",  XVIDC_CSF_XBGR8,       DRM_FORMAT_XBGR8888},
};

static const struct of_device_id xv_mixer_match[] = {
	{.compatible = "xlnx,v-mix-1.00.a"},
	{/*end of table*/},
};

/*************************** PROTOTYPES **************************************/

static int
xilinx_drm_mixer_of_init_layer_data(struct device *dev,
				struct device_node *dev_node,
				char *layer_name,
				struct xv_mixer_layer_data *layer,
				uint32_t max_layer_width,
				struct xv_mixer_layer_data **drm_primary_layer);

static int
xilinx_drm_mixer_parse_dt_logo_data(struct device_node *node,
				struct xv_mixer *mixer_hw);

static int
xilinx_drm_mixer_parse_dt_bg_video_fmt(struct device_node *layer_node,
				struct xv_mixer *mixer_hw);

static irqreturn_t
xilinx_drm_mixer_intr_handler(int irq, void *data);


/************************* IMPLEMENTATIONS ***********************************/
struct xilinx_drm_mixer	*xilinx_drm_mixer_probe(struct device *dev,
				struct device_node *node,
				struct xilinx_drm_plane_manager *manager)
{

	struct xilinx_drm_mixer		*mixer;
	struct xv_mixer			*mixer_hw;
	char				layer_node_name[20] = {0};
	struct xv_mixer_layer_data	*layer_data;
	const struct of_device_id	*match;
	struct resource			res;
	int				ret;
	int				layer_idx;
	int				layer_cnt;
	int				i;


	match = of_match_node(xv_mixer_match, node);

	if (!match) {
		dev_err(dev, "Failed to match device node for mixer\n");
		return ERR_PTR(-ENODEV);
	}

	mixer = devm_kzalloc(dev, sizeof(struct xilinx_drm_mixer), GFP_KERNEL);
	if (!mixer)
		return ERR_PTR(-ENOMEM);

	mixer_hw = &(mixer->mixer_hw);

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "Failed to parse node memory address from dts "
			"for mixer\n");
		return ERR_PTR(ret);
	}

	mixer_hw->reg_base_addr = devm_ioremap_resource(dev, &res);
	if (IS_ERR(mixer_hw->reg_base_addr)) {
		dev_err(dev, "Failed to map io space into virt memory for"
			" mixer\n");
		return ERR_CAST(mixer_hw->reg_base_addr);
	}

	ret = of_property_read_u32(node, "xlnx,num-layers",
				   &(mixer_hw->max_layers));
	if (ret) {
		dev_err(dev, "Failed to get num of layers dts prop for mixer"
			" node\n");
		return ERR_PTR(-EINVAL);
	}

	if (mixer_hw->max_layers > XVMIX_MAX_SUPPORTED_LAYERS) {
		dev_err(dev, "Number of layers specified in device "
			"tree exceeds mixer capabilities\n");
		return ERR_PTR(-EINVAL);

	}

	/* establish some global defaults subject to override via dts */
	mixer_hw->intrpts_enabled = false;
	mixer_hw->logo_pixel_alpha_enabled = false;

	mixer_hw->logo_layer_enabled = of_property_read_bool(node,
							  "xlnx,logo-layer");

	/* Alloc num_layers + 1 for logo layer if enabled in dt */
	layer_cnt = mixer_hw->max_layers +
			(mixer_hw->logo_layer_enabled ? 1 : 0);

	layer_data = devm_kzalloc(dev,
				sizeof(struct xv_mixer_layer_data) * layer_cnt,
				GFP_KERNEL);

	if (layer_data) {
		mixer_hw->layer_cnt = layer_cnt;
	} else {
		dev_err(dev, "Out of mem for mixer layer data\n");
		return ERR_PTR(-ENOMEM);
	}

	mixer_hw->layer_data = layer_data;


	/* establish background layer video properties */
	ret = xilinx_drm_mixer_parse_dt_bg_video_fmt(node, mixer_hw);
	if (ret) {
		dev_err(dev, "Incomplete mixer video format in dt\n");
		return ERR_PTR(-EINVAL);
	}

	mixer->plane_manager = manager;
	mixer->drm_primary_layer = NULL;
	mixer->hw_master_layer = &(mixer_hw->layer_data[0]); 


	/* Parse out logo data from device tree */
	ret = xilinx_drm_mixer_parse_dt_logo_data(node, mixer_hw);
	if (ret) {
		dev_err(dev,
			"Missing req'd logo layer props from dts for mixer\n");
		return ERR_PTR(-EINVAL);
	}

	layer_idx = mixer_hw->logo_layer_enabled ? 2 : 1;
	for (i = 1; i <= (mixer_hw->max_layers - 1); i++, layer_idx++) {

		snprintf(layer_node_name, sizeof(layer_node_name),
			"layer_%d", i);

		ret =
		     xilinx_drm_mixer_of_init_layer_data(dev,
					node,
					layer_node_name,
					&(mixer_hw->layer_data[layer_idx]),
					mixer_hw->max_layer_width,
					&mixer->drm_primary_layer);

		if (ret)
		    return ERR_PTR(ret);

		if (!mixer_hw->layer_data[layer_idx].hw_config.is_streaming &&
			!mixer_hw->intrpts_enabled)
			mixer_hw->intrpts_enabled = true;

	}

	/* If none of the overlay layers were designated as the drm
	 * primary layer, default to the mixer's video0 layer as drm primary
	*/
	if (!mixer->drm_primary_layer)
		mixer->drm_primary_layer = mixer->hw_master_layer;

	/* request irq and obtain pixels-per-clock (ppc) property */
	if (mixer_hw->intrpts_enabled) {

		mixer_hw->irq = irq_of_parse_and_map(node, 0);

		if (mixer_hw->irq > 0) {
			ret = devm_request_irq(dev, mixer_hw->irq,
					xilinx_drm_mixer_intr_handler,
					IRQF_SHARED, "xilinx_mixer", mixer_hw);

			if (ret) {
				dev_err(dev,
					"Failed to request irq for mixer\n");
				return ERR_PTR(ret);
			}
		}

		ret = of_property_read_u32(node, "xlnx,ppc",
				   &(mixer_hw->ppc));

		if (ret) {
			dev_err(dev, "Failed to obtain xlnx,ppc property "
				"from mixer dts\n");
			return ERR_PTR(ret);
		}
	}

	/*Pull device out of reset */
	mixer_hw->reset_gpio = devm_gpiod_get_optional(dev,
						"xlnx,mixer-reset",
						GPIOD_OUT_LOW);
	if (IS_ERR(mixer_hw->reset_gpio)) {
		dev_err(dev, "No reset gpio info from dts for mixer\n");
		return ERR_PTR(-EINVAL);
	}

	gpiod_set_raw_value(mixer_hw->reset_gpio, 0x1);


	if (mixer_hw->intrpts_enabled)
		xilinx_mixer_intrpt_enable(mixer_hw);
	else
		xilinx_mixer_intrpt_disable(mixer_hw);


	/* Init all layers to inactive state in software. An update_plane()
	* call to our drm driver will change this to 'active' and permit the
	* layer to be enabled in hardware
	*/
	for (i = 0; i < mixer_hw->layer_cnt; i++) {
		layer_data = &(mixer_hw->layer_data[i]);
		mixer_layer_active(layer_data) = false;
	}

	xilinx_mixer_init(mixer_hw);

	return mixer;
}



static int xilinx_drm_mixer_of_init_layer_data(struct device *dev,
				struct device_node *node,
				char *layer_name,
				struct xv_mixer_layer_data *layer,
				uint32_t max_layer_width,
				struct xv_mixer_layer_data **drm_pri_layer)
{
	struct device_node *layer_node;
	const char *vformat;
	int ret = 0;

	layer_node = of_get_child_by_name(node, layer_name);


	if (!layer_node)
		return -1;

	/* Set default values */
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.is_streaming = false;
	layer->hw_config.max_width = max_layer_width;
	layer->hw_config.min_width = XVMIX_LAYER_WIDTH_MIN;
	layer->hw_config.min_height = XVMIX_LAYER_HEIGHT_MIN;
	layer->hw_config.vid_fmt = 0;
	layer->id = 0;

	ret = of_property_read_u32(layer_node, "xlnx,layer-id", &layer->id);

	if (ret ||
		layer->id < 1 ||
		layer->id > (XVMIX_MAX_SUPPORTED_LAYERS - 1)) {
		dev_err(dev, "Mixer layer id in dts is out of legal range." 
			" ID=%d.  Must be between %u and %u\n",
			layer->id, 1, (XVMIX_MAX_SUPPORTED_LAYERS - 1));
		ret = -EINVAL;
	}

	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);
	if (ret) {
		dev_err(dev, "Missing mixer layer video format in dts"
			" for layer id %d\n", layer->id);
		return -EINVAL;
	}


	ret = xilinx_drm_mixer_string_to_fmt(vformat,
					     &(layer->hw_config.vid_fmt));
	if (ret < 0) {
		dev_err(dev, "No matching video format for mixer"
			" layer %d in dts\n", layer->id);
		return -EINVAL;
	}

	mixer_layer_can_scale(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-scale");

	if (mixer_layer_can_scale(layer)) {
		ret = of_property_read_u32(layer_node, "xlnx,layer-width",
					&(layer->hw_config.max_width));
		if (ret) {
			dev_err(dev, "Mixer layer %d dts missing width prop."
				" Required for layer with scaling enabled\n",
				layer->id);
			return ret;
		}

		if (layer->hw_config.max_width > max_layer_width) {
			dev_err(dev, "Mixer layer %d width in dts exceeds mixer"
				" maximum active area width\n", layer->id);
			return -EINVAL;
		}
	}

	mixer_layer_can_alpha(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-alpha");

	mixer_layer_is_streaming(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-streaming");

	if (of_property_read_bool(layer_node, "xlnx,layer-primary")) {
		if(*drm_pri_layer) {
			dev_err(dev, "Ambiguous assignment of primary layer"
				" in mixer dts for layer %d.  Only one layer "
				" can be assigned as the primary layer\n",
				layer->id);
			return -EINVAL;
		}

		mixer_layer_can_scale(layer) = false;
		*drm_pri_layer = layer;
	}


	return 0;
}



int xilinx_drm_mixer_string_to_fmt(const char *color_fmt, u32 *output)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_table); i++) {
		if (strcmp(color_fmt, (const char *)color_table[i].name) == 0) {
			*output = color_table[i].fmt_id;
			return 0;
		}
	}

	return -EINVAL;
}

int xilinx_drm_mixer_fmt_to_drm_fmt(xv_comm_color_fmt_id id, u32 *output)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(color_table); i++) {
		if (id == color_table[i].fmt_id)
			*output = color_table[i].drm_format;
	}

	if (output)
		return 0;

	return -EINVAL;
}



int
xilinx_drm_mixer_set_layer_scale(struct xilinx_drm_plane *plane,
				uint64_t val)
{
	struct xv_mixer *mixer_hw = to_xv_mixer_hw(plane);
	struct xv_mixer_layer_data *layer = plane->mixer_layer;
	int ret;

	if (layer && layer->hw_config.can_scale) {
		if (val > XVMIX_SCALE_FACTOR_4X ||
			val < XVMIX_SCALE_FACTOR_1X) {
			DRM_ERROR("Property setting for mixer layer scale "
				  "exceeds legal values\n");
			return -EINVAL;
		}
		xilinx_drm_mixer_layer_disable(plane);
		ret = xilinx_mixer_set_layer_scaling(mixer_hw, layer->id, val);
		if (ret)
			return ret;

		xilinx_drm_mixer_layer_enable(plane);

		return 0;
	}

	return -ENODEV;
}

int
xilinx_drm_mixer_set_layer_alpha(struct xilinx_drm_plane *plane,
				uint64_t val)
{
	struct xv_mixer *mixer_hw = to_xv_mixer_hw(plane);
	struct xv_mixer_layer_data *layer = plane->mixer_layer;
	int ret;

	if (layer && layer->hw_config.can_alpha) {
		if (val > XVMIX_ALPHA_MAX || val < XVMIX_ALPHA_MIN) {
			DRM_ERROR("Property setting for mixer layer alpha "
				"exceeds legal values\n");
			return -EINVAL;
		}
		ret = xilinx_mixer_set_layer_alpha(mixer_hw, layer->id, val);
		if (ret)
			return ret;

		return 0;
	}
	return -EINVAL;
}



void
xilinx_drm_mixer_layer_disable(struct xilinx_drm_plane *plane)
{
	struct xv_mixer *mixer_hw;
	u32 layer_id;

	if (plane)
		mixer_hw = to_xv_mixer_hw(plane);
	else
		return;

	layer_id = plane->mixer_layer->id;
	if (layer_id < XVMIX_LAYER_MASTER  || layer_id > XVMIX_LAYER_LOGO)
		return;

	xilinx_mixer_layer_disable(mixer_hw, layer_id);

}

void
xilinx_drm_mixer_layer_enable(struct xilinx_drm_plane *plane)
{
	struct xv_mixer *mixer_hw;
	struct xv_mixer_layer_data *layer_data;
	u32 layer_id;

	if (plane)
		mixer_hw = to_xv_mixer_hw(plane);
	else
		return;

	layer_data = plane->mixer_layer;
	layer_id = layer_data->id;

	if (layer_id < XVMIX_LAYER_MASTER  || layer_id > XVMIX_LAYER_LOGO) {
		DRM_DEBUG_KMS("Attempt to activate invalid layer: %d\n",
			layer_id);
		return;
	}

	/* JPM TODO rpp if MASTER is not primary, we could disable streaming
	 * propery, effectively, turning it off
	*/
	if (layer_id == XVMIX_LAYER_MASTER &&
		!mixer_layer_is_streaming(layer_data)) {
		return;
	}

	xilinx_mixer_layer_enable(mixer_hw, layer_id);
}



int
xilinx_drm_mixer_set_layer_dimensions(struct xilinx_drm_plane *plane,
				u32 crtc_x, u32 crtc_y,
				u32 width, u32 height, u32 stride)
{
	
	/* JPM TODO make this all a bit more elegant*/
	struct xilinx_drm_mixer *mixer = plane->manager->mixer;
	struct xv_mixer *mixer_hw = to_xv_mixer_hw(plane);
	struct xv_mixer_layer_data *layer_data;
	xv_mixer_layer_id layer_id;
	int ret = 0;

	layer_data = plane->mixer_layer;
	layer_id = layer_data->id;

	/* JPM TODO this must ensure we are dealing with the drm_primary.
	* if drm_primary != master pointer then update both active_area
	* and then plane
	*/
	if (mixer->drm_primary_layer == layer_data) {

		/*xilinx_drm_mixer_layer_disable(plane);*/
		ret = xilinx_mixer_set_active_area(mixer_hw, width, height);
		if (ret)
			return ret;

		/*xilinx_drm_mixer_layer_enable(plane);*/
	}

	/* JPM TODO rpp this must check to ensure we aren't dealing with
	* MASTER or drm_primary layer */
	if (layer_id != XVMIX_LAYER_MASTER && layer_id < XVMIX_LAYER_ALL) {

		/* only disable plane if width or height is altered */
		if (mixer_layer_width(layer_data) != width ||
			mixer_layer_height(layer_data) != height)
			xilinx_drm_mixer_layer_disable(plane);

		ret = xilinx_mixer_set_layer_window(mixer_hw, layer_id,
						 crtc_x, crtc_y,
						 width, height, stride);

		if (ret)
			return ret;

		xilinx_drm_mixer_layer_enable(plane);

	}


	return ret;
}



struct xv_mixer_layer_data *
xilinx_drm_mixer_get_layer(struct xv_mixer *mixer_hw, xv_mixer_layer_id layer_id)
{
	return xilinx_mixer_get_layer_data(mixer_hw, layer_id);
}

void xilinx_drm_mixer_reset(struct xilinx_drm_mixer *mixer)
{
	struct xv_mixer *mixer_hw = &(mixer->mixer_hw);

	gpiod_set_raw_value(mixer_hw->reset_gpio, 0x0);

	udelay(1);

	gpiod_set_raw_value(mixer_hw->reset_gpio, 0x1);

	/* restore layer properties and bg color after reset */
	xilinx_mixer_set_bkg_col(mixer_hw,
		mixer_hw->bg_color, mixer_hw->bg_layer_bpc);

	if (mixer_hw->intrpts_enabled)
		xilinx_mixer_intrpt_enable(mixer_hw);

	xilinx_drm_plane_restore(mixer->plane_manager);
}

static int xilinx_drm_mixer_parse_dt_logo_data(struct device_node *node,
					struct xv_mixer *mixer_hw)
{

	int ret = 0;
	struct xv_mixer_layer_data *layer_data;
	struct device_node *logo_node;
	uint32_t max_width, max_height;

	/* read in logo data */
	if (mixer_hw->logo_layer_enabled) {

		logo_node = of_get_child_by_name(node, "logo");
		if (!logo_node) {
			DRM_ERROR("No logo node specified in device tree.\n");
			return -EINVAL;
		}

		layer_data = &(mixer_hw->layer_data[1]);

		/* set defaults for logo layer */
		layer_data->hw_config.min_height = XVMIX_LOGO_LAYER_HEIGHT_MIN;
		layer_data->hw_config.min_width = XVMIX_LOGO_LAYER_WIDTH_MIN;
		layer_data->hw_config.is_streaming = false;
		layer_data->hw_config.vid_fmt = XVIDC_CSF_RGB;
		layer_data->hw_config.can_alpha = true;
		layer_data->hw_config.can_scale = true;
		layer_data->layer_regs.buff_addr = 0;
		layer_data->id = XVMIX_LAYER_LOGO;

		ret  = of_property_read_u32(logo_node, "xlnx,logo-width",
					&max_width);

		if (ret) {
			DRM_ERROR("Failed to get logo width prop\n");
			return -EINVAL;
		}

		if (max_width > XVMIX_LOGO_LAYER_WIDTH_MAX ||
			max_width < XVMIX_LOGO_LAYER_WIDTH_MIN) {
			DRM_ERROR("Mixer logo layer width dimensions exceed "
				  "min/max limit of %d to %d\n",
				  XVMIX_LOGO_LAYER_WIDTH_MIN,
				  XVMIX_LOGO_LAYER_WIDTH_MAX);
			return -EINVAL;
		}

		layer_data->hw_config.max_width = max_width;
		mixer_hw->max_logo_layer_width =
			layer_data->hw_config.max_width;

		ret = of_property_read_u32(logo_node, "xlnx,logo-height",
					&max_height);

		if (ret) {
			DRM_ERROR("Failed to get logo height prop\n");
			return -EINVAL;
		}

		if (max_height > XVMIX_LOGO_LAYER_HEIGHT_MAX ||
			max_height < XVMIX_LOGO_LAYER_HEIGHT_MIN) {
			DRM_ERROR("Mixer logo layer height dimensions exceed "
				"min/max limit of %d to %d\n",
				XVMIX_LOGO_LAYER_HEIGHT_MIN,
				XVMIX_LOGO_LAYER_HEIGHT_MAX);
			return -EINVAL;
		}

		layer_data->hw_config.max_height = max_height;
		mixer_hw->max_logo_layer_height =
			layer_data->hw_config.max_height;

		mixer_hw->logo_color_key_enabled =
				of_property_read_bool(logo_node,
						      "xlnx,logo-transp");

		mixer_hw->logo_pixel_alpha_enabled =
			of_property_read_bool(logo_node,
				"xlnx,logo-pixel-alpha");

		if (mixer_hw->logo_pixel_alpha_enabled)
			layer_data->hw_config.vid_fmt = XVIDC_CSF_RGBA8;

	}
	return ret;
}



static int xilinx_drm_mixer_parse_dt_bg_video_fmt(struct device_node *node,
						struct xv_mixer *mixer_hw)
{

	struct device_node *layer_node;
	struct xv_mixer_layer_data *layer;
	const char *vformat;
	int ret = 0;

	layer_node = of_get_child_by_name(node, "layer_0");

	layer = &(mixer_hw->layer_data[0]);

	/* Set default values */
	layer->hw_config.can_alpha = false;
	layer->hw_config.can_scale = false;
	layer->hw_config.is_streaming = false;
	layer->hw_config.min_width = XVMIX_LAYER_WIDTH_MIN;
	layer->hw_config.min_height = XVMIX_LAYER_HEIGHT_MIN;


	ret = of_property_read_string(layer_node, "xlnx,vformat", &vformat);

	if (ret) {
		DRM_ERROR("Failed to get mixer video format. Read %s from "
			"dts\n", vformat);
		return -1;
	}

	mixer_layer_is_streaming(layer) =
		    of_property_read_bool(layer_node, "xlnx,layer-streaming");

	ret = of_property_read_u32(node, "xlnx,bpc",
				   &(mixer_hw->bg_layer_bpc));
	if (ret) {
		DRM_ERROR("Failed to get bits per component (bpc) prop\n");
		return -1;
	}

	ret = of_property_read_u32(layer_node, "xlnx,layer-width",
		&(mixer_hw->layer_data[0].hw_config.max_width));
	if (ret) {
		DRM_ERROR("Failed to get screen width prop\n");
		return -1;
	}

	/* set global max width for mixer which will, ultimately, set the
	*  limit for the crtc
*/
	mixer_hw->max_layer_width = layer->hw_config.max_width;


	ret = of_property_read_u32(layer_node, "xlnx,layer-height",
		&(layer->hw_config.max_height));
	if (ret) {
		DRM_ERROR("Failed to get screen height prop\n");
		return -1;
	}

	mixer_hw->max_layer_height = layer->hw_config.max_height;

	/*We'll use the first layer instance to store data of the master layer*/
	layer->id = XVMIX_LAYER_MASTER;

	ret = xilinx_drm_mixer_string_to_fmt(vformat,
				&(mixer_hw->layer_data[0].hw_config.vid_fmt));
	if (ret < 0) {
		DRM_ERROR("Invalid mixer video format in dts\n");
		return -1;
	}

	return ret;
}


int
xilinx_drm_mixer_mark_layer_active(struct xilinx_drm_plane *plane)
{

	if (!plane->mixer_layer)
		return -ENODEV;

	mixer_layer_active(plane->mixer_layer) = true;

	return 0;
}

int
xilinx_drm_mixer_mark_layer_inactive(struct xilinx_drm_plane *plane)
{

	if (!plane || !plane->mixer_layer)
		return -ENODEV;

	mixer_layer_active(plane->mixer_layer) = false;


	return 0;
}

int
xilinx_drm_mixer_update_logo_img(struct xilinx_drm_plane *plane,
				 struct drm_framebuffer *fb,
				 uint32_t src_w, uint32_t src_h)
{

	struct drm_gem_cma_object *buffer;
	struct xv_mixer_layer_data *logo_layer = plane->mixer_layer;
	size_t pixel_cnt = src_h * src_w;

	/* color comp defaults to offset in RG24 buffer */
	uint32_t pix_cmp_cnt;
	uint32_t logo_cmp_cnt;
	uint32_t layer_pixel_fmt = 0;
	bool per_pixel_alpha = false;

	uint32_t max_width = logo_layer->hw_config.max_width;
	uint32_t max_height = logo_layer->hw_config.max_height;
	uint32_t min_width = logo_layer->hw_config.min_width;
	uint32_t min_height = logo_layer->hw_config.min_height;

	u8 *r_data;
	u8 *g_data;
	u8 *b_data;
	u8 *a_data;
	size_t el_size = sizeof(u8);

	u8 *pixel_mem_data;

	int ret, i, j;

	/* ensure valid conditions for update */
	if (logo_layer->id != XVMIX_LAYER_LOGO)
		return 0;

	if (src_h > max_height || src_w > max_width ||
		src_h < min_height || src_w < min_width) {
		DRM_ERROR("Mixer logo/cursor layer dimensions illegal.  Max/min"
			  " permissible size of h:%u/%u x w:%u/%u\n",
			  max_height,
			  min_height,
			  max_width,
			  min_width);

		return -EINVAL;
	}

	ret = xilinx_drm_mixer_fmt_to_drm_fmt(logo_layer->hw_config.vid_fmt,
					      &layer_pixel_fmt);

	per_pixel_alpha =
		(mixer_layer_fmt(logo_layer) == XVIDC_CSF_RGBA8) ? true : false;

	r_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);
	g_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);
	b_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);

	if (per_pixel_alpha)
		a_data = kcalloc(pixel_cnt, el_size, GFP_KERNEL);

	if (!r_data || !g_data || !b_data || (per_pixel_alpha && !a_data)) {
		DRM_ERROR("Unable to allocate memory for logo layer data\n");
		ret = -ENOMEM;
		goto free;
	}

	pix_cmp_cnt = per_pixel_alpha ? 4 : 3;
	logo_cmp_cnt = pixel_cnt * pix_cmp_cnt;

	if (ret)
		return ret;

	if (fb->pixel_format != layer_pixel_fmt) {
		DRM_ERROR("Mixer logo/cursor layer video format does not match"
			" framebuffer video format\n");
		return -EINVAL;
	}

	buffer = xilinx_drm_fb_get_gem_obj(fb, 0);

	/* ensure buffer attributes have changed to indicate new logo
	 * has been created
	*/
	if ((phys_addr_t)buffer->vaddr == logo_layer->layer_regs.buff_addr &&
		src_w == logo_layer->layer_regs.width &&
		src_h == logo_layer->layer_regs.height)
		return 0;

	/* cache buffer address for future comparison */
	logo_layer->layer_regs.buff_addr = (phys_addr_t)buffer->vaddr;

	pixel_mem_data = (u8 *)(buffer->vaddr);

	for (i = 0, j = 0; j < pixel_cnt; j++) {

		if (per_pixel_alpha && a_data)
			a_data[j] = pixel_mem_data[i++];

		b_data[j] = pixel_mem_data[i++];
		g_data[j] = pixel_mem_data[i++];
		r_data[j] = pixel_mem_data[i++];
	}

	ret = xilinx_mixer_logo_load(to_xv_mixer_hw(plane),
				src_w, src_h,
				r_data, g_data, b_data,
				per_pixel_alpha ? a_data : NULL);

free:
	kfree(r_data);
	kfree(g_data);
	kfree(b_data);
	kfree(a_data);

	return ret;
}


static irqreturn_t xilinx_drm_mixer_intr_handler(int irq, void *data)
{
	struct xv_mixer *mixer = data;

	u32 intr = xilinx_mixer_get_intr_status(mixer);

	if (!intr)
		return IRQ_NONE;

	else if (mixer->intrpt_handler_fn)
		mixer->intrpt_handler_fn(mixer->intrpt_data);

	xilinx_mixer_clear_intr_status(mixer, intr);

	return IRQ_HANDLED;
}


void xilinx_drm_mixer_set_intr_handler(struct xilinx_drm_mixer *mixer,
				void (*intr_handler_fn)(void *),
				void *data)
{
	mixer->mixer_hw.intrpt_handler_fn = intr_handler_fn;
	mixer->mixer_hw.intrpt_data = data;
}
