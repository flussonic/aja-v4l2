// SPDX-License-Identifier: GPL-2.0
/*
 * Module identity and the attach/detach calls the driver core makes for
 * every card it probes. The core (vendor/) brings the card up -- BARs,
 * interrupts, DMA engines, the monitors of the FPGA -- and then hands the
 * device number here; this layer adds what the user sees.
 */
#include "ajv4l2.h"

#ifndef AJV4L2_VERSION
#define AJV4L2_VERSION "dev"
#endif

static struct ajv4l2_device *ajv4l2_devices[NTV2_MAXBOARDS];

/*
 * The model name for the media device and the node names. The firmware
 * variants of one board (8K, 2x4K, OE...) share the board's name; what
 * they differ in is the port count, and that comes from the device id.
 */
static const char *ajv4l2_model_name(NTV2DeviceID id)
{
	switch (id) {
	case DEVICE_ID_KONA5:
	case DEVICE_ID_KONA5_8KMK:
	case DEVICE_ID_KONA5_8K:
	case DEVICE_ID_KONA5_8K_MV_TX:
	case DEVICE_ID_KONA5_2X4K:
	case DEVICE_ID_KONA5_3DLUT:
	case DEVICE_ID_KONA5_OE1 ... DEVICE_ID_KONA5_OE12:
		return "KONA 5";
	case DEVICE_ID_KONA4:
		return "KONA 4";
	case DEVICE_ID_KONA1:
		return "KONA 1";
	case DEVICE_ID_KONAX:
		return "KONA X";
	case DEVICE_ID_CORVID44:
		return "Corvid 44";
	case DEVICE_ID_CORVID44_2X4K:
	case DEVICE_ID_CORVID44_8K:
	case DEVICE_ID_CORVID44_8KMK:
		return "Corvid 44 12G";
	case DEVICE_ID_CORVID88:
	case DEVICE_ID_CORVID88_GEN3:
		return "Corvid 88";
	case DEVICE_ID_IO4KPLUS:
		return "Io 4K Plus";
	default:
		return "AJA";
	}
}

static void ajv4l2_read_serial(struct ajv4l2_device *dev)
{
	ULWord lo = 0, hi = 0;
	char *p = dev->serial;
	int i;

	GetDeviceSerialNumberWords(dev->device_number, &lo, &hi);
	/* eight ASCII characters, low word first, low byte first */
	for (i = 0; i < 4; i++)
		*p++ = (lo >> (8 * i)) & 0xff;
	for (i = 0; i < 4; i++)
		*p++ = (hi >> (8 * i)) & 0xff;
	*p = '\0';
	for (p = dev->serial; *p; p++)
		if (*p < 0x20 || *p > 0x7e)
			*p = '?';
}

int ajv4l2_attach(unsigned int device_number)
{
	struct ajv4l2_device *dev;
	NTV2PrivateParams *pp;

	if (device_number >= NTV2_MAXBOARDS)
		return -EINVAL;
	pp = getNTV2Params(device_number);
	if (!pp || !pp->systemContext.pDevice)
		return -ENODEV;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->device_number = device_number;
	dev->pp = pp;
	dev->pdev = pp->systemContext.pDevice;
	dev->device_id = pp->_DeviceID;
	dev->model = ajv4l2_model_name(dev->device_id);
	dev->sdi_inputs = NTV2DeviceGetNumVideoInputs(dev->device_id);
	dev->sdi_outputs = NTV2DeviceGetNumVideoOutputs(dev->device_id);
	dev->channels = NTV2DeviceGetNumVideoChannels(dev->device_id);
	dev->bidirectional_sdi = NTV2DeviceHasBiDirectionalSDI(dev->device_id);
	ajv4l2_read_serial(dev);

	dev_info(&dev->pdev->dev, "%s serial %s: %u SDI in, %u SDI out, %u frame stores%s\n",
		 dev->model, dev->serial, dev->sdi_inputs, dev->sdi_outputs,
		 dev->channels, dev->bidirectional_sdi ? ", bidirectional" : "");

	ajv4l2_devices[device_number] = dev;
	return 0;
}

void ajv4l2_detach(unsigned int device_number)
{
	struct ajv4l2_device *dev;

	if (device_number >= NTV2_MAXBOARDS)
		return;
	dev = ajv4l2_devices[device_number];
	if (!dev)
		return;
	ajv4l2_devices[device_number] = NULL;
	kfree(dev);
}

MODULE_DESCRIPTION("V4L2 driver for AJA SDI cards");
MODULE_AUTHOR("Max Lapshin <max@flussonic.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(AJV4L2_VERSION);
