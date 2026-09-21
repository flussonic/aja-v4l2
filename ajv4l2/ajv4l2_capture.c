// SPDX-License-Identifier: GPL-2.0
/*
 * Frame capture. The driver core's autocirculate engine keeps a ring of
 * frames in card memory for the channel and, on every input vertical
 * interrupt, advances it and stamps the frame (time, audio ring position,
 * timecode, ancillary bytes). A thread per port waits for that interrupt
 * and, for every frame the ring holds, transfers it into the next queued
 * V4L2 buffer: the picture and the audio straight into their planes by
 * DMA, the ancillary data into a bounce buffer, from which the extractor's
 * byte stream is turned into the packet records of the contract.
 */
#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#include "ajv4l2_capture.h"
#include "ajv4l2_hw.h"
#include "ajav.h"

#define AJV4L2_RING_FRAMES	8
#define AJV4L2_ANC_FIELD_BYTES	0x2000
#define AJV4L2_WAIT_MS		100

/* The autocirculate crosspoint of an input channel. */
static NTV2Crosspoint input_xpt(unsigned int ch)
{
	return (NTV2Crosspoint)(NTV2CROSSPOINT_INPUT1 + ch);
}

/* DMA-able planes: the three the card writes by DMA. */
static bool plane_by_dma(unsigned int plane)
{
	return plane == SDI_PLANE_VIDEO || plane == SDI_PLANE_AUDIO;
}

int ajv4l2_capture_buf_init(struct vb2_buffer *vb)
{
	struct ajv4l2_port *port = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int i;
	int ret;

	for (i = 0; i < SDI_NUM_PLANES; i++) {
		struct sg_table *sgt;

		if (!plane_by_dma(i))
			continue;
		sgt = vb2_dma_sg_plane_desc(vb, i);
		if (!sgt)
			return -EINVAL;
		ret = dmaPageRootAddSg(port->dev->device_number, &port->page_root, sgt,
				       vb2_plane_size(vb, i), sgt->sgl, sgt->nents, DMA_FROM_DEVICE);
		if (ret) {
			while (i--)
				if (plane_by_dma(i))
					dmaPageRootRemove(port->dev->device_number, &port->page_root,
							  vb2_dma_sg_plane_desc(vb, i), vb2_plane_size(vb, i));
			return ret;
		}
	}
	return 0;
}

void ajv4l2_capture_buf_cleanup(struct vb2_buffer *vb)
{
	struct ajv4l2_port *port = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int i;

	for (i = 0; i < SDI_NUM_PLANES; i++)
		if (plane_by_dma(i))
			dmaPageRootRemove(port->dev->device_number, &port->page_root,
					  vb2_dma_sg_plane_desc(vb, i), vb2_plane_size(vb, i));
}

/*
 * The bounce buffers for the ancillary data of the two fields, each
 * registered with the core's DMA under its own address: the transfer names
 * them separately and the core looks each up by the exact address.
 */
static int anc_bounce_alloc(struct ajv4l2_port *port)
{
	struct device *dev = &port->dev->pdev->dev;
	unsigned int f;
	int ret;

	for (f = 0; f < 2; f++) {
		struct ajv4l2_anc_bounce *b = &port->anc[f];

		b->buf = kmalloc(AJV4L2_ANC_FIELD_BYTES, GFP_KERNEL);
		if (!b->buf)
			goto err;
		b->dma = dma_map_single(dev, b->buf, AJV4L2_ANC_FIELD_BYTES, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, b->dma)) {
			kfree(b->buf);
			b->buf = NULL;
			goto err;
		}
		sg_init_table(&b->sg, 1);
		sg_set_buf(&b->sg, b->buf, AJV4L2_ANC_FIELD_BYTES);
		sg_dma_address(&b->sg) = b->dma;
		sg_dma_len(&b->sg) = AJV4L2_ANC_FIELD_BYTES;
		ret = dmaPageRootAddSg(port->dev->device_number, &port->page_root, b->buf,
				       AJV4L2_ANC_FIELD_BYTES, &b->sg, 1, DMA_FROM_DEVICE);
		if (ret) {
			dma_unmap_single(dev, b->dma, AJV4L2_ANC_FIELD_BYTES, DMA_FROM_DEVICE);
			kfree(b->buf);
			b->buf = NULL;
			goto err;
		}
	}
	return 0;
err:
	while (f--) {
		struct ajv4l2_anc_bounce *b = &port->anc[f];

		dmaPageRootRemove(port->dev->device_number, &port->page_root, b->buf, AJV4L2_ANC_FIELD_BYTES);
		dma_unmap_single(dev, b->dma, AJV4L2_ANC_FIELD_BYTES, DMA_FROM_DEVICE);
		kfree(b->buf);
		b->buf = NULL;
	}
	return -ENOMEM;
}

static void anc_bounce_free(struct ajv4l2_port *port)
{
	unsigned int f;

	for (f = 0; f < 2; f++) {
		struct ajv4l2_anc_bounce *b = &port->anc[f];

		if (!b->buf)
			continue;
		dmaPageRootRemove(port->dev->device_number, &port->page_root, b->buf, AJV4L2_ANC_FIELD_BYTES);
		dma_unmap_single(&port->dev->pdev->dev, b->dma, AJV4L2_ANC_FIELD_BYTES, DMA_FROM_DEVICE);
		kfree(b->buf);
		b->buf = NULL;
	}
}

/*
 * The extractor's byte stream: packets back to back, each 0xff, two
 * location bytes (bit 7 of the first says the location is real; bit 4 HANC,
 * bit 5 Y stream, bits 3..0 and the second byte the line), DID, SDID, DC,
 * DC payload bytes (the low 8 bits of each word) and an 8-bit sum of DID,
 * SDID, DC and the payload. The words get their parity bits back and the
 * sum is checked.
 */
static u16 udw_word(u8 b)
{
	u16 w = b;

	if (hweight8(b) & 1)
		w |= 0x100;
	else
		w |= 0x200;
	return w;
}

static u32 anc_parse(const u8 *in, u32 in_bytes, u8 *out, u32 out_bytes, bool field2)
{
	u32 ip = 0, op = 0;

	while (ip + 7 <= in_bytes) {
		struct sdi_anc_packet *pkt;
		u32 dc, bytes, i, sum;
		u16 line;

		if (in[ip] != 0xff)
			break;
		dc = in[ip + 5];
		if (ip + 7 + dc > in_bytes)
			break;
		bytes = SDI_ANC_PACKET_BYTES(dc);
		if (op + bytes > out_bytes)
			break;
		pkt = (struct sdi_anc_packet *)(out + op);
		memset(pkt, 0, sizeof(*pkt));
		line = ((in[ip + 1] & 0x0f) << 7) | (in[ip + 2] & 0x7f);
		pkt->line = line;
		pkt->hoffset = 0;
		pkt->did = in[ip + 3];
		pkt->sdid = in[ip + 4];
		pkt->data_count = dc;
		if (in[ip + 1] & 0x80) {
			if (in[ip + 1] & 0x10)
				pkt->flags |= SDI_ANC_F_HANC;
			if (!(in[ip + 1] & 0x20))
				pkt->flags |= SDI_ANC_F_CHROMA;
		}
		sum = in[ip + 3] + in[ip + 4] + in[ip + 5];
		for (i = 0; i < dc; i++) {
			u8 b = in[ip + 6 + i];

			sum += b;
			pkt->udw[i] = udw_word(b);
		}
		if ((sum & 0xff) != in[ip + 6 + dc])
			pkt->flags |= SDI_ANC_F_CS_ERROR;
		if (dc & 1)
			pkt->udw[dc] = 0;
		ip += 7 + dc;
		op += bytes;
	}
	return op;
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

/*
 * CLOCK_MONOTONIC of the start of a frame: the core stamps it with the
 * card's audio sample counter scaled to 100 ns units (GetAudioClock), and
 * that clock read now against the monotonic clock now places it.
 */
static u64 audio_clock_to_monotonic_ns(struct ajv4l2_port *port, u64 at)
{
	u64 now_audio = GetAudioClock(port->dev->device_number);
	u64 now_mono = ktime_get_ns();
	u64 ago = now_audio > at ? (now_audio - at) * 100 : 0;

	return now_mono > ago ? now_mono - ago : 0;
}

/*
 * The flags the payload identifier states (SMPTE ST 352): the register
 * holds byte 1 in bits 31..24 down to byte 4 in bits 7..0. Sampling is
 * byte 3 bits 3..0, transfer characteristics byte 2 bits 5..4, colorimetry
 * byte 3 bits 5..4 -- except in the 1080-line standards, where the
 * colorimetry code is split over byte 3 bit 4 and byte 3 bit 7.
 */
static u32 vpid_flags(u32 vpid)
{
	u32 standard = vpid >> 24, sampling = (vpid >> 8) & 0xf, xfer = (vpid >> 20) & 3;
	u32 colorimetry, flags = 0;

	if (standard == 0x85 || standard == 0x87 || standard == 0x8a || standard == 0x96 ||
	    standard == 0x98)
		colorimetry = (((vpid >> 15) & 1) << 1) | ((vpid >> 12) & 1);
	else
		colorimetry = (vpid >> 12) & 3;
	if (sampling == 2 || sampling == 6 || sampling == 0xa)	/* GBR 4:4:4, GBRA, GBRD */
		flags |= SDI_F_RGB;
	if (colorimetry == 2)
		flags |= SDI_F_REC2020;
	if (xfer == 1)
		flags |= SDI_F_HLG;
	else if (xfer == 2)
		flags |= SDI_F_PQ;
	return flags;
}

static void fill_meta(struct ajv4l2_port *port, struct vb2_buffer *vb,
		      const AUTOCIRCULATE_TRANSFER *xfer, const struct ajv4l2_input_state *st,
		      u32 audio_samples)
{
	const AUTOCIRCULATE_TRANSFER_STATUS *ts = &xfer->acTransferStatus;
	struct sdi_meta *m = vb2_plane_vaddr(vb, SDI_PLANE_META);
	struct ajav_meta *v;
	u32 crc = st->crc_errors;

	if (!m)
		return;
	memset(m, 0, AJAV_META_BYTES);
	m->magic = SDI_META_MAGIC;
	m->version = SDI_META_VERSION;
	if (st->level_b)
		m->flags |= SDI_F_LEVEL_B;
	if (port->mode->flags & AJV4L2_MODE_F_PSF)
		m->flags |= SDI_F_PSF;
	if (st->vpid_a_valid)
		m->flags |= vpid_flags(st->vpid_a);
	m->crc_errors = crc - port->crc_errors_base;
	port->crc_errors_base = crc;
	port->crc_errors += m->crc_errors;
	/* the card's audio sample counter at the start of the frame, in ns */
	m->hw_timestamp = ts->acFrameStamp.acAudioClockTimeStamp * 100;
	m->audio_present = ajv4l2_hw_audio_present(port);
	m->audio_samples[0] = audio_samples;
	m->audio_rate = SDI_AUDIO_RATE;
	m->vendor_bytes = sizeof(struct ajav_meta);
	m->vendor_magic = AJAV_VENDOR_MAGIC;
	m->vendor_version = AJAV_VENDOR_VERSION;
	v = (struct ajav_meta *)(m + 1);
	v->rp188_dbb = ts->acFrameStamp.acRP188.fDBB;
	v->rp188_low = ts->acFrameStamp.acRP188.fLo;
	v->rp188_high = ts->acFrameStamp.acRP188.fHi;
	v->rx_status = st->status;
	v->vpid_a = st->vpid_a;
	v->vpid_b = st->vpid_b;
	v->frames_dropped = ts->acFramesDropped;
	vb2_set_plane_payload(vb, SDI_PLANE_META, AJAV_META_BYTES);
}

/* One frame of the ring into a buffer, or out of the ring when there is none. */
static int transfer_frame(struct ajv4l2_port *port, struct ajv4l2_buffer *buf,
			  AUTOCIRCULATE_TRANSFER *xfer)
{
	struct ajv4l2_device *dev = port->dev;
	struct vb2_buffer *vb = buf ? &buf->vb.vb2_buf : NULL;
	struct v4l2_pix_format_mplane pix;
	int ret;

	memset(xfer, 0, sizeof(*xfer));
	xfer->acHeader.fSizeInBytes = sizeof(*xfer);
	xfer->acCrosspoint = input_xpt(port->index);
	xfer->acDesiredFrame = NTV2_INVALID_FRAME;
	xfer->acFrameBufferFormat = ajv4l2_hw_fbf(port->pixfmt);
	if (vb) {
		ajv4l2_video_geometry(port, port->pixfmt, port->mode, &pix);
		xfer->acVideoBuffer.fUserSpacePtr = (ULWord64)(uintptr_t)vb2_dma_sg_plane_desc(vb, SDI_PLANE_VIDEO);
		xfer->acVideoBuffer.fByteCount = pix.plane_fmt[SDI_PLANE_VIDEO].sizeimage;
		xfer->acAudioBuffer.fUserSpacePtr = (ULWord64)(uintptr_t)vb2_dma_sg_plane_desc(vb, SDI_PLANE_AUDIO);
		xfer->acAudioBuffer.fByteCount = vb2_plane_size(vb, SDI_PLANE_AUDIO);
		xfer->acANCBuffer.fUserSpacePtr = (ULWord64)(uintptr_t)port->anc[0].buf;
		xfer->acANCBuffer.fByteCount = AJV4L2_ANC_FIELD_BYTES;
		xfer->acANCField2Buffer.fUserSpacePtr = (ULWord64)(uintptr_t)port->anc[1].buf;
		xfer->acANCField2Buffer.fByteCount = AJV4L2_ANC_FIELD_BYTES;
	}
	ret = AutoCirculateTransfer_Ex(dev->device_number, &port->page_root, xfer);
	if (ret)
		return ret;
	if (xfer->acTransferStatus.acTransferFrame == NTV2_INVALID_FRAME)
		return -EIO;
	return 0;
}

static void finish_buffer(struct ajv4l2_port *port, struct ajv4l2_buffer *buf,
			  const AUTOCIRCULATE_TRANSFER *xfer)
{
	const AUTOCIRCULATE_TRANSFER_STATUS *ts = &xfer->acTransferStatus;
	struct vb2_buffer *vb = &buf->vb.vb2_buf;
	struct ajv4l2_input_state st;
	struct v4l2_pix_format_mplane pix;
	u32 audio_bytes, anc_bytes = 0;
	u8 *anc;

	ajv4l2_input_read(port, &st);
	ajv4l2_video_geometry(port, port->pixfmt, port->mode, &pix);
	vb2_set_plane_payload(vb, SDI_PLANE_VIDEO, pix.plane_fmt[SDI_PLANE_VIDEO].sizeimage);
	audio_bytes = ts->acAudioTransferSize - ts->acAudioTransferSize % SDI_AUDIO_FRAME_BYTES;
	if (audio_bytes > vb2_plane_size(vb, SDI_PLANE_AUDIO))
		audio_bytes = 0;
	vb2_set_plane_payload(vb, SDI_PLANE_AUDIO, audio_bytes);

	dma_sync_single_for_cpu(&port->dev->pdev->dev, port->anc[0].dma, AJV4L2_ANC_FIELD_BYTES, DMA_FROM_DEVICE);
	dma_sync_single_for_cpu(&port->dev->pdev->dev, port->anc[1].dma, AJV4L2_ANC_FIELD_BYTES, DMA_FROM_DEVICE);
	anc = vb2_plane_vaddr(vb, SDI_PLANE_ANC);
	if (anc) {
		u32 room = vb2_plane_size(vb, SDI_PLANE_ANC);

		anc_bytes = anc_parse(port->anc[0].buf, min_t(u32, ts->acAncTransferSize, AJV4L2_ANC_FIELD_BYTES),
				      anc, room, false);
		anc_bytes += anc_parse(port->anc[1].buf,
				       min_t(u32, ts->acAncField2TransferSize, AJV4L2_ANC_FIELD_BYTES),
				       anc + anc_bytes, room - anc_bytes, true);
	}
	vb2_set_plane_payload(vb, SDI_PLANE_ANC, anc_bytes);
	vb2_set_plane_payload(vb, SDI_PLANE_VBI, 0);
	fill_meta(port, vb, xfer, &st, audio_bytes / SDI_AUDIO_FRAME_BYTES);

	buf->vb.sequence = port->sequence++ + ts->acFramesDropped;
	buf->vb.field = pix.field;
	vb->timestamp = audio_clock_to_monotonic_ns(port, ts->acFrameStamp.acAudioClockTimeStamp);
	port->frames++;
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
}

static int capture_thread(void *data)
{
	struct ajv4l2_port *port = data;
	NTV2PrivateParams *pp = port->dev->pp;
	unsigned int ev = eInput1 + port->index;
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
						 count != pp->_interruptCount[ev] || kthread_should_stop(),
						 msecs_to_jiffies(AJV4L2_WAIT_MS));
		if (kthread_should_stop())
			break;
		memset(&status, 0, sizeof(status));
		status.acCrosspoint = input_xpt(port->index);
		if (AutoCirculateStatus_Ex(port->dev->device_number, &status))
			continue;
		if (status.acFramesDropped != dropped_seen) {
			port->frames_skipped += status.acFramesDropped - dropped_seen;
			dropped_seen = status.acFramesDropped;
		}
		level = status.acBufferLevel;
		while (level > 0 && !kthread_should_stop()) {
			struct ajv4l2_buffer *buf = next_buffer(port);

			if (transfer_frame(port, buf, xfer)) {
				port->dma_errors++;
				if (buf) {
					vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
					port->frames_skipped++;
				}
				break;
			}
			if (buf) {
				finish_buffer(port, buf, xfer);
			} else {
				port->no_buffer++;
				port->sequence++;
			}
			level = xfer->acTransferStatus.acBufferLevel;
		}
	}
	kfree(xfer);
	return 0;
}

int ajv4l2_capture_start(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;
	NTV2Crosspoint xpt = input_xpt(port->index);
	u32 first = port->index * AJV4L2_RING_FRAMES, last = first + AJV4L2_RING_FRAMES - 1;
	u32 frame_bytes, memory;
	struct ajv4l2_input_state st;
	int ret;

	ret = ajv4l2_hw_setup_capture(port);
	if (ret)
		return ret;
	frame_bytes = GetFrameBufferSize(dev->ctx, port->channel);
	memory = NTV2DeviceGetActiveMemorySize(dev->device_id);
	if (!frame_bytes || (u64)(last + 1) * frame_bytes > memory) {
		dev_err(&dev->pdev->dev, "SDI %u: frames %u..%u of %u bytes do not fit in %u bytes of card memory\n",
			port->index + 1, first, last, frame_bytes, memory);
		return -ENOSPC;
	}
	ret = anc_bounce_alloc(port);
	if (ret)
		return ret;
	ajv4l2_input_read(port, &st);
	port->crc_errors_base = st.crc_errors;

	ret = OemAutoCirculateInit(dev->device_number, xpt, first, last, (NTV2AudioSystem)port->index, 1,
				   true, true, false, false, false, false, true, false, false, false,
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
	port->thread = kthread_run(capture_thread, port, "ajv4l2-%s-in%u", pci_name(dev->pdev), port->index + 1);
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
	anc_bounce_free(port);
	return ret;
}

void ajv4l2_capture_stop(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;

	port->streaming = false;
	if (port->thread) {
		kthread_stop(port->thread);
		port->thread = NULL;
	}
	OemAutoCirculateAbort(dev->device_number, input_xpt(port->index));
	anc_bounce_free(port);
}

void ajv4l2_capture_kick(struct ajv4l2_port *port)
{
	NTV2PrivateParams *pp = port->dev->pp;

	if (port->streaming)
		wake_up_interruptible(&pp->_interruptWait[eInput1 + port->index]);
}

int ajv4l2_capture_init(struct ajv4l2_port *port)
{
	return dmaPageRootInit(port->dev->device_number, &port->page_root);
}

void ajv4l2_capture_exit(struct ajv4l2_port *port)
{
	dmaPageRootRelease(port->dev->device_number, &port->page_root);
}
