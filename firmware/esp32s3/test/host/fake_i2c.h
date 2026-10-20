/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Fake I2C bus with an STM8 base MCU and a PAC1943, for host tests.
 *
 * The STM8 model follows its firmware (UIH-STM8S003K3/src/I2c_slave_interrupt.c
 * and baser.c): auto-incrementing register access, channel writes stored as
 * (reg | 0x7F) & value, the fault bit recomputed from the fault pin, and the
 * first-boot flag cleared by reading AUXREG.
 */

#ifndef FAKE_I2C_H_
#define FAKE_I2C_H_

#include "uih/hub.h"
#include "uih/regs.h"
#include <string.h>

typedef struct {
    // STM8
    bool stm8_present;
    uint8_t stm8_version;
    uint8_t ch[3];
    bool fault_pin[3];          // true: fault asserted
    bool first_boot;
    uint8_t aux_inputs;         // vhost, vext, mux bits (without first boot)
    uint8_t muxoe;
    int stm8_writes;
    int drop_channel_writes;    // ignore the next N channel writes (lost on the bus)
    int fail_transfers;         // fail the next N transfers
    int fail_skip;              // ... after passing this many
    // PAC1943
    bool pac_present;
    uint16_t pac_ctrl;
    uint16_t pac_fsr;
    uint8_t pac_smbus;
    int pac_refreshes;
    uint16_t vbus[3];
    uint16_t vsense[3];
    // time
    uint32_t now_ms;
    // events
    uih_hub_event_s events[16];
    int event_count;
} fake_bus_s;

static inline void fake_bus_init(fake_bus_s * b) {
    memset(b, 0, sizeof(*b));
    b->stm8_present = true;
    b->stm8_version = 4;
    b->first_boot = true;
    b->aux_inputs = 0x01 | 0x04;  // vhost present, USB3 mux enabled
    b->muxoe = 1;
    b->pac_present = true;
}

static inline void fake_stm8_update(fake_bus_s * b) {
    for (int i = 0; i < 3; ++i) {
        b->ch[i] = b->fault_pin[i] ? (b->ch[i] | 0x80) : (b->ch[i] & 0x7F);
    }
}

static inline uint8_t fake_stm8_read_reg(fake_bus_s * b, uint8_t reg) {
    switch (reg) {
        case 0x10: return 0x35;
        case 0x12: return b->stm8_version;
        case 0x20: case 0x21: case 0x22: return b->ch[reg - 0x20];
        case 0x23: return 0x23;
        case 0x24: {
            uint8_t v = (uint8_t) (b->aux_inputs | (b->first_boot ? 0x10 : 0));
            b->first_boot = false;
            return v;
        }
        case 0x26: return b->muxoe;
        default: return 0;
    }
}

static inline bool fake_fail(fake_bus_s * b) {
    if (!b->fail_transfers) {
        return false;
    } else if (b->fail_skip) {
        b->fail_skip--;
        return false;
    }
    b->fail_transfers--;
    return true;
}

static inline int fake_write(void * ctx, uint8_t addr, const uint8_t * data, size_t len) {
    fake_bus_s * b = (fake_bus_s *) ctx;
    if (fake_fail(b)) {
        return 1;
    }
    if (addr == UIH_STM8_ADDR && b->stm8_present) {
        fake_stm8_update(b);
        uint8_t reg = data[0];
        for (size_t i = 1; i < len; ++i, ++reg) {
            if (reg >= 0x20 && reg <= 0x22) {
                b->stm8_writes++;
                if (b->drop_channel_writes) {
                    b->drop_channel_writes--;
                    continue;
                }
                b->ch[reg - 0x20] = (uint8_t) ((b->ch[reg - 0x20] | 0x7F) & data[i]);
            } else if (reg == 0x26) {
                b->muxoe = data[i] & 1;
            }
        }
        fake_stm8_update(b);
        return 0;
    }
    if (addr == UIH_PAC_ADDR && b->pac_present) {
        if (len == 1 && data[0] == UIH_PAC_REG_REFRESH) {
            b->pac_refreshes++;
        } else if (data[0] == UIH_PAC_REG_CTRL && len == 3) {
            b->pac_ctrl = (uint16_t) ((data[1] << 8) | data[2]);
        } else if (data[0] == UIH_PAC_REG_NEG_PWR_FSR && len == 3) {
            b->pac_fsr = (uint16_t) ((data[1] << 8) | data[2]);
        } else if (data[0] == UIH_PAC_REG_SMBUS_SETTINGS && len == 2) {
            b->pac_smbus = data[1];
        }
        return 0;
    }
    return 1;  // NAK
}

static inline int fake_write_read(void * ctx, uint8_t addr, const uint8_t * w, size_t wlen,
                                  uint8_t * r, size_t rlen) {
    fake_bus_s * b = (fake_bus_s *) ctx;
    if (fake_fail(b)) {
        return 1;
    }
    if (addr == UIH_STM8_ADDR && b->stm8_present && wlen == 1) {
        fake_stm8_update(b);
        for (size_t i = 0; i < rlen; ++i) {
            r[i] = fake_stm8_read_reg(b, (uint8_t) (w[0] + i));
        }
        return 0;
    }
    if (addr == UIH_PAC_ADDR && b->pac_present && wlen == 1) {
        if (w[0] == UIH_PAC_REG_PRODUCT_ID && rlen == 1) {
            r[0] = UIH_PAC_PRODUCT_ID;
            return 0;
        }
        if (w[0] == UIH_PAC_REG_VBUS1_AVG && rlen == 12) {
            for (int i = 0; i < 3; ++i) {
                r[2 * i] = (uint8_t) (b->vbus[i] >> 8);
                r[2 * i + 1] = (uint8_t) b->vbus[i];
                r[6 + 2 * i] = (uint8_t) (b->vsense[i] >> 8);
                r[6 + 2 * i + 1] = (uint8_t) b->vsense[i];
            }
            return 0;
        }
        return 1;
    }
    return 1;
}

static inline uint32_t fake_time_ms(void * ctx) {
    return ((fake_bus_s *) ctx)->now_ms;
}

static inline void fake_delay_ms(void * ctx, uint32_t ms) {
    ((fake_bus_s *) ctx)->now_ms += ms;
}

static inline void fake_event(void * ctx, const uih_hub_event_s * ev) {
    fake_bus_s * b = (fake_bus_s *) ctx;
    if (b->event_count < 16) {
        b->events[b->event_count++] = *ev;
    }
}

static inline uih_hub_ops_s fake_ops(fake_bus_s * b) {
    uih_hub_ops_s ops = {
        .i2c_write = fake_write,
        .i2c_write_read = fake_write_read,
        .time_ms = fake_time_ms,
        .delay_ms = fake_delay_ms,
        .event = fake_event,
        .ctx = b,
        .event_ctx = b,
    };
    return ops;
}

#endif  /* FAKE_I2C_H_ */
