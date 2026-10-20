/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/st7789.h"

// From the stock firmware's TFT_eSPI (TFT_RST -1): software reset, then
// TFT_Drivers/ST7789_Init.h (default sequence), then setRotation(2).
const uih_st7789_cmd_s UIH_ST7789_INIT[] = {
    {UIH_ST7789_SWRESET, 0, {0}, 150},
    {UIH_ST7789_SLPOUT, 0, {0}, 120},
    {UIH_ST7789_NORON, 0, {0}, 0},
    {UIH_ST7789_MADCTL, 1, {0x08}, 0},                  // BGR
    {0xB6, 2, {0x0A, 0x82}, 0},                         // display function control
    {UIH_ST7789_RAMCTRL, 2, {0x00, 0xE0}, 0},           // big endian, 5 to 6 bit
    {UIH_ST7789_COLMOD, 1, {0x55}, 10},                 // RGB565
    {UIH_ST7789_PORCTRL, 5, {0x1F, 0x1F, 0x00, 0x33, 0x33}, 0},
    {UIH_ST7789_GCTRL, 1, {0x00}, 0},
    {UIH_ST7789_VCOMS, 1, {0x3F}, 0},
    {UIH_ST7789_LCMCTRL, 1, {0x0C}, 0},
    {UIH_ST7789_VDVVRHEN, 2, {0x01, 0xFF}, 0},
    {UIH_ST7789_VRHS, 1, {0x0F}, 0},
    {UIH_ST7789_VDVSET, 1, {0x20}, 0},
    {UIH_ST7789_FRCTR2, 1, {0x13}, 0},
    {UIH_ST7789_PWCTRL1, 2, {0xA4, 0xA1}, 0},
    {UIH_ST7789_PVGAMCTRL, 14, {0xF0, 0x06, 0x0D, 0x0B, 0x0A, 0x07, 0x2E,
                                0x43, 0x45, 0x38, 0x14, 0x13, 0x25, 0x29}, 0},
    {UIH_ST7789_NVGAMCTRL, 14, {0xF0, 0x07, 0x0A, 0x08, 0x07, 0x23, 0x2E,
                                0x33, 0x44, 0x3A, 0x16, 0x17, 0x26, 0x2C}, 0},
    {UIH_ST7789_INVON, 0, {0}, 0},
    {UIH_ST7789_CASET, 4, {0x00, 0x00, 0x00, 0xEF}, 0},
    {UIH_ST7789_RASET, 4, {0x00, 0x00, 0x01, 0x3F}, 120},
    {UIH_ST7789_DISPON, 0, {0}, 120},
    {UIH_ST7789_MADCTL, 1, {0xC8}, 0},                  // rotation 2: MY | MX | BGR
};

const size_t UIH_ST7789_INIT_COUNT = sizeof(UIH_ST7789_INIT) / sizeof(UIH_ST7789_INIT[0]);

static void range(uint16_t start, uint16_t size, uint8_t out[4]) {
    uint16_t end = (uint16_t) (start + size - 1);
    out[0] = (uint8_t) (start >> 8);
    out[1] = (uint8_t) start;
    out[2] = (uint8_t) (end >> 8);
    out[3] = (uint8_t) end;
}

void uih_st7789_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint8_t caset[4], uint8_t raset[4]) {
    range(x, w, caset);
    range((uint16_t) (y + UIH_ST7789_ROW_OFFSET), h, raset);
}
