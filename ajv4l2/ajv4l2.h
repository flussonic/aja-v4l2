/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The V4L2 layer over the ntv2 driver core. One struct ajv4l2_device per
 * card holds what the core knows about it (its number in the core's
 * tables, the PCI device, the device id) and the media device; one struct
 * ajv4l2_port per SDI connector holds the capture node, its queue and
 * the state of the input. The frame contract the nodes follow is
 * include/sdi_av.h, the same file in every SDI driver of ours.
 */
#ifndef AJV4L2_H
#define AJV4L2_H

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dv-timings.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>
#include <media/media-device.h>
#include <media/media-entity.h>

#include "ajatypes.h"
#include "buildenv.h"
#include "ntv2enums.h"
#include "ntv2videodefines.h"
#include "ntv2audiodefines.h"
#include "ntv2publicinterface.h"
#include "ntv2linuxpublicinterface.h"
#include "ntv2devicefeatures.h"
#include "ntv2driver.h"
#include "registerio.h"
#include "ntv2dma.h"
#include "ntv2driverautocirculate.h"
#include "ntv2kona.h"
#include "ntv2xpt.h"
#include "ntv2anc.h"
#include "ntv2vpid.h"

#include "sdi_av.h"
#include "ajv4l2_hook.h"

#define AJV4L2_MAX_PORTS	8

/* Kernel API drift the module spans. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define ajv4l2_queue_min_buffers(q, n)	((q)->min_queued_buffers = (n))
#else
#define ajv4l2_queue_min_buffers(q, n)	((q)->min_buffers_needed = (n))
#endif

/*
 * A video standard the card can receive: the NTV2 name the core's
 * detector reports and the raster behind it. Timings are derived from the
 * raster (ajv4l2_mode_timings), the raster from timings the client sets
 * (ajv4l2_mode_for_timings).
 */
#define AJV4L2_MODE_F_INTERLACED	(1u << 0)
#define AJV4L2_MODE_F_PSF		(1u << 1)
#define AJV4L2_MODE_F_LEVEL_B		(1u << 2)
#define AJV4L2_MODE_F_QUAD		(1u << 3)	/* 6G/12G single link 2160p */

struct ajv4l2_mode {
	NTV2VideoFormat format;
	const char *name;
	u16 width, height, total_lines;
	u32 fps_num, fps_den;
	u32 flags;
	/* what the frame store is told: the global control register fields */
	NTV2Standard standard;
	NTV2FrameGeometry geometry;
	NTV2FrameRate rate;
};

extern const struct ajv4l2_mode ajv4l2_modes[];
extern const unsigned int ajv4l2_num_modes;

const struct ajv4l2_mode *ajv4l2_mode_for_format(NTV2VideoFormat f);
const struct ajv4l2_mode *ajv4l2_mode_for_timings(const struct v4l2_dv_timings *t);
void ajv4l2_mode_timings(const struct ajv4l2_mode *m, struct v4l2_dv_timings *t);
bool ajv4l2_mode_matches_timings(const struct ajv4l2_mode *m, const struct v4l2_dv_timings *t);

/* What the SDI receiver of a port reports. */
struct ajv4l2_input_state {
	bool locked;		/* the receiver is locked to a signal */
	NTV2VideoFormat format;	/* NTV2_FORMAT_UNKNOWN when nothing usable */
	bool level_b;
	bool rate_6g, rate_12g, rate_3g;
	u32 vpid_a, vpid_b;
	bool vpid_a_valid, vpid_b_valid;
	u32 crc_errors;		/* the card's cumulative counter, link A + B */
	u32 status;		/* raw kRegRXSDInStatus */
};

struct ajv4l2_device;

struct ajv4l2_port {
	struct ajv4l2_device *dev;
	unsigned int index;		/* SDI connector, 0-based */
	NTV2Channel channel;		/* frame store and input of the same number */

	struct video_device vdev;
	struct media_pad vdev_pad;
	struct media_entity connector;
	struct media_pad connector_pads[1];
	char connector_name[16];
	struct v4l2_ctrl_handler ctrl_handler;
	struct mutex lock;		/* ioctls and the queue */
	struct vb2_queue queue;
	spinlock_t qlock;		/* the buffer lists */
	struct list_head queued;	/* buffers waiting for a frame */

	struct v4l2_dv_timings timings;	/* S_DV_TIMINGS */
	const struct ajv4l2_mode *mode;	/* the same, as a table entry */
	u32 pixfmt;			/* SDI_PIX_FMT_* */

	/* input state as last polled; changes raise V4L2_EVENT_SOURCE_CHANGE */
	struct ajv4l2_input_state input;
	struct delayed_work poll_work;

	/* capture engine */
	DMA_PAGE_ROOT page_root;	/* the planes of every buffer, as the core's DMA sees them */
	struct ajv4l2_anc_bounce {	/* the extractor's bytes, one per field */
		u8 *buf;
		dma_addr_t dma;
		struct scatterlist sg;
	} anc[2];
	struct task_struct *thread;
	bool streaming;
	u32 sequence;
	u32 crc_errors_base;

	/* counters since STREAMON, sysfs */
	u64 frames, frames_skipped, no_buffer, resyncs, no_sync, events_missed;
	u64 crc_errors, dma_errors, restarts;
};

struct ajv4l2_device {
	unsigned int device_number;	/* index in the core's device table */
	NTV2PrivateParams *pp;
	Ntv2SystemContext *ctx;
	struct pci_dev *pdev;
	NTV2DeviceID device_id;
	const char *model;
	char serial[32];
	unsigned int sdi_inputs;
	unsigned int sdi_outputs;
	unsigned int channels;
	bool bidirectional_sdi;

	struct v4l2_device v4l2_dev;
	struct media_device mdev;
	struct device *hwmon;
	struct ajv4l2_port *ports[AJV4L2_MAX_PORTS];
	unsigned int num_ports;
};

struct ajv4l2_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static inline struct ajv4l2_buffer *to_ajv4l2_buffer(struct vb2_buffer *vb)
{
	return container_of(to_vb2_v4l2_buffer(vb), struct ajv4l2_buffer, vb);
}

/* ajv4l2_input.c */
void ajv4l2_input_read(struct ajv4l2_port *port, struct ajv4l2_input_state *st);
void ajv4l2_input_set_direction(struct ajv4l2_port *port, bool receive);
void ajv4l2_input_poll_start(struct ajv4l2_port *port);
void ajv4l2_input_poll_stop(struct ajv4l2_port *port);
const char *ajv4l2_input_describe(const struct ajv4l2_input_state *st, char *buf, size_t len);

/* ajv4l2_hwmon.c */
int ajv4l2_hwmon_register(struct ajv4l2_device *dev);
void ajv4l2_hwmon_unregister(struct ajv4l2_device *dev);

/* ajv4l2_sysfs.c */
int ajv4l2_sysfs_add(struct ajv4l2_port *port);
void ajv4l2_sysfs_remove(struct ajv4l2_port *port);

/* ajv4l2_video.c */
int ajv4l2_video_register(struct ajv4l2_port *port);
void ajv4l2_video_unregister(struct ajv4l2_port *port);
void ajv4l2_video_geometry(struct ajv4l2_port *port, u32 pixfmt, const struct ajv4l2_mode *m,
			   struct v4l2_pix_format_mplane *pix);

#endif
