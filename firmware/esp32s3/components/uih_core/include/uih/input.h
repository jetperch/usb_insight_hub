/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Input conditioning: button debounce and line assembly.
 */

#ifndef UIH_INPUT_H_
#define UIH_INPUT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "uih/proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_BUTTON_COUNT (3)
#define UIH_BUTTON_STABLE_SAMPLES (3)   ///< at 10 ms per sample: 30 ms

typedef struct {
    uint8_t stable;     ///< debounced pressed mask
    uint8_t count[UIH_BUTTON_COUNT];
} uih_buttons_s;

void uih_buttons_init(uih_buttons_s * b);

/**
 * @brief Add one sample of the raw pressed mask (bit n-1 = button n).
 *
 * @return the mask of buttons that just became pressed.
 */
uint8_t uih_buttons_update(uih_buttons_s * b, uint8_t pressed);

typedef void (*uih_line_fn)(void * ctx, const char * line, size_t len);
typedef void (*uih_overflow_fn)(void * ctx);

typedef struct {
    char buf[UIH_PROTO_LINE_MAX + 2];   ///< the limit plus an optional CR and a NUL
    size_t len;
    bool overflow;
} uih_line_s;

void uih_line_init(uih_line_s * l);

/**
 * @brief Feed received bytes; calls on_line once per complete line.
 *
 * Lines exclude the LF.  A line longer than UIH_PROTO_LINE_MAX (not
 * counting a trailing CR) is discarded and reported once through
 * on_overflow when its LF arrives.
 */
void uih_line_feed(uih_line_s * l, const uint8_t * data, size_t n,
                   uih_line_fn on_line, uih_overflow_fn on_overflow, void * ctx);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_INPUT_H_ */
