/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/st7789.h"

static int find(uint8_t cmd) {
    for (size_t i = 0; i < UIH_ST7789_INIT_COUNT; ++i) {
        if (UIH_ST7789_INIT[i].cmd == cmd) {
            return (int) i;
        }
    }
    return -1;
}

static void test_init_order(void) {
    // The stock TFT_eSPI sequence: software reset, sleep out, settings,
    // inversion, display on, then the rotation 2 MADCTL.
    ASSERT_EQ(UIH_ST7789_SWRESET, UIH_ST7789_INIT[0].cmd);
    ASSERT_TRUE(UIH_ST7789_INIT[0].delay_ms >= 150);
    ASSERT_EQ(UIH_ST7789_SLPOUT, UIH_ST7789_INIT[1].cmd);
    ASSERT_TRUE(UIH_ST7789_INIT[1].delay_ms >= 120);
    int invon = find(UIH_ST7789_INVON);
    int dispon = find(UIH_ST7789_DISPON);
    ASSERT_TRUE(invon > 1);
    ASSERT_TRUE(dispon > invon);
    ASSERT_TRUE(UIH_ST7789_INIT[dispon].delay_ms >= 120);
    const uih_st7789_cmd_s * last = &UIH_ST7789_INIT[UIH_ST7789_INIT_COUNT - 1];
    ASSERT_EQ(UIH_ST7789_MADCTL, last->cmd);
    ASSERT_EQ(1, last->len);
    ASSERT_EQ(0xC8, last->data[0]);   // MY | MX | BGR
}

static void test_init_values(void) {
    int i = find(UIH_ST7789_COLMOD);
    ASSERT_EQ(0x55, UIH_ST7789_INIT[i].data[0]);   // 16-bit RGB565
    i = find(UIH_ST7789_RAMCTRL);
    ASSERT_EQ(2, UIH_ST7789_INIT[i].len);
    ASSERT_EQ(0, UIH_ST7789_INIT[i].data[1] & 0x08);   // big endian pixels
    i = find(UIH_ST7789_VCOMS);
    ASSERT_EQ(0x3F, UIH_ST7789_INIT[i].data[0]);
    i = find(UIH_ST7789_PVGAMCTRL);
    ASSERT_EQ(14, UIH_ST7789_INIT[i].len);
    i = find(UIH_ST7789_NVGAMCTRL);
    ASSERT_EQ(14, UIH_ST7789_INIT[i].len);
    for (size_t k = 0; k < UIH_ST7789_INIT_COUNT; ++k) {
        ASSERT_TRUE(UIH_ST7789_INIT[k].len <= sizeof(UIH_ST7789_INIT[k].data));
    }
}

static void test_window(void) {
    uint8_t caset[4], raset[4];
    // Rotation 2 of a 240 x 240 window in the 240 x 320 RAM: rows start at 80.
    uih_st7789_window(0, 16, 240, 24, caset, raset);
    ASSERT_EQ(0, caset[0]);
    ASSERT_EQ(0, caset[1]);
    ASSERT_EQ(0, caset[2]);
    ASSERT_EQ(239, caset[3]);    // inclusive end
    ASSERT_EQ(0, raset[0]);
    ASSERT_EQ(96, raset[1]);     // 16 + 80
    ASSERT_EQ(0, raset[2]);
    ASSERT_EQ(119, raset[3]);    // 16 + 24 - 1 + 80
    uih_st7789_window(10, 200, 20, 40, caset, raset);
    ASSERT_EQ(10, caset[1]);
    ASSERT_EQ(29, caset[3]);
    ASSERT_EQ(1, raset[0]);      // 280 = 0x118
    ASSERT_EQ(0x18, raset[1]);
    ASSERT_EQ(1, raset[2]);      // 319 = 0x13F
    ASSERT_EQ(0x3F, raset[3]);
}

int main(void) {
    RUN(test_init_order);
    RUN(test_init_values);
    RUN(test_window);
    return test_main_result("st7789");
}
