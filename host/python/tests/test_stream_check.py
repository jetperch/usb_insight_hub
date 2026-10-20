# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import time

from usb_insight_hub import sim, stream_check
from usb_insight_hub.client import Hub


def _run(s, emit, rate=10, duration=1.0):
    hub = Hub('sim', timeout=0.5, serial_factory=s.serial_factory)
    try:
        def sleep(seconds):
            emit(s)
            time.sleep(0.05)  # let the reader thread deliver
        return stream_check.run(hub, rate, duration, sleep=sleep, interval=0.1)
    finally:
        hub.close()


def test_clean_stream():
    s = sim.SimHub()
    r = _run(s, lambda s: s.emit_status())
    assert r.received == 10
    assert r.gaps == 0
    assert r.ok
    assert s.rate_hz == 0  # stopped at the end
    assert 'received 10' in r.summary()


def test_gaps_are_counted():
    s = sim.SimHub()

    def emit(s):
        s.emit_status()
        s.status_n += 1  # one message lost per interval

    r = _run(s, emit)
    assert r.gaps == 9
    assert not r.ok


def test_too_few_messages_fails():
    s = sim.SimHub()
    calls = []

    def emit(s):
        calls.append(1)
        if len(calls) % 2:
            s.emit_status()  # half the expected rate, no gaps in n

    r = _run(s, emit)
    assert r.received == 5
    assert r.gaps == 0
    assert not r.ok  # the rate check catches a slow hub


def test_events_and_fault_counts():
    s = sim.SimHub()

    def emit(s):
        s.emit_status()
        if s.status_n == 3:
            s.press_button(1)

    r = _run(s, emit)
    assert r.events == 1
