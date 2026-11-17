/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * zephyr-cmis-lite: CMIS-inspired I2C module management firmware
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "cmis_core.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

static struct cmis_ctx module;

static const struct cmis_identity module_id = {
	.vendor = "STAR LABS",
	.part_number = "CMIS-LITE-01",
	.serial = "SN0001",
};

int main(void)
{
	uint32_t tick = 0;

	LOG_INF("cmis-lite booting on %s", CONFIG_BOARD_TARGET);

	cmis_init(&module, &module_id);
	cmis_process(&module); /* RESET -> LOW_PWR */
	LOG_INF("module state %d, reg 0x03 = 0x%02x", cmis_get_state(&module),
		cmis_read(&module, CMIS_REG_MODULE_STATE));

	while (1) {
		LOG_INF("heartbeat %u", tick++);
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
