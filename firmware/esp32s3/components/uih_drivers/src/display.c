/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief The three ST7789 displays on one SPI bus (doc/hardware.md).
 *
 * The displays share MOSI, SCLK, DC and reset, so one esp_lcd panel drives
 * them all and this file selects a display with its CS line.  As in the
 * stock firmware, all CS lines are low during initialization so the
 * displays receive the same setup.
 */

#include "uih/board.h"
#include "uih/display.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define LCD_HOST (SPI2_HOST)
#define LCD_MOSI (GPIO_NUM_11)
#define LCD_SCLK (GPIO_NUM_9)
#define LCD_DC (GPIO_NUM_10)
#define LCD_RESET (GPIO_NUM_14)
#define LCD_HZ (40000000)
#define LCD_Y_GAP (80)            ///< 240 x 240 window of the 240 x 320 RAM, rotated 180
#define BACKLIGHT_HZ (5000)
#define BACKLIGHT_DUTY (600)      ///< of 1023: fixed, no menu
#define DRAW_TIMEOUT_MS (100)

#define ESP_RETURN_ON_ERROR_(x) do { esp_err_t rc_ = (x); if (rc_ != ESP_OK) { return rc_; } } while (0)
#define ALL_DISPLAYS (-1)
#define NO_DISPLAY (-2)

static const gpio_num_t CS[3] = {GPIO_NUM_6, GPIO_NUM_16, GPIO_NUM_12};
static const gpio_num_t BACKLIGHT[3] = {GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_17};

static esp_lcd_panel_handle_t panel_;
static SemaphoreHandle_t done_;
static uint8_t * band_;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t * edata, void * ctx) {
    (void) io;
    (void) edata;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t) ctx, &woken);
    return woken == pdTRUE;
}

/// Select one display 0..2, ALL_DISPLAYS or NO_DISPLAY.
static void cs_select(int display) {
    for (int i = 0; i < 3; ++i) {
        gpio_set_level(CS[i], ((display == ALL_DISPLAYS) || (display == i)) ? 0 : 1);
    }
}

static esp_err_t backlight_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BACKLIGHT_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t rc = ledc_timer_config(&timer);
    for (int i = 0; (rc == ESP_OK) && (i < 3); ++i) {
        ledc_channel_config_t ch = {
            .gpio_num = BACKLIGHT[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t) i,
            .timer_sel = LEDC_TIMER_0,
            .duty = BACKLIGHT_DUTY,
        };
        rc = ledc_channel_config(&ch);
    }
    return rc;
}

static esp_err_t draw(int display, uint16_t y, uint16_t h, const uint8_t * pixels) {
    cs_select(display);
    xSemaphoreTake(done_, 0);  // clear a stale completion
    esp_err_t rc = esp_lcd_panel_draw_bitmap(panel_, 0, y, UIH_DISPLAY_WIDTH, y + h, pixels);
    if ((rc == ESP_OK) && (xSemaphoreTake(done_, pdMS_TO_TICKS(DRAW_TIMEOUT_MS)) != pdTRUE)) {
        rc = ESP_ERR_TIMEOUT;
    }
    cs_select(NO_DISPLAY);
    return rc;
}

esp_err_t uih_board_display_init(void) {
    done_ = xSemaphoreCreateBinary();
    band_ = heap_caps_malloc(UIH_DISPLAY_BAND_BYTES, MALLOC_CAP_DMA);
    if (!done_ || !band_) {
        return ESP_ERR_NO_MEM;
    }
    gpio_config_t cs_cfg = {.mode = GPIO_MODE_OUTPUT};
    for (int i = 0; i < 3; ++i) {
        cs_cfg.pin_bit_mask |= 1ULL << CS[i];
    }
    ESP_RETURN_ON_ERROR_(gpio_config(&cs_cfg));

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = UIH_DISPLAY_BAND_BYTES,
    };
    ESP_RETURN_ON_ERROR_(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = -1,  // CS is selected per display, above
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .user_ctx = done_,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR_(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io));
    esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR_(esp_lcd_new_panel_st7789(io, &dev_cfg, &panel_));
    cs_select(ALL_DISPLAYS);  // the common setup
    ESP_RETURN_ON_ERROR_(esp_lcd_panel_reset(panel_));
    ESP_RETURN_ON_ERROR_(esp_lcd_panel_init(panel_));
    ESP_RETURN_ON_ERROR_(esp_lcd_panel_invert_color(panel_, true));
    ESP_RETURN_ON_ERROR_(esp_lcd_panel_mirror(panel_, true, true));
    ESP_RETURN_ON_ERROR_(esp_lcd_panel_set_gap(panel_, 0, LCD_Y_GAP));

    // Clear the whole screen on all three displays at once.
    memset(band_, 0, UIH_DISPLAY_BAND_BYTES);
    for (uint16_t y = 0; y < UIH_DISPLAY_HEIGHT; y += UIH_DISPLAY_BAND_HEIGHT_MAX) {
        ESP_RETURN_ON_ERROR_(draw(ALL_DISPLAYS, y, UIH_DISPLAY_BAND_HEIGHT_MAX, band_));
    }
    cs_select(ALL_DISPLAYS);
    esp_err_t rc = esp_lcd_panel_disp_on_off(panel_, true);
    cs_select(NO_DISPLAY);
    return (rc == ESP_OK) ? backlight_init() : rc;
}

uint8_t * uih_board_display_buffer(void) {
    return band_;
}

esp_err_t uih_board_display_band(uint8_t display, uint16_t y, uint16_t h, const uint8_t * pixels) {
    return draw(display, y, h, pixels);
}
