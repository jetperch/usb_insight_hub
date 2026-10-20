<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# usb_insight_hub_jetperch

Python library and `uih` command-line tool for the
[USB Insight Hub](https://github.com/Aeriosolutions/USB-Insight-HUB-Software)
running Jetperch's replacement ESP32-S3 firmware.  The firmware, protocol
and hardware notes live at
[jetperch/usb_insight_hub](https://github.com/jetperch/usb_insight_hub).

This is **not** the official USB Insight Hub software from Aerio Solutions.
Jetperch wrote it for unattended hardware-in-the-loop test stations: the
hub acts as a plain USB hub at power-up, verifies every port command by
read-back, and streams per-port voltage and current.

```bash
pip install usb_insight_hub_jetperch
uih list
uih --serial <MAC> info
uih --serial <MAC> set 2 --power off
```

The package can also read and switch hubs that still run the stock
firmware 1.0.0, back up and restore their flash, and install the
replacement firmware.  The import name is `usb_insight_hub`:

```python
from usb_insight_hub.client import open_hub

with open_hub('B43A45B7BFBC') as hub:
    ports, aux = hub.status()
    hub.set_power(2, False)
```

Linux needs read/write access to the hub's serial port (USB 303a:1001);
see the repository README for a udev rule.
