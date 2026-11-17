/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor thread: samples the MCU die temperature and VDDA (via VREFINT)
 * once per second and feeds them into the core's monitor registers.
 * A failed read reports a sensor fault to the module.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "module.h"

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

#define SAMPLE_PERIOD K_SECONDS(1)

static const struct device *const temp_dev = DEVICE_DT_GET(DT_NODELABEL(die_temp));
static const struct device *const vref_dev = DEVICE_DT_GET(DT_NODELABEL(vref));

/* Celsius as sensor_value -> 1/256 C (CMIS-style s16) */
static int16_t to_temp_q8(const struct sensor_value *v)
{
	int64_t micro = (int64_t)v->val1 * 1000000 + v->val2;

	return (int16_t)((micro * 256) / 1000000);
}

/* Volts as sensor_value -> 100 uV units */
static uint16_t to_vcc_100uv(const struct sensor_value *v)
{
	int64_t micro = (int64_t)v->val1 * 1000000 + v->val2;

	if (micro < 0) {
		return 0;
	}
	return (uint16_t)(micro / 100);
}

static int read_channel(const struct device *dev, enum sensor_channel chan,
			struct sensor_value *out)
{
	int ret = sensor_sample_fetch(dev);

	if (ret == 0) {
		ret = sensor_channel_get(dev, chan, out);
	}
	return ret;
}

static void sensor_thread(void *p1, void *p2, void *p3)
{
	struct sensor_value temp;
	struct sensor_value vcc;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(temp_dev) || !device_is_ready(vref_dev)) {
		LOG_ERR("on-chip sensors not ready");
		module_set_fault(true);
		return;
	}

	while (true) {
		int ret_t = read_channel(temp_dev, SENSOR_CHAN_DIE_TEMP, &temp);
		int ret_v = read_channel(vref_dev, SENSOR_CHAN_VOLTAGE, &vcc);

		if (ret_t != 0 || ret_v != 0) {
			LOG_ERR("sensor read failed (temp %d, vref %d)", ret_t, ret_v);
			module_set_fault(true);
		} else {
			module_update_monitors(to_temp_q8(&temp), to_vcc_100uv(&vcc));
			module_set_fault(false); /* FAULT state itself needs a SW reset */
		}
		k_sleep(SAMPLE_PERIOD);
	}
}

K_THREAD_DEFINE(sensor_tid, 1024, sensor_thread, NULL, NULL, NULL, 7, 0, 0);
