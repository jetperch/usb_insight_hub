# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import pytest

from usb_insight_hub import update_loop
from usb_insight_hub.protocol import PortState


class World:
    """A simulated hub: which firmware runs, plus call records."""

    def __init__(self, firmware='stock'):
        self.firmware = firmware
        self.calls = []
        self.custom_fw = '0.1.0'
        self.fail_flash = False
        self.mv = {'stock': 4800, 'custom': 4805}

    def tool(self, serial):
        self.calls.append(('bootloader', self.firmware))
        return Tool(self)

    def wait(self, serial, kind, timeout=None):
        self.calls.append(('wait', kind))
        return None

    def open(self, serial, **kwargs):
        return FakeHub(self)


class Tool:

    def __init__(self, world):
        self.world = world

    def flash_firmware(self, *files):
        if self.world.fail_flash:
            raise RuntimeError('flash failed')
        self.world.calls.append(('flash', files))
        self.world.firmware = 'custom'

    def restore(self, path):
        self.world.calls.append(('restore', path))
        self.world.firmware = 'stock'


class FakeHub:

    def __init__(self, world):
        self.world = world
        self.closed = False

    def info(self):
        if self.world.firmware == 'stock':
            return {'fw': '1.0.0', 'proto': None}
        return {'fw': self.world.custom_fw, 'proto': 1, 'git': 'abc1234'}

    def status(self):
        mv = self.world.mv[self.world.firmware]
        return [PortState(p, True, True, False, mv, 10.0 * p) for p in (1, 2, 3)], {}

    def close(self):
        self.closed = True


def run(world, cycles=2, **kwargs):
    return update_loop.run('B43A45B7BFBC', ['bl.bin', 'pt.bin', 'app.bin'], 'backup.bin',
                           cycles=cycles, expect_fw='0.1.0', tool_factory=world.tool,
                           waiter=world.wait, opener=world.open,
                           sleep=kwargs.pop('sleep', lambda s: None), **kwargs)


def test_cycles_update_then_restore():
    world = World()
    results = run(world, cycles=2)
    assert len(results) == 2
    assert all(r.ok for r in results)
    assert world.firmware == 'stock'  # ends where a stock hub started
    ops = [c[0] for c in world.calls if c[0] in ('flash', 'restore')]
    assert ops == ['flash', 'restore', 'flash', 'restore']
    assert ('wait', 'jtag') in world.calls and ('wait', 'stock') in world.calls
    r = results[0]
    assert r.custom_info['fw'] == '0.1.0'
    assert r.stock_info['fw'] == '1.0.0'
    assert r.max_delta_mv() == 5
    assert r.max_delta_ma() == 0.0


def test_wrong_version_fails_the_cycle():
    world = World()
    world.custom_fw = '0.0.9'
    results = run(world, cycles=1)
    assert not results[0].ok
    assert 'version' in results[0].error
    assert world.firmware == 'stock'  # still restored


def test_error_stops_the_loop():
    world = World()
    world.fail_flash = True
    results = run(world, cycles=3)
    assert len(results) == 1
    assert 'flash failed' in results[0].error


def test_open_retries_until_the_firmware_answers():
    world = World()
    attempts = []

    def opener(serial, **kwargs):
        attempts.append(1)
        if len(attempts) < 3:
            raise TimeoutError('booting')
        return FakeHub(world)

    results = update_loop.run('B43A45B7BFBC', ['a', 'b', 'c'], 'backup.bin', cycles=1,
                              expect_fw='0.1.0', tool_factory=world.tool,
                              waiter=world.wait, opener=opener, sleep=lambda s: None)
    assert results[0].ok
    assert len(attempts) >= 4  # custom needed 3, stock 1


def test_summary():
    world = World()
    results = run(world, cycles=2)
    text = update_loop.summary(results)
    assert '2 cycles, 2 passed' in text
    assert 'max |dV| 5 mV' in text


def test_timings_recorded():
    world = World()
    t = iter(range(100))
    results = run(world, cycles=1, clock=lambda: next(t))
    r = results[0]
    assert r.update_s > 0 and r.restore_s > 0


def test_meters_read_after_settle():
    # The stock firmware's moving average starts from zero after boot, so
    # readings taken at once can be half the real value (seen 2026-10-20).
    world = World()
    sleeps = []
    update_loop.run('B43A45B7BFBC', ['a', 'b', 'c'], 'backup.bin', cycles=1,
                    expect_fw='0.1.0', tool_factory=world.tool, waiter=world.wait,
                    opener=world.open, sleep=sleeps.append, settle_s=2.0)
    assert sleeps.count(2.0) == 2  # once per firmware
