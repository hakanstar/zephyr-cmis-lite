/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * cmis_core: platform-independent, CMIS-inspired module register map
 * and state machine. No Zephyr headers here on purpose, so the core can
 * be unit-tested anywhere (ztest on native_sim, or plain gcc on a host).
 *
 * This is a simplified subset inspired by CMIS. It is NOT CMIS compliant.
 */
#ifndef CMIS_CORE_H_
#define CMIS_CORE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMIS_PAGE_SIZE 128U

/* Lower memory (0x00-0x7F), always visible */
#define CMIS_REG_IDENTIFIER    0x00U
#define CMIS_REG_REVISION      0x01U
#define CMIS_REG_MODULE_STATE  0x03U
#define CMIS_REG_LATCHED_FLAGS 0x08U
#define CMIS_REG_TEMP_MSB      0x0EU
#define CMIS_REG_TEMP_LSB      0x0FU
#define CMIS_REG_VCC_MSB       0x10U
#define CMIS_REG_VCC_LSB       0x11U
#define CMIS_REG_MODULE_CTRL   0x1AU
#define CMIS_REG_PAGE_SELECT   0x7FU

/* Upper page 00h: identity (read-only, ASCII padded with spaces) */
#define CMIS_P00_VENDOR_NAME   0x81U
#define CMIS_P00_PART_NUMBER   0x94U
#define CMIS_P00_SERIAL_NUMBER 0xA6U
#define CMIS_ID_FIELD_LEN      16U

/* Upper page 02h: thresholds (read-write in this project, big-endian) */
#define CMIS_P02_TEMP_HIGH_ALARM 0x80U
#define CMIS_P02_TEMP_LOW_ALARM  0x82U
#define CMIS_P02_VCC_LOW_ALARM   0x88U

/* Fixed register values */
#define CMIS_IDENTIFIER_QSFP_DD 0x18U
#define CMIS_MAP_REVISION       0x10U /* this project's map v1.0 */

/* Latched flags (0x08), cleared on read */
#define CMIS_FLAG_TEMP_HIGH (1U << 0)
#define CMIS_FLAG_TEMP_LOW  (1U << 1)
#define CMIS_FLAG_VCC_LOW   (1U << 2)

/* Module control (0x1A) */
#define CMIS_CTRL_SW_RESET    (1U << 3)
#define CMIS_CTRL_LOW_PWR_REQ (1U << 4)
#define CMIS_CTRL_WRITABLE    (CMIS_CTRL_SW_RESET | CMIS_CTRL_LOW_PWR_REQ)

/* Module state (0x03): bits 3:1 = state, bit 0 = 1 when IntL deasserted */
#define CMIS_MODULE_STATE_SHIFT 1U
#define CMIS_INTL_DEASSERTED    (1U << 0)

/* Default thresholds */
#define CMIS_DEFAULT_TEMP_HIGH_Q8 (75 * 256)  /* +75.0 C */
#define CMIS_DEFAULT_TEMP_LOW_Q8  (-5 * 256)  /*  -5.0 C */
#define CMIS_DEFAULT_VCC_LOW      30000U      /*  3.0 V in 100 uV units */

enum cmis_state {
	CMIS_STATE_RESET = 0,
	CMIS_STATE_LOW_PWR = 1,
	CMIS_STATE_PWR_UP = 2,
	CMIS_STATE_READY = 3,
	CMIS_STATE_FAULT = 5,
};

enum cmis_status {
	CMIS_OK = 0,
	CMIS_ERR_READ_ONLY = -1,
	CMIS_ERR_BAD_PAGE = -2,
	CMIS_ERR_PARAM = -3,
};

struct cmis_identity {
	const char *vendor;
	const char *part_number;
	const char *serial;
};

struct cmis_ctx {
	uint8_t lower[CMIS_PAGE_SIZE];
	uint8_t page00[CMIS_PAGE_SIZE];
	uint8_t page02[CMIS_PAGE_SIZE];
	enum cmis_state state;
	struct cmis_identity id;
	bool fault;
};

/* Load defaults; state becomes RESET. Call cmis_process() to leave RESET. */
int cmis_init(struct cmis_ctx *ctx, const struct cmis_identity *id);

/* Host-visible byte read. Reading 0x08 clears the latched flags. */
uint8_t cmis_read(struct cmis_ctx *ctx, uint8_t addr);

/* Host-visible byte write. Returns enum cmis_status. */
int cmis_write(struct cmis_ctx *ctx, uint8_t addr, uint8_t value);

/* New sensor sample: updates monitor registers and latches alarms. */
void cmis_set_monitors(struct cmis_ctx *ctx, int16_t temp_q8, uint16_t vcc_100uv);

/* Sensor health. A fault moves the module to FAULT on the next process. */
void cmis_set_fault(struct cmis_ctx *ctx, bool fault);

/* Advance the state machine one step based on control bits and faults. */
void cmis_process(struct cmis_ctx *ctx);

enum cmis_state cmis_get_state(const struct cmis_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* CMIS_CORE_H_ */
