/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/hub.h"
#include "uih/regs.h"
#include <string.h>

#define DEFAULT_REG() uih_stm8_channel_encode(true, true, UIH_ILIM_1_0)

static int stm8_read(uih_hub_s * hub, uint8_t reg, uint8_t * data, size_t len) {
    return hub->ops.i2c_write_read(hub->ops.ctx, UIH_STM8_ADDR, &reg, 1, data, len);
}

static int stm8_write(uih_hub_s * hub, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return hub->ops.i2c_write(hub->ops.ctx, UIH_STM8_ADDR, buf, sizeof(buf));
}

static int pac_write(uih_hub_s * hub, const uint8_t * data, size_t len) {
    return hub->ops.i2c_write(hub->ops.ctx, UIH_PAC_ADDR, data, len);
}

static void emit(uih_hub_s * hub, uih_hub_event_type_e type, uint8_t port, bool value) {
    if (hub->ops.event) {
        uih_hub_event_s ev = {.type = type, .port = port, .value = value};
        hub->ops.event(hub->ops.event_ctx, &ev);
    }
}

void uih_hub_init(uih_hub_s * hub, const uih_hub_ops_s * ops) {
    memset(hub, 0, sizeof(*hub));
    hub->ops = *ops;
    hub->stm8_version = -1;
}

static bool meter_init(uih_hub_s * hub) {
    uint8_t reg = UIH_PAC_REG_PRODUCT_ID;
    uint8_t id = 0;
    if (hub->ops.i2c_write_read(hub->ops.ctx, UIH_PAC_ADDR, &reg, 1, &id, 1) ||
            (id != UIH_PAC_PRODUCT_ID)) {
        return false;
    }
    const uint8_t ctrl[] = {UIH_PAC_REG_CTRL, UIH_PAC_CTRL >> 8, UIH_PAC_CTRL & 0xFF};
    const uint8_t fsr[] = {UIH_PAC_REG_NEG_PWR_FSR, UIH_PAC_NEG_PWR_FSR >> 8, UIH_PAC_NEG_PWR_FSR & 0xFF};
    const uint8_t smbus[] = {UIH_PAC_REG_SMBUS_SETTINGS, UIH_PAC_SMBUS_SETTINGS};
    const uint8_t alerts[] = {UIH_PAC_REG_ALERT_ENABLE, 0, 0, 0};
    const uint8_t refresh[] = {UIH_PAC_REG_REFRESH};
    return !(pac_write(hub, ctrl, sizeof(ctrl)) || pac_write(hub, fsr, sizeof(fsr)) ||
             pac_write(hub, smbus, sizeof(smbus)) || pac_write(hub, alerts, sizeof(alerts)) ||
             pac_write(hub, refresh, sizeof(refresh)));
}

static int read_channels(uih_hub_s * hub, uint8_t regs[UIH_PORT_COUNT]) {
    return stm8_read(hub, UIH_STM8_REG_CH1, regs, UIH_PORT_COUNT);
}

/// Read AUXREG.  Returns -1 on error, else the register value.
static int read_aux(uih_hub_s * hub) {
    uint8_t aux = 0;
    if (stm8_read(hub, UIH_STM8_REG_AUX, &aux, 1)) {
        return -1;
    }
    hub->aux = uih_stm8_aux_decode(aux);
    return aux;
}

static uih_proto_code_e apply_defaults(uih_hub_s * hub) {
    stm8_write(hub, UIH_STM8_REG_MUXOECTR, 1);  // USB3 mux enabled (stock default)
    return uih_hub_set(hub, UIH_PORT_ALL, 1, 1);
}

static bool stm8_probe(uih_hub_s * hub) {
    uint8_t id = 0;
    uint8_t version = 0;
    hub->stm8_ok = !stm8_read(hub, UIH_STM8_REG_WHOAMI, &id, 1) &&
                   (id == UIH_STM8_WHOAMI_ID) &&
                   !stm8_read(hub, UIH_STM8_REG_VERSION, &version, 1);
    hub->stm8_version = hub->stm8_ok ? version : -1;
    return hub->stm8_ok;
}

static bool all_ports_off(const uint8_t regs[UIH_PORT_COUNT]) {
    for (int i = 0; i < UIH_PORT_COUNT; ++i) {
        uih_stm8_channel_s ch = uih_stm8_channel_decode(regs[i]);
        if (ch.power || ch.data) {
            return false;
        }
    }
    return true;
}

bool uih_hub_boot(uih_hub_s * hub, bool poweron_reset) {
    hub->meter_ok = meter_init(hub);
    if (!stm8_probe(hub)) {
        return false;  // uih_hub_poll() keeps probing
    }
    int aux = read_aux(hub);
    bool first_boot = (aux >= 0) && uih_stm8_aux_first_boot((uint8_t) aux);
    bool regs_ok = !read_channels(hub, hub->regs);
    // The first-boot flag marks an STM8 power-up.  The ESP32 reset reason
    // alone does not (an esptool watchdog reset reports power-on), so it
    // counts only when the STM8 also holds its power-up state.
    bool defaults = first_boot || (poweron_reset && regs_ok && all_ports_off(hub->regs));
    if (defaults) {
        apply_defaults(hub);
    }
    return defaults;
}

uih_proto_code_e uih_hub_set(uih_hub_s * hub, uint8_t port_mask, int8_t power, int8_t data) {
    uih_proto_code_e rc = UIH_PROTO_IO;
    for (int attempt = 0; attempt < UIH_HUB_SET_ATTEMPTS; ++attempt) {
        uint8_t now[UIH_PORT_COUNT];
        if (read_channels(hub, now)) {
            rc = UIH_PROTO_IO;
            continue;
        }
        memcpy(hub->regs, now, sizeof(now));
        uint8_t want[UIH_PORT_COUNT];
        bool io_error = false;
        for (int i = 0; i < UIH_PORT_COUNT; ++i) {
            uih_stm8_channel_s ch = uih_stm8_channel_decode(now[i]);
            want[i] = now[i];
            if (port_mask & (1u << i)) {
                bool p = (power < 0) ? ch.power : (power != 0);
                bool d = (data < 0) ? ch.data : (data != 0);
                want[i] = uih_stm8_channel_encode(p, d, UIH_ILIM_1_0);
                if (!uih_stm8_channel_matches(want[i], now[i]) &&
                        stm8_write(hub, (uint8_t) (UIH_STM8_REG_CH1 + i), want[i])) {
                    io_error = true;
                }
            }
        }
        if (io_error || read_channels(hub, now)) {
            rc = UIH_PROTO_IO;
            continue;
        }
        memcpy(hub->regs, now, sizeof(now));
        bool ok = true;
        for (int i = 0; i < UIH_PORT_COUNT; ++i) {
            if ((port_mask & (1u << i)) && !uih_stm8_channel_matches(want[i], now[i])) {
                ok = false;
            }
        }
        if (ok) {
            return UIH_PROTO_OK;
        }
        rc = UIH_PROTO_NOT_APPLIED;
    }
    return rc;
}

uih_proto_code_e uih_hub_toggle(uih_hub_s * hub, uint8_t port) {
    if ((port < 1) || (port > UIH_PORT_COUNT)) {
        return UIH_PROTO_ARG;
    }
    bool power = !uih_stm8_channel_decode(hub->regs[port - 1]).power;
    uih_proto_code_e rc = uih_hub_set(hub, (uint8_t) (1u << (port - 1)), power ? 1 : 0, -1);
    if (rc == UIH_PROTO_OK) {
        emit(hub, UIH_HUB_EVENT_BUTTON, port, power);
    }
    return rc;
}

static void meter_poll(uih_hub_s * hub) {
    if (!hub->meter_ok) {
        return;
    }
    uint8_t reg = UIH_PAC_REG_VBUS1_AVG;
    uint8_t raw[12];
    if (!hub->ops.i2c_write_read(hub->ops.ctx, UIH_PAC_ADDR, &reg, 1, raw, sizeof(raw))) {
        for (int i = 0; i < UIH_PORT_COUNT; ++i) {
            int ch = UIH_PAC_CHANNEL_FOR_PORT[i];
            hub->mv[i] = uih_pac_vbus_mv((uint16_t) ((raw[2 * ch] << 8) | raw[2 * ch + 1]));
            hub->ma_x10[i] = uih_pac_vsense_ma_x10((uint16_t) ((raw[6 + 2 * ch] << 8) | raw[7 + 2 * ch]));
        }
    }
    const uint8_t refresh[] = {UIH_PAC_REG_REFRESH};  // averages for the next poll
    pac_write(hub, refresh, sizeof(refresh));
}

void uih_hub_poll(uih_hub_s * hub) {
    if (!hub->meter_ok) {
        hub->meter_ok = meter_init(hub);
    }
    if (!hub->stm8_ok) {
        stm8_probe(hub);  // late to start: its first-boot flag still applies below
    }
    if (hub->stm8_ok) {
        int aux = read_aux(hub);
        if ((aux >= 0) && uih_stm8_aux_first_boot((uint8_t) aux)) {
            apply_defaults(hub);  // the STM8 restarted with every port off
            emit(hub, UIH_HUB_EVENT_STM8_RESET, 0, true);
        }
        uint8_t now[UIH_PORT_COUNT];
        if (!read_channels(hub, now)) {
            for (int i = 0; i < UIH_PORT_COUNT; ++i) {
                bool was = uih_stm8_channel_decode(hub->regs[i]).fault;
                bool is = uih_stm8_channel_decode(now[i]).fault;
                hub->regs[i] = now[i];
                if (was != is) {
                    emit(hub, UIH_HUB_EVENT_FAULT, (uint8_t) (i + 1), is);
                }
            }
        }
    }
    meter_poll(hub);
}

void uih_hub_snapshot(const uih_hub_s * hub, uih_port_state_s * ports, uih_aux_s * aux) {
    for (int i = 0; i < UIH_PORT_COUNT; ++i) {
        uih_stm8_channel_s ch = uih_stm8_channel_decode(hub->regs[i]);
        ports[i].port = (uint8_t) (i + 1);
        ports[i].power = ch.power;
        ports[i].data = ch.data;
        ports[i].fault = ch.fault;
        ports[i].mv = hub->mv[i];
        ports[i].ma_x10 = hub->ma_x10[i];
    }
    if (aux) {
        *aux = hub->aux;
    }
}
