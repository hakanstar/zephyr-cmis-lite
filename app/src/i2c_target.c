/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C target front end: maps byte-level I2C callbacks onto the core.
 * Protocol: first written byte = register address, then data bytes with
 * auto-increment. A read returns bytes from the current register address.
 *
 * Callbacks run in ISR context: no logging, no blocking, short critical
 * sections only. Control writes are handed to the command thread.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "module.h"

LOG_MODULE_REGISTER(cmis_i2c, LOG_LEVEL_INF);

#define CMIS_I2C_ADDR 0x50

static const struct device *const i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));

static uint8_t reg_addr;
static bool addr_phase;
static bool ctrl_written;

static int on_write_requested(struct i2c_target_config *cfg)
{
	ARG_UNUSED(cfg);
	addr_phase = true;
	return 0;
}

static int on_write_received(struct i2c_target_config *cfg, uint8_t val)
{
	k_spinlock_key_t key;

	ARG_UNUSED(cfg);

	if (addr_phase) {
		reg_addr = val;
		addr_phase = false;
		return 0;
	}

	key = k_spin_lock(&module_lock);
	(void)cmis_write(&module_ctx, reg_addr, val); /* writes to RO bytes are ignored */
	k_spin_unlock(&module_lock, key);

	if (reg_addr == CMIS_REG_MODULE_CTRL) {
		ctrl_written = true;
	}
	reg_addr++;
	return 0;
}

static int on_read_requested(struct i2c_target_config *cfg, uint8_t *val)
{
	k_spinlock_key_t key;

	ARG_UNUSED(cfg);

	/* First byte of a read: this one reaches the host, so side effects apply */
	key = k_spin_lock(&module_lock);
	*val = cmis_read(&module_ctx, reg_addr);
	k_spin_unlock(&module_lock, key);
	return 0;
}

static int on_read_processed(struct i2c_target_config *cfg, uint8_t *val)
{
	k_spinlock_key_t key;

	ARG_UNUSED(cfg);

	/*
	 * The driver may prefetch a byte the host never clocks out, so use
	 * the side-effect-free peek. Clear-on-read therefore applies only
	 * when 0x08 is the first byte of a read transaction.
	 */
	reg_addr++;
	key = k_spin_lock(&module_lock);
	*val = cmis_peek(&module_ctx, reg_addr);
	k_spin_unlock(&module_lock, key);
	return 0;
}

static int on_stop(struct i2c_target_config *cfg)
{
	ARG_UNUSED(cfg);

	addr_phase = false;
	if (ctrl_written) {
		ctrl_written = false;
		module_request_process();
	}
	return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
	.write_requested = on_write_requested,
	.write_received = on_write_received,
	.read_requested = on_read_requested,
	.read_processed = on_read_processed,
	.stop = on_stop,
};

static struct i2c_target_config target_cfg = {
	.address = CMIS_I2C_ADDR,
	.callbacks = &target_callbacks,
};

int module_i2c_target_init(void)
{
	int ret;

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	ret = i2c_target_register(i2c_dev, &target_cfg);
	if (ret != 0) {
		LOG_ERR("i2c_target_register failed: %d", ret);
		return ret;
	}

	LOG_INF("I2C target at 0x%02x on %s", CMIS_I2C_ADDR, i2c_dev->name);
	return 0;
}
