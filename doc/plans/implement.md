<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# Plan: custom USB Insight Hub firmware and host software

**Status:** in progress (started 2026-10-20).  See "Progress" at the end.

## Goal

Replace the stock USB Insight Hub ESP32-S3 firmware with a small,
reliable ESP-IDF firmware for unattended HIL test stations, plus the host
library and tools to drive, update and recover it.  The hub must act as a
plain USB hub at power-up, never lose a command, and stream per-port
voltage and current.

## Why

The stock firmware (Aeriosolutions/USB-Insight-HUB-Software, ESP32
`cpu_ver` 1.0.0, STM8 `base_ver` 4) targets standalone use with a WiFi
web UI.  For CI it has these problems:

- **Silently dropped commands, about 1 in 50.**  The `Intercomms` task
  (`src/Intercomms.cpp`, ~63 ms loop) compares the requested port state
  with what it last sent, writes changes to the STM8, then copies the STM8
  readback over the *requested* state, with no lock against the serial
  handler on the other task.  A `set` that lands between the compare and
  the copy is overwritten before it is sent.  A failed I2C write is also
  recorded as sent and never retried.  `get` reports the requested state,
  so only the channel current reveals the drop.  Unfixed on `main` and on
  the unreleased `Development` branch.
- **Serial link wedges.**  The TinyUSB CDC stopped draining more than once
  (2026-07-19, 2026-08-01) and needed an upstream power cycle.
- **RAM pressure** from the web stack (upstream issue #21: ~50 KB free
  heap with WiFi on, no PSRAM).
- **Persistence.**  Port state and settings persist in NVS across power
  cycles, so a hub's behavior depends on its history.
- **No error detection** in the line-delimited JSON protocol.

## Decisions (Matt, 2026-10-20)

1. Plain **ESP-IDF** firmware, not a MiniBitty port: the hub must not
   depend on pyjoulescope_driver.
2. Control over the ESP32-S3 **built-in USB-Serial-JTAG** peripheral: a
   hardware serial port that cannot wedge in our code, esptool-native
   reset into the ROM bootloader, and JTAG for free.  No TinyUSB.
3. Control needs are small: port power on/off (and data enable, which is
   the same register), periodic per-port voltage and current, and entering
   firmware update.
4. **At power-up, act as a normal USB hub: all ports on.**  Persist
   nothing across power cycles: no NVS writes.
5. Minimal display: per-port on/off, voltage, current and fault.  One
   button per port toggles that port's power.
6. Out of scope: WiFi, web UI, MQTT, NTP, menus, host-rendered images,
   configuration storage, STM8 firmware changes.
7. Repository `~/repos/Jetperch/usb_insight_hub`, with subdirectories for
   firmware, host software and tools.
8. **License:** Apache-2.0 for this repository, with MIT attribution
   (`NOTICE`) for anything derived from the stock firmware.
9. **ESP-IDF:** the latest stable release, v6.1 (2026-08-27) when this plan
   was written.  Pin the exact version in `README.md` and the build.
10. **Partition table:** keep the stock layout unchanged (see below).  Our
    image is far smaller than the 3.7 MB `app0` slot.
11. **Current limit:** a fixed 1.0 A per port, the stock default.  No
    current-limit command in v1.
12. **Hub identification:** by USB serial number (the MAC) plus the `info`
    op.  The fixed USB-Serial-JTAG descriptors (303a:1001) are fine.

## Hardware reference

Verified on hub `B43A45B7BFBC` (2026-10-20) or read from the stock source
(MIT licensed; attribute in `NOTICE`).  `doc/hardware.md` will hold the
maintained copy.

- **MCU:** ESP32-S3 (QFN56, rev v0.2), 8 MB embedded GigaDevice flash, no
  PSRAM, 40 MHz crystal.  The stock firmware runs at 160 MHz.
- **eFuses:** all open.  No secure boot, no flash encryption, download
  mode and USB-Serial-JTAG enabled, `USB_PHY_SEL` = 0 (the internal PHY
  belongs to USB-Serial-JTAG at reset), ROM prints on USB-Serial-JTAG.
- **USB:** a UPD720210 hub; its port 4 is the ESP32.  The stock firmware
  moves the PHY to USB-OTG (TinyUSB, "Aerio InsightHUB Controller",
  303a:1001).  USB-Serial-JTAG enumerates as "Espressif USB JTAG/serial
  debug unit", 303a:1001, serial number = MAC.
- **I2C (board-to-board):** SDA GPIO34, SCL GPIO33, 400 kHz.
  - STM8 base MCU at 0x51.  `WHOAMI` reg 0x10 reads 0x35; `VERSION` reg
    0x12 reads 4.  `CH1REG`..`CH3REG` at 0x20..0x22: bit 7 fault (read),
    bit 6 data enable, bit 4 power enable, bits 2..1 current limit
    (0 = 0.5 A, 1 = 1.0 A, 2 = 1.5 A, 3 = 2.0 A).  The stock write is
    `ilim << 1 | 0x40 if data | 0x10 if power`, all three channels in one
    transaction starting at 0x20.  `CCSUM` 0x23, `AUXREG` 0x24 (power
    source, USB3 mux, first-boot flag), `MUXOECTR` 0x26 (USB3 mux enable),
    CC voltages 0x30..0x37.  `MCU_INT` on GPIO18.
  - PAC194x power monitor at 0x10, on the same bus; alert on GPIO35.
- **Displays:** three ST7789 240x240 on one SPI bus (MOSI GPIO11, SCLK
  GPIO9, DC GPIO10, RST GPIO14), chip selects GPIO6/16/12, BGR order,
  rotation 2, 40 MHz.  Backlights are PWM on GPIO13/15/17.
- **Buttons:** CH1..CH3 on GPIO39/38/37, SETUP on GPIO36, active low.
- **Other:** `VBUS_MONITOR` ADC on GPIO1, `AUX_LED` GPIO45, `HUB_RESET`
  GPIO21 (defined but never driven by the stock firmware; leave it alone).
- **STM8 behavior:** at its own power-up all channel registers are 0
  (power and data off), so ports stay off until the ESP32 writes them.
  Reading `AUXREG` clears its first-boot flag (0x10), so the flag is set
  only when the STM8 has just powered up.  The fault bit follows the load
  switch's fault pin and does not latch; the STM8 never turns a port off
  on a fault.  It holds port state while the ESP32 resets or sits in the
  ROM bootloader (verified: three loaded ports stayed powered through a
  1200-baud reboot, a 12-minute flash read and a reset).
- **Stock defaults:** current limit 1.0 A per port; USB3 enabled
  (`hubMode` "usb2&3").

### Stock firmware access and recovery (validated 2026-10-20)

- A 1200-baud open of the stock CDC port reboots into the ROM bootloader
  on USB-Serial-JTAG (pyserial raises `BrokenPipeError` on the DTR ioctl;
  the line coding alone triggers it).
- esptool 4.8.1 with `--before no_reset` talks to it; espefuse needs
  `bitstring==4.2.3` on Python 3.14.
- Exit: `esptool --before no_reset --no-stub --after hard_reset write_mem
  0x6000812C 0x0 0x1` clears `FORCE_DOWNLOAD_BOOT` and returns to the app.
- Full flash backup, `verify_flash` OK:
  `~/repos/Jetperch/backups/uih_B43A45B7BFBC_fw1.0.0_20261020/`
  (`flash_8MB.bin`, sha256 `d73b6754...`, plus `efuse_summary.txt`).
  Reading 8 MB takes about 12 minutes.
- Stock partitions: nvs 0x9000, otadata 0xe000, app0 0x10000, app1
  0x3c0000 (0x3b0000 each), spiffs 0x770000, coredump 0x7f0000.
- In the backup, `otadata` selects `app0` (sequence 1); `app1` is empty.
  The stock app is built on ESP-IDF v4.4.6 (Arduino) and uses about
  1.8 MB of its slot.

### Partition table and image layout

- Keep the stock partition table byte for byte, so restoring the backup
  and switching between stock and custom images stays simple.
- Our application goes in `app0`.  Every install writes the ESP-IDF v6.1
  bootloader, the (unchanged) partition table and the app together through
  the ROM bootloader, then erases `otadata` so the bootloader boots `app0`.
- `app1`, `spiffs` and `coredump` stay unused in v1; `nvs` is never
  written (decision 4).

## Repository layout

```
usb_insight_hub/
  README.md
  LICENSE, NOTICE        Jetperch license; MIT attribution for stock code
  CHANGELOG.md
  doc/
    hardware.md          pins, buses, registers, STM8 behavior
    protocol.md          the serial protocol (the contract)
    plans/
  firmware/
    esp32s3/             ESP-IDF project
      main/              boot sequence, task wiring
      components/
        uih_core/        pure C, host-testable: CRC, JSON, protocol,
                         STM8 and PAC1943 encoding, the hub state machine,
                         button debounce, display text layout
        uih_drivers/     ESP-IDF glue: I2C, USB-Serial-JTAG, ST7789, GPIO
      test/host/         host (gcc) unit tests for uih_core
      partitions.csv
      sdkconfig.defaults
  host/
    python/              package `usb_insight_hub`: client, CLI, backends
      tests/
  test/vectors/          protocol test vectors shared by both test suites
  tools/                 bootloader entry, backup/restore, flash, soak
```

## Protocol v1 (summary; `doc/protocol.md` is normative)

Line-oriented and human-readable, with error detection:

- **Framing:** each line is `<json>*<crc32>\n`, where `<crc32>` is 8 hex
  digits of CRC-32 (IEEE) over the JSON bytes.  Lines without a valid CRC
  are reported (`"code": "crc"`) and ignored.  Non-protocol lines (ROM boot
  prints) are skipped by the host.
- **Requests** carry a host sequence number `seq`; the response echoes it.
  Commands are **absolute and idempotent** (set port 2 power off, never
  toggle), so a host retry after a lost response is always safe.
- **Ops:** `ping`, `info` (firmware version, git hash, STM8 version,
  serial number, uptime, protocol version), `set` (per-port `power`,
  `data`, or all ports), `status` (one snapshot), `stream` (rate 0..50 Hz),
  `bootloader` (enter the ROM bootloader), `reset`.
- **Responses report measured state:** after a `set`, the hub reads back
  the STM8 register and replies with the actual `power`, `data`, `fault`
  and the latest voltage and current per port.  A write that does not
  verify (after a bounded retry) returns `"status": "error", "code":
  "not_applied"`.
- **Status stream:** unsolicited `{"type": "status", ...}` lines with a
  hub sequence counter so the host detects gaps, a millisecond timestamp,
  and per-port `power`, `data`, `fault`, `mV`, `mA`.  Button presses and
  fault edges appear as `{"type": "event", ...}` lines.
- **Backpressure:** responses are never dropped; stream lines are dropped
  (and counted) when the USB-Serial-JTAG TX buffer is full, for example
  with no host reading.

## Firmware architecture

- **One owner of hardware state.**  The `hub` task owns the I2C bus, the
  STM8 and the PAC194x.  The serial RX task and the button task send it
  requests through a queue; it applies them, verifies by readback, and
  replies.  No other code writes port state, which removes the stock race
  by design.
- **Boot sequence:**
  1. Init I2C; check STM8 `WHOAMI` and `VERSION` (expect 4; report a
     mismatch in `info`, keep running).  Read `AUXREG` for the first-boot
     flag.
  2. Write all ports: power on, data on, 1.0 A limit (fixed).  USB3 mux
     enabled.
  3. Init the PAC194x, then buttons, displays, USB-Serial-JTAG protocol.
  The time from hub power-up to ports on is measured in Stage 2.
- **No persistence:** no NVS writes.  The NVS partition is left as is,
  and the firmware does not read stock settings from it.
- **Logging:** ESP-IDF logs must not corrupt the protocol channel.  Route
  them to UART0 (not connected) or disable them, and expose diagnostics
  through protocol messages.
- **Watchdog:** the task watchdog covers the hub, RX and display tasks.
  A reset does not change port state (the STM8 holds it); the firmware
  then re-reads the STM8 rather than re-writing defaults.  It applies the
  all-on defaults when the STM8 first-boot flag is set or the ESP32 reset
  reason is power-on.
- **Never touch USB-OTG**, so the PHY stays on USB-Serial-JTAG and a bad
  image can always be recovered over USB.
- **Faults are report-only:** the STM8 fault flag appears in every port
  state and as `fault` events on each edge.  The firmware does not turn a
  faulted port off: the load switch limits current on its own, the stock
  firmware does not either, and an automatic off could trip on a device's
  inrush.  PAC1943 alerts are not configured (they belonged to the stock
  over-current limits, which are out of scope).

## Stages

Each stage builds, passes its tests, and is reviewable on its own.
Hardware verification uses the agent machine's hub (CH1 JS110, CH2
JS220, CH3 JS320); probe the channel map before relying on it.

### Stage 0: repository, docs and recovery tools

- Repository skeleton (git), `README.md`, `LICENSE` (Apache-2.0), `NOTICE`
  (MIT attribution for the stock firmware), `CHANGELOG.md`.
- `doc/hardware.md` from the reference above; `doc/protocol.md` v1.
- `tools/`: `enter_bootloader.py` (1200-baud touch for stock firmware,
  `bootloader` op or esptool reset for ours), `backup.py`, `restore.py`
  and `flash.py` (esptool wrappers with the settings above; `flash.py`
  writes bootloader, partition table and app, then erases `otadata`).
- Document host setup in `README.md`.  The agent machine's udev rule
  (`/etc/udev/rules.d/99-aerio-insighthub.rules`) matches tty devices by
  303a:1001 with `MODE="0666"`, so it already covers the stock firmware,
  the ROM bootloader and our firmware.  JTAG debugging (OpenOCD) would
  need a matching `SUBSYSTEM=="usb"` rule; it is optional.
- **Acceptance:** restore the 2026-10-20 backup onto the hub with
  `restore.py` and verify it (`verify_flash`), proving the round trip
  before any custom image is flashed.

### Stage 1: host library and stock baseline

- `host/python` package `usb_insight_hub`: a `Hub` API (`set_power`,
  `set_data`, `status`, `stream`, `info`, `enter_bootloader`) with two
  backends: the stock JSON API (needed during rollout and for the
  baseline) and protocol v1.  The stock backend does the read-back of
  power state *and* channel current that the stock firmware needs.
- `tools/soak.py`: N power toggles per port with read-back; reports the
  drop rate and timing.
- Unit tests for framing and CRC with shared test vectors (also used by
  the firmware tests in Stage 2).
- **Acceptance:** a recorded stock baseline (expected about 1 in 50 to 1
  in 60 dropped `set` commands without read-back).

### Stage 2: firmware skeleton with port control

- ESP-IDF v6.1 project (version pinned in `README.md`; confirm the
  minimum chip revision setting accepts ESP32-S3 rev v0.2), the
  `protocol`, `stm8`, `hub` and `buttons` components; ops `ping`, `info`,
  `set`, `status` (power, data and fault only), `bootloader`, `reset`.
- Boot sequence with all ports on; buttons toggle their port.
- Host-target unit tests for `protocol` and the STM8 register encoding.
- **Acceptance on hardware:**
  - `soak.py`: at least 10,000 toggles per port, zero drops.
  - Hub power-up turns all ports on; time to ports-on recorded.
  - An ESP32 reset (watchdog or `reset` op) leaves port state unchanged.
  - `bootloader` op, then flash and restore with `tools/` (update path).
  - The button toggles show up as events.

### Stage 3: meters, status stream and faults

- PAC1943 support (configuration and scaling), the `stream` op and fault
  events.
- **Acceptance:** voltage and current agree with the stock firmware's
  readings on the same hub and load within a documented tolerance;
  a 1-hour stream at 10 Hz with no gaps; fault parsing covered by unit
  tests (no destructive short-circuit test on the agent hub).

### Stage 4: display

- Minimal rendering on the three ST7789s: port number, ON/OFF, voltage,
  current and a fault indicator, refreshed at about 2 Hz from a state
  snapshot (the display task never touches I2C).  Fixed backlight level.
- **Acceptance:** command latency and stream rate unchanged with the
  display running (measured with `soak.py` and a stream test).

### Stage 5: update and recovery loop

- `tools/flash.py` covers: stock to custom (1200-baud touch), custom to
  custom (`bootloader` op or esptool's USB-Serial-JTAG reset), and custom
  to stock (restore the backup).
- **Acceptance:** 20 cycles of update, verify (`info` version), restore
  and verify, with no manual step; documented in `README.md`.

### Stage 6: joulescope_ci integration (separate plan in joulescope_ci)

- A hub recovery capability using the host library (the runner already
  has a USB Insight Hub capability, `49ffb45`), station inventory keyed
  by hub serial number and port, and replacing JS220-based power cycling
  on stations as they move to hubs.
- Kept separate because it spans the farm rollout and the bench-by-bench
  hardware changes.

## Testing summary

- Unit tests: host package (pytest) and firmware host-target tests, both
  using the shared protocol test vectors.
- Hardware tests (agent hub): `soak.py`, the stream continuity test, the
  update and recovery loop, and boot timing.  Record results in
  `CHANGELOG.md` per release.

## Risks and open questions

- **Identity:** 303a:1001 is shared with every Espressif development
  board, so host tools and joulescope_ci select hubs by serial number and
  confirm with `info`, never by vendor and product ID alone.
- **ROM prints** on USB-Serial-JTAG at every boot (eFuse enabled): the
  host must skip non-protocol lines; the CRC framing makes that safe.
- **USB-Serial-JTAG TX buffering** with no host reading must never block
  the hub task (responses are queued, stream lines dropped and counted).
- **STM8 details to verify in Stage 2:** the initial register values, the
  `AUXREG` first-boot flag, the fault-latch behavior, and whether a write
  of all three channels has side effects on a faulted port.
- **USB3 mux and `HUB_RESET`:** keep the stock behavior (USB3 enabled,
  `HUB_RESET` undriven) unless a reason appears.
- **Bootloader change:** the stock bootloader is from ESP-IDF v4.4.6; a
  v6.1 app needs the v6.1 bootloader, so partial (app-only) updates from
  stock are not possible.  `flash.py` always writes the full set, and
  restoring stock writes the whole backup.

## Progress

- 2026-10-20: plan accepted; Stage 0 started.
- 2026-10-20: **Stage 0 complete.**  Repository, `doc/hardware.md`,
  `doc/protocol.md`, host package with discovery, esptool wrappers, the
  `uih` CLI and `tools/` wrappers (32 tests).  Acceptance: `tools/restore.py`
  wrote the 2026-10-20 backup (8 MB in 17.7 s, "Hash of data verified") and
  the stock firmware booted with all ports powered.  Finding: esptool 5's
  RTS hard reset does not reset the chip over USB-Serial-JTAG while the
  flasher stub runs; leaving the bootloader uses `--after watchdog-reset`.
- 2026-10-20: **Stage 1 complete.**  Protocol v1 client (`client.py`) with
  idempotent retries, the stock-firmware backend (`stock.py`), simulators
  for both (`sim.py`), `soak.py` and shared test vectors.  Stock baseline
  (unverified `set`, 300 cycles x 3 ports x 2): 43 of 1800 commands dropped
  (2.39%), latency p50 51 ms, p99 101 ms.
- 2026-10-20: **Stage 2 complete** (except the manual checks below).
  Firmware `uih_core` (pure C, host tests) and `uih_drivers` (ESP-IDF).
  Acceptance on the agent hub:
  - `soak.py`, 10,000 cycles x 3 ports: 60,000 commands, 0 dropped,
    0 errors, 0 timeouts; latency p50 2.9 ms, p99 3.9 ms, max 10.2 ms
    (display task running).
  - The `reset` op and every reflash keep the port states (port 2 off and
    port 3 data off survived a reset).
  - Update path: `bootloader` op, flash, boot (`uih flash`, about 3 s).
  - Findings: esptool's watchdog reset reports `ESP_RST_POWERON`, so the
    power-up defaults use the STM8 first-boot flag (or power-on with all
    ports off); bytes left by esptool in the line assembler gave one
    `format` error, so the host sends `\n` on open; pyserial `read(n)`
    waits for its timeout, which had added 50 ms to every response.
  - **Manual, pending (needs a person at the hub):** button presses produce
    `button` events, and a hub power-up turns all ports on (time to
    ports-on).
- 2026-10-20: **Stage 3 complete.**  PAC1943 meters, `stream` and fault
  events (built in Stage 2), `stream_check.py`.  Acceptance:
  - 1-hour stream at 10 Hz: 36,012 messages, 0 lost, no reset (uptime
    continuous).
  - Meters agree with the stock firmware on the same hub and loads within
    20 mV and 2 mA (tolerance).  Ours reads about 15 mV higher on every
    port with the same currents.  The conversions match, so this is most
    likely a real supply difference: the stock firmware loads the hub
    more (WiFi, brighter displays).  Not confirmed with an external meter.
  - Finding: the PAC channels are routed 3, 1, 2 to ports 1, 2, 3 (stock
    `meterBoardMap`); fixed and confirmed by powering ports one at a time.
  - Finding: just after boot, our meters read 0 for about 100 ms
    (documented), and the stock firmware's moving average starts from 0
    (one half-voltage reading in the first 20-cycle run); `update_loop.py`
    now waits 2 s before reading the meters.
  - Fault parsing is covered by unit tests only (no short-circuit test).
- 2026-10-20: **Stage 4 implemented.**  Text layout, change detection,
  5 x 7 font and band rendering in `uih_core` (host tests, 100% lines);
  ST7789 driver shares one panel across the three CS lines; the display
  task draws at 2 Hz from a mutex-protected copy of the hub state.  Latency
  unchanged with the display running (soak above).  **Manual, pending:**
  look at the displays (orientation, colors, inversion).
- 2026-10-20: **Stage 5 complete.**  `update_loop.py` (update, verify,
  restore, verify, compare meters).  20 of 20 cycles passed with no
  manual step: update (stock to this firmware) about 3.6 s, restore (whole
  8 MB backup, back to stock) about 25 s.  Then 5 of 5 cycles with the
  meter settle.  Documented in `README.md` and `tools/README.md`.
- 2026-10-20: **Stage 6 plan** written:
  `joulescope_ci/doc/plans/usb_insight_hub.md`.

### Remaining (manual, needs a person at the hub)

- Press each port button: the port toggles and `uih stream` prints a
  `button` event.
- Unplug and replug the hub's power: all ports come on (record the time
  from power to ports on).
- Look at the displays: upright, readable, colors right (ON green, OFF
  grey, FAULT red, white text on black).  If they are inverted or
  mirrored, adjust `esp_lcd_panel_invert_color` / `mirror` in
  `components/uih_drivers/src/display.c`.
