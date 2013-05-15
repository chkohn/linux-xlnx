/*
 * xilinx-vipp.h
 *
 * Xilinx Video IP Pipeline
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __XILINX_VIPP_H__
#define __XILINX_VIPP_H__

#include <linux/list.h>
#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>

#include "xilinx-dma.h"

/*
 * struct xvip_pipeline - Xilinx Video IP device structure
 * @v4l2_dev: V4L2 device
 * @media_dev: media device
 * @pipe: media pipeline
 * @dev: (OF) device
 * @notifier: V4L2 asynchronous subdevs notifier
 * @entities: entities in the pipeline as a list of xvip_pipeline_entity
 * @num_entities: number of entities in the pipeline
 * @dma: DMA channel at the pipeline output
 * @streaming: indicates if the pipeline is currently streaming video
 */
struct xvip_pipeline {
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct media_pipeline pipe;
	struct device *dev;

	struct v4l2_async_notifier notifier;
	struct list_head entities;
	unsigned int num_entities;

	struct xvip_dma dma;
	bool streaming;
};

int xvip_pipeline_set_stream(struct xvip_pipeline *xvipp, bool on);

#endif /* __XILINX_VIPP_H__ */
