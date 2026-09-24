// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (C) 2026 Max Lapshin <max@flussonic.com>
/*
 * Stream counters of a node, in sysfs next to it
 * (/sys/class/video4linux/videoN/): the names, their meaning on each side
 * and the rule for a counter the card cannot report are the contract in
 * docs/sdi-sysfs.md, shared with our other SDI drivers. All are since the
 * last STREAMON. Next to them the input (or the output's state) as text,
 * the state of the reference input as one word of the dictionary and, on an
 * output node, the settings that have no V4L2 control.
 *
 * What is not here is as deliberate as what is: this card reports no
 * resync, no loss of sync, no missed interrupt and no restart, and its
 * playout side reports neither an empty queue nor a line CRC nor a dropped
 * sample. Those files used to exist and read 0 forever, which in a
 * monitoring system is not a missing measurement but a perfect result.
 */
#include "ajv4l2.h"
#include "ajv4l2_hw.h"
#include "ajv4l2_output.h"

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
AJV4L2_COUNTER(crc_errors);
AJV4L2_COUNTER(dma_errors);

AJV4L2_COUNTER(anc_dropped);

static ssize_t signal_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ajv4l2_port *port = port_of(dev);
	struct ajv4l2_input_state st;
	char s[64];

	if (port->output)
		return sysfs_emit(buf, "%s\n", ajv4l2_output_describe(port, s, sizeof(s)));
	ajv4l2_input_read(port, &st);
	return sysfs_emit(buf, "%s\n", ajv4l2_input_describe(&st, s, sizeof(s)));
}
static DEVICE_ATTR_RO(signal);

/* Output settings: read back as written, applied at once and at every STREAMON. */

static ssize_t level_a_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", port_of(dev)->level_a ? 1 : 0);
}

static ssize_t level_a_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ajv4l2_port *port = port_of(dev);
	bool v;

	if (kstrtobool(buf, &v))
		return -EINVAL;
	port->level_a = v;
	ajv4l2_hw_set_level(port);
	return count;
}
static DEVICE_ATTR_RW(level_a);

static ssize_t timing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", port_of(dev)->reference ? "reference" : "internal");
}

static ssize_t timing_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct ajv4l2_port *port = port_of(dev);

	if (sysfs_streq(buf, "reference"))
		port->reference = true;
	else if (sysfs_streq(buf, "internal"))
		port->reference = false;
	else
		return -EINVAL;
	if (port->streaming)
		ajv4l2_hw_set_reference(port);
	return count;
}
static DEVICE_ATTR_RW(timing);

/*
 * Colour of the frames that state none: read by the output thread at the
 * next frame it transfers.
 */
static ssize_t colorimetry_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", port_of(dev)->out_rec2020 ? "rec2020" : "rec709");
}

static ssize_t colorimetry_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ajv4l2_port *port = port_of(dev);

	if (sysfs_streq(buf, "rec2020"))
		port->out_rec2020 = true;
	else if (sysfs_streq(buf, "rec709"))
		port->out_rec2020 = false;
	else
		return -EINVAL;
	return count;
}
static DEVICE_ATTR_RW(colorimetry);

static const struct {
	const char *name;
	u8 xfer;
} eotf_names[] = {
	{ "sdr", NTV2_VPID_TC_SDR_TV },
	{ "hlg", NTV2_VPID_TC_HLG },
	{ "pq", NTV2_VPID_TC_PQ },
};

static ssize_t eotf_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 xfer = port_of(dev)->out_xfer;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(eotf_names); i++)
		if (eotf_names[i].xfer == xfer)
			break;
	return sysfs_emit(buf, "%s\n", eotf_names[i < ARRAY_SIZE(eotf_names) ? i : 0].name);
}

static ssize_t eotf_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(eotf_names); i++)
		if (sysfs_streq(buf, eotf_names[i].name)) {
			port_of(dev)->out_xfer = eotf_names[i].xfer;
			return count;
		}
	return -EINVAL;
}
static DEVICE_ATTR_RW(eotf);

/* What plays when nothing is queued: the ring keeps the last frame on air. */
static ssize_t idle_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "repeat\n");
}
static DEVICE_ATTR_RO(idle);

/*
 * Genlock as one word of the shared dictionary. This card tells whether
 * its reference input carries anything and nothing more, so a present
 * signal is unknown, not locked: the reader is asking whether the picture
 * going out is timed by that signal, and the card does not say. An output
 * told to run on the card's own clock is the one case where the answer is
 * certain, and it is unlocked.
 */
static ssize_t reference_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ajv4l2_port *port = port_of(dev);

	if (!ajv4l2_hw_reference_present(port->dev))
		return sysfs_emit(buf, "no_reference\n");
	return sysfs_emit(buf, "%s\n", port->reference ? "unknown" : "unlocked");
}
static DEVICE_ATTR_RO(reference);

static struct attribute *ajv4l2_port_attrs[] = {
	&dev_attr_frames.attr,
	&dev_attr_frames_skipped.attr,
	&dev_attr_no_buffer.attr,
	&dev_attr_crc_errors.attr,
	&dev_attr_dma_errors.attr,
	&dev_attr_signal.attr,
	NULL
};

static const struct attribute_group ajv4l2_port_group = {
	.attrs = ajv4l2_port_attrs,
};

static struct attribute *ajv4l2_output_attrs[] = {
	&dev_attr_frames.attr,
	&dev_attr_frames_skipped.attr,
	&dev_attr_dma_errors.attr,
	&dev_attr_anc_dropped.attr,
	&dev_attr_signal.attr,
	&dev_attr_level_a.attr,
	&dev_attr_timing.attr,
	&dev_attr_colorimetry.attr,
	&dev_attr_eotf.attr,
	&dev_attr_idle.attr,
	&dev_attr_reference.attr,
	NULL
};

static const struct attribute_group ajv4l2_output_group = {
	.attrs = ajv4l2_output_attrs,
};

static const struct attribute_group *group_of(struct ajv4l2_port *port)
{
	return port->output ? &ajv4l2_output_group : &ajv4l2_port_group;
}

int ajv4l2_sysfs_add(struct ajv4l2_port *port)
{
	return sysfs_create_group(&port->vdev.dev.kobj, group_of(port));
}

void ajv4l2_sysfs_remove(struct ajv4l2_port *port)
{
	sysfs_remove_group(&port->vdev.dev.kobj, group_of(port));
}
