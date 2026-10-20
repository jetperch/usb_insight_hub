/*
 * SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uih/board.h"
#include "uih/regs.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include <stdio.h>

#define I2C_SDA (GPIO_NUM_34)
#define I2C_SCL (GPIO_NUM_33)
#define I2C_HZ (400000)
#define I2C_TIMEOUT_MS (20)
#define USB_TX_BUFFER (4096)
#define USB_RX_BUFFER (1024)
#define RESPONSE_WAIT_MS (50)   ///< give up on a response if no host drains it

static const gpio_num_t BUTTONS[3] = {GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_37};

static i2c_master_bus_handle_t bus_;
static i2c_master_dev_handle_t stm8_;
static i2c_master_dev_handle_t pac_;

esp_err_t uih_board_i2c_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t rc = i2c_new_master_bus(&bus_cfg, &bus_);
    if (rc != ESP_OK) {
        return rc;
    }
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = UIH_STM8_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    rc = i2c_master_bus_add_device(bus_, &dev, &stm8_);
    if (rc != ESP_OK) {
        return rc;
    }
    dev.device_address = UIH_PAC_ADDR;
    return i2c_master_bus_add_device(bus_, &dev, &pac_);
}

static i2c_master_dev_handle_t device(uint8_t addr) {
    return (addr == UIH_STM8_ADDR) ? stm8_ : ((addr == UIH_PAC_ADDR) ? pac_ : NULL);
}

int uih_board_i2c_write(void * ctx, uint8_t addr, const uint8_t * data, size_t len) {
    (void) ctx;
    i2c_master_dev_handle_t dev = device(addr);
    return (dev && (ESP_OK == i2c_master_transmit(dev, data, len, I2C_TIMEOUT_MS))) ? 0 : 1;
}

int uih_board_i2c_write_read(void * ctx, uint8_t addr, const uint8_t * w, size_t wlen,
                             uint8_t * r, size_t rlen) {
    (void) ctx;
    i2c_master_dev_handle_t dev = device(addr);
    return (dev && (ESP_OK == i2c_master_transmit_receive(dev, w, wlen, r, rlen, I2C_TIMEOUT_MS))) ? 0 : 1;
}

uint32_t uih_board_time_ms(void * ctx) {
    (void) ctx;
    return (uint32_t) (esp_timer_get_time() / 1000);
}

void uih_board_delay_ms(void * ctx, uint32_t ms) {
    (void) ctx;
    vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t uih_board_usb_init(void) {
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = USB_TX_BUFFER,
        .rx_buffer_size = USB_RX_BUFFER,
    };
    return usb_serial_jtag_driver_install(&cfg);
}

int uih_board_usb_read(uint8_t * buf, size_t size, uint32_t timeout_ms) {
    return usb_serial_jtag_read_bytes(buf, size, pdMS_TO_TICKS(timeout_ms));
}

bool uih_board_usb_tx(void * ctx, const char * line, size_t len, bool droppable) {
    (void) ctx;
    // The ring buffer accepts a whole line or nothing, so lines never tear.
    TickType_t wait = droppable ? 0 : pdMS_TO_TICKS(RESPONSE_WAIT_MS);
    return usb_serial_jtag_write_bytes(line, len, wait) == (int) len;
}

void uih_board_buttons_init(void) {
    for (int i = 0; i < 3; ++i) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << BUTTONS[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }
}

uint8_t uih_board_buttons_read(void) {
    uint8_t mask = 0;
    for (int i = 0; i < 3; ++i) {
        if (!gpio_get_level(BUTTONS[i])) {  // active low
            mask |= (uint8_t) (1u << i);
        }
    }
    return mask;
}

void uih_board_bootloader(void * ctx) {
    (void) ctx;
    // The ROM enters download mode on the next reset (USB-Serial-JTAG).
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

void uih_board_reset(void * ctx) {
    (void) ctx;
    esp_restart();
}

const char * uih_board_reset_reason(bool * poweron) {
    esp_reset_reason_t r = esp_reset_reason();
    *poweron = (r == ESP_RST_POWERON);
    switch (r) {
        case ESP_RST_POWERON: return "poweron";
        case ESP_RST_SW: return "sw";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "wdt";
        case ESP_RST_USB: return "usb";
        case ESP_RST_BROWNOUT: return "brownout";
        default: return "other";
    }
}

void uih_board_serial(char out[13]) {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
