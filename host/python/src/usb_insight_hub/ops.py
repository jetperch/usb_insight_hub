# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Multi-step hub operations: entering the ROM bootloader, backup, flashing."""

import time

from . import discover, esp


def _request_bootloader(device):
    """Ask Jetperch firmware to reboot into the bootloader.

    :return: True if the firmware answered, False if nothing answered
        (for example, the chip is already in the ROM bootloader).
    """
    from .client import Hub, HubError
    try:
        with Hub(device, timeout=0.5, retries=1) as hub:
            hub.enter_bootloader()
        return True
    except (HubError, TimeoutError, OSError):
        return False


def enter_bootloader(serial, finder=discover.find, waiter=discover.wait,
                     toucher=esp.touch_1200, request_bootloader=_request_bootloader,
                     mac_reader=None, sleep=time.sleep, attempts=10):
    """Put the hub ``serial`` into the ESP32-S3 ROM bootloader.

    Handles stock firmware (1200-baud touch), Jetperch firmware (the
    ``bootloader`` op), and a chip that is already in the bootloader.
    Confirms through esptool that the ROM answers with the expected MAC.

    :return: the serial port device of the ROM bootloader.
    :raise LookupError: if no hub with ``serial`` is connected.
    """
    serial = discover.normalize_serial(serial)
    if mac_reader is None:
        mac_reader = lambda device: esp.Esptool(device).read_mac()  # noqa: E731
    ports = finder(serial)
    if not ports:
        raise LookupError(f'hub {serial} not found')
    port = ports[0]
    if port.kind == discover.KIND_STOCK:
        toucher(port.device)
        port = waiter(serial, discover.KIND_JTAG, timeout=10.0)
    elif request_bootloader(port.device):
        sleep(0.5)  # the firmware answers, then resets about 100 ms later
        port = waiter(serial, discover.KIND_JTAG, timeout=10.0)
    for attempt in range(attempts):
        try:
            mac = mac_reader(port.device)
            break
        except esp.EsptoolError:
            if attempt == attempts - 1:
                raise
            sleep(0.5)
    if mac != serial:
        raise RuntimeError(f'bootloader on {port.device} reports MAC {mac}, expected {serial}')
    return port.device
