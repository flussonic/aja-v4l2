/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AJV4L2_HW_H
#define AJV4L2_HW_H

#include "ajv4l2.h"

int ajv4l2_hw_setup_capture(struct ajv4l2_port *port);
u32 ajv4l2_hw_audio_present(struct ajv4l2_port *port);
NTV2FrameBufferFormat ajv4l2_hw_fbf(u32 pixfmt);

#endif
