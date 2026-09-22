/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR MIT) */
/* Copyright (C) 2026 Max Lapshin <max@flussonic.com> */
/*
 * What an AJA card adds to the SDI frame contract (sdi_av.h): its name in
 * the vendor tail of the metadata and the block that follows the common
 * part. Nothing here is needed to read the frame; a client that does not
 * know AJAV reads the common part and skips the block.
 */
#ifndef AJAV_H
#define AJAV_H

#include "sdi_av.h"

#define AJAV_VENDOR_MAGIC	v4l2_fourcc('A', 'J', 'A', 'V')
#define AJAV_VENDOR_VERSION	1

/*
 * Vendor block of an AJA card, right after the common part of sdi_meta:
 * the raw receiver status words of the input at the time of the frame,
 * for diagnostics. Everything a client needs regardless of the card --
 * the timecode, the payload identifier, lost frames -- is where the
 * contract puts it (the ANC plane, v4l2_buffer.sequence), not here.
 */
struct ajav_meta {
	__u32 rx_status;	/* receiver status register: lock, unlock count, VPID valid, TRS error */
	__u32 rx_link_status;	/* 3G/6G/12G status bits of the input: rate, level B, VPID link A/B valid */
	__u32 reserved[6];
};

#define AJAV_META_BYTES		(SDI_META_SIZE + sizeof(struct ajav_meta))

#endif
