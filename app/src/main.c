/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * zephyr-cmis-lite: CMIS-inspired I2C module management firmware (skeleton)
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	uint32_t tick = 0;

	LOG_INF("cmis-lite booting on %s", CONFIG_BOARD_TARGET);

	while (1) {
		LOG_INF("heartbeat %u", tick++);
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
