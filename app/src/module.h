/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared module context. The core is touched from I2C target callbacks,
 * which run in ISR context, so it is guarded by a spinlock (a k_mutex
 * cannot be taken from an ISR).
 */
#ifndef APP_MODULE_H_
#define APP_MODULE_H_

#include <zephyr/spinlock.h>

#include "cmis_core.h"

extern struct cmis_ctx module_ctx;
extern struct k_spinlock module_lock;

/* Load defaults and leave RESET. Call once from main. */
void module_init(void);

/* Ask the command thread to run the state machine. ISR-safe. */
void module_request_process(void);

enum cmis_state module_get_state(void);
const char *module_state_name(enum cmis_state state);

/* Register the I2C target (defined in i2c_target.c). */
int module_i2c_target_init(void);

#endif /* APP_MODULE_H_ */
