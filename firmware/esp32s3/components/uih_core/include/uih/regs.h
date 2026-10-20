/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief STM8 base MCU and PAC1943 register encoding (doc/hardware.md).
 */

#ifndef UIH_REGS_H_
#define UIH_REGS_H_

#include <stdint.h>
#include <stdbool.h>
#include "uih/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_STM8_ADDR (0x51)
#define UIH_STM8_REG_WHOAMI (0x10)
#define UIH_STM8_WHOAMI_ID (0x35)
#define UIH_STM8_REG_VERSION (0x12)
#define UIH_STM8_VERSION_EXPECTED (4)
#define UIH_STM8_REG_CH1 (0x20)     ///< CH1..CH3 at 0x20..0x22
#define UIH_STM8_REG_AUX (0x24)     ///< reading clears the first-boot flag
#define UIH_STM8_REG_MUXOECTR (0x26)

#define UIH_PAC_ADDR (0x10)
#define UIH_PAC_REG_REFRESH (0x00)
#define UIH_PAC_REG_CTRL (0x01)
#define UIH_PAC_REG_VBUS1_AVG (0x0F)  ///< 12 bytes: VBUS1..3 then VSENSE1..3
#define UIH_PAC_REG_SMBUS_SETTINGS (0x1C)
#define UIH_PAC_REG_NEG_PWR_FSR (0x1D)
#define UIH_PAC_REG_ALERT_ENABLE (0x49)
#define UIH_PAC_REG_PRODUCT_ID (0xFD)
#define UIH_PAC_PRODUCT_ID (0x6A)
#define UIH_PAC_CTRL (0x0010)          ///< channel 4 off, 1024-sample averaging
#define UIH_PAC_NEG_PWR_FSR (0xAA00)   ///< VBUS 0..9 V, VSENSE bipolar +/-50 mV
#define UIH_PAC_SMBUS_SETTINGS (0x08)  ///< timeout enabled, clear POR flag

/// The meter channel (0..2) that measures port 1..3, at index port - 1.
extern const uint8_t UIH_PAC_CHANNEL_FOR_PORT[3];

typedef enum {
    UIH_ILIM_0_5 = 0,
    UIH_ILIM_1_0 = 1,
    UIH_ILIM_1_5 = 2,
    UIH_ILIM_2_0 = 3,
} uih_ilim_e;

typedef struct {
    bool power;
    bool data;
    bool fault;
    uih_ilim_e ilim;
} uih_stm8_channel_s;

uint8_t uih_stm8_channel_encode(bool power, bool data, uih_ilim_e ilim);
uih_stm8_channel_s uih_stm8_channel_decode(uint8_t reg);

/// True if the written fields (power, data, current limit) agree.
bool uih_stm8_channel_matches(uint8_t written, uint8_t readback);

uih_aux_s uih_stm8_aux_decode(uint8_t aux);
bool uih_stm8_aux_first_boot(uint8_t aux);

/// VBUS average register to mV (0 to 9 V full scale), rounded.
int32_t uih_pac_vbus_mv(uint16_t raw);

/// VSENSE average register to 0.1 mA, positive flowing to the device.
int32_t uih_pac_vsense_ma_x10(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_REGS_H_ */
