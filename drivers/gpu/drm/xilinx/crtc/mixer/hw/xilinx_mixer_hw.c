/******************************************************************************
 *
 * Copyright (C) 2016 Xilinx, Inc.  All rights reserved.
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

#include "crtc/mixer/hw/xilinx_mixer_regs.h"
#include "crtc/mixer/hw/xilinx_mixer_data.h"


/************************** Constant Definitions *****************************/
#define XVMIX_MASK_ENABLE_ALL_LAYERS    (0x01FF)
#define XVMIX_MASK_DISABLE_ALL_LAYERS   (0)
#define XVMIX_REG_OFFSET                (8)


/************************** Function Prototypes ******************************/
static int is_window_valid(struct xv_mixer *mixer,
			u32 new_x_pos, u32 new_y_pos,
			u32 width, u32 height,
			xv_mixer_scale_factor scale);

/******************************************************************************
* Initializes the core instance.
* Sets initial state of mixer primary video layer and max height/width
* settings which should be retrieved from the device tree.
******************************************************************************/

void xilinx_mixer_init(struct xv_mixer *mixer)
{

	int i;
	u32 init_rgb_bg_color = 0xFF0000;
	xv_mixer_layer_id layer_id;
	struct xv_mixer_layer_data *layer_data;

	layer_data = xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	xilinx_mixer_layer_disable(mixer, XVMIX_LAYER_ALL);

	xilinx_mixer_set_active_area(mixer,
				     layer_data->hw_config.max_width,
				     layer_data->hw_config.max_height);

		reg_writel(mixer->reg_base_addr,
		XV_MIX_CTRL_ADDR_HWREG_VIDEO_FORMAT_DATA, 2);

	/* default to blue */
	xilinx_mixer_set_bkg_col(mixer, init_rgb_bg_color);

	for (i = 0; i <= mixer->layer_cnt; i++) {

		layer_id = mixer->layer_data[i].id;
		layer_data = &(mixer->layer_data[i]);

		if (layer_id == XVMIX_LAYER_MASTER)
			continue;

		xilinx_mixer_set_layer_window(mixer, layer_id, 0, 0,
					XVMIX_LAYER_WIDTH_MIN,
					XVMIX_LAYER_HEIGHT_MIN,
					0);

		if (mixer_layer_can_scale(layer_data))
			xilinx_mixer_set_layer_scaling(mixer, layer_id, 0);

		if (mixer_layer_can_alpha(layer_data))
			xilinx_mixer_set_layer_alpha(mixer,
						layer_id,
						XVMIX_ALPHA_MAX);

	}
}


/******************************************************************************
* Enables interrupts in the core
******************************************************************************/
void xilinx_mixer_intrpt_enable(struct xv_mixer *mixer)
{

	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u32 curr_val =  reg_readl(reg_base_addr, XV_MIX_CTRL_ADDR_IER);

	/* Enable Interrupts */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_IER,
	curr_val | XVMIX_IRQ_DONE_MASK);

	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_GIE, 0x1);

}

/******************************************************************************
* Disables interrupts in the core
******************************************************************************/
void xilinx_mixer_intrpt_disable(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u32 curr_val =  reg_readl(reg_base_addr, XV_MIX_CTRL_ADDR_IER);

	/* Disable Interrupts */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_IER,
	curr_val & (~XVMIX_IRQ_DONE_MASK));

	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_GIE, 0);
}

/******************************************************************************
* Starts the core instance
******************************************************************************/
/* JPM TODO consider adding boolean param to indicate if free-running mode is
 * desired.  Right now, defaulting to free running mode
*/
void xilinx_mixer_start(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u32 curr_val;

	curr_val = reg_readl(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL) & 0x80;
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL, curr_val | 0x81);
}

/******************************************************************************
* Stops the core instance
******************************************************************************/
void xilinx_mixer_stop(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;

	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_AP_CTRL, 0);
}

/******************************************************************************
* Validates if the requested window is within the frame boundary
******************************************************************************/
static int is_window_valid(struct xv_mixer *mixer,
			u32 new_x_pos, u32 new_y_pos, u32 width, u32 height,
			xv_mixer_scale_factor scale)
{
	struct xv_mixer_layer_data *master_layer;
	int scale_factor[3] = {1, 2, 4};

	master_layer = xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	/* Check if window scale factor is set */
	if (scale < XVMIX_SCALE_FACTOR_NOT_SUPPORTED) {
		/* update window per scale factor before validating */
		width  *= scale_factor[scale];
		height *= scale_factor[scale];
	  }

	/* verify overlay falls within currently active background area */
	if ((new_x_pos >= 0) && (new_y_pos >= 0) &&
		((new_x_pos + width)  <= master_layer->layer_regs.width) &&
		((new_y_pos + height) <= master_layer->layer_regs.height)) {

		return 0;
	}
	return -EINVAL;
}

/******************************************************************************
* Configures the mixer input stream
******************************************************************************/
int xilinx_mixer_set_active_area(struct xv_mixer *mixer,
				u32 hactive, u32 vactive)
{
	struct xv_mixer_layer_data *ld =
		xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_MASTER);

	void __iomem *reg_base_addr = mixer->reg_base_addr;

	if (hactive > ld->hw_config.max_width ||
	   vactive > ld->hw_config.max_height)
	return -EINVAL;

	/* set resolution */
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_HWREG_HEIGHT_DATA, vactive);
	reg_writel(reg_base_addr, XV_MIX_CTRL_ADDR_HWREG_WIDTH_DATA, hactive);

	ld->layer_regs.width  = hactive;
	ld->layer_regs.height = vactive;

	return 0;
}

/******************************************************************************
* Enables the specified layer of the core instance
******************************************************************************/
void xilinx_mixer_layer_enable(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id)
{
	u32 num_layers = mixer->layer_cnt;
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	u32 curr_state;

	/* Ensure layer is marked as 'active' by application before
	 * turning on in hardware.  In some cases, layer register data
	 * may be written to otherwise inactive layers in lieu of, eventually,
	 * turning them on.
	*/
	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	if (layer_data) {
		if (!mixer_layer_active(layer_data))
			return;
	} else {
		return;
	}

	  /*Check if request is to enable all layers or single layer*/
	if (layer_id == XVMIX_LAYER_ALL) {
		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
			XVMIX_MASK_ENABLE_ALL_LAYERS);

	} else if ((layer_id < num_layers) ||
		((layer_id == XVMIX_LAYER_LOGO) && mixer->logo_layer_enabled)) {

		curr_state = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA);

		curr_state |= (1<<layer_id);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
			curr_state);
	}

}

/******************************************************************************
* Disables the specified layer of the core instance
******************************************************************************/
void xilinx_mixer_layer_disable(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id)
{
	u32 num_layers, curr_state;
	void __iomem *reg_addr = mixer->reg_base_addr;

	num_layers = mixer->layer_cnt;

	if (layer_id == XVMIX_LAYER_ALL) {

		reg_writel(reg_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
			XVMIX_MASK_DISABLE_ALL_LAYERS);

	} else if ((layer_id < num_layers) ||
		((layer_id == XVMIX_LAYER_LOGO) &&
						(mixer->logo_layer_enabled))) {

		curr_state = reg_readl(reg_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA);

		curr_state &= ~(1 << layer_id);

		reg_writel(reg_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA,
			curr_state);
	}
}

/******************************************************************************
* Returns state of the specified layer [enabled or disabled]
******************************************************************************/
int xilinx_mixer_is_layer_enabled(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id)
{
	u32 state, mask;

	mask = (1<<layer_id);
	state = reg_readl(mixer->reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LAYERENABLE_DATA);
	return ((state & mask) ? 1 : 0);
}

/******************************************************************************
* Sets the background color to be displayed when stream layer is
* disabled
******************************************************************************/
void xilinx_mixer_set_bkg_col(struct xv_mixer *mixer, u64 rgb_value)
{
	u32 bpc = mixer->bg_layer_bpc;
	u32 bpc_mask_shift = 16 - bpc;
	u32 val_mask = (0xFFFF >> bpc_mask_shift);

	u16 b_val = (rgb_value >> (bpc * 2)) & val_mask;
	u16 g_val = (rgb_value >> bpc) & val_mask;
	u16 r_val = (rgb_value >> 0) &  val_mask;

	/* Set Background Color */
	reg_writel(mixer->reg_base_addr,
		XV_MIX_CTRL_ADDR_HWREG_BACKGROUND_Y_R_DATA, r_val);
	reg_writel(mixer->reg_base_addr,
		XV_MIX_CTRL_ADDR_HWREG_BACKGROUND_U_G_DATA, g_val);
	reg_writel(mixer->reg_base_addr,
		XV_MIX_CTRL_ADDR_HWREG_BACKGROUND_V_B_DATA, b_val);

	mixer->bg_color = rgb_value;
}

/******************************************************************************
* Configures the window coordinates of the specified layer
******************************************************************************/
int xilinx_mixer_set_layer_window(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				u32 x_pos, u32 y_pos,
				u32 win_width, u32 win_height,
				u32 stride_bytes)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	xv_mixer_scale_factor scale = 0;
	int status = 0;
	bool win_valid = false;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	if (!layer_data)
		return (-ENODEV);

	/* Check window coordinates */
	scale = xilinx_mixer_get_layer_scaling(mixer, layer_id);

	if (is_window_valid(mixer, x_pos, y_pos, win_width, win_height, scale))
		return(-EINVAL);

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled &&
			win_width <= layer_data->hw_config.max_width &&
			win_height <= layer_data->hw_config.max_height &&
			win_height >= layer_data->hw_config.min_height &&
			win_height >= layer_data->hw_config.min_width) {

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTX_DATA, x_pos);

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTY_DATA, y_pos);

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOWIDTH_DATA,
				win_width);

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOHEIGHT_DATA,
				win_height);

			layer_data->layer_regs.x_pos = x_pos;
			layer_data->layer_regs.y_pos = y_pos;

			layer_data->layer_regs.width = win_width;
			layer_data->layer_regs.height = win_height;

		} else {
			status = -EINVAL;
		}
		break;

	default: /*Layer1-Layer7*/
		if (layer_id < mixer->layer_cnt) {

			if (win_width <= layer_data->hw_config.max_width &&
				win_width >= layer_data->hw_config.min_width) {

			/* Check layer interface is Stream or Memory */
			if (layer_data->hw_config.is_streaming) {

			/* Stride is not required for stream layer */
				win_valid = true;
			} else {
			/* Check if stride is aligned to aximm width
			* (2*PPC*32-bits)
			*/
				u32 align = 2 * mixer->ppc * 4;

				if ((stride_bytes % align) != 0) {
					win_valid = false;
					status   = -EINVAL;
				} else {
					win_valid = true;
				}
			}

			if (win_valid) {

				u32 offset = layer_id * XVMIX_REG_OFFSET;

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTX_0_DATA +
				offset),
				x_pos);

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTY_0_DATA +
				offset),
				y_pos);

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERWIDTH_0_DATA +
				offset),
				win_width);

				reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERHEIGHT_0_DATA +
				offset),
				win_height);


				layer_data->layer_regs.x_pos = x_pos;
				layer_data->layer_regs.y_pos = y_pos;

				layer_data->layer_regs.width = win_width;
				layer_data->layer_regs.height = win_height;

				if (!(layer_data->hw_config.is_streaming)) {

					reg_writel(reg_base_addr,
					(XV_MIX_CTRL_ADDR_HWREG_STRIDE_0_DATA +
					offset),
					stride_bytes);
				}

				status = 0;
			}
		}
		} else {
			status = -EINVAL;
		}

		break;
	}

	return status;
}

/******************************************************************************
* Read the window coordinates of the specified layer
******************************************************************************/
int xilinx_mixer_get_layer_window(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = 0;
	u32 *x_pos, *y_pos, *win_width, *win_height;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	x_pos = &(mixer_layer_x_pos(layer_data));
	y_pos = &(mixer_layer_y_pos(layer_data));

	win_width  = &(mixer_layer_width(layer_data));
	win_height = &(mixer_layer_height(layer_data));


	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled) {

			*x_pos = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTX_DATA);
			*y_pos = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTY_DATA);
			*win_width = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOWIDTH_DATA);
			*win_height = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOHEIGHT_DATA);

			status = 0;
		} else {
			status = -EINVAL;
		}
		break;

	default: /*Layer1-Layer7*/
		if (layer_id < mixer->layer_cnt) {

			u32 offset = layer_id * XVMIX_REG_OFFSET;

			*x_pos = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTX_0_DATA +
				offset));
			*y_pos = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTY_0_DATA +
				offset));
			*win_width  = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERWIDTH_0_DATA +
				offset));
			*win_height = reg_readl(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYERHEIGHT_0_DATA +
				offset));

			status = 0;
		} else {
			status = -EINVAL;
		}
		break;
	}

	return status;
}

/******************************************************************************
* Moves the window position of the specified layer to new corrdinates
******************************************************************************/
int xilinx_mixer_move_layer_window(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				u32 new_x_pos,
				u32 new_y_pos)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = 0;
	u32 win_status = 0;
	u32 scale_val = 0;
	u32 *org_y, *org_x, *win_width, *win_height;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);
	/* Current window settings for layer */

	org_x = &(mixer_layer_x_pos(layer_data));
	org_y = &(mixer_layer_y_pos(layer_data));

	win_width = &(mixer_layer_width(layer_data));
	win_height = &(mixer_layer_height(layer_data));

	/* Update window settings in cache from hardware */
	win_status = xilinx_mixer_get_layer_window(mixer, layer_id);

	if (win_status != 0)
		return win_status;

	/* Get scale factor */
	scale_val = xilinx_mixer_get_layer_scaling(mixer, layer_id);
	/* Validate new start position will not cause the layer window
	* to go out of scope
	*/
	if (is_window_valid(mixer, new_x_pos, new_y_pos,
		*win_width, *win_height, scale_val)) {
		return(-EINVAL);
	}

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled) {

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTX_DATA,
				new_x_pos);

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSTARTY_DATA,
				new_y_pos);

			*org_x = new_x_pos;
			*org_y = new_y_pos;

			status = 0;
		}
		break;

	default: /*Layer1-Layer7*/
		if (layer_id < mixer->layer_cnt) {
			u32 offset;

			offset = layer_id * XVMIX_REG_OFFSET;

			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTX_0_DATA + offset),
			new_x_pos);

			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERSTARTY_0_DATA + offset),
			new_y_pos);

			*org_x = new_x_pos;
			*org_y = new_y_pos;

			status = 0;
		}
		break;
	}
	return status;
}

/******************************************************************************
* Configures the scaling factor of the specified layer
*
* Applicable only for overlay layers
******************************************************************************/
int xilinx_mixer_set_layer_scaling(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				xv_mixer_scale_factor scale)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;

	int status = 0;
	int win_status = 0;
	u32 layer_x_pos, layer_y_pos, layer_width, layer_height;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	/* Validate if scaling will cause the layer window to go out of scope */
	win_status = xilinx_mixer_get_layer_window(mixer, layer_id);

	if (win_status != 0)
		return win_status;

	layer_x_pos = mixer_layer_x_pos(layer_data);
	layer_y_pos = mixer_layer_y_pos(layer_data);

	layer_width  = mixer_layer_width(layer_data);
	layer_height = mixer_layer_height(layer_data);

	if (is_window_valid(mixer, layer_x_pos, layer_y_pos,
		      layer_width, layer_height, scale)) {
		return(-EINVAL);
	}

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled) {
			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSCALEFACTOR_DATA,
				scale);

			layer_data->layer_regs.scale_fact = scale;

			status = 0;
		}
		break;

	default: /*Layer0-Layer7*/
		if (layer_id < mixer->layer_cnt &&
			mixer_layer_can_scale(layer_data)) {

			u32 offset = layer_id * XVMIX_REG_OFFSET;

			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERSCALEFACTOR_0_DATA+offset),
			scale);

			layer_data->layer_regs.scale_fact = scale;

			status = 0;
		}
		break;
	}
	return status;
}

/******************************************************************************
* Returns the scaling factor of the specified layer
*
* Applicable only for overlay layers
******************************************************************************/
int xilinx_mixer_get_layer_scaling(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id)
{
	int scale_factor = 0;
	struct xv_mixer_layer_data *layer_data =
		xilinx_mixer_get_layer_data(mixer, layer_id);

	if (layer_id == XVMIX_LAYER_LOGO)

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled) {
			scale_factor = reg_readl(mixer->reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOSCALEFACTOR_DATA);
			layer_data->layer_regs.scale_fact = scale_factor;
		}
		break;

	default: /*Layer0-Layer7*/
		if ((layer_id < XVMIX_LAYER_LOGO) &&
					mixer_layer_can_scale(layer_data)) {

			u32 reg_b;

			reg_b = XV_MIX_CTRL_ADDR_HWREG_LAYERSCALEFACTOR_0_DATA;
			scale_factor = reg_readl(mixer->reg_base_addr,
				(reg_b + (layer_id * XVMIX_REG_OFFSET)));
			layer_data->layer_regs.scale_fact = scale_factor;
		}
		break;
	}
	return scale_factor;
}

/******************************************************************************
* Configures the Alpha level of the specified layer
*
* Applicable only for overlay layers
*
******************************************************************************/
int xilinx_mixer_set_layer_alpha(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				u32 alpha)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = 0;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled) {

			reg_writel(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOALPHA_DATA, alpha);

			layer_data->layer_regs.alpha = alpha;

			status = 0;

		} else {
			status = -EINVAL;
		}
		break;

	default: /*Layer1-Layer7*/
		if ((layer_id < mixer->layer_cnt) &&
					mixer_layer_can_alpha(layer_data)) {

			u32 offset =  layer_id * XVMIX_REG_OFFSET;

			reg_writel(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERALPHA_0_DATA + offset),
			alpha);

			layer_data->layer_regs.alpha = alpha;

			status = 0;
		} else {
			status = -EINVAL;
		}
		break;
	}
	return status;
}

/******************************************************************************
* Returns the alpha of the specified layer
******************************************************************************/
int xilinx_mixer_get_layer_alpha(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id, u32 *reg_val)
{

	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;
	int status = -EINVAL;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	switch (layer_id) {
	case XVMIX_LAYER_LOGO:
		if (mixer->logo_layer_enabled) {

			*reg_val = reg_readl(reg_base_addr,
				XV_MIX_CTRL_ADDR_HWREG_LOGOALPHA_DATA);
			status = 0;
			layer_data->layer_regs.alpha = *reg_val;
		}
		break;

	default: /*Layer1-Layer7*/
		if ((layer_id < mixer->layer_cnt) &&
					mixer_layer_can_alpha(layer_data)) {

			u32 offset = layer_id * XVMIX_REG_OFFSET;
			*reg_val = reg_readl(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYERALPHA_0_DATA + offset));
			layer_data->layer_regs.alpha = *reg_val;
			status = 0;
		}
		break;
	}
	return status;
}

/******************************************************************************
* Reads the color format of the specified layer
******************************************************************************/
int xilinx_mixer_get_layer_colorspace_fmt(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				xv_comm_color_fmt_id *c_fmt)
{
	struct xv_mixer_layer_data *layer_data;
	int status = -EINVAL;

	layer_data = xilinx_mixer_get_layer_data(mixer, layer_id);

	if (layer_id <= mixer->layer_cnt) {

		*c_fmt = layer_data->hw_config.vid_fmt;
		status = 0;
	}

	return status;
}

/******************************************************************************
* Sets the buffer address of the specified layer
******************************************************************************/
int xilinx_mixer_set_layer_buff_addr(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				u32 buff_addr)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	int status = 0;
	u32 align;
	u32 win_valid = 0;

	if (layer_id < mixer->layer_cnt) {
	/* Check if addr is aligned to aximm width (PPC * 64-bits) */
		align = mixer->ppc * 8;
		if ((buff_addr % align) != 0) {
			win_valid = 0;
			status   = -EINVAL;
		} else {
			win_valid = 1;
		}
		if (win_valid) {

			u32 offset = (layer_id-1) * XVMIX_REG_OFFSET;

			reg_writel(reg_base_addr,
				(XV_MIX_CTRL_ADDR_HWREG_LAYER1_V_DATA + offset),
				buff_addr);

			mixer->layer_data[layer_id].layer_regs.buff_addr =
								     buff_addr;
		}
	}
	return status;
}

/******************************************************************************
* Reads the buffer address of the specified layer
******************************************************************************/
int xilinx_mixer_get_layer_buff_addr(struct xv_mixer *mixer,
				xv_mixer_layer_id layer_id,
				u32 *buff_addr)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	int status = -ENODEV;

	if (layer_id < mixer->layer_cnt) {

		u32 offset = (layer_id-1) * XVMIX_REG_OFFSET;

		*buff_addr = reg_readl(reg_base_addr,
			(XV_MIX_CTRL_ADDR_HWREG_LAYER1_V_DATA + offset));
		status = 0;
	}
	return status;
}

/******************************************************************************
* Sets the logo layer color key data
* TODO
******************************************************************************/
int xilinx_mixer_set_logo_color_key(struct xv_mixer *mixer)
{

	int status = -ENODEV;

	if (mixer->logo_layer_enabled && mixer->logo_color_key_enabled) {

		void __iomem *reg_base_addr = mixer->reg_base_addr;
		u8 *rgb_min = mixer->logo_color_key.rgb_min;
		u8 *rgb_max = mixer->logo_color_key.rgb_max;

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_R_DATA,
			rgb_min[0]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_G_DATA,
			rgb_min[1]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_B_DATA,
			rgb_min[2]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_R_DATA,
			rgb_max[0]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_G_DATA,
			rgb_max[1]);

		reg_writel(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_B_DATA,
			rgb_max[2]);

		status = 0;
	}

	return status;
}

/******************************************************************************
* Reads the logo layer color key data
******************************************************************************/
int xilinx_mixer_get_logo_color_key(struct xv_mixer *mixer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	u8 *rgb_min = mixer->logo_color_key.rgb_min;
	u8 *rgb_max = mixer->logo_color_key.rgb_max;
	int status = -ENODEV;

	if (mixer->logo_layer_enabled && mixer->logo_color_key_enabled) {

		rgb_min[0] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_R_DATA);

		rgb_min[1] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_G_DATA);

		rgb_min[2] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMIN_B_DATA);

		rgb_max[0] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_R_DATA);

		rgb_max[1] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_G_DATA);

		rgb_max[2] = reg_readl(reg_base_addr,
			XV_MIX_CTRL_ADDR_HWREG_LOGOCLRKEYMAX_B_DATA);

		status = 0;
	}
	return status;
}


/******************************************************************************
* Loads the logo data into core BRAM
******************************************************************************/
int xilinx_mixer_logo_load(struct xv_mixer *mixer,
				u32 logo_w, u32 logo_h,
				u8 *r_buffer,
				u8 *g_buffer,
				u8 *b_buffer,
				u8 *a_buffer)
{
	void __iomem *reg_base_addr = mixer->reg_base_addr;
	struct xv_mixer_layer_data *layer_data;

	int status = 0;
	int x;
	u32 aligned_pix_cnt;
	u32 rword, gword, bword, aword;
	u32 pixel_cnt = logo_w * logo_h;
	u32 unaligned_pix_cnt = pixel_cnt % 4;
	u32 width, height, curr_x_pos, curr_y_pos;
	u32 rbase_addr, gbase_addr, bbase_addr, abase_addr;

	layer_data = xilinx_mixer_get_layer_data(mixer, XVMIX_LAYER_LOGO);
	rword = gword = bword = aword = 0;


	if (!layer_data)
		return -ENODEV;

	/* RGBA data should be 32-bit word aligned */
	if (unaligned_pix_cnt && mixer->logo_pixel_alpha_enabled)
		return -EINVAL;

	if (mixer->logo_layer_enabled &&
		logo_w <= layer_data->hw_config.max_width &&
		logo_h <= layer_data->hw_config.max_height) {

		width  = logo_w;
		height = logo_h;

		rbase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOR_V_BASE;
		gbase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOG_V_BASE;
		bbase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOB_V_BASE;
		abase_addr = XV_MIX_CTRL_ADDR_HWREG_LOGOA_V_BASE;

		aligned_pix_cnt = pixel_cnt - unaligned_pix_cnt;


		for (x = 0; x < aligned_pix_cnt; x += 4) {

			rword = (u32)r_buffer[x] |
			(((u32)r_buffer[x+1])<<8) |
			(((u32)r_buffer[x+2])<<16) |
			(((u32)r_buffer[x+3])<<24);

			gword = (u32)g_buffer[x] |
			(((u32)g_buffer[x+1])<<8) |
			(((u32)g_buffer[x+2])<<16) |
			(((u32)g_buffer[x+3])<<24);

			bword = (u32)b_buffer[x] |
			(((u32)b_buffer[x+1])<<8) |
			(((u32)b_buffer[x+2])<<16) |
			(((u32)b_buffer[x+3])<<24);

			if (mixer->logo_pixel_alpha_enabled) {
				aword = (u32)a_buffer[x] |
				(((u32)a_buffer[x+1])<<8) |
				(((u32)a_buffer[x+2])<<16) |
				(((u32)a_buffer[x+3])<<24);
			}

			reg_writel(reg_base_addr, (rbase_addr + x), rword);
			reg_writel(reg_base_addr, (gbase_addr + x), gword);
			reg_writel(reg_base_addr, (bbase_addr + x), bword);


			if (mixer->logo_pixel_alpha_enabled)
				reg_writel(reg_base_addr,
					(abase_addr + x), aword);
		}

		if (unaligned_pix_cnt) {

			rword = gword = bword = 0;

			switch (unaligned_pix_cnt) {

			case 3:
			rword = rword |
				(((u32)r_buffer[aligned_pix_cnt+2])<<16);
			gword = gword |
				(((u32)g_buffer[aligned_pix_cnt+2])<<16);
			bword = bword |
				(((u32)b_buffer[aligned_pix_cnt+2])<<16);
			case 2:
			rword = rword |
				(((u32)r_buffer[aligned_pix_cnt+1])<<8);
			gword = gword |
				(((u32)g_buffer[aligned_pix_cnt+1])<<8);
			bword = bword |
				(((u32)b_buffer[aligned_pix_cnt+1])<<8);
			case 1:
			rword = rword |
				((u32)r_buffer[aligned_pix_cnt]);
			gword = gword |
				((u32)g_buffer[aligned_pix_cnt]);
			bword = bword |
				((u32)b_buffer[aligned_pix_cnt]);

			}
			reg_writel(reg_base_addr,
				(rbase_addr + aligned_pix_cnt), rword);
			reg_writel(reg_base_addr,
				(gbase_addr + aligned_pix_cnt), gword);
			reg_writel(reg_base_addr,
				(bbase_addr + aligned_pix_cnt), bword);

		}

		curr_x_pos = mixer_layer_x_pos(layer_data);
		curr_y_pos = mixer_layer_y_pos(layer_data);

		status = xilinx_mixer_set_layer_window(mixer, XVMIX_LAYER_LOGO,
				curr_x_pos, curr_y_pos, logo_w, logo_h, 0);
	} else {
		status = -EINVAL;
	}
	return status;
}


struct xv_mixer_layer_data *xilinx_mixer_get_layer_data(struct xv_mixer *mixer,
						xv_mixer_layer_id id)
{
	int i;
	struct xv_mixer_layer_data *layer_data;

	for (i = 0; i <= (mixer->layer_cnt - 1); i++) {
		layer_data = &(mixer->layer_data[i]);
		if (layer_data->id == id)
		return layer_data;
	}
	return NULL;
}
