/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/input.h"
#include <string.h>

void uih_buttons_init(uih_buttons_s * b) {
    memset(b, 0, sizeof(*b));
}

uint8_t uih_buttons_update(uih_buttons_s * b, uint8_t pressed) {
    uint8_t new_presses = 0;
    for (int i = 0; i < UIH_BUTTON_COUNT; ++i) {
        uint8_t bit = (uint8_t) (1u << i);
        if ((pressed & bit) == (b->stable & bit)) {
            b->count[i] = 0;  // agrees with the debounced state
            continue;
        }
        if (++b->count[i] >= UIH_BUTTON_STABLE_SAMPLES) {
            b->count[i] = 0;
            b->stable ^= bit;
            if (b->stable & bit) {
                new_presses |= bit;
            }
        }
    }
    return new_presses;
}

void uih_line_init(uih_line_s * l) {
    memset(l, 0, sizeof(*l));
}

void uih_line_feed(uih_line_s * l, const uint8_t * data, size_t n,
                   uih_line_fn on_line, uih_overflow_fn on_overflow, void * ctx) {
    for (size_t i = 0; i < n; ++i) {
        char ch = (char) data[i];
        if (ch == '\n') {
            size_t len = l->len;
            size_t content = (len && (l->buf[len - 1] == '\r')) ? (len - 1) : len;
            if (l->overflow || (content > UIH_PROTO_LINE_MAX)) {
                on_overflow(ctx);
            } else {
                l->buf[len] = 0;
                on_line(ctx, l->buf, len);
            }
            l->len = 0;
            l->overflow = false;
        } else if (l->len < (sizeof(l->buf) - 1)) {
            l->buf[l->len++] = ch;
        } else {
            l->overflow = true;
        }
    }
}
