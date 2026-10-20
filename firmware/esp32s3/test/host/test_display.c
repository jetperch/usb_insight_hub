/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih_test.h"
#include "uih/display.h"
#include "uih/font.h"
#include "fake_i2c.h"

static uih_port_state_s port_on(void) {
    uih_port_state_s s = {.port = 2, .power = true, .data = true, .fault = false,
                          .mv = 5012, .ma_x10 = 1767};
    return s;
}

static void layout(const uih_port_state_s * s, bool hub_ok, bool meter_ok, uih_display_page_s * page) {
    uih_display_status_s st = {.port = *s, .hub_ok = hub_ok, .meter_ok = meter_ok};
    uih_display_layout(&st, page);
}

static void test_format_voltage(void) {
    char buf[UIH_DISPLAY_COLS + 1];
    uih_display_format_mv(5012, buf);
    ASSERT_STR_EQ("5.01 V", buf);
    uih_display_format_mv(4996, buf);
    ASSERT_STR_EQ("5.00 V", buf);  // rounds to 10 mV
    uih_display_format_mv(0, buf);
    ASSERT_STR_EQ("0.00 V", buf);
    uih_display_format_mv(-3, buf);
    ASSERT_STR_EQ("0.00 V", buf);  // noise around zero
    uih_display_format_mv(-20, buf);
    ASSERT_STR_EQ("-0.02 V", buf);
    uih_display_format_mv(12345, buf);
    ASSERT_STR_EQ("12.35 V", buf);
}

static void test_format_current(void) {
    char buf[UIH_DISPLAY_COLS + 1];
    uih_display_format_ma_x10(1767, buf);
    ASSERT_STR_EQ("176.7 mA", buf);
    uih_display_format_ma_x10(5, buf);
    ASSERT_STR_EQ("0.5 mA", buf);
    uih_display_format_ma_x10(0, buf);
    ASSERT_STR_EQ("0.0 mA", buf);
    uih_display_format_ma_x10(-123, buf);
    ASSERT_STR_EQ("-12.3 mA", buf);
    uih_display_format_ma_x10(9999, buf);
    ASSERT_STR_EQ("999.9 mA", buf);
    uih_display_format_ma_x10(10000, buf);
    ASSERT_STR_EQ("1.000 A", buf);
    uih_display_format_ma_x10(12346, buf);
    ASSERT_STR_EQ("1.235 A", buf);  // rounds to 1 mA
    uih_display_format_ma_x10(-25000, buf);
    ASSERT_STR_EQ("-2.500 A", buf);
}

static void test_layout_on(void) {
    uih_display_page_s page;
    uih_port_state_s s = port_on();
    layout(&s, true, true, &page);
    ASSERT_STR_EQ("CH2", page.line[UIH_DISPLAY_LINE_TITLE].text);
    ASSERT_STR_EQ("ON", page.line[UIH_DISPLAY_LINE_STATE].text);
    ASSERT_EQ(UIH_COLOR_GREEN, page.line[UIH_DISPLAY_LINE_STATE].fg);
    ASSERT_STR_EQ("5.01 V", page.line[UIH_DISPLAY_LINE_VOLTAGE].text);
    ASSERT_STR_EQ("176.7 mA", page.line[UIH_DISPLAY_LINE_CURRENT].text);
}

static void test_layout_off_data_off_fault(void) {
    uih_display_page_s page;
    uih_port_state_s s = port_on();
    s.power = false;
    layout(&s, true, true, &page);
    ASSERT_STR_EQ("OFF", page.line[UIH_DISPLAY_LINE_STATE].text);
    ASSERT_EQ(UIH_COLOR_GREY, page.line[UIH_DISPLAY_LINE_STATE].fg);

    s = port_on();
    s.data = false;
    layout(&s, true, true, &page);
    ASSERT_STR_EQ("CH2 DATA OFF", page.line[UIH_DISPLAY_LINE_TITLE].text);
    ASSERT_STR_EQ("ON", page.line[UIH_DISPLAY_LINE_STATE].text);

    s = port_on();
    s.fault = true;
    layout(&s, true, true, &page);
    ASSERT_STR_EQ("FAULT", page.line[UIH_DISPLAY_LINE_STATE].text);
    ASSERT_EQ(UIH_COLOR_RED, page.line[UIH_DISPLAY_LINE_STATE].fg);
}

static void test_layout_missing_devices(void) {
    uih_display_page_s page;
    uih_port_state_s s = port_on();
    layout(&s, true, false, &page);
    ASSERT_STR_EQ("-.-- V", page.line[UIH_DISPLAY_LINE_VOLTAGE].text);
    ASSERT_STR_EQ("-.- mA", page.line[UIH_DISPLAY_LINE_CURRENT].text);
    layout(&s, false, true, &page);
    ASSERT_STR_EQ("NO HUB", page.line[UIH_DISPLAY_LINE_STATE].text);
    ASSERT_EQ(UIH_COLOR_RED, page.line[UIH_DISPLAY_LINE_STATE].fg);
}

static void test_layout_fits(void) {
    // Every line fits the display width at its scale.
    uih_display_page_s page;
    uih_port_state_s s = port_on();
    s.data = false;
    s.mv = -12345;
    s.ma_x10 = -25000;  // the PAC1943 full scale
    layout(&s, true, true, &page);
    for (int i = 0; i < UIH_DISPLAY_LINES; ++i) {
        const uih_display_line_s * line = &page.line[i];
        ASSERT_TRUE(uih_display_text_width(line->text, line->scale) <= UIH_DISPLAY_WIDTH);
        uint16_t y, h;
        uih_display_line_geometry(i, &y, &h);
        ASSERT_TRUE(UIH_FONT_HEIGHT * line->scale <= h);
        ASSERT_TRUE(h <= UIH_DISPLAY_BAND_HEIGHT_MAX);
        ASSERT_TRUE(y + h <= UIH_DISPLAY_HEIGHT);
        if (i) {
            uint16_t py, ph;
            uih_display_line_geometry(i - 1, &py, &ph);
            ASSERT_TRUE(py + ph <= y);  // bands do not overlap
        }
    }
}

static void test_changed(void) {
    uih_display_page_s a, b;
    uih_port_state_s s = port_on();
    layout(&s, true, true, &a);
    layout(&s, true, true, &b);
    ASSERT_EQ(0, uih_display_changed(&a, &b));
    ASSERT_EQ((1u << UIH_DISPLAY_LINES) - 1, uih_display_changed(NULL, &b));  // first draw
    s.ma_x10 = 1768;
    layout(&s, true, true, &b);
    ASSERT_EQ(1u << UIH_DISPLAY_LINE_CURRENT, uih_display_changed(&a, &b));
    s.power = false;
    layout(&s, true, true, &b);
    ASSERT_EQ((1u << UIH_DISPLAY_LINE_CURRENT) | (1u << UIH_DISPLAY_LINE_STATE),
              uih_display_changed(&a, &b));
}

static void test_font_glyphs(void) {
    // '1': a vertical stroke in column 2.
    const uint8_t * g = uih_font_glyph('1');
    ASSERT_EQ(0x7F, g[2] & 0x7F);
    ASSERT_TRUE(uih_font_glyph(' ')[0] == 0);
    // Unknown characters render as '?'.
    ASSERT_TRUE(uih_font_glyph('~') == uih_font_glyph('?'));
    ASSERT_TRUE(uih_font_glyph((char) 0x80) == uih_font_glyph('?'));
    // Every character the layout can produce has its own glyph.
    const char * used = "0123456789.- CHONFVmADTULS";
    for (const char * c = used; *c; ++c) {
        if (*c != '?') {
            ASSERT_TRUE(uih_font_glyph(*c) != uih_font_glyph('?'));
        }
    }
}

static uint16_t pixel(const uint8_t * buf, int x, int y) {
    const uint8_t * p = buf + 2 * (y * UIH_DISPLAY_WIDTH + x);
    return (uint16_t) ((p[0] << 8) | p[1]);  // big endian on the wire
}

static void test_render(void) {
    static uint8_t buf[UIH_DISPLAY_BAND_BYTES];
    memset(buf, 0xAA, sizeof(buf));
    uih_display_line_s line = {.text = "1", .fg = 0xF81F, .scale = 2};
    uint16_t h = 20;
    uih_display_render(&line, buf, h);
    // "1" is 5 columns wide minus nothing trailing: width 10 at scale 2,
    // centered: x0 = (240 - 10) / 2 = 115.  Height 14, y0 = (20 - 14) / 2 = 3.
    ASSERT_EQ(10, uih_display_text_width("1", 2));
    ASSERT_EQ(UIH_COLOR_BLACK, pixel(buf, 0, 0));
    ASSERT_EQ(UIH_COLOR_BLACK, pixel(buf, 239, 19));
    // column 2 of '1' is fully lit: x = 115 + 4..5, y = 3..16
    for (int y = 3; y < 17; ++y) {
        ASSERT_EQ(0xF81F, pixel(buf, 119, y));
        ASSERT_EQ(0xF81F, pixel(buf, 120, y));
    }
    ASSERT_EQ(UIH_COLOR_BLACK, pixel(buf, 119, 2));
    ASSERT_EQ(UIH_COLOR_BLACK, pixel(buf, 119, 17));
    // Bytes past the band are untouched.
    ASSERT_EQ(0xAA, buf[2 * UIH_DISPLAY_WIDTH * h]);
}

static void test_render_clips(void) {
    static uint8_t buf[UIH_DISPLAY_BAND_BYTES];
    memset(buf, 0xAA, sizeof(buf));
    // Too wide and too tall for a 10-row band: clipped, no overrun.
    uih_display_line_s line = {.text = "888888888888", .fg = UIH_COLOR_WHITE, .scale = 6};
    uih_display_render(&line, buf, 10);
    ASSERT_EQ(0xAA, buf[2 * UIH_DISPLAY_WIDTH * 10]);
    int lit = 0;
    for (int x = 0; x < UIH_DISPLAY_WIDTH; ++x) {
        lit += pixel(buf, x, 5) == UIH_COLOR_WHITE;
    }
    ASSERT_TRUE(lit > 0);
    // A band taller than the buffer is limited to the buffer.
    line.scale = 1;
    uih_display_render(&line, buf, UIH_DISPLAY_BAND_HEIGHT_MAX + 10);
    line.scale = 0;   // invalid scale draws nothing
    uih_display_render(&line, buf, 10);
    ASSERT_EQ(UIH_COLOR_BLACK, pixel(buf, 120, 5));
}

static void test_status_from_hub(void) {
    static fake_bus_s bus;
    static uih_hub_s hub;
    fake_bus_init(&bus);
    uih_hub_ops_s ops = fake_ops(&bus);
    uih_hub_init(&hub, &ops);
    uih_display_status_s st[UIH_PORT_COUNT];
    uih_display_status_from_hub(&hub, st);  // before boot: nothing answered yet
    ASSERT_FALSE(st[0].hub_ok);
    ASSERT_FALSE(st[0].meter_ok);
    uih_hub_boot(&hub, true);
    bus.vbus[1] = 34939;   // meter channel 2 is port 3
    uih_hub_poll(&hub);
    uih_hub_set(&hub, 0x02, 0, -1);
    uih_display_status_from_hub(&hub, st);
    for (int i = 0; i < UIH_PORT_COUNT; ++i) {
        ASSERT_EQ(i + 1, st[i].port.port);
        ASSERT_TRUE(st[i].hub_ok);
        ASSERT_TRUE(st[i].meter_ok);
    }
    ASSERT_FALSE(st[1].port.power);
    ASSERT_EQ(4798, st[2].port.mv);
}

int main(void) {
    RUN(test_format_voltage);
    RUN(test_format_current);
    RUN(test_layout_on);
    RUN(test_layout_off_data_off_fault);
    RUN(test_layout_missing_devices);
    RUN(test_layout_fits);
    RUN(test_changed);
    RUN(test_font_glyphs);
    RUN(test_render);
    RUN(test_render_clips);
    RUN(test_status_from_hub);
    return test_main_result("display");
}
