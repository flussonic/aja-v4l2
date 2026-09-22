// SPDX-License-Identifier: GPL-2.0
/*
 * Frame playout. The driver core's autocirculate engine plays a ring of
 * frames in card memory for the channel: on every output vertical
 * interrupt it moves to the next frame the ring holds, and keeps the
 * current one on air when there is none. A thread per output port moves
 * every queued V4L2 buffer into the ring while the ring has room: the
 * picture and the audio straight from their planes by DMA, the ancillary
 * packets as the inserter's byte stream, built in a bounce buffer per
 * field. The ring is ordered, so the buffers come back in the order they
 * went in, each once its frame has been on air and the next one has
 * replaced it.
 */
#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#include "ajv4l2_output.h"
#include "ajv4l2_capture.h"
#include "ajv4l2_hw.h"

/* The autocirculate crosspoint of an output channel: the enum is not contiguous. */
static NTV2Crosspoint output_xpt(unsigned int ch)
{
	static const NTV2Crosspoint xpt[8] = {
		NTV2CROSSPOINT_CHANNEL1, NTV2CROSSPOINT_CHANNEL2, NTV2CROSSPOINT_CHANNEL3, NTV2CROSSPOINT_CHANNEL4,
		NTV2CROSSPOINT_CHANNEL5, NTV2CROSSPOINT_CHANNEL6, NTV2CROSSPOINT_CHANNEL7, NTV2CROSSPOINT_CHANNEL8,
	};

	return ch < 8 ? xpt[ch] : NTV2CROSSPOINT_INVALID;
}

/* The first line of the second field of an interlaced standard, 0 for progressive. */
static u16 field2_first_line(const struct ajv4l2_mode *m)
{
	if (!(m->flags & (AJV4L2_MODE_F_INTERLACED | AJV4L2_MODE_F_PSF)))
		return 0;
	switch (m->total_lines) {
	case 1125: return 563;
	case 625: return 313;
	case 525: return 263;
	default: return m->total_lines / 2 + 1;
	}
}

/* The audio groups: the embedder places them itself. */
static bool packet_is_audio(u8 did)
{
	return (did >= 0xe0 && did <= 0xe7) || (did >= 0xa0 && did <= 0xa7);
}

/*
 * The inserter's byte stream, the extractor's in reverse: 0xff, the two
 * location bytes, DID, SDID, DC, the low byte of every word and the 8-bit
 * sum of DID, SDID, DC and the payload. Field 2 packets go to the second
 * buffer. Returns the bytes of each field's stream, rounded up to the
 * DMA's word, in bytes[]; the count of what was left out in dropped; a
 * payload identifier the plane carries, as the word the output register
 * takes, in vpid (0 when there is none): the transmitter inserts that
 * one itself.
 */
static void anc_build(struct ajv4l2_port *port, const u8 *in, u32 in_bytes, u32 bytes[2],
		      u32 *dropped, u32 *vpid)
{
	u16 f2 = field2_first_line(port->mode);
	u32 ip = 0, op[2] = { 0, 0 };
	unsigned int f;

	*dropped = 0;
	*vpid = 0;
	while (ip + sizeof(struct sdi_anc_packet) <= in_bytes) {
		const struct sdi_anc_packet *pkt = (const void *)(in + ip);
		u32 dc = pkt->data_count, len = SDI_ANC_PACKET_BYTES(dc), i, sum;
		u8 *out;

		/* an all-zero header ends the list */
		if (!pkt->line && !pkt->hoffset && !pkt->did && !pkt->sdid && !dc && !pkt->flags)
			break;
		if (ip + len > in_bytes)
			break;
		ip += len;
		if (pkt->did == 0x41 && pkt->sdid == 0x01 && dc == 4) {
			*vpid = (pkt->udw[0] & 0xff) | (pkt->udw[1] & 0xff) << 8 |
				(pkt->udw[2] & 0xff) << 16 | (pkt->udw[3] & 0xff) << 24;
			continue;
		}
		f = f2 && pkt->line >= f2 ? 1 : 0;
		if (dc > 255 || pkt->line == 0 || pkt->line > port->mode->total_lines ||
		    packet_is_audio(pkt->did) ||
		    op[f] + 7 + dc > AJV4L2_ANC_FIELD_BYTES) {
			(*dropped)++;
			continue;
		}
		out = port->anc[f].buf + op[f];
		out[0] = 0xff;
		out[1] = 0x80 | (pkt->flags & SDI_ANC_F_HANC ? 0x10 : 0) |
			 (pkt->flags & SDI_ANC_F_CHROMA ? 0 : 0x20) | ((pkt->line >> 7) & 0x0f);
		out[2] = pkt->line & 0x7f;
		out[3] = pkt->did;
		out[4] = pkt->sdid;
		out[5] = dc;
		sum = pkt->did + pkt->sdid + dc;
		for (i = 0; i < dc; i++) {
			out[6 + i] = pkt->udw[i] & 0xff;
			sum += out[6 + i];
		}
		out[6 + dc] = sum & 0xff;
		op[f] += 7 + dc;
	}
	for (f = 0; f < 2; f++) {
		u32 padded = ALIGN(op[f], 4);

		/* a zero byte after the last packet ends the stream for the inserter */
		if (padded < AJV4L2_ANC_FIELD_BYTES)
			memset(port->anc[f].buf + op[f], 0, min_t(u32, padded + 4, AJV4L2_ANC_FIELD_BYTES) - op[f]);
		bytes[f] = padded;
	}
}

static struct ajv4l2_buffer *next_buffer(struct ajv4l2_port *port)
{
	struct ajv4l2_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&port->qlock, flags);
	if (!list_empty(&port->queued)) {
		buf = list_first_entry(&port->queued, struct ajv4l2_buffer, list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&port->qlock, flags);
	return buf;
}

static bool buffers_queued(struct ajv4l2_port *port)
{
	unsigned long flags;
	bool queued;

	spin_lock_irqsave(&port->qlock, flags);
	queued = !list_empty(&port->queued);
	spin_unlock_irqrestore(&port->qlock, flags);
	return queued;
}

/* One buffer into the next free frame of the ring. */
static int transfer_frame(struct ajv4l2_port *port, struct ajv4l2_buffer *buf,
			  AUTOCIRCULATE_TRANSFER *xfer)
{
	struct ajv4l2_device *dev = port->dev;
	struct vb2_buffer *vb = &buf->vb.vb2_buf;
	struct v4l2_pix_format_mplane pix;
	u32 audio_bytes, anc_bytes[2], dropped, vpid;
	int ret;

	ajv4l2_video_geometry(port, port->pixfmt, port->mode, &pix);
	audio_bytes = vb2_get_plane_payload(vb, SDI_PLANE_AUDIO);
	audio_bytes -= audio_bytes % SDI_AUDIO_FRAME_BYTES;
	anc_build(port, vb2_plane_vaddr(vb, SDI_PLANE_ANC), vb2_get_plane_payload(vb, SDI_PLANE_ANC),
		  anc_bytes, &dropped, &vpid);
	port->anc_dropped += dropped;
	if (vpid)
		ajv4l2_hw_set_vpid(port, vpid);
	dma_sync_single_for_device(&dev->pdev->dev, port->anc[0].dma, AJV4L2_ANC_FIELD_BYTES, DMA_TO_DEVICE);
	dma_sync_single_for_device(&dev->pdev->dev, port->anc[1].dma, AJV4L2_ANC_FIELD_BYTES, DMA_TO_DEVICE);

	memset(xfer, 0, sizeof(*xfer));
	xfer->acHeader.fSizeInBytes = sizeof(*xfer);
	xfer->acCrosspoint = output_xpt(port->index);
	xfer->acDesiredFrame = NTV2_INVALID_FRAME;
	xfer->acFrameBufferFormat = ajv4l2_hw_fbf(port->pixfmt);
	xfer->acFrameRepeatCount = 1;
	xfer->acVideoBuffer.fUserSpacePtr = (ULWord64)(uintptr_t)vb2_dma_sg_plane_desc(vb, SDI_PLANE_VIDEO);
	xfer->acVideoBuffer.fByteCount = pix.plane_fmt[SDI_PLANE_VIDEO].sizeimage;
	if (audio_bytes) {
		xfer->acAudioBuffer.fUserSpacePtr = (ULWord64)(uintptr_t)vb2_dma_sg_plane_desc(vb, SDI_PLANE_AUDIO);
		xfer->acAudioBuffer.fByteCount = audio_bytes;
	}
	xfer->acANCBuffer.fUserSpacePtr = (ULWord64)(uintptr_t)port->anc[0].buf;
	xfer->acANCBuffer.fByteCount = anc_bytes[0];
	xfer->acANCField2Buffer.fUserSpacePtr = (ULWord64)(uintptr_t)port->anc[1].buf;
	xfer->acANCField2Buffer.fByteCount = anc_bytes[1];
	ret = AutoCirculateTransfer_Ex(dev->device_number, &port->page_root, xfer);
	if (ret)
		return ret;
	if (xfer->acTransferStatus.acTransferFrame == NTV2_INVALID_FRAME)
		return -EIO;
	return 0;
}

/*
 * Buffers whose frames have left the air: the ring holds `level` frames
 * that are still to be played or are on air, the rest of what went in
 * has been replaced.
 */
static void complete_played(struct ajv4l2_port *port, unsigned int level)
{
	unsigned long flags;
	unsigned int held = 0;
	struct ajv4l2_buffer *buf;
	LIST_HEAD(done);

	spin_lock_irqsave(&port->qlock, flags);
	list_for_each_entry(buf, &port->on_air, list)
		held++;
	while (held > level && !list_empty(&port->on_air)) {
		buf = list_first_entry(&port->on_air, struct ajv4l2_buffer, list);
		list_move_tail(&buf->list, &done);
		held--;
	}
	spin_unlock_irqrestore(&port->qlock, flags);
	while (!list_empty(&done)) {
		buf = list_first_entry(&done, struct ajv4l2_buffer, list);
		list_del(&buf->list);
		buf->vb.sequence = port->sequence++;
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		port->frames++;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

static int output_thread(void *data)
{
	struct ajv4l2_port *port = data;
	NTV2PrivateParams *pp = port->dev->pp;
	unsigned int ev = ajv4l2_hw_output_event(port->index);
	AUTOCIRCULATE_TRANSFER *xfer;
	u32 dropped_seen = 0;

	xfer = kzalloc(sizeof(*xfer), GFP_KERNEL);
	if (!xfer)
		return -ENOMEM;
	while (!kthread_should_stop()) {
		AUTOCIRCULATE_STATUS status;
		ULWord64 count = pp->_interruptCount[ev];
		unsigned int level;

		wait_event_interruptible_timeout(pp->_interruptWait[ev],
						 count != pp->_interruptCount[ev] || kthread_should_stop() ||
						 buffers_queued(port),
						 msecs_to_jiffies(AJV4L2_WAIT_MS));
		if (kthread_should_stop())
			break;
		memset(&status, 0, sizeof(status));
		status.acCrosspoint = output_xpt(port->index);
		if (AutoCirculateStatus_Ex(port->dev->device_number, &status))
			continue;
		if (status.acFramesDropped != dropped_seen) {
			port->frames_skipped += status.acFramesDropped - dropped_seen;
			dropped_seen = status.acFramesDropped;
		}
		level = status.acBufferLevel;
		complete_played(port, level);
		/* one frame of the ring stays free, as the core's own player keeps it */
		while (level + 1 < AJV4L2_RING_FRAMES && !kthread_should_stop()) {
			struct ajv4l2_buffer *buf = next_buffer(port);
			unsigned long flags;

			if (!buf)
				break;
			if (transfer_frame(port, buf, xfer)) {
				port->dma_errors++;
				vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
				break;
			}
			spin_lock_irqsave(&port->qlock, flags);
			list_add_tail(&buf->list, &port->on_air);
			spin_unlock_irqrestore(&port->qlock, flags);
			level = xfer->acTransferStatus.acBufferLevel;
		}
	}
	kfree(xfer);
	return 0;
}

int ajv4l2_output_start(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;
	NTV2Crosspoint xpt = output_xpt(port->index);
	u32 first, last;
	int ret;

	ret = ajv4l2_ring_check(port);
	if (ret)
		return ret;
	ret = ajv4l2_hw_setup_output(port);
	if (ret)
		return ret;
	ret = ajv4l2_ring_fits(port);
	if (ret)
		return ret;
	ajv4l2_ring_frames(port, &first, &last);
	ret = ajv4l2_anc_bounce_alloc(port);
	if (ret)
		return ret;
	ret = OemAutoCirculateInit(dev->device_number, xpt, first, last, (NTV2AudioSystem)port->index, 1,
				   true, false, false, false, false, false, true, false, false, false,
				   false, false, false);
	if (ret) {
		dev_err(&dev->pdev->dev, "SDI %u: autocirculate init failed (%d)\n", port->index + 1, ret);
		goto err_anc;
	}
	ret = OemAutoCirculateStart(dev->device_number, xpt, 0);
	if (ret) {
		dev_err(&dev->pdev->dev, "SDI %u: autocirculate start failed (%d)\n", port->index + 1, ret);
		goto err_ac;
	}
	port->thread = kthread_run(output_thread, port, "ajv4l2-%s-out%u", pci_name(dev->pdev), port->index + 1);
	if (IS_ERR(port->thread)) {
		ret = PTR_ERR(port->thread);
		port->thread = NULL;
		goto err_ac;
	}
	port->streaming = true;
	return 0;

err_ac:
	OemAutoCirculateAbort(dev->device_number, xpt);
err_anc:
	ajv4l2_anc_bounce_free(port);
	return ret;
}

void ajv4l2_output_stop(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;
	struct ajv4l2_buffer *buf, *tmp;
	unsigned long flags;

	port->streaming = false;
	if (port->thread) {
		kthread_stop(port->thread);
		port->thread = NULL;
	}
	OemAutoCirculateAbort(dev->device_number, output_xpt(port->index));
	ajv4l2_anc_bounce_free(port);
	spin_lock_irqsave(&port->qlock, flags);
	list_for_each_entry_safe(buf, tmp, &port->on_air, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&port->qlock, flags);
}

void ajv4l2_output_kick(struct ajv4l2_port *port)
{
	NTV2PrivateParams *pp = port->dev->pp;

	if (port->streaming)
		wake_up_interruptible(&pp->_interruptWait[ajv4l2_hw_output_event(port->index)]);
}

/* What the output is doing, for sysfs and LOG_STATUS. */
const char *ajv4l2_output_describe(const struct ajv4l2_port *port, char *buf, size_t len)
{
	if (!port->streaming)
		snprintf(buf, len, "idle");
	else
		snprintf(buf, len, "%s on air, %s", port->mode->name,
			 ajv4l2_hw_reference_present(port->dev) && port->reference ? "reference" : "free-running");
	return buf;
}
