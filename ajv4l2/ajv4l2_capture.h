/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AJV4L2_CAPTURE_H
#define AJV4L2_CAPTURE_H

#include "ajv4l2.h"

int ajv4l2_capture_init(struct ajv4l2_port *port);
void ajv4l2_capture_exit(struct ajv4l2_port *port);
int ajv4l2_capture_buf_init(struct vb2_buffer *vb);
void ajv4l2_capture_buf_cleanup(struct vb2_buffer *vb);
int ajv4l2_capture_start(struct ajv4l2_port *port);
void ajv4l2_capture_stop(struct ajv4l2_port *port);
void ajv4l2_capture_kick(struct ajv4l2_port *port);
void ajv4l2_return_buffers(struct ajv4l2_port *port, enum vb2_buffer_state state);
u32 ajv4l2_row_bytes(u32 pixfmt, u32 width);

/* shared with the output engine */
#define AJV4L2_RING_FRAMES	8
#define AJV4L2_ANC_FIELD_BYTES	0x2000
#define AJV4L2_WAIT_MS		100
int ajv4l2_anc_bounce_alloc(struct ajv4l2_port *port);
void ajv4l2_anc_bounce_free(struct ajv4l2_port *port);
void ajv4l2_ring_frames(const struct ajv4l2_port *port, u32 *first, u32 *last);
int ajv4l2_ring_check(struct ajv4l2_port *port);
int ajv4l2_ring_fits(struct ajv4l2_port *port);

#endif
