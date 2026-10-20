# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Update and recovery loop: flash this firmware, verify, restore stock, verify.

Each cycle also reads the ports under both firmwares, so the meter
readings can be compared on the same hub and load.
"""

from dataclasses import dataclass, field
import time

from . import discover, esp, ops

_OPEN_TIMEOUT_S = 15.0


@dataclass
class CycleResult:
    cycle: int
    error: str = ''
    custom_info: dict = field(default_factory=dict)
    custom_ports: list = field(default_factory=list)
    stock_info: dict = field(default_factory=dict)
    stock_ports: list = field(default_factory=list)
    update_s: float = 0.0
    restore_s: float = 0.0

    @property
    def ok(self):
        return not self.error

    def _deltas(self, attr):
        return [abs(getattr(c, attr) - getattr(s, attr))
                for c, s in zip(self.custom_ports, self.stock_ports)]

    def max_delta_mv(self):
        return max(self._deltas('mV'), default=0)

    def max_delta_ma(self):
        return max(self._deltas('mA'), default=0.0)


def _default_tool(serial):
    return esp.Esptool(ops.enter_bootloader(serial))


def _read(serial, opener, sleep, clock):
    """Open the hub once its firmware answers; return (info, ports)."""
    deadline = clock() + _OPEN_TIMEOUT_S
    while True:
        try:
            hub = opener(serial, timeout=1.0)
            try:
                info = hub.info()
                ports, _ = hub.status()
                return info, ports
            finally:
                hub.close()
        except (OSError, LookupError, TimeoutError):
            if clock() >= deadline:
                raise
            sleep(0.5)


def run(serial, files, backup, cycles, expect_fw, tool_factory=_default_tool,
        waiter=discover.wait, opener=ops.open_any, sleep=time.sleep,
        clock=time.monotonic, progress=None):
    """Run the loop; stop at the first cycle with an error.

    :param files: the bootloader, partition table and app images.
    :param backup: the full-flash stock backup.
    :param expect_fw: the version this firmware must report.
    :param progress: optional callable(CycleResult) after each cycle.
    :return: the list of CycleResult.
    """
    results = []
    for cycle in range(cycles):
        r = CycleResult(cycle)
        results.append(r)
        try:
            t0 = clock()
            tool_factory(serial).flash_firmware(*files)
            waiter(serial, discover.KIND_JTAG, timeout=_OPEN_TIMEOUT_S)
            r.custom_info, r.custom_ports = _read(serial, opener, sleep, clock)
            r.update_s = clock() - t0
            if r.custom_info.get('fw') != expect_fw:
                r.error = f'version {r.custom_info.get("fw")}, expected {expect_fw}'
        except Exception as ex:  # noqa: BLE001 - report, then recover below
            r.error = f'update: {ex}'
        try:
            t0 = clock()
            tool_factory(serial).restore(backup)
            waiter(serial, discover.KIND_STOCK, timeout=_OPEN_TIMEOUT_S)
            r.stock_info, r.stock_ports = _read(serial, opener, sleep, clock)
            r.restore_s = clock() - t0
        except Exception as ex:  # noqa: BLE001
            r.error = (r.error + '; ' if r.error else '') + f'restore: {ex}'
        if progress:
            progress(r)
        if not r.ok:
            break
    return results


def summary(results):
    passed = sum(r.ok for r in results)
    lines = [f'{len(results)} cycles, {passed} passed']
    done = [r for r in results if r.custom_ports and r.stock_ports]
    if done:
        lines.append(f'max |dV| {max(r.max_delta_mv() for r in done)} mV, '
                     f'max |dI| {max(r.max_delta_ma() for r in done):.1f} mA '
                     f'(this firmware vs stock)')
        lines.append(f'update {max(r.update_s for r in done):.1f} s max, '
                     f'restore {max(r.restore_s for r in done):.1f} s max')
    for r in results:
        if r.error:
            lines.append(f'cycle {r.cycle}: {r.error}')
    return '\n'.join(lines)
