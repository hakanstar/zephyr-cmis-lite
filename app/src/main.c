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
		LOG_INF("heartbeat %u, state %s", tick++,
			module_state_name(module_get_state()));
		k_sleep(HEARTBEAT_PERIOD);
	}

	return 0;
}
