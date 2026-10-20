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


def test_flash_from_build_dir(env, tmp_path):
    build = tmp_path / 'build'
    (build / 'bootloader').mkdir(parents=True)
    (build / 'partition_table').mkdir()
    for p in ('bootloader/bootloader.bin', 'partition_table/partition-table.bin',
              'uih_firmware.bin'):
        (build / p).write_bytes(b'\x00')
    assert 0 == cli.main(['flash', str(build)])
    name, args = FakeTool.instances[-1].calls[0]
    assert name == 'flash_firmware'
    assert [str(a) for a in args] == [str(build / 'bootloader/bootloader.bin'),
                                      str(build / 'partition_table/partition-table.bin'),
                                      str(build / 'uih_firmware.bin')]


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
