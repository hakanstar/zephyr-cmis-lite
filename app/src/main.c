/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * zephyr-cmis-lite: CMIS-inspired I2C module management firmware
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "module.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD K_SECONDS(5)

int main(void)
{
	uint32_t tick = 0;

	LOG_INF("cmis-lite booting on %s", CONFIG_BOARD_TARGET);

	module_init();
	if (module_i2c_target_init() != 0) {
		LOG_ERR("running without I2C target");
	}

	while (true) {
		int16_t temp_q8;
		uint16_t vcc;
		uint8_t flags;

		module_get_monitors(&temp_q8, &vcc, &flags);
		/* temp_q8 / 256 = degC; print with one decimal using integers only */
		int32_t t10 = ((int32_t)temp_q8 * 10) / 256;

		LOG_INF("hb %u, state %s, temp %d.%d C, vcc %u.%04u V, flags 0x%02x",
			tick++, module_state_name(module_get_state()),
			t10 / 10, (t10 < 0 ? -t10 : t10) % 10,
			vcc / 10000U, vcc % 10000U, flags);
		k_sleep(HEARTBEAT_PERIOD);
	}

	return 0;
}
