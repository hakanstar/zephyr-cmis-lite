/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cmis_core.h"

#include <stddef.h>
#include <string.h>

static void put_be16(uint8_t *dst, uint16_t v)
{
	dst[0] = (uint8_t)(v >> 8);
	dst[1] = (uint8_t)(v & 0xFFU);
}

static uint16_t get_be16(const uint8_t *src)
{
	return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static void put_ascii(uint8_t *dst, const char *s)
{
	size_t n = (s != NULL) ? strlen(s) : 0U;

	if (n > CMIS_ID_FIELD_LEN) {
		n = CMIS_ID_FIELD_LEN;
	}
	memset(dst, ' ', CMIS_ID_FIELD_LEN);
	if (n > 0U) {
		memcpy(dst, s, n);
	}
}

static void update_module_state_reg(struct cmis_ctx *ctx)
{
	uint8_t reg = (uint8_t)((uint8_t)ctx->state << CMIS_MODULE_STATE_SHIFT);

	if (ctx->lower[CMIS_REG_LATCHED_FLAGS] == 0U) {
		reg |= CMIS_INTL_DEASSERTED;
	}
	ctx->lower[CMIS_REG_MODULE_STATE] = reg;
}

static void load_defaults(struct cmis_ctx *ctx)
{
	memset(ctx->lower, 0, sizeof(ctx->lower));
	memset(ctx->page00, 0, sizeof(ctx->page00));
	memset(ctx->page02, 0, sizeof(ctx->page02));

	ctx->lower[CMIS_REG_IDENTIFIER] = CMIS_IDENTIFIER_QSFP_DD;
	ctx->lower[CMIS_REG_REVISION] = CMIS_MAP_REVISION;
	ctx->lower[CMIS_REG_MODULE_CTRL] = CMIS_CTRL_LOW_PWR_REQ;
	ctx->lower[CMIS_REG_PAGE_SELECT] = 0x00U;

	put_ascii(&ctx->page00[CMIS_P00_VENDOR_NAME - CMIS_PAGE_SIZE], ctx->id.vendor);
	put_ascii(&ctx->page00[CMIS_P00_PART_NUMBER - CMIS_PAGE_SIZE], ctx->id.part_number);
	put_ascii(&ctx->page00[CMIS_P00_SERIAL_NUMBER - CMIS_PAGE_SIZE], ctx->id.serial);

	put_be16(&ctx->page02[CMIS_P02_TEMP_HIGH_ALARM - CMIS_PAGE_SIZE],
		 (uint16_t)(int16_t)CMIS_DEFAULT_TEMP_HIGH_Q8);
	put_be16(&ctx->page02[CMIS_P02_TEMP_LOW_ALARM - CMIS_PAGE_SIZE],
		 (uint16_t)(int16_t)CMIS_DEFAULT_TEMP_LOW_Q8);
	put_be16(&ctx->page02[CMIS_P02_VCC_LOW_ALARM - CMIS_PAGE_SIZE],
		 (uint16_t)CMIS_DEFAULT_VCC_LOW);

	ctx->state = CMIS_STATE_RESET;
	ctx->fault = false;
	update_module_state_reg(ctx);
}

int cmis_init(struct cmis_ctx *ctx, const struct cmis_identity *id)
{
	if (ctx == NULL || id == NULL) {
		return CMIS_ERR_PARAM;
	}
	ctx->id = *id;
	load_defaults(ctx);
	return CMIS_OK;
}

uint8_t cmis_read(struct cmis_ctx *ctx, uint8_t addr)
{
	uint8_t val;

	if (addr < CMIS_PAGE_SIZE) {
		val = ctx->lower[addr];
		if (addr == CMIS_REG_LATCHED_FLAGS) {
			ctx->lower[CMIS_REG_LATCHED_FLAGS] = 0U;
			update_module_state_reg(ctx);
		}
		return val;
	}

	switch (ctx->lower[CMIS_REG_PAGE_SELECT]) {
	case 0x00U:
		return ctx->page00[addr - CMIS_PAGE_SIZE];
	case 0x02U:
		return ctx->page02[addr - CMIS_PAGE_SIZE];
	default:
		return 0x00U; /* unreachable: page select is validated on write */
	}
}

int cmis_write(struct cmis_ctx *ctx, uint8_t addr, uint8_t value)
{
	if (addr == CMIS_REG_MODULE_CTRL) {
		ctx->lower[CMIS_REG_MODULE_CTRL] = value & CMIS_CTRL_WRITABLE;
		return CMIS_OK;
	}

	if (addr == CMIS_REG_PAGE_SELECT) {
		if (value != 0x00U && value != 0x02U) {
			return CMIS_ERR_BAD_PAGE;
		}
		ctx->lower[CMIS_REG_PAGE_SELECT] = value;
		return CMIS_OK;
	}

	if (addr >= CMIS_PAGE_SIZE && ctx->lower[CMIS_REG_PAGE_SELECT] == 0x02U) {
		ctx->page02[addr - CMIS_PAGE_SIZE] = value;
		return CMIS_OK;
	}

	return CMIS_ERR_READ_ONLY;
}

void cmis_set_monitors(struct cmis_ctx *ctx, int16_t temp_q8, uint16_t vcc_100uv)
{
	const uint8_t *p02 = ctx->page02;
	int16_t t_high = (int16_t)get_be16(&p02[CMIS_P02_TEMP_HIGH_ALARM - CMIS_PAGE_SIZE]);
	int16_t t_low = (int16_t)get_be16(&p02[CMIS_P02_TEMP_LOW_ALARM - CMIS_PAGE_SIZE]);
	uint16_t v_low = get_be16(&p02[CMIS_P02_VCC_LOW_ALARM - CMIS_PAGE_SIZE]);
	uint8_t flags = 0U;

	put_be16(&ctx->lower[CMIS_REG_TEMP_MSB], (uint16_t)temp_q8);
	put_be16(&ctx->lower[CMIS_REG_VCC_MSB], vcc_100uv);

	if (temp_q8 > t_high) {
		flags |= CMIS_FLAG_TEMP_HIGH;
	}
	if (temp_q8 < t_low) {
		flags |= CMIS_FLAG_TEMP_LOW;
	}
	if (vcc_100uv < v_low) {
		flags |= CMIS_FLAG_VCC_LOW;
	}

	/* Latch: bits stay set until the host reads 0x08 */
	ctx->lower[CMIS_REG_LATCHED_FLAGS] |= flags;
	update_module_state_reg(ctx);
}

void cmis_set_fault(struct cmis_ctx *ctx, bool fault)
{
	ctx->fault = fault;
}

void cmis_process(struct cmis_ctx *ctx)
{
	uint8_t ctrl = ctx->lower[CMIS_REG_MODULE_CTRL];

	if ((ctrl & CMIS_CTRL_SW_RESET) != 0U) {
		/* Self-clearing: defaults reload CTRL without the reset bit */
		load_defaults(ctx);
		return;
	}

	if (ctx->fault && ctx->state != CMIS_STATE_RESET) {
		ctx->state = CMIS_STATE_FAULT;
		update_module_state_reg(ctx);
		return;
	}

	switch (ctx->state) {
	case CMIS_STATE_RESET:
		ctx->state = CMIS_STATE_LOW_PWR;
		break;
	case CMIS_STATE_LOW_PWR:
		if ((ctrl & CMIS_CTRL_LOW_PWR_REQ) == 0U) {
			ctx->state = CMIS_STATE_PWR_UP;
		}
		break;
	case CMIS_STATE_PWR_UP:
		ctx->state = ((ctrl & CMIS_CTRL_LOW_PWR_REQ) != 0U) ? CMIS_STATE_LOW_PWR
								      : CMIS_STATE_READY;
		break;
	case CMIS_STATE_READY:
		if ((ctrl & CMIS_CTRL_LOW_PWR_REQ) != 0U) {
			ctx->state = CMIS_STATE_LOW_PWR;
		}
		break;
	case CMIS_STATE_FAULT:
	default:
		/* Leave FAULT only via software reset */
		break;
	}

	update_module_state_reg(ctx);
}

enum cmis_state cmis_get_state(const struct cmis_ctx *ctx)
{
	return ctx->state;
}
