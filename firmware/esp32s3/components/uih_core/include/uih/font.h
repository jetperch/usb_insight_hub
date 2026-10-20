/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief A 5 x 7 bitmap font with the characters the display uses.
 */

#ifndef UIH_FONT_H_
#define UIH_FONT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_FONT_WIDTH (5)
#define UIH_FONT_HEIGHT (7)
#define UIH_FONT_ADVANCE (UIH_FONT_WIDTH + 1)   ///< one blank column between characters

/**
 * @brief The glyph for a character.
 *
 * @return UIH_FONT_WIDTH column bytes, left to right; bit 0 is the top row.
 *      Characters without a glyph return the '?' glyph.
 */
const uint8_t * uih_font_glyph(char c);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_FONT_H_ */
