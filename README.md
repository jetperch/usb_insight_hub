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


## Recovery

Every hub can return to its stock firmware from a full-flash backup:

```bash
python tools/backup.py --serial <MAC> hub_backup.bin    # about 12 minutes
python tools/restore.py --serial <MAC> hub_backup.bin   # about 20 seconds
```

Both enter the ROM bootloader automatically, from the stock firmware
(1200-baud touch) or from this firmware (`bootloader` op).


## License

Apache-2.0 (`LICENSES/Apache-2.0.txt`).  Hardware knowledge derived from the
MIT-licensed stock firmware is attributed in `NOTICE`.
