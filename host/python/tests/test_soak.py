# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

from usb_insight_hub import sim, soak, stock
from usb_insight_hub.client import Hub


def test_soak_jetperch_firmware_counts_commands():
    s = sim.SimHub()
    with Hub('sim', timeout=0.5, serial_factory=s.serial_factory) as hub:
        result = soak.run(hub, cycles=5, ports=(1, 2), dwell=0.0, settle=0.0)
    assert result.commands == 5 * 2 * 2
    assert result.drops == 0
    assert result.errors == 0
    assert result.ok
    assert all(s.ports[p].power for p in (1, 2))  # ends with ports on
    assert len(result.latency_s) == result.commands


def test_soak_raw_stock_counts_drops():
    s = sim.StockSimHub(drop_every=7)
    hub = stock.StockHub('sim', serial_factory=s.serial_factory, settle=0.0)
    result = soak.run(hub, cycles=10, ports=(1, 2, 3), dwell=0.0, settle=0.0, raw=True)
    assert result.commands == 60
    assert result.drops == 60 // 7
    assert not result.ok
    assert 0 < result.drop_rate < 1


def test_soak_verified_stock_recovers_drops():
    s = sim.StockSimHub(drop_every=7)
    hub = stock.StockHub('sim', serial_factory=s.serial_factory, settle=0.0)
    result = soak.run(hub, cycles=10, ports=(1, 2, 3), dwell=0.0, settle=0.0)
    assert result.drops == 0
    assert result.ok


def test_soak_counts_errors_and_continues():
    s = sim.SimHub()
    with Hub('sim', timeout=0.5, serial_factory=s.serial_factory) as hub:
        s.fail_next_set = 'not_applied'
        result = soak.run(hub, cycles=2, ports=(1,), dwell=0.0, settle=0.0)
    assert result.errors == 1
    assert result.commands == 4
    assert not result.ok


def test_summary_text():
    r = soak.Result(commands=100, drops=2, errors=1, timeouts=0,
                    latency_s=[0.001] * 99 + [0.010])
    text = r.summary()
    assert '100 commands' in text and '2 dropped' in text and 'p99' in text


def test_raw_soak_restores_ports_on():
    s = sim.StockSimHub(drop_every=2)  # every other command dropped
    hub = stock.StockHub('sim', serial_factory=s.serial_factory, settle=0.0)
    soak.run(hub, cycles=3, ports=(1, 2, 3), dwell=0.0, settle=0.0, raw=True)
    assert all(ch['powerEn'] for ch in s.channels.values())
