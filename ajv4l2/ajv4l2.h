/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The V4L2 layer over the ntv2 driver core: one struct ajv4l2_device per
 * card, holding what the core knows about it (its device number in the
 * core's tables, the PCI device, the device id) and our nodes.
 */
#ifndef AJV4L2_H
#define AJV4L2_H

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>

#include "ajatypes.h"
#include "buildenv.h"
#include "ntv2enums.h"
#include "ntv2videodefines.h"
#include "ntv2audiodefines.h"
#include "ntv2publicinterface.h"
#include "ntv2linuxpublicinterface.h"
#include "ntv2devicefeatures.h"
#include "ntv2driver.h"
#include "registerio.h"
#include "ntv2dma.h"
#include "ntv2driverautocirculate.h"
#include "ntv2kona.h"

#include "ajv4l2_hook.h"

struct ajv4l2_device {
	unsigned int device_number;	/* index in the core's device table */
	NTV2PrivateParams *pp;
	struct pci_dev *pdev;
	NTV2DeviceID device_id;
	const char *model;
	char serial[32];
	unsigned int sdi_inputs;
	unsigned int sdi_outputs;
	unsigned int channels;
	bool bidirectional_sdi;
};

#endif
