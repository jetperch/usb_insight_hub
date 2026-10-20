# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Power-toggle soak: measure dropped commands, errors and latency."""

from dataclasses import dataclass, field
import time

from .client import HubError


@dataclass
class Result:
    commands: int = 0
    drops: int = 0
    errors: int = 0
    timeouts: int = 0
    latency_s: list = field(default_factory=list)

    @property
    def ok(self):
        return not (self.drops or self.errors or self.timeouts)

    @property
    def drop_rate(self):
        return self.drops / self.commands if self.commands else 0.0

    def _pct(self, q):
        if not self.latency_s:
            return 0.0
        v = sorted(self.latency_s)
        return v[min(len(v) - 1, int(q * len(v)))] * 1000

    def summary(self):
        return (f'{self.commands} commands, {self.drops} dropped ({self.drop_rate:.3%}), '
                f'{self.errors} errors, {self.timeouts} timeouts; latency p50 '
                f'{self._pct(0.5):.1f} ms, p99 {self._pct(0.99):.1f} ms, '
                f'max {self._pct(1.0):.1f} ms')


def run(hub, cycles, ports=(1, 2, 3), dwell=0.05, settle=0.25, raw=False,
        progress=None):
    """Toggle each port off then on ``cycles`` times; ends with ports on.

    After each batch, waits ``settle`` seconds and reads the ports back; a
    port in the wrong state is a dropped command.

    :param hub: a client.Hub or stock.StockHub.
    :param raw: send unverified commands (StockHub.set_raw) to measure the
        stock firmware's drop rate.
    :param progress: optional callable(cycle, result) after each cycle.
    """
    result = Result()
    for cycle in range(cycles):
        for state in (False, True):
            failed = set()
            for port in ports:
                result.commands += 1
                t0 = time.perf_counter()
                try:
                    if raw:
                        hub.set_raw(port, power=state)
                    else:
                        reported = hub.set_power(port, state)
                        if any(p.power != state for p in reported):
                            result.drops += 1
                            failed.add(port)
                except HubError:
                    result.errors += 1
                    failed.add(port)
                except TimeoutError:
                    result.timeouts += 1
                    failed.add(port)
                result.latency_s.append(time.perf_counter() - t0)
            if settle:
                time.sleep(settle)
            actual, _ = hub.status()
            for p in actual:
                if p.port in ports and p.port not in failed and p.power != state:
                    result.drops += 1
            if dwell:
                time.sleep(dwell)
        if progress:
            progress(cycle, result)
    if raw:
        hub.set('all', power=True)  # verified: unverified commands may have dropped
    return result
