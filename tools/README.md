<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# Tools

Thin wrappers around the `uih` command line tool from the host package
(`host/python`).  Install it first, for example:

```bash
python3 -m venv host/python/.venv
host/python/.venv/bin/pip install -e host/python
```

Every tool accepts `--serial <MAC>` (with or without colons).  It is
optional when exactly one hub is connected.

| Tool | Purpose |
| --- | --- |
| `enter_bootloader.py` | Enter the ESP32-S3 ROM bootloader (stock or Jetperch firmware) |
| `exit_bootloader.py` | Leave the ROM bootloader and boot the application |
| `backup.py PATH` | Back up the whole 8 MB flash, verify it, and boot again |
| `restore.py PATH` | Write a whole-flash backup and boot it |
| `flash.py [BUILD_DIR]` | Install the bundled firmware (release packages), or one from an ESP-IDF build directory |
| `bundle_firmware.py BUILD_DIR` | Release builds: bundle a firmware build into the host package |
| `soak.py` | Toggle port power repeatedly and report dropped commands |
| `usb_soak.py` | Power-cycle a port; its USB device must disconnect, reconnect and stay connected (Linux) |
| `update_loop.py BUILD_DIR BACKUP` | Repeat: install, verify, restore stock, verify (`--cycles`, default 20) |
| `stream_check.py` | Stream status messages and count lost ones (`--rate`, `--duration`) |

A full backup takes about 12 minutes.
