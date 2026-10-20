<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# USB Insight Hub firmware and host software

Replacement ESP32-S3 firmware for the
[USB Insight Hub](https://github.com/Aeriosolutions/USB-Insight-HUB-Software),
plus the host library and tools that drive, update and recover it.  Built
for unattended hardware-in-the-loop test stations: the hub acts as a plain
USB hub at power-up, never loses a command, and streams per-port voltage
and current.  See `doc/plans/implement.md` for the design and status.

| Directory | Contents |
| --- | --- |
| `doc/` | `hardware.md`, `protocol.md` (normative), `plans/` |
| `firmware/esp32s3/` | ESP-IDF project |
| `host/python/` | Python package `usb_insight_hub` and the `uih` tool |
| `test/vectors/` | protocol test vectors shared by the Python and C tests |
| `tools/` | bootloader entry, backup, restore, flash, soak |


## Host setup

The host package is on PyPI as `usb_insight_hub_jetperch` (import name
`usb_insight_hub`, command `uih`):

```bash
pip install usb_insight_hub_jetperch
```

For development, from this repository:

```bash
python3 -m venv host/python/.venv
host/python/.venv/bin/pip install -e "host/python[test]"
env -u PYTHONPATH host/python/.venv/bin/uih list
```

Run the host tests with `env -u PYTHONPATH` if your shell exports a
`PYTHONPATH`, so they only see this package.

Linux needs read/write access to the hub's serial port.  A udev rule like
this covers the stock firmware, the ROM bootloader and this firmware (all
use USB 303a:1001):

```
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", MODE="0666"
```

Hosts select a hub by its USB serial number (the ESP32-S3 MAC), never by
vendor and product ID alone: every Espressif development board uses
303a:1001 too.


## Firmware

Build with ESP-IDF v6.1 (`firmware/esp32s3`), then install it on a hub
running either the stock firmware or this firmware:

```bash
cd firmware/esp32s3
. ~/esp/esp-idf-v6.1/export.sh && idf.py build
cd ../..
python tools/flash.py --serial <MAC> firmware/esp32s3/build
uih --serial <MAC> info
```

`flash.py` enters the ROM bootloader (1200-baud touch for stock, the
`bootloader` op for this firmware), writes the bootloader, partition table
and app, erases `otadata`, and reboots.  The port states survive the
update: the STM8 holds them while the ESP32-S3 restarts.

Host-side C unit tests (pure `uih_core`, no ESP-IDF):

```bash
cmake -S firmware/esp32s3/test/host -B build/host && cmake --build build/host
ctest --test-dir build/host --output-on-failure
gcovr -r firmware/esp32s3/components/uih_core build/host   # coverage
```

Hardware checks: `tools/soak.py` (dropped commands and latency) and
`tools/update_loop.py <build_dir> <backup>` (update, verify, restore stock,
verify; also compares the meter readings of both firmwares).


## Releases

GitHub Actions (`.github/workflows/packaging.yml`) runs REUSE lint, the
Python tests (Linux, Windows, macOS), the firmware core host tests, and
builds the sdist and wheel on every push.  Pushing a `vX.Y.Z` tag that
matches `host/python/src/usb_insight_hub/version.py` also publishes to
PyPI (trusted publishing) and attaches the packages to a GitHub release.


## Recovery

Every hub can return to its stock firmware from a full-flash backup:

```bash
python tools/backup.py --serial <MAC> hub_backup.bin    # about 12 minutes
python tools/restore.py --serial <MAC> hub_backup.bin   # about 20 seconds
```

Both enter the ROM bootloader automatically, from the stock firmware
(1200-baud touch) or from this firmware (`bootloader` op).


## License

Apache-2.0 (`LICENSES/Apache-2.0.txt`).  `NOTICE` attributes the hardware
knowledge derived from the MIT-licensed stock firmware and the ST7789 setup
from TFT_eSPI (BSD-2-Clause).
