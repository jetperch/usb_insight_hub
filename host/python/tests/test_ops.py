# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import pytest

from usb_insight_hub import discover, ops
from usb_insight_hub.discover import HubPort

SERIAL = 'B43A45B7BFBC'
STOCK = HubPort('/dev/ttyACM0', SERIAL, discover.KIND_STOCK)
JTAG = HubPort('/dev/ttyACM1', SERIAL, discover.KIND_JTAG)


class Env:
    """Fakes for discovery, the 1200-baud touch, the firmware op and esptool."""

    def __init__(self, before, after=JTAG, macs=(SERIAL,), firmware=False):
        self.before = before
        self.after = after
        self.macs = list(macs)
        self.firmware = firmware
        self.events = []

    def find(self, serial):
        assert serial == SERIAL
        return [self.before] if self.before else []

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
            sleep=lambda s: None, **kwargs)


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
