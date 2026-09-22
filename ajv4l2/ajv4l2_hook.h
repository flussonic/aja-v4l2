/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Copyright (C) 2026 Max Lapshin <max@flussonic.com> */
/*
 * What the vendor driver core sees of the V4L2 layer: the name the PCI
 * driver carries and the two calls made from its probe and remove. Nothing
 * else of ours is visible from that side.
 */
#ifndef AJV4L2_HOOK_H
#define AJV4L2_HOOK_H

#define AJV4L2_DRIVER_NAME "ajv4l2"

int ajv4l2_attach(unsigned int device_number);
void ajv4l2_detach(unsigned int device_number);

#endif
