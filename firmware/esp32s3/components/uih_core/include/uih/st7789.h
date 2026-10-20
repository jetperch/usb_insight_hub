/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief ST7789 setup for the hub's displays, as the stock firmware sends it.
 *
 * The stock firmware uses TFT_eSPI's ST7789 initialization (panel voltages,
 * porch, gamma) and rotation 2; doc/hardware.md has the details.
 */

#ifndef UIH_ST7789_H_
#define UIH_ST7789_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UIH_ST7789_SWRESET (0x01)
#define UIH_ST7789_SLPOUT (0x11)
#define UIH_ST7789_NORON (0x13)
#define UIH_ST7789_INVON (0x21)
#define UIH_ST7789_DISPON (0x29)
#define UIH_ST7789_CASET (0x2A)
#define UIH_ST7789_RASET (0x2B)
#define UIH_ST7789_RAMWR (0x2C)
#define UIH_ST7789_MADCTL (0x36)
#define UIH_ST7789_COLMOD (0x3A)
#define UIH_ST7789_RAMCTRL (0xB0)
#define UIH_ST7789_PORCTRL (0xB2)
#define UIH_ST7789_GCTRL (0xB7)
#define UIH_ST7789_VCOMS (0xBB)
#define UIH_ST7789_LCMCTRL (0xC0)
#define UIH_ST7789_VDVVRHEN (0xC2)
#define UIH_ST7789_VRHS (0xC3)
#define UIH_ST7789_VDVSET (0xC4)
#define UIH_ST7789_FRCTR2 (0xC6)
#define UIH_ST7789_PWCTRL1 (0xD0)
#define UIH_ST7789_PVGAMCTRL (0xE0)
#define UIH_ST7789_NVGAMCTRL (0xE1)

/// Rotation 2 shows rows 80..319 of the 240 x 320 RAM.
#define UIH_ST7789_ROW_OFFSET (80)

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[14];
    uint16_t delay_ms;   ///< wait after the command
} uih_st7789_cmd_s;

/// The setup commands, in order, after the hardware reset pulse.
extern const uih_st7789_cmd_s UIH_ST7789_INIT[];
extern const size_t UIH_ST7789_INIT_COUNT;

/**
 * @brief The CASET and RASET parameters for a display window.
 *
 * @param x, y The top left corner in display coordinates.
 * @param w, h The size in pixels.
 */
void uih_st7789_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint8_t caset[4], uint8_t raset[4]);

#ifdef __cplusplus
}
#endif

#endif  /* UIH_ST7789_H_ */
