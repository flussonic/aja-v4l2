// SPDX-License-Identifier: GPL-2.0
/*
 * Stream counters of a node, in sysfs next to it
 * (/sys/class/video4linux/videoN/): the names every SDI driver of ours uses,
 * all since the last STREAMON, plus the input as text.
 */
#include "ajv4l2.h"

static struct ajv4l2_port *port_of(struct device *dev)
{
	return video_get_drvdata(to_video_device(dev));
}

#define AJV4L2_COUNTER(name)							\
static ssize_t name##_show(struct device *dev, struct device_attribute *attr,	\
			   char *buf)						\
{										\
	return sysfs_emit(buf, "%llu\n", port_of(dev)->name);			\
}										\
static DEVICE_ATTR_RO(name)

AJV4L2_COUNTER(frames);
AJV4L2_COUNTER(frames_skipped);
AJV4L2_COUNTER(no_buffer);
AJV4L2_COUNTER(resyncs);
AJV4L2_COUNTER(no_sync);
AJV4L2_COUNTER(events_missed);
AJV4L2_COUNTER(crc_errors);
AJV4L2_COUNTER(dma_errors);
AJV4L2_COUNTER(restarts);

static ssize_t signal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ajv4l2_port *port = port_of(dev);
	struct ajv4l2_input_state st;
	char s[64];

	ajv4l2_input_read(port, &st);
	return sysfs_emit(buf, "%s\n", ajv4l2_input_describe(&st, s, sizeof(s)));
}
static DEVICE_ATTR_RO(signal);

static struct attribute *ajv4l2_port_attrs[] = {
	&dev_attr_frames.attr,
	&dev_attr_frames_skipped.attr,
	&dev_attr_no_buffer.attr,
	&dev_attr_resyncs.attr,
	&dev_attr_no_sync.attr,
	&dev_attr_events_missed.attr,
	&dev_attr_crc_errors.attr,
	&dev_attr_dma_errors.attr,
	&dev_attr_restarts.attr,
	&dev_attr_signal.attr,
	NULL
};

static const struct attribute_group ajv4l2_port_group = {
	.attrs = ajv4l2_port_attrs,
};

int ajv4l2_sysfs_add(struct ajv4l2_port *port)
{
	return sysfs_create_group(&port->vdev.dev.kobj, &ajv4l2_port_group);
}

void ajv4l2_sysfs_remove(struct ajv4l2_port *port)
{
	sysfs_remove_group(&port->vdev.dev.kobj, &ajv4l2_port_group);
}
