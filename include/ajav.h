/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

/* Vendor block of an AJA card, right after the common part of sdi_meta. */
struct ajav_meta {
	__u32 rp188_dbb;	/* SMPTE RP 188 timecode of the frame as the input received it: DBB word */
	__u32 rp188_low;	/* ... low word (frames, seconds) */
	__u32 rp188_high;	/* ... high word (minutes, hours); all three 0xffffffff when none */
	__u32 rx_status;	/* raw receiver status register at the time of the frame */
	__u32 vpid_a;		/* payload identifier of link A as received, 0 when invalid */
	__u32 vpid_b;		/* ... link B */
	__u32 frames_dropped;	/* frames the card ring dropped since STREAMON, cumulative */
	__u32 reserved[5];
};

#define AJAV_META_BYTES		(SDI_META_SIZE + sizeof(struct ajav_meta))

#endif
