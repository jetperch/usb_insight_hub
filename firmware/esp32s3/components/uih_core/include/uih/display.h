/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Port display content: layout, change detection and rendering.
 *
 * Each 240 x 240 display shows four text lines for its port.  The driver
 * renders only the lines that changed, one horizontal band at a time, so
 * it needs a single band buffer of UIH_DISPLAY_BAND_BYTES.
 */

#ifndef UIH_DISPLAY_H_
#define UIH_DISPLAY_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "uih/hub.h"
#include "uih/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_DISPLAY_WIDTH (240)
#define UIH_DISPLAY_HEIGHT (240)
#define UIH_DISPLAY_COLS (12)
#define UIH_DISPLAY_BAND_HEIGHT_MAX (48)
/// RGB565, big endian (the ST7789 wire order).
#define UIH_DISPLAY_BAND_BYTES (UIH_DISPLAY_WIDTH * UIH_DISPLAY_BAND_HEIGHT_MAX * 2)

#define UIH_COLOR_BLACK (0x0000)
#define UIH_COLOR_WHITE (0xFFFF)
#define UIH_COLOR_GREY (0x7BEF)
#define UIH_COLOR_RED (0xF800)
#define UIH_COLOR_GREEN (0x07E0)

typedef enum {
    UIH_DISPLAY_LINE_TITLE,     ///< "CH1", plus " DATA OFF"
    UIH_DISPLAY_LINE_STATE,     ///< ON, OFF, FAULT or NO HUB
    UIH_DISPLAY_LINE_VOLTAGE,
    UIH_DISPLAY_LINE_CURRENT,
    UIH_DISPLAY_LINES,
} uih_display_line_e;

typedef struct {
    char text[UIH_DISPLAY_COLS + 1];
    uint16_t fg;      ///< RGB565
    uint8_t scale;    ///< font pixel size
} uih_display_line_s;

typedef struct {
    uih_display_line_s line[UIH_DISPLAY_LINES];
} uih_display_page_s;

/// What one display shows: a hub snapshot for its port.
typedef struct {
    uih_port_state_s port;
    bool hub_ok;     ///< the STM8 answers
    bool meter_ok;   ///< the PAC1943 answers
} uih_display_status_s;

/// Copy the hub state for the three displays (hub task only).
void uih_display_status_from_hub(const uih_hub_s * hub, uih_display_status_s status[UIH_PORT_COUNT]);

/// Format millivolts as "5.01 V" (10 mV resolution).
void uih_display_format_mv(int32_t mv, char out[UIH_DISPLAY_COLS + 1]);

/// Format 0.1 mA units as "176.7 mA", or "1.235 A" from 1 A.
void uih_display_format_ma_x10(int32_t ma_x10, char out[UIH_DISPLAY_COLS + 1]);

void uih_display_layout(const uih_display_status_s * status, uih_display_page_s * page);

/// The fixed band for a line: top row y and height h.
void uih_display_line_geometry(int line, uint16_t * y, uint16_t * h);

/**
 * @brief The lines that differ.
 *
 * @param prev The page on the display, or NULL if nothing was drawn.
 * @return A bit mask: bit n for line n.
 */
uint32_t uih_display_changed(const uih_display_page_s * prev, const uih_display_page_s * next);

/// The text width in pixels at a scale.
uint16_t uih_display_text_width(const char * text, uint8_t scale);

/**
 * @brief Render a line into a full-width band, centered, on black.
 *
 * Text outside the band is clipped.
 *
 * @param buf At least UIH_DISPLAY_WIDTH * h * 2 bytes.
 * @param h The band height, at most UIH_DISPLAY_BAND_HEIGHT_MAX.
 */
void uih_display_render(const uih_display_line_s * line, uint8_t * buf, uint16_t h);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_DISPLAY_H_ */
