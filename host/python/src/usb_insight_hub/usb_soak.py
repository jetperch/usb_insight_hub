# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""USB-level power-cycle soak: the device on a port must really drop and return.

``soak.py`` checks the hub's read-back state.  This test also watches the
host's USB device list: after each power-off the device on the port must
disconnect, and after each power-on it must enumerate again.  Optional
watched devices (on other ports) must never disappear.
"""

from dataclasses import dataclass, field
import os
import time

from .client import HubError

_POLL_S = 0.05


@dataclass(frozen=True)
class UsbDevice:
    vid: int
    pid: int
    serial: str = None   # None matches any serial number

    def matches(self, entry):
        vid, pid, serial = entry
        return vid == self.vid and pid == self.pid and self.serial in (None, serial)

    def __str__(self):
        s = f'{self.vid:04x}:{self.pid:04x}'
        return s if self.serial is None else f'{s}:{self.serial}'


def parse_device(spec):
    """Parse ``VID:PID[:SERIAL]`` (hex VID and PID)."""
    parts = spec.split(':')
    if len(parts) not in (2, 3):
        raise ValueError(f'invalid USB device {spec!r}: use VID:PID[:SERIAL]')
    try:
        vid, pid = int(parts[0], 16), int(parts[1], 16)
    except ValueError:
        raise ValueError(f'invalid USB device {spec!r}: VID and PID are hex') from None
    return UsbDevice(vid, pid, parts[2] if len(parts) == 3 else None)


def list_linux(root='/sys/bus/usb/devices'):
    """Return ``(vid, pid, serial)`` for each USB device, from sysfs."""
    def read(path):
        try:
            with open(path) as f:
                return f.read().strip()
        except OSError:
            return None
    found = []
    for name in os.listdir(root):
        if ':' in name:
            continue   # an interface, not a device
        vid = read(os.path.join(root, name, 'idVendor'))
        pid = read(os.path.join(root, name, 'idProduct'))
        if vid and pid:
            found.append((int(vid, 16), int(pid, 16),
                          read(os.path.join(root, name, 'serial')) or ''))
    return found


@dataclass
class Result:
    cycles: int = 0
    failures: list = field(default_factory=list)
    disconnect_s: list = field(default_factory=list)
    reconnect_s: list = field(default_factory=list)
    stopped: bool = False

    @property
    def ok(self):
        return not self.failures

    @staticmethod
    def _stats(values):
        if not values:
            return 'n/a'
        v = sorted(values)
        return f'median {v[len(v) // 2]:.2f} s, max {v[-1]:.2f} s'

    def summary(self):
        text = (f'{self.cycles} cycles, {len(self.failures)} failures; '
                f'disconnect {self._stats(self.disconnect_s)}; '
                f'reconnect {self._stats(self.reconnect_s)}')
        if self.stopped:
            text += '; stopped after consecutive failures'
        return text


def run(hub, port, device, cycles, off_s=2.0, on_s=3.0, off_timeout=5.0, on_timeout=20.0,
        watch=(), lister=list_linux, clock=time.monotonic, sleep=time.sleep,
        progress=None, max_consecutive_failures=3):
    """Power-cycle ``port`` and check that ``device`` disconnects and reconnects.

    :param hub: a client.Hub or stock.StockHub.
    :param device: the UsbDevice powered by ``port``.
    :param off_s: minimum time the port stays off.
    :param on_s: time the device must stay connected after it enumerates.
    :param off_timeout: seconds allowed for the device to disconnect.
    :param on_timeout: seconds allowed for the device to enumerate.
    :param watch: UsbDevices on other ports that must stay connected.
    :param progress: optional callable(cycle, Result) after each cycle.
    :return: the Result.  The port is left on.
    """
    r = Result()
    lost = set()

    def present():
        entries = lister()
        for w in watch:
            here = any(w.matches(e) for e in entries)
            if not here and w not in lost:
                lost.add(w)
                r.failures.append(f'cycle {r.cycles}: watched {w} disappeared')
            elif here:
                lost.discard(w)
        return any(device.matches(e) for e in entries)

    def wait(want, timeout):
        """Return the seconds until present() == want, or None on timeout."""
        t0 = clock()
        while True:
            if present() == want:
                return clock() - t0
            if clock() - t0 >= timeout:
                return None
            sleep(_POLL_S)

    def hold(seconds):
        """Return False if the device drops within ``seconds``."""
        t0 = clock()
        while clock() - t0 < seconds:
            if not present():
                return False
            sleep(_POLL_S)
        return True

    def set_power(on):
        """Set and check the read-back; return an error string or None."""
        try:
            states = hub.set_power(port, on)
        except (HubError, TimeoutError, OSError) as ex:
            return f'set power {"on" if on else "off"} failed: {ex}'
        if any(s.port == port and s.power != on for s in states):
            return f'hub read back power {"off" if on else "on"} after a power {"on" if on else "off"} command'
        return None

    def recover():
        """Leave the port on with the device present, if possible."""
        set_power(False)
        sleep(off_s)
        set_power(True)
        wait(True, on_timeout)

    if wait(True, on_timeout) is None:
        r.failures.append(f'{device} not present at start')
        return r

    try:
        consecutive = 0
        for cycle in range(cycles):
            r.cycles = cycle + 1
            failure = set_power(False)
            if failure is None:
                t_off = clock()
                dt = wait(False, off_timeout)
                if dt is None:
                    failure = f'{device} still present {off_timeout:g} s after power off'
                else:
                    r.disconnect_s.append(dt)
                    sleep(max(0.0, off_s - (clock() - t_off)))
                    failure = set_power(True)
                    if failure is None:
                        dt = wait(True, on_timeout)
                        if dt is None:
                            failure = f'{device} did not reconnect within {on_timeout:g} s'
                        else:
                            r.reconnect_s.append(dt)
                            if not hold(on_s):
                                failure = (f'{device} dropped within {on_s:g} s '
                                           f'after reconnecting')
            if failure is None:
                consecutive = 0
            else:
                r.failures.append(f'cycle {r.cycles}: {failure}')
                consecutive += 1
                recover()
                if consecutive >= max_consecutive_failures:
                    r.stopped = True
                    break
            if progress:
                progress(cycle, r)
    finally:
        set_power(True)   # always leave the port on, also on Ctrl-C
    return r
