/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Copyright (C) 2026 Max Lapshin <max@flussonic.com> */
#ifndef AJV4L2_HW_H
#define AJV4L2_HW_H

#include "ajv4l2.h"

int ajv4l2_hw_setup_capture(struct ajv4l2_port *port);
int ajv4l2_hw_setup_output(struct ajv4l2_port *port);
void ajv4l2_hw_set_reference(struct ajv4l2_port *port);
bool ajv4l2_hw_reference_present(struct ajv4l2_device *dev);
void ajv4l2_hw_set_level(struct ajv4l2_port *port);
void ajv4l2_hw_set_vpid(struct ajv4l2_port *port, u32 vpid);
void ajv4l2_hw_set_hdr(struct ajv4l2_port *port, bool stated, bool rec2020, u8 xfer);
void ajv4l2_hw_set_timecode_output(struct ajv4l2_port *port, bool on);
unsigned int ajv4l2_hw_output_event(unsigned int ch);
u32 ajv4l2_hw_audio_present(struct ajv4l2_port *port);
NTV2FrameBufferFormat ajv4l2_hw_fbf(u32 pixfmt);
bool ajv4l2_hw_has_hdmi(struct ajv4l2_device *dev);
void ajv4l2_hw_set_hdmi(struct ajv4l2_port *port, bool on);
bool ajv4l2_hw_hdmi_sink(struct ajv4l2_device *dev);
size_t ajv4l2_hw_hdmi_edid(struct ajv4l2_device *dev, u8 *buf, size_t len);

#endif
