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

#endif
