/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief The three ST7789 displays on one SPI bus (doc/hardware.md).
 *
 * The displays share MOSI, SCLK, DC and reset, so one esp_lcd SPI panel IO
 * drives them all and this file selects a display with its CS line.  The
 * setup matches the stock firmware (TFT_eSPI): SPI mode 3 at 40 MHz, an
 * active-high reset pulse, the uih/st7789.h command table sent with all CS
 * lines low, and the backlight PWM at 800 / 1023, 193 Hz.  esp_lcd's own
 * ST7789 driver sends only a minimal setup, without the panel voltages.
 */

#include "uih/board.h"
#include "uih/display.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "uih/st7789.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define LCD_HOST (SPI2_HOST)
#define LCD_MOSI (GPIO_NUM_11)
#define LCD_SCLK (GPIO_NUM_9)
#define LCD_DC (GPIO_NUM_10)
#define LCD_RESET (GPIO_NUM_14)
#define LCD_HZ (40000000)
#define LCD_SPI_MODE (3)          ///< TFT_eSPI's mode for every ST7789
#define RESET_PULSE_MS (10)
#define BACKLIGHT_HZ (193)        ///< stock, at a 160 MHz CPU
#define BACKLIGHT_DUTY (800)      ///< of 1023: the stock default brightness
#define DRAW_TIMEOUT_MS (100)

/// Return on error, naming the failed step for uih_board_display_init().
#define STEP_(name, x) do { \
    esp_err_t rc_ = (x); \
    if (rc_ != ESP_OK) { *step = (name); return rc_; } \
} while (0)
#define ALL_DISPLAYS (-1)
#define NO_DISPLAY (-2)

static const gpio_num_t CS[3] = {GPIO_NUM_6, GPIO_NUM_16, GPIO_NUM_12};
static const gpio_num_t BACKLIGHT[3] = {GPIO_NUM_13, GPIO_NUM_15, GPIO_NUM_17};

static esp_lcd_panel_io_handle_t io_;
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
    uint8_t caset[4], raset[4];
    uih_st7789_window(0, y, UIH_DISPLAY_WIDTH, h, caset, raset);
    cs_select(display);
    xSemaphoreTake(done_, 0);  // clear a stale completion
    esp_err_t rc = esp_lcd_panel_io_tx_param(io_, UIH_ST7789_CASET, caset, 4);
    if (rc == ESP_OK) {
        rc = esp_lcd_panel_io_tx_param(io_, UIH_ST7789_RASET, raset, 4);
    }
    if (rc == ESP_OK) {
        rc = esp_lcd_panel_io_tx_color(io_, UIH_ST7789_RAMWR, pixels,
                                       (size_t) UIH_DISPLAY_WIDTH * h * 2);
    }
    if ((rc == ESP_OK) && (xSemaphoreTake(done_, pdMS_TO_TICKS(DRAW_TIMEOUT_MS)) != pdTRUE)) {
        rc = ESP_ERR_TIMEOUT;
    }
    cs_select(NO_DISPLAY);
    return rc;
}

esp_err_t uih_board_display_init(const char ** step) {
    done_ = xSemaphoreCreateBinary();
    band_ = heap_caps_malloc(UIH_DISPLAY_BAND_BYTES, MALLOC_CAP_DMA);
    if (!done_ || !band_) {
        *step = "alloc";
        return ESP_ERR_NO_MEM;
    }
    gpio_config_t out_cfg = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << LCD_RESET};
    for (int i = 0; i < 3; ++i) {
        out_cfg.pin_bit_mask |= 1ULL << CS[i];
    }
    gpio_set_level(LCD_RESET, 0);   // released (active high)
    cs_select(NO_DISPLAY);
    STEP_("gpio", gpio_config(&out_cfg));

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = UIH_DISPLAY_BAND_BYTES,
    };
    STEP_("spi", spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = -1,  // CS is selected per display, above
        .dc_gpio_num = LCD_DC,
        .spi_mode = LCD_SPI_MODE,
        .pclk_hz = LCD_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .user_ctx = done_,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    STEP_("io", esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io_));
    // A NOP with no display selected leaves SCLK at its mode 3 idle level
    // before the first CS falling edge.
    STEP_("nop", esp_lcd_panel_io_tx_param(io_, 0x00, NULL, 0));

    // As the stock firmware: reset pulse, then the setup with all CS low.
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(RESET_PULSE_MS));
    gpio_set_level(LCD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(RESET_PULSE_MS));
    cs_select(ALL_DISPLAYS);
    for (size_t i = 0; i < UIH_ST7789_INIT_COUNT; ++i) {
        const uih_st7789_cmd_s * c = &UIH_ST7789_INIT[i];
        esp_err_t rc = esp_lcd_panel_io_tx_param(io_, c->cmd, c->len ? c->data : NULL, c->len);
        if (rc != ESP_OK) {
            cs_select(NO_DISPLAY);
            STEP_("init", rc);
        }
        if (c->delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
        }
    }

    // Clear the whole screen on all three displays at once.
    memset(band_, 0, UIH_DISPLAY_BAND_BYTES);
    for (uint16_t y = 0; y < UIH_DISPLAY_HEIGHT; y += UIH_DISPLAY_BAND_HEIGHT_MAX) {
        STEP_("clear", draw(ALL_DISPLAYS, y, UIH_DISPLAY_BAND_HEIGHT_MAX, band_));
    }
    STEP_("backlight", backlight_init());
    return ESP_OK;
}

uint8_t * uih_board_display_buffer(void) {
    return band_;
}

esp_err_t uih_board_display_band(uint8_t display, uint16_t y, uint16_t h, const uint8_t * pixels) {
    return draw(display, y, h, pixels);
}
