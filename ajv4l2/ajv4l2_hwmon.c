// SPDX-License-Identifier: GPL-2.0
/*
 * The board temperature: the FPGA's system monitor die sensor, the one the
 * driver core's thermostat reads on the boards that have a software fan
 * control, as a hwmon device named after the module. The 10-bit reading
 * converts by the UltraScale formula; a board without the sensor gives a
 * reading outside any real temperature and gets no hwmon device.
 */
#include <linux/hwmon.h>
#include "ajv4l2.h"

static int die_temp_millicelsius(struct ajv4l2_device *dev)
{
	u32 code = 0;

	ntv2ReadRegisterMS(dev->ctx, kRegSysmonVccIntDieTemp, &code, 0xffc0, 6);
	return (int)(code * 502909 / 1024) - 273819;
}

static umode_t ajv4l2_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	if (type == hwmon_temp && (attr == hwmon_temp_input || attr == hwmon_temp_label ||
				   attr == hwmon_temp_max || attr == hwmon_temp_crit))
		return 0444;
	return 0;
}

static int ajv4l2_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct ajv4l2_device *ajdev = dev_get_drvdata(dev);

	if (type != hwmon_temp)
		return -EOPNOTSUPP;
	switch (attr) {
	case hwmon_temp_input:
		*val = die_temp_millicelsius(ajdev);
		return 0;
	case hwmon_temp_max:
		*val = 85000;
		return 0;
	case hwmon_temp_crit:
		*val = 100000;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int ajv4l2_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				    u32 attr, int channel, const char **str)
{
	if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = "FPGA die";
		return 0;
	}
	return -EOPNOTSUPP;
}

static const struct hwmon_ops ajv4l2_hwmon_ops = {
	.is_visible = ajv4l2_hwmon_is_visible,
	.read = ajv4l2_hwmon_read,
	.read_string = ajv4l2_hwmon_read_string,
};

static const struct hwmon_channel_info *const ajv4l2_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MAX | HWMON_T_CRIT),
	NULL
};

static const struct hwmon_chip_info ajv4l2_hwmon_chip = {
	.ops = &ajv4l2_hwmon_ops,
	.info = ajv4l2_hwmon_info,
};

int ajv4l2_hwmon_register(struct ajv4l2_device *dev)
{
	struct device *hw;
	int t = die_temp_millicelsius(dev);

	/* a board without the system monitor reads all zeros or all ones */
	if (t < 0 || t > 125000)
		return 0;
	hw = hwmon_device_register_with_info(&dev->pdev->dev, AJV4L2_DRIVER_NAME, dev,
					     &ajv4l2_hwmon_chip, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	dev->hwmon = hw;
	return 0;
}

void ajv4l2_hwmon_unregister(struct ajv4l2_device *dev)
{
	if (dev->hwmon)
		hwmon_device_unregister(dev->hwmon);
	dev->hwmon = NULL;
}
