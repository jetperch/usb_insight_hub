# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

file(GLOB srcs ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include
                       REQUIRES uih_core
                       PRIV_REQUIRES esp_driver_i2c esp_driver_usb_serial_jtag
                                     esp_driver_gpio esp_driver_spi esp_driver_ledc esp_lcd esp_timer esp_system efuse esp_hw_support)
