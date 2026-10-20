# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import pytest

from usb_insight_hub import cli, discover
from usb_insight_hub.discover import HubPort

SERIAL = 'B43A45B7BFBC'
HUB = HubPort('/dev/ttyACM0', SERIAL, discover.KIND_STOCK)


class FakeTool:
    instances = []

    def __init__(self, port, log=None):
        self.port = port
        self.calls = []
        FakeTool.instances.append(self)

    def __getattr__(self, name):
        def call(*args):
            self.calls.append((name, args))
        return call


@pytest.fixture
def env(monkeypatch):
    FakeTool.instances = []
    events = []
    monkeypatch.setattr(cli.discover, 'find', lambda serial=None: [HUB])
    monkeypatch.setattr(cli.ops, 'enter_bootloader',
                        lambda serial: events.append(('enter', serial)) or '/dev/rom')
    monkeypatch.setattr(cli.esp, 'Esptool', FakeTool)
    return events


def test_list(env, capsys):
    assert 0 == cli.main(['list'])
    out = capsys.readouterr().out
    assert SERIAL in out and 'stock' in out and '/dev/ttyACM0' in out


def test_backup_enters_bootloader_then_exits(env, tmp_path):
    path = tmp_path / 'b.bin'
    assert 0 == cli.main(['--serial', SERIAL, 'backup', str(path)])
    assert env == [('enter', SERIAL)]
    tool = FakeTool.instances[-1]
    assert tool.port == '/dev/rom'
    assert [c[0] for c in tool.calls] == ['backup', 'exit_bootloader']


def test_restore(env, tmp_path):
    path = tmp_path / 'b.bin'
    path.write_bytes(b'\xff')
    assert 0 == cli.main(['restore', str(path)])  # one hub: serial optional
    assert [c[0] for c in FakeTool.instances[-1].calls] == ['restore']


def _build_dir(tmp_path, version='0.1.0'):
    build = tmp_path / 'build'
    (build / 'bootloader').mkdir(parents=True)
    (build / 'partition_table').mkdir()
    for p in ('bootloader/bootloader.bin', 'partition_table/partition-table.bin',
              'uih_firmware.bin'):
        (build / p).write_bytes(b'\x00')
    (build / 'project_description.json').write_text(f'{{"project_version": "{version}"}}')
    return build


def test_flash_from_build_dir(env, tmp_path):
    build = _build_dir(tmp_path)
    assert 0 == cli.main(['flash', str(build)])
    name, args = FakeTool.instances[-1].calls[0]
    assert name == 'flash_firmware'
    assert [str(a) for a in args] == [str(build / 'bootloader/bootloader.bin'),
                                      str(build / 'partition_table/partition-table.bin'),
                                      str(build / 'uih_firmware.bin')]


def test_update_loop_cmd(env, tmp_path, monkeypatch, capsys):
    from usb_insight_hub import update_loop
    build = _build_dir(tmp_path, '0.2.0')
    backup = tmp_path / 'b.bin'
    backup.write_bytes(b'\xff')
    seen = {}

    def run(serial, files, path, cycles, expect_fw, progress=None):
        seen.update(serial=serial, files=files, path=path, cycles=cycles, expect_fw=expect_fw)
        r = update_loop.CycleResult(0)
        progress(r)
        return [r]

    monkeypatch.setattr(cli.update_loop, 'run', run)
    assert 0 == cli.main(['update-loop', str(build), str(backup), '--cycles', '3'])
    assert seen['serial'] == SERIAL
    assert seen['cycles'] == 3
    assert seen['expect_fw'] == '0.2.0'  # from the build
    assert seen['files'][-1].endswith('uih_firmware.bin')
    assert '1 cycles, 1 passed' in capsys.readouterr().out

    monkeypatch.setattr(cli.update_loop, 'run',
                        lambda *a, **k: [update_loop.CycleResult(0, error='boom')])
    assert 1 == cli.main(['update-loop', str(build), str(backup)])


def test_serial_required_with_several_hubs(monkeypatch, capsys):
    other = HubPort('/dev/ttyACM5', 'AABBCCDDEEFF', discover.KIND_JTAG)
    monkeypatch.setattr(cli.discover, 'find', lambda serial=None: [HUB, other])
    assert 1 == cli.main(['bootloader'])
    assert '--serial' in capsys.readouterr().err


def test_no_hub(monkeypatch, capsys):
    monkeypatch.setattr(cli.discover, 'find', lambda serial=None: [])
    assert 1 == cli.main(['bootloader'])
    assert 'no hub' in capsys.readouterr().err


def test_serial_after_command(env, tmp_path):
    path = tmp_path / 'b.bin'
    assert 0 == cli.main(['backup', '--serial', 'b4:3a:45:b7:bf:bc', str(path)])
    assert env == [('enter', SERIAL)]


@pytest.fixture
def simhub(monkeypatch):
    from usb_insight_hub import sim
    from usb_insight_hub.client import Hub
    s = sim.SimHub()
    monkeypatch.setattr(cli.discover, 'find',
                        lambda serial=None: [HubPort('/dev/sim', s.serial, discover.KIND_JTAG)])
    monkeypatch.setattr(cli.ops, 'open_any',
                        lambda serial, **kw: Hub('/dev/sim', timeout=0.5,
                                                 serial_factory=s.serial_factory))
    return s


def test_info_cmd(simhub, capsys):
    assert 0 == cli.main(['info'])
    out = capsys.readouterr().out
    assert '"serial": "B43A45B7BFBC"' in out


def test_status_cmd(simhub, capsys):
    assert 0 == cli.main(['status'])
    out = capsys.readouterr().out.splitlines()
    assert out[0].split() == ['port', 'power', 'data', 'fault', 'mV', 'mA']
    assert out[1].split()[:3] == ['1', 'on', 'on']


def test_set_cmd(simhub, capsys):
    assert 0 == cli.main(['set', '2', '--power', 'off'])
    assert simhub.ports[2].power is False
    assert 0 == cli.main(['set', 'all', '--data', 'off'])
    assert not any(p.data for p in simhub.ports.values())
    assert 1 == cli.main(['set', '2'])  # nothing to set
    assert 'power' in capsys.readouterr().err


def test_soak_cmd(simhub, capsys):
    assert 0 == cli.main(['soak', '--cycles', '2', '--settle', '0', '--dwell', '0'])
    assert '12 commands' in capsys.readouterr().out


def test_stream_check_cmd(simhub, monkeypatch, capsys):
    from usb_insight_hub import stream_check
    seen = {}

    def run(hub, rate, duration, progress=None):
        seen.update(rate=rate, duration=duration)
        return stream_check.Result(rate, duration, received=int(rate * duration))

    monkeypatch.setattr(cli.stream_check, 'run', run)
    assert 0 == cli.main(['stream-check', '--rate', '10', '--duration', '3600'])
    assert seen == {'rate': 10, 'duration': 3600.0}
    assert 'received 36000 of 36000' in capsys.readouterr().out
    monkeypatch.setattr(cli.stream_check, 'run',
                        lambda hub, rate, duration, progress=None:
                        stream_check.Result(rate, duration, received=1, gaps=3))
    assert 1 == cli.main(['stream-check'])
