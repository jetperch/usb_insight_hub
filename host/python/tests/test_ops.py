# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import pytest

from usb_insight_hub import discover, ops
from usb_insight_hub.discover import HubPort

SERIAL = 'B43A45B7BFBC'
STOCK = HubPort('/dev/ttyACM0', SERIAL, discover.KIND_STOCK)
JTAG = HubPort('/dev/ttyACM1', SERIAL, discover.KIND_JTAG)
UNKNOWN = HubPort('COM7', SERIAL, discover.KIND_UNKNOWN)   # Windows: no product string


class Env:
    """Fakes for discovery, the 1200-baud touch, the firmware op and esptool."""

    def __init__(self, before, after=JTAG, macs=(SERIAL,), firmware=False, stock=False,
                 later=None):
        self.before = before
        self.after = after
        self.macs = list(macs)
        self.firmware = firmware
        self.stock = stock
        self.later = list(later or [])   # ports for finds after the first
        self.events = []
        self.finds = 0

    def find(self, serial):
        assert serial == SERIAL
        self.finds += 1
        if self.finds > 1 and self.later:
            return [self.later.pop(0)]
        return [self.before] if self.before else []

    def probe_stock(self, device):
        self.events.append(('stock?', device))
        return self.stock

    def wait(self, serial, kind, timeout):
        self.events.append(('wait', kind))
        return self.after

    def touch(self, device):
        self.events.append(('touch', device))

    def request_bootloader(self, device):
        """Return True if Jetperch firmware answered and accepted the op."""
        self.events.append(('op', device))
        return self.firmware

    def read_mac(self, device):
        self.events.append(('mac', device))
        mac = self.macs.pop(0)
        if isinstance(mac, Exception):
            raise mac
        return mac

    def run(self, **kwargs):
        return ops.enter_bootloader(
            SERIAL, finder=self.find, waiter=self.wait, toucher=self.touch,
            request_bootloader=self.request_bootloader, mac_reader=self.read_mac,
            stock_prober=self.probe_stock, sleep=lambda s: None, **kwargs)


def test_stock_uses_1200_baud_touch():
    env = Env(STOCK)
    assert '/dev/ttyACM1' == env.run()
    assert env.events == [('touch', '/dev/ttyACM0'), ('wait', discover.KIND_JTAG),
                          ('mac', '/dev/ttyACM1')]


def test_jetperch_firmware_uses_op():
    env = Env(JTAG, firmware=True)
    env.run()
    assert env.events[0] == ('op', '/dev/ttyACM1')
    assert ('wait', discover.KIND_JTAG) in env.events
    assert env.events[-1] == ('mac', '/dev/ttyACM1')


def test_already_in_rom_bootloader():
    env = Env(JTAG, firmware=False)
    assert '/dev/ttyACM1' == env.run()
    assert env.events == [('op', '/dev/ttyACM1'), ('mac', '/dev/ttyACM1')]


def test_rom_retries_until_it_answers():
    env = Env(STOCK, macs=[ops.esp.EsptoolError('sync'), SERIAL])
    env.run()
    assert 2 == sum(1 for e in env.events if e[0] == 'mac')


def test_wrong_chip_is_rejected():
    env = Env(STOCK, macs=['AABBCCDDEEFF'])
    with pytest.raises(RuntimeError, match='AABBCCDDEEFF'):
        env.run()


def test_not_found():
    env = Env(None)
    with pytest.raises(LookupError):
        env.run()


def test_rom_never_answers():
    err = ops.esp.EsptoolError('sync')
    env = Env(STOCK, macs=[err] * 10)
    with pytest.raises(ops.esp.EsptoolError):
        env.run(attempts=3)


def test_unknown_jetperch_firmware_uses_op():
    env = Env(UNKNOWN, firmware=True)
    assert 'COM7' == env.run()
    assert env.events[0] == ('op', 'COM7')
    assert ('stock?', 'COM7') not in env.events


def test_unknown_stock_uses_touch_and_finds_the_new_port():
    # The ROM bootloader may come back on another COM port.
    rom = HubPort('COM9', SERIAL, discover.KIND_UNKNOWN)
    env = Env(UNKNOWN, stock=True, macs=[ops.esp.EsptoolError('gone'), SERIAL],
              later=[UNKNOWN, rom])
    assert 'COM9' == env.run()
    assert env.events[:3] == [('op', 'COM7'), ('stock?', 'COM7'), ('touch', 'COM7')]
    assert ('mac', 'COM9') in env.events


def test_unknown_already_in_rom_bootloader():
    env = Env(UNKNOWN)
    assert 'COM7' == env.run()
    assert ('touch', 'COM7') not in env.events


def test_open_any_probes_unknown(monkeypatch):
    from usb_insight_hub import client, stock
    made = []

    class FakeHub:
        answers = True

        def __init__(self, device, **kw):
            made.append(('v1', device))

        def ping(self):
            if not FakeHub.answers:
                raise TimeoutError('no v1')

        def close(self):
            made.append(('v1 closed',))

    monkeypatch.setattr(client, 'Hub', FakeHub)
    monkeypatch.setattr(stock, 'StockHub', lambda device, **kw: made.append(('stock', device)))
    monkeypatch.setattr(ops.discover, 'find', lambda serial=None: [UNKNOWN])
    assert isinstance(ops.open_any(SERIAL), FakeHub)
    FakeHub.answers = False
    ops.open_any(SERIAL)
    assert made == [('v1', 'COM7'), ('v1', 'COM7'), ('v1 closed',), ('stock', 'COM7')]


def test_open_any_selects_backend(monkeypatch):
    from usb_insight_hub import client, stock
    made = []
    monkeypatch.setattr(stock, 'StockHub', lambda device, **kw: made.append(('stock', device)))
    monkeypatch.setattr(client, 'Hub', lambda device, **kw: made.append(('v1', device)))
    monkeypatch.setattr(ops.discover, 'find', lambda serial=None: [STOCK])
    ops.open_any(SERIAL)
    monkeypatch.setattr(ops.discover, 'find', lambda serial=None: [JTAG])
    ops.open_any(SERIAL)
    monkeypatch.setattr(ops.discover, 'find', lambda serial=None: [])
    with pytest.raises(LookupError):
        ops.open_any(SERIAL)
    assert made == [('stock', '/dev/ttyACM0'), ('v1', '/dev/ttyACM1')]
