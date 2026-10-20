/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/regs.h"

// The board routing (stock firmware meterBoardMap, doc/hardware.md).
const uint8_t UIH_PAC_CHANNEL_FOR_PORT[3] = {2, 0, 1};

#define CH_FAULT (0x80)
#define CH_DATA (0x40)
#define CH_POWER (0x10)
#define CH_ILIM_SHIFT (1)
#define CH_ILIM_MASK (0x06)
#define CH_WRITTEN (CH_DATA | CH_POWER | CH_ILIM_MASK)

#define AUX_VHOST (0x01)
#define AUX_VEXT (0x02)
#define AUX_MUX_OE (0x04)
#define AUX_FIRST_BOOT (0x10)

uint8_t uih_stm8_channel_encode(bool power, bool data, uih_ilim_e ilim) {
    uint8_t reg = (uint8_t) (((unsigned) ilim << CH_ILIM_SHIFT) & CH_ILIM_MASK);
    if (data) {
        reg |= CH_DATA;
    }
    if (power) {
        reg |= CH_POWER;
    }
    return reg;
}

uih_stm8_channel_s uih_stm8_channel_decode(uint8_t reg) {
    uih_stm8_channel_s ch = {
        .power = (reg & CH_POWER) != 0,
        .data = (reg & CH_DATA) != 0,
        .fault = (reg & CH_FAULT) != 0,
        .ilim = (uih_ilim_e) ((reg & CH_ILIM_MASK) >> CH_ILIM_SHIFT),
    };
    return ch;
}

bool uih_stm8_channel_matches(uint8_t written, uint8_t readback) {
    return (written & CH_WRITTEN) == (readback & CH_WRITTEN);
}

uih_aux_s uih_stm8_aux_decode(uint8_t aux) {
    uih_aux_s a = {
        .vhost = (aux & AUX_VHOST) != 0,
        .vext = (aux & AUX_VEXT) != 0,
        .usb3 = (aux & AUX_MUX_OE) != 0,
    };
    return a;
}

bool uih_stm8_aux_first_boot(uint8_t aux) {
    return (aux & AUX_FIRST_BOOT) != 0;
}

static int32_t div_round(int64_t num, int64_t den) {
    return (int32_t) ((num >= 0) ? ((num + den / 2) / den) : ((num - den / 2) / den));
}

int32_t uih_pac_vbus_mv(uint16_t raw) {
    return div_round((int64_t) raw * 9000, 65536);
}

int32_t uih_pac_vsense_ma_x10(uint16_t raw) {
    // 2500 mA full scale (50 mV / 20 mOhm), sign inverted by the board.
    return div_round(-(int64_t) (int16_t) raw * 25000, 32768);
}
