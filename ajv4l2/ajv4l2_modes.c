// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (C) 2026 Max Lapshin <max@flussonic.com>
/*
 * The video standards a port can receive, by the NTV2 name the core's
 * detector reports, and the mapping between them and V4L2 DV timings.
 * The 4K entries are the single-link 6G/12G standards; the quad-link
 * (4 x 1.5G / 4 x 3G) ones are not offered.
 */
#include "ajv4l2.h"

#define I	AJV4L2_MODE_F_INTERLACED
#define PSF	AJV4L2_MODE_F_PSF
#define B	AJV4L2_MODE_F_LEVEL_B
#define Q	AJV4L2_MODE_F_QUAD

const struct ajv4l2_mode ajv4l2_modes[] = {
	{ NTV2_FORMAT_525_5994,	"525i59.94",	720, 486, 525, 30000, 1001, I,
	  NTV2_STANDARD_525, NTV2_FG_720x486, NTV2_FRAMERATE_2997 },
	{ NTV2_FORMAT_625_5000,	"625i50",	720, 576, 625, 25, 1, I,
	  NTV2_STANDARD_625, NTV2_FG_720x576, NTV2_FRAMERATE_2500 },
	{ NTV2_FORMAT_720p_5000,	"720p50",	1280, 720, 750, 50, 1, 0,
	  NTV2_STANDARD_720, NTV2_FG_1280x720, NTV2_FRAMERATE_5000 },
	{ NTV2_FORMAT_720p_5994,	"720p59.94",	1280, 720, 750, 60000, 1001, 0,
	  NTV2_STANDARD_720, NTV2_FG_1280x720, NTV2_FRAMERATE_5994 },
	{ NTV2_FORMAT_720p_6000,	"720p60",	1280, 720, 750, 60, 1, 0,
	  NTV2_STANDARD_720, NTV2_FG_1280x720, NTV2_FRAMERATE_6000 },
	{ NTV2_FORMAT_1080i_5000,	"1080i50",	1920, 1080, 1125, 25, 1, I,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2500 },
	{ NTV2_FORMAT_1080i_5994,	"1080i59.94",	1920, 1080, 1125, 30000, 1001, I,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2997 },
	{ NTV2_FORMAT_1080i_6000,	"1080i60",	1920, 1080, 1125, 30, 1, I,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_3000 },
	{ NTV2_FORMAT_1080psf_2398,	"1080psf23.98",	1920, 1080, 1125, 24000, 1001, PSF,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2398 },
	{ NTV2_FORMAT_1080psf_2400,	"1080psf24",	1920, 1080, 1125, 24, 1, PSF,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2400 },
	{ NTV2_FORMAT_1080psf_2500_2,	"1080psf25",	1920, 1080, 1125, 25, 1, PSF,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2500 },
	{ NTV2_FORMAT_1080psf_2997_2,	"1080psf29.97",	1920, 1080, 1125, 30000, 1001, PSF,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2997 },
	{ NTV2_FORMAT_1080psf_3000_2,	"1080psf30",	1920, 1080, 1125, 30, 1, PSF,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_3000 },
	{ NTV2_FORMAT_1080p_2398,	"1080p23.98",	1920, 1080, 1125, 24000, 1001, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_2398 },
	{ NTV2_FORMAT_1080p_2400,	"1080p24",	1920, 1080, 1125, 24, 1, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_2400 },
	{ NTV2_FORMAT_1080p_2500,	"1080p25",	1920, 1080, 1125, 25, 1, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_2500 },
	{ NTV2_FORMAT_1080p_2997,	"1080p29.97",	1920, 1080, 1125, 30000, 1001, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_2997 },
	{ NTV2_FORMAT_1080p_3000,	"1080p30",	1920, 1080, 1125, 30, 1, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_3000 },
	{ NTV2_FORMAT_1080p_5000_A,	"1080p50",	1920, 1080, 1125, 50, 1, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_5000 },
	{ NTV2_FORMAT_1080p_5994_A,	"1080p59.94",	1920, 1080, 1125, 60000, 1001, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_5994 },
	{ NTV2_FORMAT_1080p_6000_A,	"1080p60",	1920, 1080, 1125, 60, 1, 0,
	  NTV2_STANDARD_1080p, NTV2_FG_1920x1080, NTV2_FRAMERATE_6000 },
	{ NTV2_FORMAT_1080p_5000_B,	"1080p50 B",	1920, 1080, 1125, 50, 1, B,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2500 },
	{ NTV2_FORMAT_1080p_5994_B,	"1080p59.94 B",	1920, 1080, 1125, 60000, 1001, B,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_2997 },
	{ NTV2_FORMAT_1080p_6000_B,	"1080p60 B",	1920, 1080, 1125, 60, 1, B,
	  NTV2_STANDARD_1080, NTV2_FG_1920x1080, NTV2_FRAMERATE_3000 },
	{ NTV2_FORMAT_3840x2160p_2398,	"2160p23.98",	3840, 2160, 2250, 24000, 1001, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_2398 },
	{ NTV2_FORMAT_3840x2160p_2400,	"2160p24",	3840, 2160, 2250, 24, 1, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_2400 },
	{ NTV2_FORMAT_3840x2160p_2500,	"2160p25",	3840, 2160, 2250, 25, 1, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_2500 },
	{ NTV2_FORMAT_3840x2160p_2997,	"2160p29.97",	3840, 2160, 2250, 30000, 1001, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_2997 },
	{ NTV2_FORMAT_3840x2160p_3000,	"2160p30",	3840, 2160, 2250, 30, 1, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_3000 },
	{ NTV2_FORMAT_3840x2160p_5000,	"2160p50",	3840, 2160, 2250, 50, 1, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_5000 },
	{ NTV2_FORMAT_3840x2160p_5994,	"2160p59.94",	3840, 2160, 2250, 60000, 1001, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_5994 },
	{ NTV2_FORMAT_3840x2160p_6000,	"2160p60",	3840, 2160, 2250, 60, 1, Q,
	  NTV2_STANDARD_3840x2160p, NTV2_FG_4x1920x1080, NTV2_FRAMERATE_6000 },
};
const unsigned int ajv4l2_num_modes = ARRAY_SIZE(ajv4l2_modes);

const struct ajv4l2_mode *ajv4l2_mode_for_format(NTV2VideoFormat f)
{
	unsigned int i;

	for (i = 0; i < ajv4l2_num_modes; i++)
		if (ajv4l2_modes[i].format == f)
			return &ajv4l2_modes[i];
	return NULL;
}

/*
 * A level B standard is the same raster as its level A twin; the client
 * cannot ask for one over the other, the flag in the metadata tells which
 * came. Progressive segmented frames are matched as interlaced timings.
 */
bool ajv4l2_mode_matches_timings(const struct ajv4l2_mode *m, const struct v4l2_dv_timings *t)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	u64 fps1000, want;
	u32 htot, vtot;
	bool interlaced = m->flags & (AJV4L2_MODE_F_INTERLACED | AJV4L2_MODE_F_PSF);

	if (t->type != V4L2_DV_BT_656_1120 || m->flags & AJV4L2_MODE_F_LEVEL_B)
		return false;
	if (bt->width != m->width)
		return false;
	/* NTSC is captured with 486 lines; the CEA timings say 480. */
	if (bt->height != m->height && !(m->height == 486 && bt->height == 480))
		return false;
	if (!!bt->interlaced != interlaced)
		return false;
	htot = V4L2_DV_BT_FRAME_WIDTH(bt);
	vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
	if (!htot || !vtot)
		return false;
	fps1000 = div64_u64(bt->pixelclock * 1000, (u64)htot * vtot);
	/*
	 * A 1000/1001 rate comes either as the divided clock or, the way
	 * v4l2_calc_timeperframe() reads it, as the nominal clock with
	 * V4L2_DV_FL_REDUCED_FPS; the flag on a whole rate divides it.
	 */
	if ((bt->flags & V4L2_DV_FL_REDUCED_FPS) &&
	    (fps1000 % 1000 <= 10 || fps1000 % 1000 >= 990))
		fps1000 = div_u64(fps1000 * 1000, 1001);
	want = div_u64((u64)m->fps_num * 1000, m->fps_den);
	return fps1000 + 10 >= want && fps1000 <= want + 10;
}

const struct ajv4l2_mode *ajv4l2_mode_for_timings(const struct v4l2_dv_timings *t)
{
	unsigned int i;

	for (i = 0; i < ajv4l2_num_modes; i++)
		if (ajv4l2_mode_matches_timings(&ajv4l2_modes[i], t))
			return &ajv4l2_modes[i];
	return NULL;
}

/* The standard preset for a mode when there is one; a bare frame otherwise. */
void ajv4l2_mode_timings(const struct ajv4l2_mode *m, struct v4l2_dv_timings *t)
{
	const struct v4l2_dv_timings *p;
	unsigned int i;

	for (i = 0; ; i++) {
		p = &v4l2_dv_timings_presets[i];
		if (!p->bt.width)
			break;
		if (ajv4l2_mode_matches_timings(m, p)) {
			*t = *p;
			return;
		}
	}
	/*
	 * A 1000/1001 mode is the preset of the whole rate with
	 * V4L2_DV_FL_REDUCED_FPS and the nominal clock, the way
	 * v4l2_calc_timeperframe() reads it.
	 */
	for (i = 0; ; i++) {
		struct v4l2_dv_timings q;

		p = &v4l2_dv_timings_presets[i];
		if (!p->bt.width)
			break;
		if (!(p->bt.flags & V4L2_DV_FL_CAN_REDUCE_FPS))
			continue;
		q = *p;
		q.bt.flags |= V4L2_DV_FL_REDUCED_FPS;
		if (ajv4l2_mode_matches_timings(m, &q)) {
			*t = q;
			return;
		}
	}
	memset(t, 0, sizeof(*t));
	t->type = V4L2_DV_BT_656_1120;
	t->bt.width = m->width;
	t->bt.height = m->height;
	t->bt.interlaced = !!(m->flags & (AJV4L2_MODE_F_INTERLACED | AJV4L2_MODE_F_PSF));
	t->bt.vsync = m->total_lines - m->height;
	t->bt.pixelclock = div_u64((u64)m->width * m->total_lines * m->fps_num, m->fps_den);
	t->bt.standards = V4L2_DV_BT_STD_SDI;
	if (m->fps_den == 1001)
		t->bt.flags |= V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_REDUCED_FPS;
}
