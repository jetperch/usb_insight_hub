/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief The hub state machine: the single owner of the STM8 and PAC1943.
 *
 * Every hardware access goes through the injected I2C operations, from one
 * task.  Writes are verified by reading the STM8 back, so the port state
 * reported to the host is always the read-back state.
 */

#ifndef UIH_HUB_H_
#define UIH_HUB_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "uih/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_HUB_SET_ATTEMPTS (3)

typedef enum {
    UIH_HUB_EVENT_BUTTON,      ///< port, value = new power state
    UIH_HUB_EVENT_FAULT,       ///< port, value = fault asserted
    UIH_HUB_EVENT_STM8_RESET,  ///< the STM8 restarted; defaults were reapplied
} uih_hub_event_type_e;

typedef struct {
    uih_hub_event_type_e type;
    uint8_t port;   ///< 1..3, or 0
    bool value;
} uih_hub_event_s;

typedef struct {
    /// Write len bytes to the 7-bit address.  Return 0 on success.
    int (*i2c_write)(void * ctx, uint8_t addr, const uint8_t * data, size_t len);
    /// Write wlen bytes, then read rlen bytes (repeated start).  Return 0 on success.
    int (*i2c_write_read)(void * ctx, uint8_t addr, const uint8_t * w, size_t wlen,
                          uint8_t * r, size_t rlen);
    uint32_t (*time_ms)(void * ctx);
    void (*delay_ms)(void * ctx, uint32_t ms);
    /// Optional, called from uih_hub_poll() and uih_hub_toggle() with event_ctx.
    void (*event)(void * event_ctx, const uih_hub_event_s * event);
    void * ctx;         ///< for i2c_*, time_ms and delay_ms
    void * event_ctx;   ///< for event
} uih_hub_ops_s;

typedef struct {
    uih_hub_ops_s ops;
    bool stm8_ok;
    int32_t stm8_version;   ///< -1 if the STM8 did not answer
    bool meter_ok;
    uint8_t regs[UIH_PORT_COUNT];   ///< last STM8 channel read-back
    uih_aux_s aux;
    int32_t mv[UIH_PORT_COUNT];
    int32_t ma_x10[UIH_PORT_COUNT];
} uih_hub_s;

void uih_hub_init(uih_hub_s * hub, const uih_hub_ops_s * ops);

/**
 * @brief Start the hub after a reset.
 *
 * Applies the power-up defaults (all ports power and data on, 1.0 A, USB3
 * enabled) when the STM8 first-boot flag is set or poweron_reset is true.
 * Otherwise adopts the port state the STM8 holds.
 *
 * @return true if the defaults were applied.
 */
bool uih_hub_boot(uih_hub_s * hub, bool poweron_reset);

/**
 * @brief Set power and/or data on the ports in port_mask, then verify.
 *
 * @param power -1 unchanged, 0 off, 1 on.
 * @param data -1 unchanged, 0 off, 1 on.
 * @return UIH_PROTO_OK, UIH_PROTO_IO or UIH_PROTO_NOT_APPLIED.
 */
uih_proto_code_e uih_hub_set(uih_hub_s * hub, uint8_t port_mask, int8_t power, int8_t data);

/// Toggle a port's power (button) and emit a button event on success.
uih_proto_code_e uih_hub_toggle(uih_hub_s * hub, uint8_t port);

/// Read the STM8 and meter: tracks faults, STM8 resets and measurements.
void uih_hub_poll(uih_hub_s * hub);

/// Copy the current port states (array of 3) and optionally aux.
void uih_hub_snapshot(const uih_hub_s * hub, uih_port_state_s * ports, uih_aux_s * aux);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_HUB_H_ */
