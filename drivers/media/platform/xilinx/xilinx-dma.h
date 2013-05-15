/*
 * xilinx-dma.h
 *
 * Xilinx Video DMA
 *
 * Copyright (C) 2013 Ideas on Board SPRL
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __XILINX_VIP_DMA_H__
#define __XILINX_VIP_DMA_H__

#include <linux/mutex.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-core.h>

struct dma_chan;
struct xvip_pipeline;
struct xvip_video_format;

struct xvip_dma {
	struct video_device video;
	enum v4l2_buf_type type;
	struct media_pad pad;

	struct xvip_pipeline *xvipp;

	struct mutex lock;
	struct v4l2_pix_format format;
	const struct xvip_video_format *fmtinfo;

	struct vb2_queue queue;
	void *alloc_ctx;
	spinlock_t irqlock;
	struct list_head irqqueue;
	unsigned int sequence;

	struct dma_chan *dma;
	unsigned int align;
};

#define to_xvip_dma(vdev)	container_of(vdev, struct xvip_dma, video)

int xvip_dma_init(struct xvip_pipeline *xvipp, struct xvip_dma *dma);
void xvip_dma_cleanup(struct xvip_dma *dma);

#endif /* __XILINX_VIP_DMA_H__ */
