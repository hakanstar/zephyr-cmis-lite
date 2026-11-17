/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "module.h"

LOG_MODULE_REGISTER(module, LOG_LEVEL_INF);

#define MODULE_CMD_PROCESS   1U
#define TRANSITION_DELAY_MS  50

struct cmis_ctx module_ctx;
struct k_spinlock module_lock;

K_MSGQ_DEFINE(module_cmd_q, sizeof(uint8_t), 8, 1);

static const struct cmis_identity module_id = {
	.vendor = "STAR LABS",
	.part_number = "CMIS-LITE-01",
	.serial = "SN0001",
};

const char *module_state_name(enum cmis_state state)
{
	switch (state) {
	case CMIS_STATE_RESET:
		return "RESET";
	case CMIS_STATE_LOW_PWR:
		return "LOW_PWR";
	case CMIS_STATE_PWR_UP:
		return "PWR_UP";
	case CMIS_STATE_READY:
		return "READY";
	case CMIS_STATE_FAULT:
		return "FAULT";
	default:
		return "?";
	}
}

enum cmis_state module_get_state(void)
{
	k_spinlock_key_t key = k_spin_lock(&module_lock);
	enum cmis_state state = cmis_get_state(&module_ctx);

	k_spin_unlock(&module_lock, key);
	return state;
}

static bool is_transitional(enum cmis_state state)
{
	return state == CMIS_STATE_RESET || state == CMIS_STATE_PWR_UP;
}

static void run_state_machine(void)
{
	enum cmis_state prev = module_get_state();
	enum cmis_state state;

	do {
		k_spinlock_key_t key = k_spin_lock(&module_lock);

		cmis_process(&module_ctx);
		state = cmis_get_state(&module_ctx);
		k_spin_unlock(&module_lock, key);

		if (state != prev) {
			LOG_INF("state %s -> %s", module_state_name(prev),
				module_state_name(state));
			prev = state;
		}
		if (is_transitional(state)) {
			/* Stand-in for real power-up time */
			k_msleep(TRANSITION_DELAY_MS);
		}
	} while (is_transitional(state));
}

static void cmd_thread(void *p1, void *p2, void *p3)
{
	uint8_t cmd;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		k_msgq_get(&module_cmd_q, &cmd, K_FOREVER);
		if (cmd == MODULE_CMD_PROCESS) {
			run_state_machine();
		}
	}
}

K_THREAD_DEFINE(module_cmd_tid, 1024, cmd_thread, NULL, NULL, NULL, 5, 0, 0);

void module_request_process(void)
{
	uint8_t cmd = MODULE_CMD_PROCESS;

	/* K_NO_WAIT keeps this ISR-safe; if the queue is full a run is pending anyway */
	(void)k_msgq_put(&module_cmd_q, &cmd, K_NO_WAIT);
}

void module_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&module_lock);

	(void)cmis_init(&module_ctx, &module_id);
	k_spin_unlock(&module_lock, key);

	module_request_process(); /* RESET -> LOW_PWR */
}
