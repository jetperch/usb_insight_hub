# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import pytest

from usb_insight_hub import sim, stock
from usb_insight_hub.client import HubError


def _hub(**sim_kwargs):
    s = sim.StockSimHub(**sim_kwargs)
    return stock.StockHub('sim', serial_factory=s.serial_factory, settle=0.0), s


def test_info():
    hub, s = _hub()
    info = hub.info()
    assert info['fw'] == '1.0.0'
    assert info['stm8'] == 4
    assert info['serial'] == 'B43A45B7BFBC'
    assert info['meter'] is True


def test_status():
    hub, s = _hub()
    ports, aux = hub.status()
    assert [p.port for p in ports] == [1, 2, 3]
    assert ports[0].power and ports[0].mA == pytest.approx(176.7)


def test_set_verifies_by_read_back():
    hub, s = _hub()
    ports = hub.set_power(2, False)
    assert [(p.port, p.power) for p in ports] == [(2, False)]
    assert s.channels[2]['powerEn'] is False
    assert s.sent[-1] == {'action': 'get', 'params': ['CH2']}


def test_set_retries_a_dropped_command():
    hub, s = _hub(drop_sets=1)
    assert hub.set_power(1, False)[0].power is False
    assert s.set_count == 2


def test_set_gives_up():
    hub, s = _hub(drop_sets=10)
    with pytest.raises(HubError) as ex:
        hub.set_power(1, False)
    assert ex.value.code == 'not_applied'


def test_set_raw_does_not_verify():
    hub, s = _hub(drop_sets=1)
    hub.set_raw(3, power=False)
    assert s.channels[3]['powerEn'] is True  # dropped, and nobody checked
    assert s.set_count == 1


def test_set_all_and_data():
    hub, s = _hub()
    ports = hub.set('all', power=True, data=False)
    assert [(p.port, p.data) for p in ports] == [(1, False), (2, False), (3, False)]
    assert s.sent[-2] == {'action': 'set', 'params': {
        'CH1': {'powerEn': 'true', 'dataEn': 'false'},
        'CH2': {'powerEn': 'true', 'dataEn': 'false'},
        'CH3': {'powerEn': 'true', 'dataEn': 'false'}}}


def test_non_json_lines_are_skipped():
    hub, s = _hub()
    s.noise = [b'I (123) boot: something\r\n']
    assert hub.info()['fw'] == '1.0.0'


def test_timeout():
    hub, s = _hub()
    s.mute = True
    hub.timeout = 0.1
    with pytest.raises(TimeoutError):
        hub.info()


def test_enter_bootloader_uses_touch(monkeypatch):
    hub, s = _hub()
    calls = []
    monkeypatch.setattr(stock.esp, 'touch_1200', lambda device: calls.append(device))
    hub.enter_bootloader()
    assert calls == ['sim']
