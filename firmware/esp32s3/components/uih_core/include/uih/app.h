/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief The protocol application: requests in, responses and events out.
 *
 * Runs in the single hub task.  The platform supplies the transmit
 * function and the bootloader/reset actions; the hub supplies time.
 */

#ifndef UIH_APP_H_
#define UIH_APP_H_

#include "uih/hub.h"
#include "uih/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_APP_POLL_MS (50)
#define UIH_APP_ACTION_DELAY_MS (100)
#define UIH_APP_TX_MAX (1024)

typedef struct {
    /**
     * @brief Send one complete line.
     *
     * @param droppable true for status messages, which may be dropped when
     *      the transmit buffer is full.  Others must be sent.
     * @return true if sent.
     */
    bool (*tx)(void * ctx, const char * line, size_t len, bool droppable);
    void (*bootloader)(void * ctx);   ///< reboot into the ROM bootloader
    void (*reset)(void * ctx);        ///< reboot the firmware
    void * ctx;
    uih_info_s info;   ///< fw, git, idf and serial; the rest is filled in
} uih_app_platform_s;

typedef enum {
    UIH_APP_ACTION_NONE,
    UIH_APP_ACTION_BOOTLOADER,
    UIH_APP_ACTION_RESET,
} uih_app_action_e;

typedef struct {
    uih_app_platform_s pf;
    uih_hub_s * hub;
    uint32_t status_n;
    uint32_t event_n;
    uint8_t rate_hz;
    uint32_t next_status_ms;
    uint32_t last_poll_ms;
    uih_app_action_e action;
    uint32_t action_ms;
    const char * reset_reason;
    char buf[UIH_APP_TX_MAX];
} uih_app_s;

/// Initialize; routes hub events to the app.
void uih_app_init(uih_app_s * app, uih_hub_s * hub, const uih_app_platform_s * platform);

/// Boot the hub and send the boot event.
void uih_app_boot(uih_app_s * app, bool poweron_reset, const char * reset_reason);

/// Handle one received line, without its line ending.
void uih_app_line(uih_app_s * app, const char * line, size_t len);

/// Report a received line that exceeded UIH_PROTO_LINE_MAX.
void uih_app_overflow(uih_app_s * app);

/// A button press for port 1..3.
void uih_app_button(uih_app_s * app, uint8_t port);

/// Periodic work: hub polling, the status stream and pending actions.
void uih_app_tick(uih_app_s * app);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_APP_H_ */
