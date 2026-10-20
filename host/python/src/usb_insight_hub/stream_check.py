# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Status stream continuity check: count messages, gaps in ``n`` and events."""

from dataclasses import dataclass
import time

_RATE_TOLERANCE = 0.98   # received / expected, allowing for start and stop edges


@dataclass
class Result:
    rate_hz: int
    duration_s: float
    received: int = 0
    gaps: int = 0
    events: int = 0
    first_n: int = None
    last_n: int = None

    @property
    def expected(self):
        return int(self.rate_hz * self.duration_s)

    @property
    def ok(self):
        return self.gaps == 0 and self.received >= int(self.expected * _RATE_TOLERANCE)

    def summary(self):
        return (f'received {self.received} of {self.expected} expected '
                f'({self.rate_hz} Hz for {self.duration_s:g} s), {self.gaps} lost '
                f'(gaps in n), {self.events} events')


def run(hub, rate_hz, duration_s, sleep=time.sleep, interval=1.0, progress=None):
    """Stream at ``rate_hz`` for ``duration_s`` and check every message arrived.

    :param hub: a client.Hub.
    :param progress: optional callable(Result) every ``interval`` seconds.
    """
    r = Result(rate_hz, duration_s)

    def on_status(msg):
        n = msg['n']
        if r.last_n is not None and n > r.last_n + 1:
            r.gaps += n - r.last_n - 1
        if r.first_n is None:
            r.first_n = n
        r.last_n = n
        r.received += 1

    def on_event(msg):
        r.events += 1

    hub.on_status(on_status)
    hub.on_event(on_event)
    hub.stream(rate_hz)
    try:
        remaining = duration_s
        while remaining > 1e-9:
            step = min(interval, remaining)
            sleep(step)
            remaining -= step
            if progress:
                progress(r)
    finally:
        hub.stream(0)
    return r
