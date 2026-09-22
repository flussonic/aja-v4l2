// SPDX-License-Identifier: GPL-2.0
/*
 * The node of a port: the V4L2 ioctls, the vb2 queue and the media
 * entities, for the capture node of an input and the output node of a
 * connector that transmits. The frames themselves are moved by
 * ajv4l2_capture.c and ajv4l2_output.c.
 */
#include "ajv4l2.h"
#include "ajv4l2_capture.h"
#include "ajv4l2_output.h"
#include "ajav.h"

static const u32 ajv4l2_pixfmts[] = { SDI_PIX_FMT_UYVY, SDI_PIX_FMT_V210 };

static bool pixfmt_supported(u32 f)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ajv4l2_pixfmts); i++)
		if (ajv4l2_pixfmts[i] == f)
			return true;
	return false;
}

/* Bytes of one picture line as the frame store lays it out. */
u32 ajv4l2_row_bytes(u32 pixfmt, u32 width)
{
	if (pixfmt == SDI_PIX_FMT_V210)
		return ((width + 47) / 48) * 128;
	return width * 2;
}

void ajv4l2_video_geometry(struct ajv4l2_port *port, u32 pixfmt, const struct ajv4l2_mode *m,
			   struct v4l2_pix_format_mplane *pix)
{
	u32 row = ajv4l2_row_bytes(pixfmt, m->width);

	memset(pix, 0, sizeof(*pix));
	pix->width = m->width;
	pix->height = m->height;
	pix->pixelformat = pixfmt;
	pix->field = m->flags & (AJV4L2_MODE_F_INTERLACED | AJV4L2_MODE_F_PSF) ?
		     V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
	pix->colorspace = m->height >= 720 ? V4L2_COLORSPACE_REC709 : V4L2_COLORSPACE_SMPTE170M;
	pix->num_planes = SDI_NUM_PLANES;
	pix->plane_fmt[SDI_PLANE_VIDEO].bytesperline = row;
	pix->plane_fmt[SDI_PLANE_VIDEO].sizeimage = row * m->height;
	pix->plane_fmt[SDI_PLANE_AUDIO].sizeimage = SDI_AUDIO_PLANE_SIZE;
	pix->plane_fmt[SDI_PLANE_ANC].sizeimage = SDI_ANC_PLANE_SIZE;
	pix->plane_fmt[SDI_PLANE_META].sizeimage = AJAV_META_BYTES;
	pix->plane_fmt[SDI_PLANE_VBI].sizeimage = SDI_VBI_PLANE_SIZE;
}

/* vb2 */

static int ajv4l2_queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
			      unsigned int *nplanes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct ajv4l2_port *port = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane pix;
	unsigned int i;

	ajv4l2_video_geometry(port, port->pixfmt, port->mode, &pix);
	if (*nplanes) {
		if (*nplanes != SDI_NUM_PLANES)
			return -EINVAL;
		for (i = 0; i < SDI_NUM_PLANES; i++)
			if (sizes[i] < pix.plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}
	*nplanes = SDI_NUM_PLANES;
	for (i = 0; i < SDI_NUM_PLANES; i++)
		sizes[i] = pix.plane_fmt[i].sizeimage;
	return 0;
}

static int ajv4l2_buf_prepare(struct vb2_buffer *vb)
{
	struct ajv4l2_port *port = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format_mplane pix;
	unsigned int i;

	ajv4l2_video_geometry(port, port->pixfmt, port->mode, &pix);
	for (i = 0; i < SDI_NUM_PLANES; i++)
		if (vb2_plane_size(vb, i) < pix.plane_fmt[i].sizeimage)
			return -EINVAL;
	/* an output frame is a whole picture; the other planes carry what they carry */
	if (port->output && vb2_get_plane_payload(vb, SDI_PLANE_VIDEO) < pix.plane_fmt[SDI_PLANE_VIDEO].sizeimage)
		return -EINVAL;
	return 0;
}

static void ajv4l2_buf_queue(struct vb2_buffer *vb)
{
	struct ajv4l2_port *port = vb2_get_drv_priv(vb->vb2_queue);
	struct ajv4l2_buffer *buf = to_ajv4l2_buffer(vb);
	unsigned long flags;

	spin_lock_irqsave(&port->qlock, flags);
	list_add_tail(&buf->list, &port->queued);
	spin_unlock_irqrestore(&port->qlock, flags);
	if (port->output)
		ajv4l2_output_kick(port);
	else
		ajv4l2_capture_kick(port);
}

void ajv4l2_return_buffers(struct ajv4l2_port *port, enum vb2_buffer_state state)
{
	struct ajv4l2_buffer *buf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&port->qlock, flags);
	list_for_each_entry_safe(buf, tmp, &port->queued, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&port->qlock, flags);
}

static int ajv4l2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct ajv4l2_port *port = vb2_get_drv_priv(q);
	int ret;

	port->sequence = 0;
	port->frames = port->frames_skipped = port->no_buffer = 0;
	port->resyncs = port->no_sync = port->events_missed = 0;
	port->crc_errors = port->dma_errors = port->restarts = 0;
	port->anc_dropped = port->audio_dropped = 0;
	ret = port->output ? ajv4l2_output_start(port) : ajv4l2_capture_start(port);
	if (ret)
		ajv4l2_return_buffers(port, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void ajv4l2_stop_streaming(struct vb2_queue *q)
{
	struct ajv4l2_port *port = vb2_get_drv_priv(q);

	if (port->output)
		ajv4l2_output_stop(port);
	else
		ajv4l2_capture_stop(port);
	ajv4l2_return_buffers(port, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops ajv4l2_vb2_ops = {
	.queue_setup = ajv4l2_queue_setup,
	.buf_init = ajv4l2_capture_buf_init,
	.buf_cleanup = ajv4l2_capture_buf_cleanup,
	.buf_prepare = ajv4l2_buf_prepare,
	.buf_queue = ajv4l2_buf_queue,
	.start_streaming = ajv4l2_start_streaming,
	.stop_streaming = ajv4l2_stop_streaming,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
#endif
};

/* ioctls */

static int ajv4l2_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct ajv4l2_port *port = video_drvdata(file);

	strscpy(cap->driver, AJV4L2_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, port->vdev.name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s", pci_name(port->dev->pdev));
	return 0;
}

static int ajv4l2_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	static const char *const descr[] = { "SDI frame, 4:2:2 8-bit UYVY", "SDI frame, 4:2:2 10-bit v210" };

	if (f->index >= ARRAY_SIZE(ajv4l2_pixfmts))
		return -EINVAL;
	f->pixelformat = ajv4l2_pixfmts[f->index];
	strscpy(f->description, descr[f->index], sizeof(f->description));
	return 0;
}

static int ajv4l2_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct ajv4l2_port *port = video_drvdata(file);

	ajv4l2_video_geometry(port, port->pixfmt, port->mode, &f->fmt.pix_mp);
	return 0;
}

static int ajv4l2_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct ajv4l2_port *port = video_drvdata(file);
	u32 pixfmt = f->fmt.pix_mp.pixelformat;

	if (!pixfmt_supported(pixfmt))
		pixfmt = SDI_PIX_FMT_UYVY;
	ajv4l2_video_geometry(port, pixfmt, port->mode, &f->fmt.pix_mp);
	return 0;
}

static int ajv4l2_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct ajv4l2_port *port = video_drvdata(file);

	if (vb2_is_busy(&port->queue))
		return -EBUSY;
	ajv4l2_try_fmt(file, fh, f);
	port->pixfmt = f->fmt.pix_mp.pixelformat;
	return 0;
}

static int ajv4l2_enum_output(struct file *file, void *fh, struct v4l2_output *out)
{
	struct ajv4l2_port *port = video_drvdata(file);

	if (out->index)
		return -EINVAL;
	snprintf(out->name, sizeof(out->name), "SDI %u", port->index + 1);
	out->type = V4L2_OUTPUT_TYPE_ANALOG;
	out->capabilities = V4L2_OUT_CAP_DV_TIMINGS;
	return 0;
}

static int ajv4l2_g_output(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int ajv4l2_s_output(struct file *file, void *fh, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int ajv4l2_enum_input(struct file *file, void *fh, struct v4l2_input *inp)
{
	struct ajv4l2_port *port = video_drvdata(file);
	struct ajv4l2_input_state st;

	if (inp->index)
		return -EINVAL;
	snprintf(inp->name, sizeof(inp->name), "SDI %u", port->index + 1);
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	ajv4l2_input_read(port, &st);
	if (!st.locked)
		inp->status = V4L2_IN_ST_NO_SIGNAL;
	else if (!ajv4l2_mode_for_format(st.format))
		inp->status = V4L2_IN_ST_NO_SYNC;
	return 0;
}

static int ajv4l2_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int ajv4l2_s_input(struct file *file, void *fh, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int ajv4l2_query_dv_timings(struct file *file, void *fh, struct v4l2_dv_timings *t)
{
	struct ajv4l2_port *port = video_drvdata(file);
	struct ajv4l2_input_state st;
	const struct ajv4l2_mode *m;

	ajv4l2_input_read(port, &st);
	if (!st.locked)
		return -ENOLINK;
	m = ajv4l2_mode_for_format(st.format);
	if (!m)
		return -ERANGE;
	ajv4l2_mode_timings(m, t);
	return 0;
}

static int ajv4l2_s_dv_timings(struct file *file, void *fh, struct v4l2_dv_timings *t)
{
	struct ajv4l2_port *port = video_drvdata(file);
	const struct ajv4l2_mode *m;

	m = ajv4l2_mode_for_timings(t);
	if (!m)
		return -ERANGE;
	if (v4l2_match_dv_timings(t, &port->timings, 0, false))
		return 0;
	if (vb2_is_busy(&port->queue))
		return -EBUSY;
	ajv4l2_mode_timings(m, &port->timings);
	port->mode = m;
	*t = port->timings;
	return 0;
}

static int ajv4l2_g_dv_timings(struct file *file, void *fh, struct v4l2_dv_timings *t)
{
	struct ajv4l2_port *port = video_drvdata(file);

	*t = port->timings;
	return 0;
}

static int ajv4l2_enum_dv_timings(struct file *file, void *fh, struct v4l2_enum_dv_timings *e)
{
	unsigned int i, n = 0;

	for (i = 0; i < ajv4l2_num_modes; i++) {
		if (ajv4l2_modes[i].flags & AJV4L2_MODE_F_LEVEL_B)
			continue;
		if (n++ == e->index) {
			ajv4l2_mode_timings(&ajv4l2_modes[i], &e->timings);
			return 0;
		}
	}
	return -EINVAL;
}

static int ajv4l2_dv_timings_cap(struct file *file, void *fh, struct v4l2_dv_timings_cap *cap)
{
	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.min_width = 720;
	cap->bt.max_width = 3840;
	cap->bt.min_height = 486;
	cap->bt.max_height = 2160;
	cap->bt.min_pixelclock = 13500000;
	cap->bt.max_pixelclock = 594000000;
	cap->bt.standards = V4L2_DV_BT_STD_SDI;
	cap->bt.capabilities = V4L2_DV_BT_CAP_INTERLACED | V4L2_DV_BT_CAP_PROGRESSIVE |
			       V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM;
	return 0;
}

static int ajv4l2_log_status(struct file *file, void *fh)
{
	struct ajv4l2_port *port = video_drvdata(file);
	struct ajv4l2_input_state st;
	char buf[64];

	if (port->output) {
		v4l2_info(&port->dev->v4l2_dev, "%s: output %s, set %s, level %c, %s\n",
			  port->vdev.name, ajv4l2_output_describe(port, buf, sizeof(buf)),
			  port->mode->name, port->level_a ? 'A' : 'B',
			  port->reference ? "reference" : "internal");
	} else {
		ajv4l2_input_read(port, &st);
		v4l2_info(&port->dev->v4l2_dev, "%s: input %s (status 0x%08x link 0x%02x vpid %08x/%08x, %s), set %s, %s\n",
			  port->vdev.name, ajv4l2_input_describe(&st, buf, sizeof(buf)), st.status,
			  st.link_status, st.vpid_a, st.vpid_b,
			  ajv4l2_input_is_receiving(port) ? "receiving" : "transmitting", port->mode->name,
			  port->streaming ? "streaming" : "idle");
	}
	v4l2_info(&port->dev->v4l2_dev, "%s: frames %llu skipped %llu no_buffer %llu resyncs %llu no_sync %llu events_missed %llu crc_errors %llu dma_errors %llu restarts %llu anc_dropped %llu audio_dropped %llu\n",
		  port->vdev.name, port->frames, port->frames_skipped, port->no_buffer,
		  port->resyncs, port->no_sync, port->events_missed, port->crc_errors,
		  port->dma_errors, port->restarts, port->anc_dropped, port->audio_dropped);
	return 0;
}

/* The one control: whether the receiver sees a carrier, read live. */
static int ajv4l2_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ajv4l2_port *port = container_of(ctrl->handler, struct ajv4l2_port, ctrl_handler);
	struct ajv4l2_input_state st;

	if (ctrl->id != V4L2_CID_DV_RX_POWER_PRESENT)
		return -EINVAL;
	ajv4l2_input_read(port, &st);
	ctrl->val = st.locked ? 1 : 0;
	return 0;
}

static const struct v4l2_ctrl_ops ajv4l2_ctrl_ops = {
	.g_volatile_ctrl = ajv4l2_g_volatile_ctrl,
};

static int ajv4l2_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	if (sub->type == V4L2_EVENT_SOURCE_CHANGE)
		return v4l2_src_change_event_subscribe(fh, sub);
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static const struct v4l2_ioctl_ops ajv4l2_ioctl_ops = {
	.vidioc_querycap = ajv4l2_querycap,
	.vidioc_enum_fmt_vid_cap = ajv4l2_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = ajv4l2_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = ajv4l2_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = ajv4l2_s_fmt,
	.vidioc_enum_input = ajv4l2_enum_input,
	.vidioc_g_input = ajv4l2_g_input,
	.vidioc_s_input = ajv4l2_s_input,
	.vidioc_query_dv_timings = ajv4l2_query_dv_timings,
	.vidioc_s_dv_timings = ajv4l2_s_dv_timings,
	.vidioc_g_dv_timings = ajv4l2_g_dv_timings,
	.vidioc_enum_dv_timings = ajv4l2_enum_dv_timings,
	.vidioc_dv_timings_cap = ajv4l2_dv_timings_cap,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_log_status = ajv4l2_log_status,
	.vidioc_subscribe_event = ajv4l2_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_ioctl_ops ajv4l2_output_ioctl_ops = {
	.vidioc_querycap = ajv4l2_querycap,
	.vidioc_enum_fmt_vid_out = ajv4l2_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane = ajv4l2_g_fmt,
	.vidioc_try_fmt_vid_out_mplane = ajv4l2_try_fmt,
	.vidioc_s_fmt_vid_out_mplane = ajv4l2_s_fmt,
	.vidioc_enum_output = ajv4l2_enum_output,
	.vidioc_g_output = ajv4l2_g_output,
	.vidioc_s_output = ajv4l2_s_output,
	.vidioc_s_dv_timings = ajv4l2_s_dv_timings,
	.vidioc_g_dv_timings = ajv4l2_g_dv_timings,
	.vidioc_enum_dv_timings = ajv4l2_enum_dv_timings,
	.vidioc_dv_timings_cap = ajv4l2_dv_timings_cap,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_log_status = ajv4l2_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations ajv4l2_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll,
};

int ajv4l2_video_register(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;
	struct vb2_queue *q = &port->queue;
	struct video_device *vdev = &port->vdev;
	struct ajv4l2_input_state st;
	const struct ajv4l2_mode *m;
	struct v4l2_ctrl *ctrl;
	char buf[64];
	int ret;

	mutex_init(&port->lock);
	spin_lock_init(&port->qlock);
	INIT_LIST_HEAD(&port->queued);
	INIT_LIST_HEAD(&port->on_air);
	port->pixfmt = SDI_PIX_FMT_UYVY;
	port->level_a = true;
	port->reference = true;
	ret = ajv4l2_capture_init(port);
	if (ret)
		return ret;

	/* A connector of a bidirectional card is an input until its output node streams. */
	if (!port->output)
		ajv4l2_input_set_direction(port, true);
	ajv4l2_input_read(port, &st);
	m = st.locked && !port->output ? ajv4l2_mode_for_format(st.format) : NULL;
	if (!m)
		m = ajv4l2_mode_for_format(NTV2_FORMAT_1080i_5000);
	port->mode = m;
	ajv4l2_mode_timings(m, &port->timings);

	q->type = port->output ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = port;
	q->buf_struct_size = sizeof(struct ajv4l2_buffer);
	q->ops = &ajv4l2_vb2_ops;
	q->mem_ops = &vb2_dma_sg_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	ajv4l2_queue_min_buffers(q, 2);
	q->lock = &port->lock;
	q->dev = &dev->pdev->dev;
	ret = vb2_queue_init(q);
	if (ret)
		return ret;

	v4l2_ctrl_handler_init(&port->ctrl_handler, 1);
	if (!port->output) {
		ctrl = v4l2_ctrl_new_std(&port->ctrl_handler, &ajv4l2_ctrl_ops,
					 V4L2_CID_DV_RX_POWER_PRESENT, 0, 1, 0, 0);
		if (ctrl)
			ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;
	}
	ret = port->ctrl_handler.error;
	if (ret)
		goto err_ctrl;

	snprintf(vdev->name, sizeof(vdev->name), "%s SDI %s %u", dev->model,
		 port->output ? "out" : "in", port->index + 1);
	vdev->fops = &ajv4l2_fops;
	vdev->ioctl_ops = port->output ? &ajv4l2_output_ioctl_ops : &ajv4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->lock = &port->lock;
	vdev->queue = q;
	vdev->v4l2_dev = &dev->v4l2_dev;
	vdev->ctrl_handler = &port->ctrl_handler;
	vdev->vfl_dir = port->output ? VFL_DIR_TX : VFL_DIR_RX;
	vdev->device_caps = (port->output ? V4L2_CAP_VIDEO_OUTPUT_MPLANE : V4L2_CAP_VIDEO_CAPTURE_MPLANE) |
			    V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, port);

	/* Media graph: connector "SDI n" -> the capture node, the output node -> connector "SDI out n". */
	port->vdev_pad.flags = port->output ? MEDIA_PAD_FL_SOURCE : MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &port->vdev_pad);
	if (ret)
		goto err_ctrl;
	snprintf(port->connector_name, sizeof(port->connector_name), "SDI %s%u",
		 port->output ? "out " : "", port->index + 1);
	port->connector.name = port->connector_name;
	port->connector.function = port->output ? MEDIA_ENT_F_DV_ENCODER : MEDIA_ENT_F_DV_DECODER;
	port->connector_pads[0].flags = port->output ? MEDIA_PAD_FL_SINK : MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&port->connector, 1, port->connector_pads);
	if (!ret)
		ret = media_device_register_entity(&dev->mdev, &port->connector);
	if (ret)
		goto err_entity;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_connector;
	if (port->output)
		ret = media_create_pad_link(&vdev->entity, 0, &port->connector, 0,
					    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	else
		ret = media_create_pad_link(&port->connector, 0, &vdev->entity, 0,
					    MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		dev_warn(&dev->pdev->dev, "SDI %u: no media link (%d)\n", port->index + 1, ret);
	if (ajv4l2_sysfs_add(port))
		dev_warn(&dev->pdev->dev, "SDI %u: no sysfs counters\n", port->index + 1);
	if (port->output) {
		dev_info(&dev->pdev->dev, "SDI out %u: %s\n", port->index + 1, video_device_node_name(vdev));
	} else {
		ajv4l2_input_poll_start(port);
		dev_info(&dev->pdev->dev, "SDI %u: %s, %s\n", port->index + 1,
			 video_device_node_name(vdev), ajv4l2_input_describe(&st, buf, sizeof(buf)));
	}
	return 0;

err_connector:
	media_device_unregister_entity(&port->connector);
	media_entity_cleanup(&port->connector);
err_entity:
	media_entity_cleanup(&vdev->entity);
err_ctrl:
	v4l2_ctrl_handler_free(&port->ctrl_handler);
	vb2_queue_release(q);
	return ret;
}

void ajv4l2_video_unregister(struct ajv4l2_port *port)
{
	if (!port->output)
		ajv4l2_input_poll_stop(port);
	ajv4l2_sysfs_remove(port);
	video_unregister_device(&port->vdev);
	media_device_unregister_entity(&port->connector);
	media_entity_cleanup(&port->connector);
	media_entity_cleanup(&port->vdev.entity);
	v4l2_ctrl_handler_free(&port->ctrl_handler);
	vb2_queue_release(&port->queue);
	ajv4l2_capture_exit(port);
	mutex_destroy(&port->lock);
}
