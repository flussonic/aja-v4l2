// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (C) 2026 Max Lapshin <max@flussonic.com>
/*
 * The SDI receiver of a port: whether it is locked, what standard it sees,
 * the payload identifier and the CRC counter; the direction of a
 * bidirectional connector; and the poll that turns a change of any of it
 * into V4L2_EVENT_SOURCE_CHANGE.
 */
#include "ajv4l2.h"

#define AJV4L2_POLL_MS	100

/* Register blocks of the receivers, one per SDI input. */
static u32 rxsdi_status_reg(unsigned int ch)
{
	return kRegRXSDI1Status + 8 * ch;
}

static u32 rxsdi_crc_reg(unsigned int ch)
{
	return kRegRXSDI1CRCErrorCount + 8 * ch;
}

static const u32 vpid_a_reg[8] = {
	kRegSDIIn1VPIDA, kRegSDIIn2VPIDA, kRegSDIIn3VPIDA, kRegSDIIn4VPIDA,
	kRegSDIIn5VPIDA, kRegSDIIn6VPIDA, kRegSDIIn7VPIDA, kRegSDIIn8VPIDA,
};

static const u32 vpid_b_reg[8] = {
	kRegSDIIn1VPIDB, kRegSDIIn2VPIDB, kRegSDIIn3VPIDB, kRegSDIIn4VPIDB,
	kRegSDIIn5VPIDB, kRegSDIIn6VPIDB, kRegSDIIn7VPIDB, kRegSDIIn8VPIDB,
};

/* The 3G/6G/12G status words: two inputs per register. */
static const u32 in3g_reg[8] = {
	kRegSDIInput3GStatus, kRegSDIInput3GStatus, kRegSDIInput3GStatus2, kRegSDIInput3GStatus2,
	kRegSDI5678Input3GStatus, kRegSDI5678Input3GStatus, kRegSDI5678Input3GStatus, kRegSDI5678Input3GStatus,
};

static u32 in3g_shift(unsigned int ch)
{
	if (ch < 4)
		return (ch & 1) ? 8 : 0;
	return (ch - 4) * 8;
}

/* The transmit-enable bits of kRegSDITransmitControl. */
static const u32 xmit_mask[8] = {
	kRegMaskSDI1Transmit, kRegMaskSDI2Transmit, kRegMaskSDI3Transmit, kRegMaskSDI4Transmit,
	kRegMaskSDI5Transmit, kRegMaskSDI6Transmit, kRegMaskSDI7Transmit, kRegMaskSDI8Transmit,
};

/*
 * The core reports a 6G/12G link by the 1080-line raster of its
 * sub-images; the single-link 2160p standard of the same rate is what the
 * frame store receives.
 */
static NTV2VideoFormat quad_sized(NTV2VideoFormat f)
{
	const struct ajv4l2_mode *m = ajv4l2_mode_for_format(f);
	unsigned int i;

	if (!m || m->height != 1080)
		return f;
	for (i = 0; i < ajv4l2_num_modes; i++) {
		const struct ajv4l2_mode *q = &ajv4l2_modes[i];

		if (q->flags & AJV4L2_MODE_F_QUAD && q->width == 2 * m->width &&
		    q->fps_num == m->fps_num && q->fps_den == m->fps_den)
			return q->format;
	}
	return f;
}

void ajv4l2_input_set_direction(struct ajv4l2_port *port, bool receive)
{
	Ntv2SystemContext *ctx = port->dev->ctx;
	u32 reg;

	if (!port->dev->bidirectional_sdi || port->index >= 8)
		return;
	reg = ntv2ReadRegister(ctx, kRegSDITransmitControl);
	if (receive)
		reg &= ~xmit_mask[port->index];
	else
		reg |= xmit_mask[port->index];
	ntv2WriteRegister(ctx, kRegSDITransmitControl, reg);
}

bool ajv4l2_input_is_receiving(struct ajv4l2_port *port)
{
	if (!port->dev->bidirectional_sdi || port->index >= 8)
		return true;
	return !(ntv2ReadRegister(port->dev->ctx, kRegSDITransmitControl) & xmit_mask[port->index]);
}

void ajv4l2_input_read(struct ajv4l2_port *port, struct ajv4l2_input_state *st)
{
	Ntv2SystemContext *ctx = port->dev->ctx;
	unsigned int ch = port->index;
	u32 status, s3g, crc;

	memset(st, 0, sizeof(*st));
	if (ch >= 8)
		return;
	status = ntv2ReadRegister(ctx, rxsdi_status_reg(ch));
	st->status = status;
	st->locked = !!(status & kRegMaskSDIInLocked);
	st->vpid_a_valid = !!(status & kRegMaskSDIInVpidValidA);
	st->vpid_b_valid = !!(status & kRegMaskSDIInVpidValidB);
	/* The VPID registers hold the four wire bytes in reverse order. */
	if (st->vpid_a_valid)
		st->vpid_a = be32_to_cpu(ntv2ReadRegister(ctx, vpid_a_reg[ch]));
	if (st->vpid_b_valid)
		st->vpid_b = be32_to_cpu(ntv2ReadRegister(ctx, vpid_b_reg[ch]));
	crc = ntv2ReadRegister(ctx, rxsdi_crc_reg(ch));
	st->crc_errors = (crc & 0xffff) + (crc >> 16);

	s3g = ntv2ReadRegister(ctx, in3g_reg[ch]) >> in3g_shift(ch);
	st->link_status = s3g & 0xff;
	st->rate_3g = !!(s3g & kRegMaskSDIIn3GbpsMode);
	st->level_b = !!(s3g & kRegMaskSDIIn3GbpsSMPTELevelBMode);
	st->rate_6g = !!(s3g & kRegMaskSDIIn16GbpsMode);
	st->rate_12g = !!(s3g & kRegMaskSDIIn112GbpsMode);

	st->format = st->locked ? GetInputVideoFormat(ctx, (NTV2Channel)ch) : NTV2_FORMAT_UNKNOWN;
	if (st->format != NTV2_FORMAT_UNKNOWN && (st->rate_6g || st->rate_12g))
		st->format = quad_sized(st->format);
}

const char *ajv4l2_input_describe(const struct ajv4l2_input_state *st, char *buf, size_t len)
{
	const struct ajv4l2_mode *m;

	if (!st->locked) {
		snprintf(buf, len, "no signal");
		return buf;
	}
	m = ajv4l2_mode_for_format(st->format);
	snprintf(buf, len, "%s%s%s", m ? m->name : "unknown standard",
		 st->rate_12g ? " 12G" : st->rate_6g ? " 6G" : st->rate_3g ? " 3G" : "",
		 st->level_b ? " level B" : "");
	return buf;
}

static bool input_changed(const struct ajv4l2_input_state *a, const struct ajv4l2_input_state *b)
{
	return a->locked != b->locked || a->format != b->format || a->level_b != b->level_b;
}

static void ajv4l2_input_poll(struct work_struct *work)
{
	struct ajv4l2_port *port = container_of(work, struct ajv4l2_port, poll_work.work);
	struct ajv4l2_input_state st;

	ajv4l2_input_read(port, &st);
	if (input_changed(&st, &port->input)) {
		static const struct v4l2_event ev = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};
		char buf[64];

		port->input = st;
		v4l2_event_queue(&port->vdev, &ev);
		dev_info(&port->dev->pdev->dev, "SDI %u: %s\n", port->index + 1,
			 ajv4l2_input_describe(&st, buf, sizeof(buf)));
	} else {
		port->input = st;
	}
	schedule_delayed_work(&port->poll_work, msecs_to_jiffies(AJV4L2_POLL_MS));
}

void ajv4l2_input_poll_start(struct ajv4l2_port *port)
{
	INIT_DELAYED_WORK(&port->poll_work, ajv4l2_input_poll);
	ajv4l2_input_read(port, &port->input);
	schedule_delayed_work(&port->poll_work, msecs_to_jiffies(AJV4L2_POLL_MS));
}

void ajv4l2_input_poll_stop(struct ajv4l2_port *port)
{
	cancel_delayed_work_sync(&port->poll_work);
}
