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

/* Push a new sensor sample into the core (thread context). */
void module_update_monitors(int16_t temp_q8, uint16_t vcc_100uv);

/* Report sensor health; a fault moves the module to FAULT. */
void module_set_fault(bool fault);

/* Snapshot of the monitor registers for logging (no side effects). */
void module_get_monitors(int16_t *temp_q8, uint16_t *vcc_100uv, uint8_t *flags);
const char *module_state_name(enum cmis_state state);

/* Force a simulated sensor failure (defined in sensor.c). */
void sensor_inject_fault(bool on);
bool sensor_fault_injected(void);

/* Register the I2C target (defined in i2c_target.c). */
int module_i2c_target_init(void);

#endif /* APP_MODULE_H_ */
