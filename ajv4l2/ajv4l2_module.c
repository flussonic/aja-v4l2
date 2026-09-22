// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (C) 2026 Max Lapshin <max@flussonic.com>
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

static void ajv4l2_media_init(struct ajv4l2_device *dev)
{
	struct media_device *mdev = &dev->mdev;
	/* The bitfile date as the FPGA reports it: 0xYYYYMMDD in BCD. */
	u32 fw = ntv2ReadRegister(dev->ctx, kRegBitfileDate);

	mdev->dev = &dev->pdev->dev;
	strscpy(mdev->model, dev->model, sizeof(mdev->model));
	strscpy(mdev->serial, dev->serial, sizeof(mdev->serial));
	snprintf(mdev->bus_info, sizeof(mdev->bus_info), "PCI:%s", pci_name(dev->pdev));
	mdev->hw_revision = fw;
	media_device_init(mdev);
	dev->v4l2_dev.mdev = mdev;
}

/*
 * The V4L2 side of a card: a media device, a v4l2_device, a capture node
 * per SDI input and an output node per SDI output that has a frame store.
 */
static int ajv4l2_register(struct ajv4l2_device *dev)
{
	unsigned int i, inputs, outputs;
	int ret;

	ajv4l2_media_init(dev);
	ret = v4l2_device_register(&dev->pdev->dev, &dev->v4l2_dev);
	if (ret) {
		media_device_cleanup(&dev->mdev);
		return ret;
	}
	inputs = min_t(unsigned int, dev->sdi_inputs, AJV4L2_MAX_PORTS);
	outputs = min3(dev->sdi_outputs, dev->channels, (unsigned int)AJV4L2_MAX_PORTS);
	dev->num_ports = inputs + outputs;
	for (i = 0; i < dev->num_ports; i++) {
		struct ajv4l2_port *port = kzalloc(sizeof(*port), GFP_KERNEL);
		unsigned int n = i < inputs ? i : i - inputs;

		if (!port) {
			ret = -ENOMEM;
			goto err;
		}
		port->dev = dev;
		port->index = n;
		port->channel = (NTV2Channel)n;
		port->output = i >= inputs;
		/* the capture node of the same channel, when there is one */
		if (port->output && n < inputs) {
			port->sibling = dev->ports[n];
			dev->ports[n]->sibling = port;
		}
		ret = ajv4l2_video_register(port);
		if (ret) {
			kfree(port);
			goto err;
		}
		dev->ports[i] = port;
	}
	ret = media_device_register(&dev->mdev);
	if (ret)
		goto err;
	if (ajv4l2_hwmon_register(dev))
		dev_warn(&dev->pdev->dev, "no hwmon device\n");
	return 0;
err:
	for (i = 0; i < dev->num_ports; i++) {
		if (dev->ports[i]) {
			ajv4l2_video_unregister(dev->ports[i]);
			kfree(dev->ports[i]);
			dev->ports[i] = NULL;
		}
	}
	v4l2_device_unregister(&dev->v4l2_dev);
	media_device_cleanup(&dev->mdev);
	return ret;
}

static void ajv4l2_unregister(struct ajv4l2_device *dev)
{
	unsigned int i;

	ajv4l2_hwmon_unregister(dev);
	media_device_unregister(&dev->mdev);
	for (i = 0; i < dev->num_ports; i++) {
		if (dev->ports[i]) {
			ajv4l2_video_unregister(dev->ports[i]);
			kfree(dev->ports[i]);
			dev->ports[i] = NULL;
		}
	}
	v4l2_device_unregister(&dev->v4l2_dev);
	media_device_cleanup(&dev->mdev);
}

int ajv4l2_attach(unsigned int device_number)
{
	struct ajv4l2_device *dev;
	NTV2PrivateParams *pp;
	int ret;

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
	dev->ctx = &pp->systemContext;
	ajv4l2_read_serial(dev);

	dev_info(&dev->pdev->dev, "%s serial %s: %u SDI in, %u SDI out, %u frame stores%s\n",
		 dev->model, dev->serial, dev->sdi_inputs, dev->sdi_outputs,
		 dev->channels, dev->bidirectional_sdi ? ", bidirectional" : "");

	ret = ajv4l2_register(dev);
	if (ret) {
		kfree(dev);
		return ret;
	}
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
	ajv4l2_unregister(dev);
	kfree(dev);
}

MODULE_DESCRIPTION("V4L2 driver for AJA SDI cards");
MODULE_AUTHOR("Max Lapshin <max@flussonic.com>");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION(AJV4L2_VERSION);
