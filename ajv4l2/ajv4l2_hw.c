// SPDX-License-Identifier: GPL-2.0
/*
 * What the frame store, the crosspoints and the audio system of a channel
 * are told before a capture or a playout: the same register writes the
 * SDK makes for a capture or a player application, done in the kernel.
 * The card runs in independent ("multi-format") mode, so every channel
 * has its own standard. The SDI outputs themselves are kept by the core's
 * output monitor task, which derives the standard, the link rate and the
 * payload identifier of an output from its route and the frame store
 * behind it; the level of a 3G output is its converter bit.
 */
#include "ajv4l2_hw.h"
#include "ntv2rp188.h"

static const u32 global_control_reg[8] = {
	kRegGlobalControl, kRegGlobalControlCh2, kRegGlobalControlCh3, kRegGlobalControlCh4,
	kRegGlobalControlCh5, kRegGlobalControlCh6, kRegGlobalControlCh7, kRegGlobalControlCh8,
};

static const u32 control_reg[8] = {
	kRegCh1Control, kRegCh2Control, kRegCh3Control, kRegCh4Control,
	kRegCh5Control, kRegCh6Control, kRegCh7Control, kRegCh8Control,
};

/* SMPTE 372 (level B dual link) enable: two channels per register field. */
static const struct { u32 reg, mask, shift; } smpte372[8] = {
	{ kRegGlobalControl, kRegMaskSmpte372Enable, kRegShiftSmpte372 },
	{ kRegGlobalControl, kRegMaskSmpte372Enable, kRegShiftSmpte372 },
	{ kRegGlobalControl2, kRegMaskSmpte372Enable4, kRegShiftSmpte372Enable4 },
	{ kRegGlobalControl2, kRegMaskSmpte372Enable4, kRegShiftSmpte372Enable4 },
	{ kRegGlobalControl2, kRegMaskSmpte372Enable6, kRegShiftSmpte372Enable6 },
	{ kRegGlobalControl2, kRegMaskSmpte372Enable6, kRegShiftSmpte372Enable6 },
	{ kRegGlobalControl2, kRegMaskSmpte372Enable8, kRegShiftSmpte372Enable8 },
	{ kRegGlobalControl2, kRegMaskSmpte372Enable8, kRegShiftSmpte372Enable8 },
};

/* The frame store's input select: which crosspoint output feeds it. */
static const struct { u32 reg, shift; } fb_input[8] = {
	{ kRegXptSelectGroup2, 0 }, { kRegXptSelectGroup5, 0 },
	{ kRegXptSelectGroup13, 0 }, { kRegXptSelectGroup13, 16 },
	{ kRegXptSelectGroup21, 0 }, { kRegXptSelectGroup21, 8 },
	{ kRegXptSelectGroup21, 16 }, { kRegXptSelectGroup21, 24 },
};

static const u32 sdi_in_xpt[8] = {
	NTV2_XptSDIIn1, NTV2_XptSDIIn2, NTV2_XptSDIIn3, NTV2_XptSDIIn4,
	NTV2_XptSDIIn5, NTV2_XptSDIIn6, NTV2_XptSDIIn7, NTV2_XptSDIIn8,
};

static const u32 fb_yuv_xpt[8] = {
	NTV2_XptFrameBuffer1YUV, NTV2_XptFrameBuffer2YUV, NTV2_XptFrameBuffer3YUV, NTV2_XptFrameBuffer4YUV,
	NTV2_XptFrameBuffer5YUV, NTV2_XptFrameBuffer6YUV, NTV2_XptFrameBuffer7YUV, NTV2_XptFrameBuffer8YUV,
};

/* The second data stream select of an SDI output. */
static const struct { u32 reg, shift; } sdi_out_ds2[8] = {
	{ kRegXptSelectGroup10, 0 }, { kRegXptSelectGroup10, 8 },
	{ kRegXptSelectGroup14, 8 }, { kRegXptSelectGroup14, 24 },
	{ kRegXptSelectGroup14, 16 }, { kRegXptSelectGroup22, 8 },
	{ kRegXptSelectGroup22, 24 }, { kRegXptSelectGroup30, 8 },
};

static const u32 sdi_out_control_reg[8] = {
	kRegSDIOut1Control, kRegSDIOut2Control, kRegSDIOut3Control, kRegSDIOut4Control,
	kRegSDIOut5Control, kRegSDIOut6Control, kRegSDIOut7Control, kRegSDIOut8Control,
};

static const u32 audio_source_reg[8] = {
	kRegAud1SourceSelect, kRegAud2SourceSelect, kRegAud3SourceSelect, kRegAud4SourceSelect,
	kRegAud5SourceSelect, kRegAud6SourceSelect, kRegAud7SourceSelect, kRegAud8SourceSelect,
};

static const u32 audio_control_reg[8] = {
	kRegAud1Control, kRegAud2Control, kRegAud3Control, kRegAud4Control,
	kRegAud5Control, kRegAud6Control, kRegAud7Control, kRegAud8Control,
};

static const struct { u32 mask, shift; } audio_rate_high[8] = {
	{ kRegMaskAud1RateHigh, kRegShiftAud1RateHigh }, { kRegMaskAud2RateHigh, kRegShiftAud2RateHigh },
	{ kRegMaskAud3RateHigh, kRegShiftAud3RateHigh }, { kRegMaskAud4RateHigh, kRegShiftAud4RateHigh },
	{ kRegMaskAud5RateHigh, kRegShiftAud5RateHigh }, { kRegMaskAud6RateHigh, kRegShiftAud6RateHigh },
	{ kRegMaskAud7RateHigh, kRegShiftAud7RateHigh }, { kRegMaskAud8RateHigh, kRegShiftAud8RateHigh },
};

/* The embedded-audio detector: one register per pair of audio systems, eight bits each. */
static const struct { u32 reg, shift; } audio_detect[8] = {
	{ kRegAud1Detect, 0 }, { kRegAud1Detect, 8 }, { kRegAudDetect2, 0 }, { kRegAudDetect2, 8 },
	{ kRegAudioDetect5678, 0 }, { kRegAudioDetect5678, 8 },
	{ kRegAudioDetect5678, 16 }, { kRegAudioDetect5678, 24 },
};

NTV2FrameBufferFormat ajv4l2_hw_fbf(u32 pixfmt)
{
	return pixfmt == SDI_PIX_FMT_V210 ? NTV2_FBF_10BIT_YCBCR : NTV2_FBF_8BIT_YCBCR;
}

static void wr(struct ajv4l2_port *port, u32 reg, u32 val, u32 mask, u32 shift)
{
	ntv2WriteRegisterMS(port->dev->ctx, reg, val, mask, shift);
}

/* The standard, geometry, rate and dual-link bit of a channel, in one write each. */
static void set_video_format(struct ajv4l2_port *port, const struct ajv4l2_mode *m)
{
	Ntv2SystemContext *ctx = port->dev->ctx;
	unsigned int ch = port->index;
	u32 reg = global_control_reg[ch];
	u32 v = ntv2ReadRegister(ctx, reg);
	NTV2FrameGeometry geometry = m->geometry;
	bool quad = m->flags & AJV4L2_MODE_F_QUAD;

	/* the frame store of a single-link 2160p input sees a quarter raster */
	if (quad)
		geometry = NTV2_FG_1920x1080;
	v &= ~(kRegMaskStandard | kRegMaskGeometry | kRegMaskFrameRate | kRegMaskFrameRateHiBit);
	v |= ((quad ? NTV2_STANDARD_1080p : m->standard) << kRegShiftStandard) & kRegMaskStandard;
	v |= (geometry << kRegShiftGeometry) & kRegMaskGeometry;
	v |= ((m->rate & 0x7) << kRegShiftFrameRate) & kRegMaskFrameRate;
	v |= (((m->rate >> 3) & 1) << kRegShiftFrameRateHiBit) & kRegMaskFrameRateHiBit;
	ntv2WriteRegister(ctx, reg, v);
	wr(port, smpte372[ch].reg, m->flags & AJV4L2_MODE_F_LEVEL_B ? 1 : 0,
	   smpte372[ch].mask, smpte372[ch].shift);
	/* two-sample interleave of a 12G link into one frame store */
	if (NTV2DeviceCanDo12gRouting(port->dev->device_id))
		wr(port, reg, quad ? 1 : 0, kRegMaskQuadTsiEnable, kRegShiftQuadTsiEnable);
	WriteRegister(port->dev->device_number, kVRegVideoFormatCh1 + ch, m->format, NO_MASK, NO_SHIFT);
	WriteRegister(port->dev->device_number, kVRegProgressivePicture,
		      !(m->flags & (AJV4L2_MODE_F_INTERLACED | AJV4L2_MODE_F_PSF)), NO_MASK, NO_SHIFT);
}

static void set_audio_system(struct ajv4l2_port *port)
{
	unsigned int ch = port->index;
	u32 src = audio_source_reg[ch], ctl = audio_control_reg[ch];

	/* embedded audio of SDI input ch, clocked by that input; by the reference on playout */
	wr(port, src, 0x1, kRegMaskAudioSource, kRegShiftAudioSource);
	wr(port, src, ch & 1, kRegMaskEmbeddedAudioInput, kRegShiftEmbeddedAudioInput);
	wr(port, src, (ch >> 1) & 1, kRegMaskEmbeddedAudioInput2, kRegShiftEmbeddedAudioInput2);
	wr(port, src, port->output ? NTV2_EMBEDDED_AUDIO_CLOCK_REFERENCE : NTV2_EMBEDDED_AUDIO_CLOCK_VIDEO_INPUT,
	   kRegMaskEmbeddedAudioClock, kRegShiftEmbeddedAudioClock);
	/* 16 channels, 48 kHz, the 4 MB ring, no loopback */
	wr(port, ctl, 1, kRegMaskAudio16Channel, kRegShiftAudio16Channel);
	wr(port, ctl, 0, kRegMaskAudioRate, kRegShiftAudioRate);
	wr(port, kRegAudioControl2, 0, audio_rate_high[ch].mask, audio_rate_high[ch].shift);
	wr(port, ctl, NTV2_AUDIO_BUFFER_SIZE_4MB, kK2RegMaskAudioBufferSize, kK2RegShiftAudioBufferSize);
	wr(port, ctl, 0, kRegMaskLoopBack, kRegShiftLoopBack);
}

int ajv4l2_hw_setup_capture(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;
	unsigned int ch = port->index;

	if (ch >= 8 || ch >= dev->channels)
		return -ENODEV;
	if (NTV2DeviceCanDoMultiFormat(dev->device_id))
		wr(port, kRegGlobalControl2, 1, kRegMaskIndependentMode, kRegShiftIndependentMode);

	ajv4l2_input_set_direction(port, true);
	set_video_format(port, port->mode);
	SetFrameBufferFormat(dev->ctx, port->channel, ajv4l2_hw_fbf(port->pixfmt));
	SetFrameBufferOrientation(dev->ctx, port->channel, NTV2_FRAMEBUFFER_ORIENTATION_TOPDOWN);
	SetMode(dev->ctx, port->channel, NTV2_MODE_CAPTURE);
	wr(port, control_reg[ch], 0, kRegMaskChannelDisable, kRegShiftChannelDisable);
	/* SDI in ch -> frame store ch */
	wr(port, fb_input[ch].reg, sdi_in_xpt[ch], 0xff << fb_input[ch].shift, fb_input[ch].shift);
	set_audio_system(port);
	AvInterruptControl(dev->device_number, eInput1 + ch, 1);
	return 0;
}

/*
 * The reference the outputs of the card lock to: the reference input when
 * an output asks for it and the input carries a signal, else the card's
 * own clock. One setting for the whole card.
 */
void ajv4l2_hw_set_reference(struct ajv4l2_port *port)
{
	NTV2ReferenceSource ref = NTV2_REFERENCE_FREERUN;

	if (port->reference && ajv4l2_hw_reference_present(port->dev))
		ref = NTV2_REFERENCE_EXTERNAL;
	wr(port, kRegGlobalControl, ref, kRegMaskRefSource, kRegShiftRefSource);
	if (port->dev->channels > 4)
		wr(port, kRegGlobalControl2, 0, kRegMaskRefSource2, kRegShiftRefSource2);
}

/* Whether the reference input carries a signal the card could lock to. */
bool ajv4l2_hw_reference_present(struct ajv4l2_device *dev)
{
	u32 status = ntv2ReadRegister(dev->ctx, kRegInputStatus);

	/* frame rate code of the reference input, 0 when nothing is there */
	return ((status >> 16) & 0xf) != 0;
}

/* The converter of a 3G output: SMPTE 425 level A as it is, or mapped to level B. */
void ajv4l2_hw_set_level(struct ajv4l2_port *port)
{
	unsigned int ch = port->index;

	if (ch >= 8)
		return;
	wr(port, sdi_out_control_reg[ch], port->level_a ? 0 : 1,
	   kRegMaskSDIOutLevelAtoLevelB, kRegShiftSDIOutLevelAtoLevelB);
}

/*
 * The card has one free-running frame pulse, and its rate is the frame
 * rate of channel 1: an output on another channel runs at that pulse
 * whatever its own rate register says, so an output in the other rate
 * family (30 against 25) would play at the wrong speed. The pulse follows
 * the output that starts; a port already streaming in the other family
 * has been refused before this.
 */
static void set_free_run_rate(struct ajv4l2_port *port)
{
	const struct ajv4l2_mode *m = port->mode;

	if (port->index == 0)
		return;
	wr(port, kRegGlobalControl, m->rate & 0x7, kRegMaskFrameRate, kRegShiftFrameRate);
	wr(port, kRegGlobalControl, (m->rate >> 3) & 1, kRegMaskFrameRateHiBit, kRegShiftFrameRateHiBit);
}

int ajv4l2_hw_setup_output(struct ajv4l2_port *port)
{
	struct ajv4l2_device *dev = port->dev;
	unsigned int ch = port->index;
	u32 ctl;

	if (ch >= 8 || ch >= dev->channels)
		return -ENODEV;
	if (NTV2DeviceCanDoMultiFormat(dev->device_id))
		wr(port, kRegGlobalControl2, 1, kRegMaskIndependentMode, kRegShiftIndependentMode);

	ajv4l2_input_set_direction(port, false);
	set_video_format(port, port->mode);
	SetFrameBufferFormat(dev->ctx, port->channel, ajv4l2_hw_fbf(port->pixfmt));
	SetFrameBufferOrientation(dev->ctx, port->channel, NTV2_FRAMEBUFFER_ORIENTATION_TOPDOWN);
	SetMode(dev->ctx, port->channel, NTV2_MODE_DISPLAY);
	wr(port, control_reg[ch], 0, kRegMaskChannelDisable, kRegShiftChannelDisable);
	/* frame store ch -> SDI out ch, nothing on the second data stream */
	SetXptSDIOutInputSelect(dev->ctx, port->channel, (NTV2OutputXptID)fb_yuv_xpt[ch]);
	wr(port, sdi_out_ds2[ch].reg, NTV2_XptBlack, 0xff << sdi_out_ds2[ch].shift, sdi_out_ds2[ch].shift);
	ajv4l2_hw_set_level(port);
	set_audio_system(port);
	/* the embedder of SDI out ch takes audio system ch: three scattered bits */
	ctl = ntv2ReadRegister(dev->ctx, sdi_out_control_reg[ch]);
	ctl &= ~(kK2RegMaskSDIOutDS1AudioSelect | kK2RegMaskSDIOutDS2AudioSelect);
	ctl |= ((ch << 16) & kK2RegMaskSDIOutDS1Audio_Bit2) | ((ch << 27) & kK2RegMaskSDIOutDS1Audio_Bit1) |
	       ((ch << 30) & kK2RegMaskSDIOutDS1Audio_Bit0);
	ctl |= ((ch << 17) & kK2RegMaskSDIOutDS2Audio_Bit2) | ((ch << 28) & kK2RegMaskSDIOutDS2Audio_Bit1) |
	       ((ch << 31) & kK2RegMaskSDIOutDS2Audio_Bit0);
	ntv2WriteRegister(dev->ctx, sdi_out_control_reg[ch], ctl);
	ajv4l2_hw_set_reference(port);
	set_free_run_rate(port);
	ajv4l2_hw_set_vpid(port, 0);
	port->timecode_output = true;
	ajv4l2_hw_set_timecode_output(port, false);
	AvInterruptControl(dev->device_number, ajv4l2_hw_output_event(ch), 1);
	return 0;
}

/*
 * The payload identifier of an output. The core's output monitor would
 * derive one from the route on every pass and overwrite whatever a client
 * asked for, so that is turned off and the identifier is derived once at
 * start; a frame whose ANC plane carries one replaces it (the register
 * holds the four wire bytes in reverse order, like the receiver's).
 */
void ajv4l2_hw_set_vpid(struct ajv4l2_port *port, u32 vpid)
{
	struct ajv4l2_device *dev = port->dev;

	WriteRegister(dev->device_number, kVRegDisableAutoVPID, 1, NO_MASK, NO_SHIFT);
	if (vpid) {
		SetSDIOutVPID(dev->ctx, port->channel, vpid, vpid);
	} else {
		/* the output's standard and link rate first: the identifier is read off them */
		SetVideoOutputStandard(dev->ctx, port->channel);
		SetVPIDOutput(dev->ctx, port->channel);
	}
}

/*
 * The card's own ATC inserter: on while frames carry a timecode packet,
 * off otherwise, so that no empty timecode goes out. The DBB word of the
 * packet is used as given rather than the core's default.
 */
void ajv4l2_hw_set_timecode_output(struct ajv4l2_port *port, bool on)
{
	struct ajv4l2_device *dev = port->dev;

	if (port->timecode_output == on)
		return;
	port->timecode_output = on;
	WriteRegister(dev->device_number, kVRegUserDefinedDBB, 1, NO_MASK, NO_SHIFT);
	SetRP188Mode(dev->ctx, port->channel, on ? NTV2_RP188_OUTPUT : NTV2_RP188_INPUT);
}

/* The output vertical interrupt of a channel: the enum is not contiguous. */
unsigned int ajv4l2_hw_output_event(unsigned int ch)
{
	return ch ? eOutput2 + ch - 1 : eOutput1;
}

/* The channel pairs the embedder of this input carries, as a 16-bit mask. */
u32 ajv4l2_hw_audio_present(struct ajv4l2_port *port)
{
	unsigned int ch = port->index;
	u32 pairs, mask = 0, i;

	if (ch >= 8)
		return 0;
	pairs = (ntv2ReadRegister(port->dev->ctx, audio_detect[ch].reg) >> audio_detect[ch].shift) & 0xff;
	for (i = 0; i < 8; i++)
		if (pairs & BIT(i))
			mask |= 3u << (2 * i);
	return mask;
}
