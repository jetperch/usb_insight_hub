/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief USB Insight Hub board support (doc/hardware.md).
 *
 * The I2C, time and USB transmit functions match the uih_hub_ops_s and
 * uih_app_platform_s callbacks; their ctx argument is unused.
 */

#ifndef UIH_BOARD_H_
#define UIH_BOARD_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t uih_board_i2c_init(void);
int uih_board_i2c_write(void * ctx, uint8_t addr, const uint8_t * data, size_t len);
int uih_board_i2c_write_read(void * ctx, uint8_t addr, const uint8_t * w, size_t wlen,
                             uint8_t * r, size_t rlen);
uint32_t uih_board_time_ms(void * ctx);
void uih_board_delay_ms(void * ctx, uint32_t ms);

esp_err_t uih_board_usb_init(void);
/// Read up to size bytes, waiting up to timeout_ms.  Returns the count.
int uih_board_usb_read(uint8_t * buf, size_t size, uint32_t timeout_ms);
/// Send one whole line; droppable lines never wait for buffer space.
bool uih_board_usb_tx(void * ctx, const char * line, size_t len, bool droppable);

void uih_board_buttons_init(void);
/// Return the raw pressed mask: bit n-1 for the CHn button.
uint8_t uih_board_buttons_read(void);

/**
 * @brief Start the three displays: blank, on, with the fixed backlight level.
 *
 * @param[out] step On error, the name of the step that failed.
 */
esp_err_t uih_board_display_init(const char ** step);
/// The DMA band buffer, UIH_DISPLAY_BAND_BYTES.
uint8_t * uih_board_display_buffer(void);
/// Draw a full-width band of rows y..y+h-1 on display 0..2 and wait for it.
esp_err_t uih_board_display_band(uint8_t display, uint16_t y, uint16_t h, const uint8_t * pixels);

/// Reboot into the ESP32-S3 ROM bootloader (USB-Serial-JTAG download mode).
void uih_board_bootloader(void * ctx);
void uih_board_reset(void * ctx);

/// The reset reason as a protocol string; sets *poweron for a power-on reset.
const char * uih_board_reset_reason(bool * poweron);

/// The MAC as 12 uppercase hex digits plus NUL (the USB serial number).
void uih_board_serial(char out[13]);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_BOARD_H_ */
