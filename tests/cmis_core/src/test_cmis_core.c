/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/ztest.h>
#include <string.h>

#include "cmis_core.h"

static struct cmis_ctx ctx;

static const struct cmis_identity test_id = {
	.vendor = "STAR LABS",
	.part_number = "CMIS-LITE-01",
	.serial = "SN0001",
};

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_equal(cmis_init(&ctx, &test_id), CMIS_OK);
}

static void go_ready(void)
{
	cmis_process(&ctx); /* RESET -> LOW_PWR */
	zassert_equal(cmis_write(&ctx, CMIS_REG_MODULE_CTRL, 0x00), CMIS_OK);
	cmis_process(&ctx); /* LOW_PWR -> PWR_UP */
	cmis_process(&ctx); /* PWR_UP -> READY */
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_READY);
}

ZTEST(cmis_core, test_init_rejects_null)
{
	zassert_equal(cmis_init(NULL, &test_id), CMIS_ERR_PARAM);
	zassert_equal(cmis_init(&ctx, NULL), CMIS_ERR_PARAM);
}

ZTEST(cmis_core, test_defaults_after_init)
{
	zassert_equal(cmis_read(&ctx, CMIS_REG_IDENTIFIER), CMIS_IDENTIFIER_QSFP_DD);
	zassert_equal(cmis_read(&ctx, CMIS_REG_REVISION), CMIS_MAP_REVISION);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_RESET);
	zassert_equal(cmis_read(&ctx, CMIS_REG_MODULE_CTRL), CMIS_CTRL_LOW_PWR_REQ);
	zassert_equal(cmis_read(&ctx, CMIS_REG_PAGE_SELECT), 0x00);
}

ZTEST(cmis_core, test_reset_goes_to_low_power)
{
	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_LOW_PWR);
	/* state 1 in bits 3:1, IntL deasserted in bit 0 */
	zassert_equal(cmis_read(&ctx, CMIS_REG_MODULE_STATE), 0x03);
}

ZTEST(cmis_core, test_stays_low_power_while_requested)
{
	cmis_process(&ctx);
	cmis_process(&ctx);
	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_LOW_PWR);
}

ZTEST(cmis_core, test_power_up_sequence)
{
	go_ready();
	zassert_equal(cmis_read(&ctx, CMIS_REG_MODULE_STATE), 0x07);
}

ZTEST(cmis_core, test_low_power_request_from_ready)
{
	go_ready();
	zassert_equal(cmis_write(&ctx, CMIS_REG_MODULE_CTRL, CMIS_CTRL_LOW_PWR_REQ), CMIS_OK);
	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_LOW_PWR);
}

ZTEST(cmis_core, test_software_reset_restores_defaults)
{
	go_ready();
	zassert_equal(cmis_write(&ctx, CMIS_REG_PAGE_SELECT, 0x02), CMIS_OK);
	zassert_equal(cmis_write(&ctx, CMIS_P02_TEMP_HIGH_ALARM, 0x10), CMIS_OK);

	zassert_equal(cmis_write(&ctx, CMIS_REG_MODULE_CTRL, CMIS_CTRL_SW_RESET), CMIS_OK);
	cmis_process(&ctx);

	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_RESET);
	zassert_equal(cmis_read(&ctx, CMIS_REG_MODULE_CTRL), CMIS_CTRL_LOW_PWR_REQ,
		      "reset bit must self-clear");
	zassert_equal(cmis_read(&ctx, CMIS_REG_PAGE_SELECT), 0x00);

	zassert_equal(cmis_write(&ctx, CMIS_REG_PAGE_SELECT, 0x02), CMIS_OK);
	zassert_equal(cmis_read(&ctx, CMIS_P02_TEMP_HIGH_ALARM), 0x4B, "75 C default MSB");

	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_LOW_PWR);
}

ZTEST(cmis_core, test_ctrl_ignores_reserved_bits)
{
	zassert_equal(cmis_write(&ctx, CMIS_REG_MODULE_CTRL, 0xFF & ~CMIS_CTRL_SW_RESET),
		      CMIS_OK);
	zassert_equal(cmis_read(&ctx, CMIS_REG_MODULE_CTRL), CMIS_CTRL_LOW_PWR_REQ);
}

ZTEST(cmis_core, test_page00_identity)
{
	uint8_t buf[CMIS_ID_FIELD_LEN];

	for (uint8_t i = 0; i < CMIS_ID_FIELD_LEN; i++) {
		buf[i] = cmis_read(&ctx, CMIS_P00_VENDOR_NAME + i);
	}
	zassert_mem_equal(buf, "STAR LABS       ", CMIS_ID_FIELD_LEN);

	for (uint8_t i = 0; i < CMIS_ID_FIELD_LEN; i++) {
		buf[i] = cmis_read(&ctx, CMIS_P00_SERIAL_NUMBER + i);
	}
	zassert_mem_equal(buf, "SN0001          ", CMIS_ID_FIELD_LEN);
}

ZTEST(cmis_core, test_identity_truncated_to_field)
{
	const struct cmis_identity long_id = {
		.vendor = "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
		.part_number = "",
		.serial = NULL,
	};
	uint8_t buf[CMIS_ID_FIELD_LEN];

	zassert_equal(cmis_init(&ctx, &long_id), CMIS_OK);
	for (uint8_t i = 0; i < CMIS_ID_FIELD_LEN; i++) {
		buf[i] = cmis_read(&ctx, CMIS_P00_VENDOR_NAME + i);
	}
	zassert_mem_equal(buf, "ABCDEFGHIJKLMNOP", CMIS_ID_FIELD_LEN);
	zassert_equal(cmis_read(&ctx, CMIS_P00_SERIAL_NUMBER), ' ');
}

ZTEST(cmis_core, test_page_select_validation)
{
	zassert_equal(cmis_write(&ctx, CMIS_REG_PAGE_SELECT, 0x02), CMIS_OK);
	zassert_equal(cmis_write(&ctx, CMIS_REG_PAGE_SELECT, 0x01), CMIS_ERR_BAD_PAGE);
	zassert_equal(cmis_read(&ctx, CMIS_REG_PAGE_SELECT), 0x02, "page must not change");
}

ZTEST(cmis_core, test_read_only_regions)
{
	zassert_equal(cmis_write(&ctx, CMIS_REG_IDENTIFIER, 0x00), CMIS_ERR_READ_ONLY);
	zassert_equal(cmis_write(&ctx, CMIS_REG_TEMP_MSB, 0x00), CMIS_ERR_READ_ONLY);
	zassert_equal(cmis_write(&ctx, CMIS_P00_VENDOR_NAME, 'X'), CMIS_ERR_READ_ONLY);
	zassert_equal(cmis_read(&ctx, CMIS_REG_IDENTIFIER), CMIS_IDENTIFIER_QSFP_DD);
	zassert_equal(cmis_read(&ctx, CMIS_P00_VENDOR_NAME), 'S');
}

ZTEST(cmis_core, test_temperature_encoding)
{
	cmis_set_monitors(&ctx, -2688, 33000); /* -10.5 C, 3.3 V */
	zassert_equal(cmis_read(&ctx, CMIS_REG_TEMP_MSB), 0xF5);
	zassert_equal(cmis_read(&ctx, CMIS_REG_TEMP_LSB), 0x80);
	zassert_equal(cmis_read(&ctx, CMIS_REG_VCC_MSB), 0x80);
	zassert_equal(cmis_read(&ctx, CMIS_REG_VCC_LSB), 0xE8);
}

ZTEST(cmis_core, test_no_alarm_in_range)
{
	cmis_set_monitors(&ctx, 25 * 256, 33000);
	zassert_equal(cmis_read(&ctx, CMIS_REG_LATCHED_FLAGS), 0x00);
	zassert_true(cmis_read(&ctx, CMIS_REG_MODULE_STATE) & CMIS_INTL_DEASSERTED);
}

ZTEST(cmis_core, test_temp_high_alarm_latch_and_clear_on_read)
{
	cmis_set_monitors(&ctx, 80 * 256, 33000);
	zassert_false(cmis_read(&ctx, CMIS_REG_MODULE_STATE) & CMIS_INTL_DEASSERTED,
		      "IntL must assert");

	/* Back in range: the flag stays latched until read */
	cmis_set_monitors(&ctx, 25 * 256, 33000);
	zassert_equal(cmis_read(&ctx, CMIS_REG_LATCHED_FLAGS), CMIS_FLAG_TEMP_HIGH);
	zassert_equal(cmis_read(&ctx, CMIS_REG_LATCHED_FLAGS), 0x00, "clear on read");
	zassert_true(cmis_read(&ctx, CMIS_REG_MODULE_STATE) & CMIS_INTL_DEASSERTED);
}

ZTEST(cmis_core, test_host_lowers_threshold_to_trigger_alarm)
{
	/* Host writes TempHighAlarm = 20.0 C (0x1400) on page 02h */
	zassert_equal(cmis_write(&ctx, CMIS_REG_PAGE_SELECT, 0x02), CMIS_OK);
	zassert_equal(cmis_write(&ctx, CMIS_P02_TEMP_HIGH_ALARM, 0x14), CMIS_OK);
	zassert_equal(cmis_write(&ctx, CMIS_P02_TEMP_HIGH_ALARM + 1, 0x00), CMIS_OK);

	cmis_set_monitors(&ctx, 25 * 256, 33000);
	zassert_equal(cmis_read(&ctx, CMIS_REG_LATCHED_FLAGS), CMIS_FLAG_TEMP_HIGH);
}

ZTEST(cmis_core, test_low_temp_and_vcc_alarms)
{
	cmis_set_monitors(&ctx, -10 * 256, 29000);
	zassert_equal(cmis_read(&ctx, CMIS_REG_LATCHED_FLAGS),
		      CMIS_FLAG_TEMP_LOW | CMIS_FLAG_VCC_LOW);
}

ZTEST(cmis_core, test_fault_and_recovery_by_reset)
{
	go_ready();
	cmis_set_fault(&ctx, true);
	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_FAULT);
	zassert_equal(cmis_read(&ctx, CMIS_REG_MODULE_STATE) >> CMIS_MODULE_STATE_SHIFT,
		      CMIS_STATE_FAULT);

	/* Clearing the low-power request does not leave FAULT */
	zassert_equal(cmis_write(&ctx, CMIS_REG_MODULE_CTRL, 0x00), CMIS_OK);
	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_FAULT);

	cmis_set_fault(&ctx, false);
	zassert_equal(cmis_write(&ctx, CMIS_REG_MODULE_CTRL, CMIS_CTRL_SW_RESET), CMIS_OK);
	cmis_process(&ctx);
	cmis_process(&ctx);
	zassert_equal(cmis_get_state(&ctx), CMIS_STATE_LOW_PWR);
}

ZTEST_SUITE(cmis_core, NULL, NULL, before_each, NULL, NULL);
