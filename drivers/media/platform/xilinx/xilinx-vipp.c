/*
 * xilinx-xvipp.c
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

#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-of.h>

#include "xilinx-dma.h"
#include "xilinx-vipp.h"

struct xvip_pipeline_entity {
	struct list_head list;
	struct device_node *node;
	struct media_entity *entity;

	struct v4l2_async_subdev asd;
	struct v4l2_subdev *subdev;
};

/* -----------------------------------------------------------------------------
 * Pipeline Stream Management
 */

/*
 * xvip_pipeline_enable - Enable streaming on a pipeline
 * @xvipp: Xilinx Video Pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and start
 * all modules in the chain.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise.
 */
static int xvip_pipeline_enable(struct xvip_pipeline *xvipp)
{
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int ret;

	entity = &xvipp->dma.video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	return 0;
}

/*
 * xvip_pipeline_disable - Disable streaming on a pipeline
 * @xvipp: Xilinx Video Pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and stop
 * all modules in the chain.
 */
static void xvip_pipeline_disable(struct xvip_pipeline *xvipp)
{
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;

	entity = &xvipp->dma.video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		v4l2_subdev_call(subdev, video, s_stream, 0);
	}
}

/*
 * xvip_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @xvipp: Xilinx Video Pipeline
 * @state: Stream state (stopped, single shot or continuous)
 *
 * Set the pipeline to the given stream state. Pipelines can be started in
 * single-shot or continuous mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise. Stopping the pipeline never fails. The pipeline state is
 * not updated when the operation fails.
 */
int xvip_pipeline_set_stream(struct xvip_pipeline *xvipp, bool on)
{
	int ret;

	if (on) {
		ret = xvip_pipeline_enable(xvipp);
		if (ret < 0)
			return ret;
	} else {
		xvip_pipeline_disable(xvipp);
	}

	xvipp->streaming = on;
	return 0;
}

/* -----------------------------------------------------------------------------
 * Pipeline Management
 */

static struct xvip_pipeline_entity *
xvipp_pipeline_find_entity(struct xvip_pipeline *xvipp,
			   const struct device_node *node)
{
	struct xvip_pipeline_entity *entity;

	list_for_each_entry(entity, &xvipp->entities, list) {
		if (entity->node == node)
			return entity;
	}

	return NULL;
}

static int xvipp_pipeline_build_one(struct xvip_pipeline *xvipp,
				    struct xvip_pipeline_entity *entity)
{
	u32 link_flags = MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED;
	struct media_entity *local = entity->entity;
	struct media_entity *remote;
	struct media_pad *local_pad;
	struct media_pad *remote_pad;
	struct xvip_pipeline_entity *ent;
	struct v4l2_of_link link;
	struct device_node *ep = NULL;
	struct device_node *next;
	int ret = 0;

	dev_dbg(xvipp->dev, "creating links for entity %s\n", local->name);

	while (1) {
		/* Get the next endpoint and parse its link. */
		next = v4l2_of_get_next_endpoint(entity->node, ep);
		if (next == NULL)
			break;

		of_node_put(ep);
		ep = next;

		dev_dbg(xvipp->dev, "processing endpoint %s\n", ep->full_name);

		ret = v4l2_of_parse_link(ep, &link);
		if (ret < 0) {
			dev_err(xvipp->dev, "failed to parse link for %s\n",
				ep->full_name);
			continue;
		}

		/* Skip sink ports, they will be processed from the other end of
		 * the link.
		 */
		if (link.local_port >= local->num_pads) {
			dev_err(xvipp->dev, "invalid port number %u on %s\n",
				link.local_port, link.local_node->full_name);
			v4l2_of_put_link(&link);
			ret = -EINVAL;
			break;
		}

		local_pad = &local->pads[link.local_port];

		if (local_pad->flags & MEDIA_PAD_FL_SINK) {
			dev_dbg(xvipp->dev, "skipping sink port %s:%u\n",
				link.local_node->full_name, link.local_port);
			v4l2_of_put_link(&link);
			continue;
		}

		/* Find the remote entity. */
		ent = xvipp_pipeline_find_entity(xvipp, link.remote_node);
		if (ent == NULL) {
			dev_err(xvipp->dev, "no entity found for %s\n",
				link.remote_node->full_name);
			v4l2_of_put_link(&link);
			ret = -ENODEV;
			break;
		}

		remote = ent->entity;

		if (link.remote_port >= remote->num_pads) {
			dev_err(xvipp->dev, "invalid port number %u on %s\n",
				link.remote_port, link.remote_node->full_name);
			v4l2_of_put_link(&link);
			ret = -EINVAL;
			break;
		}

		remote_pad = &remote->pads[link.remote_port];

		v4l2_of_put_link(&link);

		/* Create the media link. */
		dev_dbg(xvipp->dev, "creating %s:%u -> %s:%u link\n",
			local->name, local_pad->index,
			remote->name, remote_pad->index);

		ret = media_entity_create_link(local, local_pad->index,
					       remote, remote_pad->index,
					       link_flags);
		if (ret < 0) {
			dev_err(xvipp->dev,
				"failed to create %s:%u -> %s:%u link\n",
				local->name, local_pad->index,
				remote->name, remote_pad->index);
			break;
		}
	}

	of_node_put(ep);
	return ret;
}

static int xvipp_pipeline_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct xvip_pipeline *xvipp =
		container_of(notifier, struct xvip_pipeline, notifier);
	struct xvip_pipeline_entity *entity;
	int ret;

	dev_dbg(xvipp->dev, "notify complete, all subdevs registered\n");

	/* Create links for every entity. */
	list_for_each_entry(entity, &xvipp->entities, list) {
		ret = xvipp_pipeline_build_one(xvipp, entity);
		if (ret < 0)
			return ret;
	}

	ret = v4l2_device_register_subdev_nodes(&xvipp->v4l2_dev);
	if (ret < 0)
		dev_err(xvipp->dev, "failed to register subdev nodes\n");
		return ret;

	return 0;
}

static int xvipp_pipeline_notify_bound(struct v4l2_async_notifier *notifier,
				       struct v4l2_async_subdev_list *asdl)
{
	struct xvip_pipeline *xvipp =
		container_of(notifier, struct xvip_pipeline, notifier);
	struct v4l2_subdev *subdev = v4l2_async_to_subdev(asdl);
	struct xvip_pipeline_entity *entity;

	/* Locate the entity corresponding to the bound subdev and store the
	 * subdev pointer.
	 */
	list_for_each_entry(entity, &xvipp->entities, list) {
		if (entity->node != subdev->dev->of_node)
			continue;

		if (entity->subdev) {
			dev_err(xvipp->dev, "duplicate subdev for node %s\n",
				entity->node->full_name);
			return -EINVAL;
		}

		dev_dbg(xvipp->dev, "subdev %s bound\n", subdev->name);
		entity->entity = &subdev->entity;
		entity->subdev = subdev;
		return 0;
	}

	dev_err(xvipp->dev, "no entity for subdev %s\n", subdev->name);
	return -EINVAL;
}

static int xvipp_pipeline_parse_one(struct xvip_pipeline *xvipp,
				    struct device_node *node)
{
	struct xvip_pipeline_entity *entity;
	struct device_node *remote;
	struct device_node *ep = NULL;
	struct device_node *next;
	int ret = 0;

	dev_dbg(xvipp->dev, "parsing node %s\n", node->full_name);

	while (1) {
		next = v4l2_of_get_next_endpoint(node, ep);
		if (next == NULL)
			break;

		of_node_put(ep);
		ep = next;

		dev_dbg(xvipp->dev, "handling endpoint %s\n", ep->full_name);

		remote = v4l2_of_get_remote_port_parent(ep);
		if (remote == NULL) {
			ret = -EINVAL;
			break;
		}

		/* Skip entities that we have already processed. */
		if (xvipp_pipeline_find_entity(xvipp, remote))
			continue;

		entity = kzalloc(sizeof(*entity), GFP_KERNEL);
		if (entity == NULL) {
			of_node_put(remote);
			ret = -ENOMEM;
			break;
		}

		entity->node = remote;
		entity->asd.hw.bus_type = V4L2_ASYNC_BUS_DT;
		entity->asd.hw.match.dt.node = remote;
		list_add_tail(&entity->list, &xvipp->entities);
		xvipp->num_entities++;
	}

	of_node_put(ep);
	return ret;
}

static int xvipp_pipeline_parse(struct xvip_pipeline *xvipp)
{
	struct xvip_pipeline_entity *entity;
	int ret;

	/* Create an initial entity for the DMA channel. */
	ret = xvip_dma_init(xvipp, &xvipp->dma);
	if (ret < 0) {
		dev_err(xvipp->dev, "DMA initialization failed\n");
		return ret;
	}

	entity = kzalloc(sizeof(*entity), GFP_KERNEL);
	if (entity == NULL)
		return -ENOMEM;

	entity->node = of_node_get(xvipp->dev->of_node);
	entity->entity = &xvipp->dma.video.entity;

	list_add_tail(&entity->list, &xvipp->entities);
	xvipp->num_entities++;

	/* Walk the links to parse the full pipeline. */
	list_for_each_entry(entity, &xvipp->entities, list) {
		ret = xvipp_pipeline_parse_one(xvipp, entity->node);
		if (ret < 0)
			break;
	}

	return ret;
}

static void xvipp_pipeline_cleanup(struct xvip_pipeline *xvipp)
{
	struct xvip_pipeline_entity *entity;
	struct xvip_pipeline_entity *prev;

	v4l2_async_notifier_unregister(&xvipp->notifier);

	list_for_each_entry_safe(entity, prev, &xvipp->entities, list) {
		of_node_put(entity->node);
		list_del(&entity->list);
		kfree(entity);
	}

	xvip_dma_cleanup(&xvipp->dma);
}

static int xvipp_pipeline_init(struct xvip_pipeline *xvipp)
{
	struct xvip_pipeline_entity *entity;
	struct v4l2_async_subdev **subdevs = NULL;
	unsigned int num_subdevs;
	unsigned int i;
	int ret;

	/* Parse the pipeline to extract a list of subdevice DT nodes. */
	ret = xvipp_pipeline_parse(xvipp);
	if (ret < 0) {
		dev_err(xvipp->dev, "pipeline parsing failed\n");
		goto done;
	}

	if (xvipp->num_entities <= 1) {
		dev_err(xvipp->dev, "no entity found in pipeline\n");
		goto done;
	}

	/* Register the subdevices notifier. */
	num_subdevs = xvipp->num_entities - 1;
	subdevs = kzalloc(sizeof(*subdevs) * num_subdevs, GFP_KERNEL);
	if (subdevs == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	i = 0;
	entity = list_first_entry(&xvipp->entities, typeof(*entity), list);
	list_for_each_entry_continue(entity, &xvipp->entities, list)
		subdevs[i++] = &entity->asd;

	xvipp->notifier.subdev = subdevs;
	xvipp->notifier.subdev_num = num_subdevs;
	xvipp->notifier.bound = xvipp_pipeline_notify_bound;
	xvipp->notifier.complete = xvipp_pipeline_notify_complete;

	ret = v4l2_async_notifier_register(&xvipp->v4l2_dev, &xvipp->notifier);
	if (ret < 0) {
		dev_err(xvipp->dev, "notifier registration failed\n");
		goto done;
	}

	ret = 0;

done:
	if (ret < 0)
		xvipp_pipeline_cleanup(xvipp);

	kfree(subdevs);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Media Controller and V4L2
 */

static void xvipp_v4l2_cleanup(struct xvip_pipeline *xvipp)
{
	v4l2_device_unregister(&xvipp->v4l2_dev);
	media_device_unregister(&xvipp->media_dev);
}

static int xvipp_v4l2_init(struct xvip_pipeline *xvipp)
{
	int ret;

	xvipp->media_dev.dev = xvipp->dev;
	strlcpy(xvipp->media_dev.model, "Xilinx Video Pipeline",
		sizeof(xvipp->media_dev.model));
	xvipp->media_dev.hw_revision = 0;

	ret = media_device_register(&xvipp->media_dev);
	if (ret < 0) {
		dev_err(xvipp->dev, "media device registration failed (%d)\n",
			ret);
		return ret;
	}

	xvipp->v4l2_dev.mdev = &xvipp->media_dev;
	ret = v4l2_device_register(xvipp->dev, &xvipp->v4l2_dev);
	if (ret < 0) {
		dev_err(xvipp->dev, "V4L2 device registration failed (%d)\n",
			ret);
		media_device_unregister(&xvipp->media_dev);
		return ret;
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * Power Management
 */

#undef CONFIG_PM
#ifdef CONFIG_PM

static int xvipp_pm_suspend(struct device *dev)
{
	struct xvip_pipeline *xvipp = dev_get_drvdata(dev);

	return 0;
}

static int xvipp_pm_resume(struct device *dev)
{
	struct xvip_pipeline *xvipp = dev_get_drvdata(dev);

	return 0;
}

#else

#define xvipp_pm_suspend	NULL
#define xvipp_pm_resume		NULL

#endif /* CONFIG_PM */

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static int xvipp_probe(struct platform_device *pdev)
{
	struct xvip_pipeline *xvipp;
	int ret;

	xvipp = devm_kzalloc(&pdev->dev, sizeof(*xvipp), GFP_KERNEL);
	if (!xvipp)
		return -ENOMEM;

	xvipp->dev = &pdev->dev;
	INIT_LIST_HEAD(&xvipp->entities);

	ret = xvipp_v4l2_init(xvipp);
	if (ret < 0)
		return ret;

	ret = xvipp_pipeline_init(xvipp);
	if (ret < 0)
		goto error;

	platform_set_drvdata(pdev, xvipp);

	dev_info(xvipp->dev, "device registered\n");

	return 0;

error:
	xvipp_v4l2_cleanup(xvipp);
	return ret;
}

static int xvipp_remove(struct platform_device *pdev)
{
	struct xvip_pipeline *xvipp = platform_get_drvdata(pdev);

	xvipp_pipeline_cleanup(xvipp);
	xvipp_v4l2_cleanup(xvipp);

	return 0;
}

static const struct dev_pm_ops xvipp_pm_ops = {
	.suspend = xvipp_pm_suspend,
	.resume = xvipp_pm_resume,
};

static const struct of_device_id xvipp_of_id_table[] = {
	{ .compatible = "xlnx,axi-video" },
	{ }
};
MODULE_DEVICE_TABLE(of, xvipp_of_id_table);

static struct platform_driver xvipp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "xilinx-axi-video",
		.pm = &xvipp_pm_ops,
		.of_match_table = of_match_ptr(xvipp_of_id_table),
	},
	.probe = xvipp_probe,
	.remove = xvipp_remove,
};

module_platform_driver(xvipp_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Xilinx Video IP Pipeline Driver");
MODULE_LICENSE("GPL");
