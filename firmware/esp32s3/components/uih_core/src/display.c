/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/display.h"
#include "uih/font.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t y;
    uint16_t h;
    uint8_t scale;
} band_s;

static const band_s BANDS[UIH_DISPLAY_LINES] = {
    [UIH_DISPLAY_LINE_TITLE] = {.y = 16, .h = 24, .scale = 3},
    [UIH_DISPLAY_LINE_STATE] = {.y = 52, .h = 48, .scale = 6},
    [UIH_DISPLAY_LINE_VOLTAGE] = {.y = 124, .h = 40, .scale = 5},
    [UIH_DISPLAY_LINE_CURRENT] = {.y = 176, .h = 40, .scale = 5},
};

void uih_display_status_from_hub(const uih_hub_s * hub, uih_display_status_s status[UIH_PORT_COUNT]) {
    uih_port_state_s ports[UIH_PORT_COUNT];
    uih_hub_snapshot(hub, ports, NULL);
    for (int i = 0; i < UIH_PORT_COUNT; ++i) {
        status[i].port = ports[i];
        status[i].hub_ok = hub->stm8_ok;
        status[i].meter_ok = hub->meter_ok;
    }
}

/// Round to a multiple of div, half away from zero, and return the quotient.
static int32_t round_div(int32_t v, int32_t div) {
    return (v >= 0) ? ((v + div / 2) / div) : -((-v + div / 2) / div);
}

/// Copy at most UIH_DISPLAY_COLS characters: out-of-range values are clipped.
static void copy_text(char out[UIH_DISPLAY_COLS + 1], const char * text) {
    memset(out, 0, UIH_DISPLAY_COLS + 1);
    strncpy(out, text, UIH_DISPLAY_COLS);
}

void uih_display_format_mv(int32_t mv, char out[UIH_DISPLAY_COLS + 1]) {
    char buf[32];
    int32_t cv = round_div(mv, 10);
    int32_t a = (cv < 0) ? -cv : cv;
    snprintf(buf, sizeof(buf), "%s%ld.%02ld V", (cv < 0) ? "-" : "",
             (long) (a / 100), (long) (a % 100));
    copy_text(out, buf);
}

void uih_display_format_ma_x10(int32_t ma_x10, char out[UIH_DISPLAY_COLS + 1]) {
    char buf[32];
    int32_t a = (ma_x10 < 0) ? -ma_x10 : ma_x10;
    const char * sign = (ma_x10 < 0) ? "-" : "";
    if (a < 10000) {
        snprintf(buf, sizeof(buf), "%s%ld.%ld mA", sign, (long) (a / 10), (long) (a % 10));
    } else {
        int32_t ma = round_div(a, 10);
        snprintf(buf, sizeof(buf), "%s%ld.%03ld A", sign, (long) (ma / 1000), (long) (ma % 1000));
    }
    copy_text(out, buf);
}

static void set_line(uih_display_page_s * page, int idx, const char * text, uint16_t fg) {
    uih_display_line_s * line = &page->line[idx];
    copy_text(line->text, text);
    line->fg = fg;
    line->scale = BANDS[idx].scale;
}

void uih_display_layout(const uih_display_status_s * status, uih_display_page_s * page) {
    const uih_port_state_s * p = &status->port;
    char buf[32];
    snprintf(buf, sizeof(buf), "CH%u%s", (unsigned) p->port, p->data ? "" : " DATA OFF");
    set_line(page, UIH_DISPLAY_LINE_TITLE, buf, UIH_COLOR_WHITE);

    if (!status->hub_ok) {
        set_line(page, UIH_DISPLAY_LINE_STATE, "NO HUB", UIH_COLOR_RED);
    } else if (p->fault) {
        set_line(page, UIH_DISPLAY_LINE_STATE, "FAULT", UIH_COLOR_RED);
    } else if (p->power) {
        set_line(page, UIH_DISPLAY_LINE_STATE, "ON", UIH_COLOR_GREEN);
    } else {
        set_line(page, UIH_DISPLAY_LINE_STATE, "OFF", UIH_COLOR_GREY);
    }

    if (status->meter_ok) {
        uih_display_format_mv(p->mv, buf);
        set_line(page, UIH_DISPLAY_LINE_VOLTAGE, buf, UIH_COLOR_WHITE);
        uih_display_format_ma_x10(p->ma_x10, buf);
        set_line(page, UIH_DISPLAY_LINE_CURRENT, buf, UIH_COLOR_WHITE);
    } else {
        set_line(page, UIH_DISPLAY_LINE_VOLTAGE, "-.-- V", UIH_COLOR_GREY);
        set_line(page, UIH_DISPLAY_LINE_CURRENT, "-.- mA", UIH_COLOR_GREY);
    }
}

void uih_display_line_geometry(int line, uint16_t * y, uint16_t * h) {
    *y = BANDS[line].y;
    *h = BANDS[line].h;
}

uint32_t uih_display_changed(const uih_display_page_s * prev, const uih_display_page_s * next) {
    uint32_t mask = 0;
    for (int i = 0; i < UIH_DISPLAY_LINES; ++i) {
        const uih_display_line_s * a = prev ? &prev->line[i] : NULL;
        const uih_display_line_s * b = &next->line[i];
        if (!a || strcmp(a->text, b->text) || (a->fg != b->fg) || (a->scale != b->scale)) {
            mask |= 1u << i;
        }
    }
    return mask;
}

uint16_t uih_display_text_width(const char * text, uint8_t scale) {
    size_t n = strlen(text);
    return n ? (uint16_t) ((n * UIH_FONT_ADVANCE - 1) * scale) : 0;
}

void uih_display_render(const uih_display_line_s * line, uint8_t * buf, uint16_t h) {
    if (h > UIH_DISPLAY_BAND_HEIGHT_MAX) {
        h = UIH_DISPLAY_BAND_HEIGHT_MAX;
    }
    memset(buf, 0, (size_t) UIH_DISPLAY_WIDTH * h * 2);  // black
    int scale = line->scale;
    if (!scale) {
        return;
    }
    uint8_t hi = (uint8_t) (line->fg >> 8);
    uint8_t lo = (uint8_t) line->fg;
    int x0 = (UIH_DISPLAY_WIDTH - (int) uih_display_text_width(line->text, line->scale)) / 2;
    int y0 = ((int) h - UIH_FONT_HEIGHT * scale) / 2;
    for (const char * c = line->text; *c; ++c, x0 += UIH_FONT_ADVANCE * scale) {
        const uint8_t * glyph = uih_font_glyph(*c);
        for (int col = 0; col < UIH_FONT_WIDTH; ++col) {
            for (int row = 0; row < UIH_FONT_HEIGHT; ++row) {
                if (!(glyph[col] & (1u << row))) {
                    continue;
                }
                for (int dy = 0; dy < scale; ++dy) {
                    int y = y0 + row * scale + dy;
                    if ((y < 0) || (y >= h)) {
                        continue;
                    }
                    for (int dx = 0; dx < scale; ++dx) {
                        int x = x0 + col * scale + dx;
                        if ((x >= 0) && (x < UIH_DISPLAY_WIDTH)) {
                            uint8_t * p = buf + 2 * (y * UIH_DISPLAY_WIDTH + x);
                            p[0] = hi;
                            p[1] = lo;
                        }
                    }
                }
            }
        }
    }
}
