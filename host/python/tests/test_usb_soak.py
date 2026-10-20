# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import pytest

from usb_insight_hub import usb_soak
from usb_insight_hub.protocol import PortState

JS320 = usb_soak.parse_device('16d0:135a:8W2A')
JS220 = usb_soak.parse_device('16d0:10ba:002122')


class World:
    """A hub port powering a USB device that drops and enumerates with delays."""

    def __init__(self, port=3, drop_s=0.2, enum_s=1.5):
        self.t = 0.0
        self.port = port
        self.power = True
        self.changed = 0.0
        self.drop_s = drop_s
        self.enum_s = enum_s
        self.ignore_sets = 0     # the hub drops the next N sets
        self.never_drop = False  # the device stays (back-powered)
        self.never_enum = 0      # the device fails to enumerate the next N times
        self.watch_gone = False
        self.sets = []

    # clock
    def clock(self):
        return self.t

    def sleep(self, s):
        self.t += s

    # hub
    def set_power(self, port, on):
        assert port == self.port
        self.sets.append(on)
        if self.ignore_sets:
            self.ignore_sets -= 1
        elif on != self.power:
            self.power = on
            self.changed = self.t
            if on and self.never_enum:
                self.never_enum -= 1
                self.changed = float('inf')
        return [PortState(port, self.power, True, False, 5000 if self.power else 0, 0.0)]

    # USB
    def present(self):
        dt = self.t - self.changed
        if self.power:
            return dt >= self.enum_s
        return self.never_drop or dt < self.drop_s

    def lister(self):
        found = [(0x16D0, 0x10BA, '002122')] if not self.watch_gone else []
        if self.present():
            found.append((0x16D0, 0x135A, '8W2A'))
        return found


def run(world, cycles=3, **kwargs):
    return usb_soak.run(world, world.port, JS320, cycles, off_s=1.0, off_timeout=3.0,
                        on_timeout=5.0, lister=world.lister, clock=world.clock,
                        sleep=world.sleep, **kwargs)


def test_parse_device():
    assert (0x16D0, 0x135A, '8W2A') == (JS320.vid, JS320.pid, JS320.serial)
    d = usb_soak.parse_device('16D0:135A')
    assert d.serial is None
    assert d.matches((0x16D0, 0x135A, 'anything'))
    assert not JS320.matches((0x16D0, 0x135A, '8W2B'))
    for bad in ('16d0', 'xyz:135a', '16d0:135a:a:b'):
        with pytest.raises(ValueError):
            usb_soak.parse_device(bad)


def test_clean_cycles():
    world = World()
    r = run(world, cycles=3)
    assert r.ok, r.failures
    assert r.cycles == 3
    assert len(r.disconnect_s) == 3 and len(r.reconnect_s) == 3
    assert all(0.15 <= t <= 0.3 for t in r.disconnect_s)
    assert all(1.4 <= t <= 1.7 for t in r.reconnect_s)
    assert world.power is True
    assert world.sets == [False, True] * 3 + [True]   # always leaves the port on
    assert '3 cycles, 0 failures' in r.summary()


def test_device_must_be_present_at_start():
    world = World()
    world.changed = 0.0
    world.power = True
    world.enum_s = 100.0     # never enumerates
    r = run(world, cycles=2)
    assert not r.ok
    assert r.cycles == 0
    assert 'not present at start' in r.failures[0]


def test_device_that_never_drops():
    world = World()
    world.never_drop = True
    r = run(world, cycles=1)
    assert not r.ok
    assert 'still present' in r.failures[0]
    assert world.power is True   # recovered: the port ends on


def test_dropped_hub_command_is_detected():
    # A hub that replies but ignores the set (the stock firmware bug).
    world = World()
    world.ignore_sets = 1
    r = run(world, cycles=1)
    assert not r.ok
    assert 'read back' in r.failures[0]


def test_failed_enumeration_then_recovery():
    world = World()
    world.never_enum = 1
    r = run(world, cycles=3)
    assert len(r.failures) == 1
    assert 'did not reconnect' in r.failures[0]
    assert r.cycles == 3                       # continued after recovery
    assert world.power is True


def test_device_must_stay_connected_while_on():
    # A device that enumerates, then drops (a reset or re-enumeration).
    world = World()
    real_present = world.present

    def flaky():
        dt = world.t - world.changed
        return real_present() and not (world.power and 2.0 <= dt < 2.5)
    world.present = flaky
    r = run(world, cycles=1, on_s=3.0)
    assert not r.ok
    assert 'dropped' in r.failures[0]


def test_on_hold_time():
    world = World()
    r = run(world, cycles=2, on_s=3.0)
    assert r.ok, r.failures
    assert world.t >= 2 * (1.0 + 3.0)


def test_watched_device_lost():
    world = World()
    calls = []

    def lister():
        calls.append(1)
        if len(calls) == 20:
            world.watch_gone = True
        return world.lister()

    r = usb_soak.run(world, 3, JS320, 2, off_s=1.0, off_timeout=3.0, on_timeout=5.0,
                     watch=[JS220], lister=lister, clock=world.clock, sleep=world.sleep)
    assert any('16d0:10ba:002122 disappeared' in f for f in r.failures)


def test_stops_after_consecutive_failures():
    world = World()
    world.never_drop = True
    r = run(world, cycles=50, max_consecutive_failures=3)
    assert r.cycles == 3
    assert 'stopped' in r.summary()


def test_linux_lister(tmp_path):
    def dev(name, vid, pid, serial=None):
        d = tmp_path / name
        d.mkdir()
        (d / 'idVendor').write_text(vid + '\n')
        (d / 'idProduct').write_text(pid + '\n')
        if serial is not None:
            (d / 'serial').write_text(serial + '\n')
    dev('3-6.3', '16d0', '135a', '8W2A')
    dev('3-6', '2109', '2817')
    (tmp_path / '3-6.3:1.0').mkdir()   # an interface, not a device
    found = sorted(usb_soak.list_linux(str(tmp_path)))
    assert found == [(0x16D0, 0x135A, '8W2A'), (0x2109, 0x2817, '')]


def test_interrupt_leaves_the_port_on():
    world = World()

    def progress(cycle, r):
        world.power = False      # interrupted mid-cycle, port off
        raise KeyboardInterrupt()

    with pytest.raises(KeyboardInterrupt):
        run(world, cycles=5, progress=progress)
    assert world.power is True
