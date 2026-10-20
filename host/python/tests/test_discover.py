# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

from types import SimpleNamespace

import pytest

from usb_insight_hub import discover


def _port(device, serial, product, vid=0x303A, pid=0x1001, manufacturer=''):
    return SimpleNamespace(device=device, serial_number=serial, product=product,
                           manufacturer=manufacturer, vid=vid, pid=pid)


STOCK = _port('/dev/ttyACM0', 'B43A45B7BFBC', 'InsightHUB Controller', manufacturer='Aerio')
JTAG = _port('/dev/ttyACM1', 'B4:3A:45:B7:BF:01', 'USB JTAG/serial debug unit',
             manufacturer='Espressif')
OTHER = _port('/dev/ttyACM2', '000578', 'Joulescope', vid=0x16D0, pid=0x0E88)


@pytest.mark.parametrize('raw, expect', [
    ('B4:3A:45:B7:BF:BC', 'B43A45B7BFBC'),
    ('b43a45b7bfbc', 'B43A45B7BFBC'),
    ('B4-3A-45-B7-BF-BC', 'B43A45B7BFBC'),
    (None, ''),
])
def test_normalize_serial(raw, expect):
    assert expect == discover.normalize_serial(raw)


def test_find_classifies_ports():
    found = discover.find(ports=[STOCK, JTAG, OTHER])
    assert [(p.serial, p.kind, p.device) for p in found] == [
        ('B43A45B7BFBC', discover.KIND_STOCK, '/dev/ttyACM0'),
        ('B43A45B7BF01', discover.KIND_JTAG, '/dev/ttyACM1'),
    ]


def test_find_by_serial_any_format():
    found = discover.find(serial='b4:3a:45:b7:bf:bc', ports=[STOCK, JTAG])
    assert ['/dev/ttyACM0'] == [p.device for p in found]


def test_find_ignores_unknown_espressif_product():
    weird = _port('/dev/ttyACM3', 'AABBCCDDEEFF', 'Some Board')
    assert [] == discover.find(ports=[weird])


def test_wait_returns_when_kind_appears():
    # The stock CDC disappears, then the hub returns on USB-Serial-JTAG.
    rom = _port('/dev/ttyACM1', 'B4:3A:45:B7:BF:BC', 'USB JTAG/serial debug unit')
    calls = iter([[STOCK], [], [rom]])
    clock = iter(range(100))
    p = discover.wait('B43A45B7BFBC', discover.KIND_JTAG, timeout=10.0,
                      lister=lambda: next(calls), sleep=lambda s: None,
                      clock=lambda: next(clock))
    assert p.kind == discover.KIND_JTAG
    assert p.device == '/dev/ttyACM1'


def test_wait_timeout():
    clock = iter(range(100))
    with pytest.raises(TimeoutError):
        discover.wait('B43A45B7BFBC', discover.KIND_JTAG, timeout=3.0,
                      lister=lambda: [STOCK], sleep=lambda s: None,
                      clock=lambda: next(clock))


def test_find_without_product_string():
    # pyserial on Windows never sets product: the firmware is unknown until
    # probed (both firmwares and the ROM share VID, PID and serial number).
    win = _port('COM7', 'B43A45B7BFBC', None)
    found = discover.find(ports=[win, OTHER])
    assert [(p.device, p.serial, p.kind) for p in found] == [
        ('COM7', 'B43A45B7BFBC', discover.KIND_UNKNOWN)]
    assert [] == discover.find(serial='AABBCCDDEEFF', ports=[win])


def test_wait_accepts_unknown_kind():
    win = _port('COM7', 'B43A45B7BFBC', None)
    p = discover.wait('B43A45B7BFBC', discover.KIND_STOCK, timeout=1.0,
                      lister=lambda: [win], sleep=lambda s: None)
    assert p.device == 'COM7'
