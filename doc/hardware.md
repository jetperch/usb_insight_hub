<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# USB Insight Hub hardware reference

What the firmware relies on.  Sources: the stock firmware and STM8 source
(Aeriosolutions/USB-Insight-HUB-Software, `USBInsightHub-A1/`, MIT) and
measurements on hub `B43A45B7BFBC` (2026-10-20).


## Overview

- A Renesas UPD720210 USB hub with three downstream ports for devices
  (CH1..CH3).  Its fourth port connects to the ESP32-S3.
- The ESP32-S3 drives three displays and talks over I2C to an STM8S003
  (port power, data and fault) and a PAC1943 power monitor.
- The STM8 keeps driving the port outputs while the ESP32 resets or sits
  in its ROM bootloader.


## ESP32-S3

- ESP32-S3 QFN56, revision v0.2, 8 MB embedded GigaDevice flash, no PSRAM,
  40 MHz crystal.
- eFuses (all open): no secure boot, no flash encryption, download mode
  enabled, USB-Serial-JTAG enabled, `USB_PHY_SEL` = 0 (the internal PHY is
  connected to USB-Serial-JTAG at reset), ROM prints on USB-Serial-JTAG.
- The stock firmware switches the PHY to USB-OTG (TinyUSB, descriptor
  "Aerio InsightHUB Controller").  Our firmware never touches USB-OTG, so
  the port stays "Espressif USB JTAG/serial debug unit".  Both use
  303a:1001; the USB serial number is the MAC (for example
  `B4:3A:45:B7:BF:BC`).


## Pins

| Function | GPIO | Notes |
| --- | --- | --- |
| I2C SDA (board to board) | 34 | 400 kHz; STM8 and PAC1943 |
| I2C SCL (board to board) | 33 | |
| STM8 interrupt (`MCU_INT`) | 18 | unused |
| PAC1943 alert | 35 | unused (alerts not configured) |
| Display SPI MOSI | 11 | shared by the three displays |
| Display SPI SCLK | 9 | 40 MHz |
| Display DC | 10 | shared |
| Display reset | 14 | shared, **active high** (high holds the displays in reset) |
| Display CS for CH1, CH2, CH3 | 6, 16, 12 | |
| Display backlight for CH1, CH2, CH3 | 13, 15, 17 | PWM |
| Buttons for CH1, CH2, CH3 | 39, 38, 37 | active low, internal pull-up |
| SETUP button | 36 | unused |
| `VBUS_MONITOR` | 1 | ADC, unused |
| `AUX_LED` | 45 | unused |
| `HUB_RESET` | 21 | never driven (as in the stock firmware) |

Displays: ST7789, 240 x 240, BGR color order, rotation 2 in the stock
firmware.  The stock build uses the TFT_eSPI library's own `User_Setup.h`
(`TFT_RST -1`, so the library never drives the reset pin), not the copy in
`src/`; the stock firmware pulses `DISPLAY_ALL_DRES` high for 10 ms and
leaves it low.  An active-low reset (the esp_lcd default) holds the
displays in reset, and every SPI write still succeeds.


## STM8 base MCU (I2C address 0x51)

Register access: write the register address, then read or write bytes.
The address auto-increments, so one transaction can read or write
consecutive registers.

| Reg | Name | Access | Content |
| --- | --- | --- | --- |
| 0x10 | `WHOAMI` | read | 0x35 |
| 0x12 | `VERSION` | read | firmware version (4) |
| 0x20..0x22 | `CH1REG`..`CH3REG` | read/write | see below |
| 0x23 | `CCSUM` | read | CC line summary for the host and external inputs |
| 0x24 | `AUXREG` | read | see below; reading it clears the first-boot flag |
| 0x26 | `MUXOECTR` | read/write | bit 0: USB3 mux enable (default 1) |
| 0x30..0x37 | CC voltages | read | VEXT CC1/CC2, VHOST CC1/CC2, 16-bit little endian |

Channel register bits:

| Bit | Mask | Meaning |
| --- | --- | --- |
| 7 | 0x80 | fault, read only: set while the load switch fault pin is low |
| 6 | 0x40 | data enable |
| 4 | 0x10 | power enable |
| 2..1 | 0x06 | current limit: 0 = 0.5 A, 1 = 1.0 A, 2 = 1.5 A, 3 = 2.0 A |

The STM8 stores a written channel byte as `(reg | 0x7F) & value`, so a
write can clear the fault bit but never set it.  Its main loop then
updates the outputs from the registers and recomputes the fault bit from
the pin, so the fault bit follows the pin and does not latch.  The STM8
does not turn a port off on a fault.

`AUXREG` bits: 0x01 VHOST present, 0x02 VEXT present, 0x04 USB3 mux output
enabled, 0x08 USB3 mux select, 0x10 first boot.  The first-boot flag is
set at STM8 power-up and cleared by the first read of `AUXREG`.

At STM8 power-up all channel registers are 0: power and data off.  The
ESP32 firmware must write them.  The STM8 holds its outputs while the ESP32
resets.

The stock firmware writes the three channel registers in one transaction
starting at 0x20 and reads five bytes starting at 0x20 (CH1..CH3, `CCSUM`,
`AUXREG`).


## PAC1943 power monitor (I2C address 0x10)

- Product ID register 0xFD reads 0x6A; revision ID at 0xFF.
- Channels 1..3 measure the three ports (channel 4 disabled), but not in
  order: channel 3 measures port 1, channel 1 port 2, and channel 2 port 3
  (stock `meterBoardMap` = {2, 0, 1}; confirmed on hardware 2026-10-20 by
  powering one port at a time).
- Stock configuration:
  - `CTRL` (0x01) = 0x0010: channel 4 disabled, 1024-sample adaptive
    accumulation, alert pins as alerts.
  - `NEG_PWR_FSR` (0x1D) = 0xAA00: VBUS unipolar 0 to 9 V, VSENSE bipolar
    -50 to +50 mV.
  - `SMBUS_SETTINGS` (0x1C) = 0x08: timeout enabled, clear the POR flag.
  - All alerts disabled.
- Reading: send `REFRESH` (0x00), wait at least 1 ms, then read 12 bytes
  from `VBUS1_AVG` (0x0F): VBUS1..3 then VSENSE1..3, 16 bits big endian.
- Conversion:
  - Voltage (mV) = raw / 65536 * 9000.
  - Current: VSENSE is a signed 16-bit value.  Shunts are 20 mOhm, so full
    scale is 50 mV / 0.020 = 2500 mA, and current (mA) = raw / 32768 *
    2500.  The board wires the shunt reversed: a negative raw value is
    current flowing to the device.  The firmware reports that as positive.


## Power and current limit

The firmware sets a 1.0 A current limit on every port (the stock default).
