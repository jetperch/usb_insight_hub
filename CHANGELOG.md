<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# CHANGELOG

This file lists the changes to the USB Insight Hub firmware, host software
and tools.

---

## 0.1.0

2026 Oct 20

* Started the project: hardware reference, protocol v1, and recovery tools
  (`doc/plans/implement.md`).
* Firmware (ESP-IDF v6.1): verified port control (0 drops in 60,000
  commands, about 3 ms per command), port states kept across firmware
  resets, PAC1943 voltage and current, status stream, button and fault
  events, and a minimal V/I display.
* Host package: protocol v1 client, stock-firmware backend, simulators,
  and the `uih` commands `soak`, `stream-check` and `update-loop`.
* Python package published to PyPI as `usb_insight_hub_jetperch` (not the
  official USB Insight Hub software); GitHub Actions for tests, builds and
  tagged releases.
