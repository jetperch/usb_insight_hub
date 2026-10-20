# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import threading

import pytest

from usb_insight_hub import client, protocol, sim
from usb_insight_hub.client import Hub, HubError


@pytest.fixture
def hub_and_sim():
    s = sim.SimHub()
    hub = Hub('sim', timeout=0.5, retries=2, serial_factory=s.serial_factory)
    yield hub, s
    hub.close()


def test_ping_and_info(hub_and_sim):
    hub, s = hub_and_sim
    hub.ping()
    info = hub.info()
    assert info['proto'] == protocol.PROTOCOL_VERSION
    assert info['serial'] == s.serial


def test_status_reports_all_ports(hub_and_sim):
    hub, s = hub_and_sim
    ports, aux = hub.status()
    assert [p.port for p in ports] == [1, 2, 3]
    assert all(p.power and p.data for p in ports)
    assert aux['usb3'] is True


def test_set_power_returns_measured_state(hub_and_sim):
    hub, s = hub_and_sim
    ports = hub.set_power(2, False)
    assert [(p.port, p.power) for p in ports] == [(2, False)]
    assert s.ports[2].power is False
    ports = hub.set(port='all', power=True, data=False)
    assert [(p.power, p.data) for p in ports] == [(True, False)] * 3


def test_set_rejects_bad_args(hub_and_sim):
    hub, s = hub_and_sim
    with pytest.raises(ValueError):
        hub.set(port=4, power=True)
    with pytest.raises(ValueError):
        hub.set(port=1)


def test_error_response_raises(hub_and_sim):
    hub, s = hub_and_sim
    s.fail_next_set = 'not_applied'
    with pytest.raises(HubError) as ex:
        hub.set_power(1, False)
    assert ex.value.code == 'not_applied'


def test_lost_response_is_retried(hub_and_sim):
    hub, s = hub_and_sim
    s.drop_responses = 1
    assert hub.set_power(3, False)[0].power is False
    assert s.requests_seen == 2  # idempotent retry with a new seq


def test_timeout_after_retries(hub_and_sim):
    hub, s = hub_and_sim
    s.drop_responses = 10
    with pytest.raises(TimeoutError):
        hub.ping()


def test_noise_and_corrupt_lines_are_skipped(hub_and_sim):
    hub, s = hub_and_sim
    s.inject(b'ESP-ROM:esp32s3-20210327\r\n')
    s.inject(b'{"type":"event"}*00000000\n')
    hub.ping()
    assert hub.counters['skipped'] >= 1
    assert hub.counters['crc'] >= 1


def test_stream_and_events(hub_and_sim):
    hub, s = hub_and_sim
    got = []
    done = threading.Event()

    def on_status(msg):
        got.append(msg)
        if len(got) >= 3:
            done.set()

    hub.on_status(on_status)
    hub.stream(20)
    for _ in range(3):
        s.emit_status()
    assert done.wait(1.0)
    assert [m['n'] for m in got[:3]] == [0, 1, 2]
    assert isinstance(got[0]['ports'][0], protocol.PortState)

    events = []
    ev = threading.Event()
    hub.on_event(lambda m: (events.append(m), ev.set()))
    s.press_button(1)
    assert ev.wait(1.0)
    assert events[0]['event'] == 'button' and events[0]['port'] == 1


def test_stream_gap_counter(hub_and_sim):
    hub, s = hub_and_sim
    done = threading.Event()
    hub.on_status(lambda m: done.set() if m['n'] == 3 else None)
    s.emit_status()
    s.status_n += 2  # two status messages lost on the hub side
    s.emit_status()
    assert done.wait(1.0)
    assert hub.counters['status_gap'] == 2


def test_enter_bootloader_and_reset(hub_and_sim):
    hub, s = hub_and_sim
    hub.enter_bootloader()
    assert s.last_op == 'bootloader'
    hub.reset()
    assert s.last_op == 'reset'


def test_context_manager_closes():
    s = sim.SimHub()
    with Hub('sim', serial_factory=s.serial_factory) as hub:
        hub.ping()
    assert s.closed


def test_open_by_serial(monkeypatch):
    s = sim.SimHub()
    from usb_insight_hub.discover import HubPort, KIND_JTAG
    monkeypatch.setattr(client.discover, 'find',
                        lambda serial=None: [HubPort('/dev/sim', s.serial, KIND_JTAG)])
    with client.open_hub(s.serial, serial_factory=s.serial_factory) as hub:
        assert hub.info()['serial'] == s.serial


def test_open_flushes_hub_line_buffer():
    # Bytes left in the hub's line assembler (for example esptool's binary
    # protocol after a flash) would corrupt the first request.
    s = sim.SimHub()
    writes = []
    factory = s.serial_factory

    def spy_factory(*args, **kwargs):
        ser = factory(*args, **kwargs)
        orig = ser.write
        ser.write = lambda data: (writes.append(bytes(data)), orig(data))[1]
        return ser

    with Hub('sim', timeout=0.5, serial_factory=spy_factory) as hub:
        hub.ping()
        assert hub.counters['errors'] == 0
    assert writes[0] == b'\n'


def test_responses_do_not_wait_for_the_read_timeout():
    # pyserial's read(n) blocks until n bytes or its timeout; the reader
    # must not wait out the timeout on every response.
    import time
    s = sim.SimHub()
    with Hub('sim', timeout=0.5, serial_factory=s.serial_factory) as hub:
        hub.ping()
        t0 = time.perf_counter()
        for _ in range(20):
            hub.ping()
        assert time.perf_counter() - t0 < 0.5   # 20 x 50 ms read timeout = 1 s
