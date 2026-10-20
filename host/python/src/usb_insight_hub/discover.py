# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Find USB Insight Hubs by their USB serial number (the ESP32-S3 MAC).

Both the stock firmware and the ESP32-S3 USB-Serial-JTAG port use USB
303a:1001, which every Espressif development board shares, so a hub is
selected by serial number and classified by its product string.  A
``jtag`` port is either the Jetperch firmware or the ROM bootloader.
"""

from dataclasses import dataclass
import time

VID = 0x303A
PID = 0x1001
KIND_STOCK = 'stock'   # stock firmware: TinyUSB CDC, "InsightHUB Controller"
KIND_JTAG = 'jtag'     # USB-Serial-JTAG: Jetperch firmware or ROM bootloader

_PRODUCTS = {
    'InsightHUB Controller': KIND_STOCK,
    'USB JTAG/serial debug unit': KIND_JTAG,
}


@dataclass(frozen=True)
class HubPort:
    device: str
    serial: str
    kind: str


def normalize_serial(serial):
    """Return a serial number as 12 uppercase hex digits, without separators."""
    if not serial:
        return ''
    return ''.join(c for c in serial if c.isalnum()).upper()


def _list_ports():
    from serial.tools import list_ports
    return list_ports.comports()


def find(serial=None, ports=None):
    """List hub serial ports, optionally only those matching ``serial``."""
    if ports is None:
        ports = _list_ports()
    want = normalize_serial(serial)
    found = []
    for p in ports:
        if p.vid != VID or p.pid != PID:
            continue
        kind = _PRODUCTS.get((p.product or '').strip())
        if kind is None:
            continue
        sn = normalize_serial(p.serial_number)
        if want and sn != want:
            continue
        found.append(HubPort(p.device, sn, kind))
    return sorted(found, key=lambda h: h.device)


def wait(serial, kind, timeout=10.0, lister=None, sleep=time.sleep,
         clock=time.monotonic):
    """Wait for the hub ``serial`` to enumerate as ``kind``.

    :raise TimeoutError: if it does not appear within ``timeout`` seconds.
    """
    lister = lister or _list_ports
    deadline = clock() + timeout
    while True:
        for p in find(serial, ports=lister()):
            if p.kind == kind:
                return p
        if clock() >= deadline:
            raise TimeoutError(f'hub {normalize_serial(serial)} did not appear as {kind}')
        sleep(0.1)
