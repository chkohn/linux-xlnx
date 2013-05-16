/*
 * V4L2 asynchronous subdevice registration API
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef V4L2_ASYNC_H
#define V4L2_ASYNC_H

#include <linux/list.h>
#include <linux/mutex.h>

struct device;
struct device_node;
struct v4l2_device;
struct v4l2_subdev;
struct v4l2_async_notifier;

enum v4l2_async_bus_type {
	V4L2_ASYNC_BUS_CUSTOM,
	V4L2_ASYNC_BUS_PLATFORM,
	V4L2_ASYNC_BUS_I2C,
	V4L2_ASYNC_BUS_DT,
};

struct v4l2_async_hw_info {
	enum v4l2_async_bus_type bus_type;
	union {
		struct {
			const char *name;
		} platform;
		struct {
			int adapter_id;
			unsigned short address;
		} i2c;
		struct {
			bool (*match)(struct device *,
				      struct v4l2_async_hw_info *);
			void *priv;
		} custom;
		struct {
			struct device_node *node;
		} dt;
	} match;
};

/**
 * struct v4l2_async_subdev - sub-device descriptor, as known to a bridge
 * @hw:		this device descriptor
 * @list:	member in a list of subdevices
 */
struct v4l2_async_subdev {
	struct v4l2_async_hw_info hw;
	struct list_head list;
};

/**
 * v4l2_async_subdev_list - provided by subdevices
 * @list:	member in a list of subdevices
 * @asd:	pointer to respective struct v4l2_async_subdev
 * @notifier:	pointer to managing notifier
 */
struct v4l2_async_subdev_list {
	struct list_head list;
	struct v4l2_async_subdev *asd;
	struct v4l2_async_notifier *notifier;
};

/**
 * v4l2_async_notifier - provided by bridges
 * @subdev_num:	number of subdevices
 * @subdev:	array of pointers to subdevices
 * @v4l2_dev:	pointer to struct v4l2_device
 * @waiting:	list of subdevices, waiting for their drivers
 * @done:	list of subdevices, already probed
 * @list:	member in a global list of notifiers
 * @bound:	a subdevice driver has successfully probed one of subdevices
 * @complete:	all subdevices have been probed successfully
 * @unbind:	a subdevice is leaving
 */
struct v4l2_async_notifier {
	unsigned int subdev_num;
	struct v4l2_async_subdev **subdev;
	struct v4l2_device *v4l2_dev;
	struct list_head waiting;
	struct list_head done;
	struct list_head list;
	int (*bound)(struct v4l2_async_notifier *notifier,
		     struct v4l2_async_subdev_list *asdl);
	int (*complete)(struct v4l2_async_notifier *notifier);
	void (*unbind)(struct v4l2_async_notifier *notifier,
		       struct v4l2_async_subdev_list *asdl);
};

int v4l2_async_notifier_register(struct v4l2_device *v4l2_dev,
				 struct v4l2_async_notifier *notifier);
void v4l2_async_notifier_unregister(struct v4l2_async_notifier *notifier);
int v4l2_async_register_subdev(struct v4l2_subdev *sd);
void v4l2_async_unregister_subdev(struct v4l2_subdev *sd);
#endif
