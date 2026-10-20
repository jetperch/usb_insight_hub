/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Task wiring: one hub task owns all hardware state.
 *
 * The RX task assembles USB lines into a queue.  The hub task boots the
 * hub, then handles queued lines as they arrive and runs a 10 ms tick
 * for button debounce, hub polling and the status stream.  It feeds the
 * task watchdog; a hang resets the firmware while the STM8 holds the ports.
 *
 * The lower-priority display task never touches I2C: it draws from the
 * status the hub task copies under a mutex after every tick.
 */

#include "uih/app.h"
#include "uih/board.h"
#include "uih/display.h"
#include "uih/hub.h"
#include "uih/input.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define TICK_MS (10)
#define RX_QUEUE_DEPTH (4)
#define DISPLAY_PERIOD_MS (500)

typedef struct {
    bool overflow;
    uint16_t len;
    char line[UIH_PROTO_LINE_MAX + 2];
} rx_item_s;

static uih_hub_s hub_;
static uih_app_s app_;
static uih_buttons_s buttons_;
static QueueHandle_t rx_queue_;
static char serial_[13];
static SemaphoreHandle_t display_mutex_;
static uih_display_status_s display_status_[UIH_PORT_COUNT];

static void on_line(void * ctx, const char * line, size_t len) {
    (void) ctx;
    static rx_item_s item;  // RX task only
    item.overflow = false;
    item.len = (uint16_t) len;
    memcpy(item.line, line, len);
    xQueueSend(rx_queue_, &item, portMAX_DELAY);
}

static void on_overflow(void * ctx) {
    (void) ctx;
    static rx_item_s item;
    item.overflow = true;
    item.len = 0;
    xQueueSend(rx_queue_, &item, portMAX_DELAY);
}

static void rx_task(void * arg) {
    (void) arg;
    static uih_line_s line;
    uint8_t buf[64];
    uih_line_init(&line);
    while (1) {
        int n = uih_board_usb_read(buf, sizeof(buf), 100);
        if (n > 0) {
            uih_line_feed(&line, buf, (size_t) n, on_line, on_overflow, NULL);
        }
    }
}

static void publish_display_status(void) {
    uih_display_status_s status[UIH_PORT_COUNT];
    uih_display_status_from_hub(&hub_, status);
    xSemaphoreTake(display_mutex_, portMAX_DELAY);
    memcpy(display_status_, status, sizeof(status));
    xSemaphoreGive(display_mutex_);
}

static void display_task(void * arg) {
    (void) arg;
    static uih_display_page_s shown[UIH_PORT_COUNT];
    bool valid[UIH_PORT_COUNT] = {false};
    static char error[48];
    const char * step = "";
    esp_err_t rc = uih_board_display_init(&step);
    if (rc != ESP_OK) {
        snprintf(error, sizeof(error), "error: %s %s", step, esp_err_to_name(rc));
        uih_app_display_status(&app_, error);
        vTaskDelete(NULL);  // the hub works without its displays
    }
    uih_app_display_status(&app_, "ok");
    uint8_t * band = uih_board_display_buffer();
    while (1) {
        uih_display_status_s status[UIH_PORT_COUNT];
        xSemaphoreTake(display_mutex_, portMAX_DELAY);
        memcpy(status, display_status_, sizeof(status));
        xSemaphoreGive(display_mutex_);
        for (uint8_t d = 0; d < UIH_PORT_COUNT; ++d) {
            uih_display_page_s page;
            uih_display_layout(&status[d], &page);
            uint32_t changed = uih_display_changed(valid[d] ? &shown[d] : NULL, &page);
            valid[d] = true;
            for (int i = 0; i < UIH_DISPLAY_LINES; ++i) {
                if (!(changed & (1u << i))) {
                    continue;
                }
                uint16_t y, h;
                uih_display_line_geometry(i, &y, &h);
                uih_display_render(&page.line[i], band, h);
                if (uih_board_display_band(d, y, h, band) != ESP_OK) {
                    valid[d] = false;  // redraw everything next time
                }
            }
            shown[d] = page;
        }
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

static void hub_task(void * arg) {
    (void) arg;
    static rx_item_s item;
    bool poweron = false;
    const char * reason = uih_board_reset_reason(&poweron);
    esp_task_wdt_add(NULL);
    uih_app_boot(&app_, poweron, reason);
    TickType_t next_tick = xTaskGetTickCount() + pdMS_TO_TICKS(TICK_MS);
    while (1) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = ((int32_t) (next_tick - now) > 0) ? (next_tick - now) : 0;
        if (xQueueReceive(rx_queue_, &item, wait) == pdTRUE) {
            if (item.overflow) {
                uih_app_overflow(&app_);
            } else {
                uih_app_line(&app_, item.line, item.len);
            }
            continue;  // answer queued lines before the next tick
        }
        next_tick += pdMS_TO_TICKS(TICK_MS);
        uint8_t presses = uih_buttons_update(&buttons_, uih_board_buttons_read());
        for (uint8_t port = 1; port <= 3; ++port) {
            if (presses & (1u << (port - 1))) {
                uih_app_button(&app_, port);
            }
        }
        uih_app_tick(&app_);
        publish_display_status();
        esp_task_wdt_reset();
    }
}

void app_main(void) {
    uih_board_usb_init();
    uih_board_i2c_init();
    uih_board_buttons_init();
    uih_buttons_init(&buttons_);
    uih_board_serial(serial_);

    uih_hub_ops_s ops = {
        .i2c_write = uih_board_i2c_write,
        .i2c_write_read = uih_board_i2c_write_read,
        .time_ms = uih_board_time_ms,
        .delay_ms = uih_board_delay_ms,
    };
    uih_hub_init(&hub_, &ops);

    const esp_app_desc_t * desc = esp_app_get_description();
    uih_app_platform_s pf = {
        .tx = uih_board_usb_tx,
        .bootloader = uih_board_bootloader,
        .reset = uih_board_reset,
        .info = {.fw = desc->version, .git = UIH_GIT_HASH, .idf = desc->idf_ver, .serial = serial_},
    };
    uih_app_init(&app_, &hub_, &pf);

    display_mutex_ = xSemaphoreCreateMutex();
    for (uint8_t i = 0; i < UIH_PORT_COUNT; ++i) {
        display_status_[i].port.port = i + 1;  // until the hub task publishes
    }
    rx_queue_ = xQueueCreate(RX_QUEUE_DEPTH, sizeof(rx_item_s));
    xTaskCreate(rx_task, "uih_rx", 3072, NULL, 5, NULL);
    xTaskCreate(hub_task, "uih_hub", 6144, NULL, 6, NULL);
    xTaskCreate(display_task, "uih_display", 4096, NULL, 3, NULL);
}
