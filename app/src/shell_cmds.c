/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr shell commands for inspecting and driving the module locally:
 *   cmis status | dump [lower|p00|p02] | read <addr> | write <addr> <val>
 *   cmis lowpwr <on|off> | reset | fault <on|off>
 * Reads use cmis_peek(), so the shell never clears latched flags.
 */
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "module.h"

static int parse_u8(const struct shell *sh, const char *arg, uint8_t *out)
{
	char *end;
	unsigned long v = strtoul(arg, &end, 0);

	if (*arg == '\0' || *end != '\0' || v > 0xFFUL) {
		shell_error(sh, "invalid byte value: %s", arg);
		return -EINVAL;
	}
	*out = (uint8_t)v;
	return 0;
}

static int parse_on_off(const struct shell *sh, const char *arg, bool *out)
{
	if (strcmp(arg, "on") == 0) {
		*out = true;
	} else if (strcmp(arg, "off") == 0) {
		*out = false;
	} else {
		shell_error(sh, "expected on|off");
		return -EINVAL;
	}
	return 0;
}

static int host_write(const struct shell *sh, uint8_t addr, uint8_t val)
{
	k_spinlock_key_t key = k_spin_lock(&module_lock);
	int ret = cmis_write(&module_ctx, addr, val);

	k_spin_unlock(&module_lock, key);

	if (ret != CMIS_OK) {
		shell_error(sh, "write 0x%02x <- 0x%02x rejected (%d)", addr, val, ret);
		return -EACCES;
	}
	if (addr == CMIS_REG_MODULE_CTRL) {
		module_request_process();
	}
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	int16_t temp_q8;
	uint16_t vcc;
	uint8_t flags;
	uint8_t state_reg;
	uint8_t ctrl;
	uint8_t page;
	k_spinlock_key_t key;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	module_get_monitors(&temp_q8, &vcc, &flags);
	key = k_spin_lock(&module_lock);
	state_reg = cmis_peek(&module_ctx, CMIS_REG_MODULE_STATE);
	ctrl = cmis_peek(&module_ctx, CMIS_REG_MODULE_CTRL);
	page = cmis_peek(&module_ctx, CMIS_REG_PAGE_SELECT);
	k_spin_unlock(&module_lock, key);

	int32_t t10 = ((int32_t)temp_q8 * 10) / 256;

	shell_print(sh, "state       : %s (reg 0x03 = 0x%02x, IntL %s)",
		    module_state_name(module_get_state()), state_reg,
		    (state_reg & CMIS_INTL_DEASSERTED) ? "deasserted" : "ASSERTED");
	shell_print(sh, "temperature : %d.%d C (raw 0x%04x)", t10 / 10,
		    (t10 < 0 ? -t10 : t10) % 10, (uint16_t)temp_q8);
	shell_print(sh, "vcc         : %u.%04u V (raw 0x%04x)", vcc / 10000U, vcc % 10000U, vcc);
	shell_print(sh, "flags (0x08): 0x%02x  ctrl (0x1a): 0x%02x  page: 0x%02x", flags, ctrl,
		    page);
	shell_print(sh, "fault inject: %s", sensor_fault_injected() ? "on" : "off");
	return 0;
}

static void dump_region(const struct shell *sh, const uint8_t *src, unsigned int base)
{
	uint8_t copy[CMIS_PAGE_SIZE];
	k_spinlock_key_t key = k_spin_lock(&module_lock);

	memcpy(copy, src, sizeof(copy));
	k_spin_unlock(&module_lock, key);

	for (unsigned int off = 0; off < CMIS_PAGE_SIZE; off += 16U) {
		shell_hexdump_line(sh, base + off, &copy[off], 16);
	}
}

static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
	const char *which = (argc > 1) ? argv[1] : "all";
	bool all = strcmp(which, "all") == 0;

	if (all || strcmp(which, "lower") == 0) {
		shell_print(sh, "lower memory (0x00-0x7f):");
		dump_region(sh, module_ctx.lower, 0x00);
	}
	if (all || strcmp(which, "p00") == 0) {
		shell_print(sh, "page 00h (0x80-0xff):");
		dump_region(sh, module_ctx.page00, 0x80);
	}
	if (all || strcmp(which, "p02") == 0) {
		shell_print(sh, "page 02h (0x80-0xff):");
		dump_region(sh, module_ctx.page02, 0x80);
	}
	return 0;
}

static int cmd_read(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t addr;
	k_spinlock_key_t key;
	uint8_t val;

	ARG_UNUSED(argc);
	if (parse_u8(sh, argv[1], &addr) != 0) {
		return -EINVAL;
	}
	key = k_spin_lock(&module_lock);
	val = cmis_peek(&module_ctx, addr);
	k_spin_unlock(&module_lock, key);

	shell_print(sh, "0x%02x = 0x%02x", addr, val);
	return 0;
}

static int cmd_write(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t addr;
	uint8_t val;

	ARG_UNUSED(argc);
	if (parse_u8(sh, argv[1], &addr) != 0 || parse_u8(sh, argv[2], &val) != 0) {
		return -EINVAL;
	}
	return host_write(sh, addr, val);
}

static int cmd_lowpwr(const struct shell *sh, size_t argc, char **argv)
{
	bool on;

	ARG_UNUSED(argc);
	if (parse_on_off(sh, argv[1], &on) != 0) {
		return -EINVAL;
	}
	return host_write(sh, CMIS_REG_MODULE_CTRL, on ? CMIS_CTRL_LOW_PWR_REQ : 0x00);
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return host_write(sh, CMIS_REG_MODULE_CTRL, CMIS_CTRL_SW_RESET);
}

static int cmd_fault(const struct shell *sh, size_t argc, char **argv)
{
	bool on;

	ARG_UNUSED(argc);
	if (parse_on_off(sh, argv[1], &on) != 0) {
		return -EINVAL;
	}
	sensor_inject_fault(on);
	shell_print(sh, "sensor fault injection %s", on ? "on" : "off");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmis,
	SHELL_CMD_ARG(status, NULL, "Show state, monitors and flags", cmd_status, 1, 0),
	SHELL_CMD_ARG(dump, NULL, "Hex dump: dump [all|lower|p00|p02]", cmd_dump, 1, 1),
	SHELL_CMD_ARG(read, NULL, "Read byte (no side effects): read <addr>", cmd_read, 2, 0),
	SHELL_CMD_ARG(write, NULL, "Host-style write: write <addr> <val>", cmd_write, 3, 0),
	SHELL_CMD_ARG(lowpwr, NULL, "Request low power: lowpwr <on|off>", cmd_lowpwr, 2, 0),
	SHELL_CMD_ARG(reset, NULL, "Software reset via control register", cmd_reset, 1, 0),
	SHELL_CMD_ARG(fault, NULL, "Simulate sensor failure: fault <on|off>", cmd_fault, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(cmis, &sub_cmis, "CMIS-lite module commands", NULL);
