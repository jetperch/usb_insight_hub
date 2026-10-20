# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import sys

import pytest

from usb_insight_hub import esp


class Recorder:
    """Stand-in for subprocess.run that records esptool invocations."""

    def __init__(self, returncode=0):
        self.calls = []
        self.returncode = returncode

    def __call__(self, cmd, **kwargs):
        self.calls.append(cmd)
        return esp.RunResult(self.returncode, 'out', 'err')

    def esptool_args(self, index=-1):
        cmd = self.calls[index]
        assert cmd[:3] == [sys.executable, '-m', 'esptool']
        return cmd[3:]


PORT = '/dev/ttyACM9'


def _tool(rc=0):
    rec = Recorder(rc)
    return esp.Esptool(PORT, runner=rec), rec


def _common(args):
    # Every call talks to an ESP32-S3 already in the ROM bootloader.
    assert args[:6] == ['--chip', 'esp32s3', '--port', PORT, '--before', 'no-reset']


def test_backup_reads_then_verifies(tmp_path):
    tool, rec = _tool()
    path = tmp_path / 'flash.bin'
    tool.backup(path)
    first, second = rec.esptool_args(0), rec.esptool_args(1)
    _common(first)
    assert first[6:] == ['--after', 'no-reset', 'read-flash', '0', str(0x800000), str(path)]
    _common(second)
    assert second[6:] == ['--after', 'no-reset', 'verify-flash', '0', str(path)]


def test_restore_writes_whole_image_then_exits(tmp_path):
    tool, rec = _tool()
    path = tmp_path / 'flash.bin'
    path.write_bytes(b'\xff' * 16)
    tool.restore(path)
    write = rec.esptool_args(0)
    _common(write)
    assert write[6:] == ['--after', 'no-reset', 'write-flash', '0', str(path)]
    _assert_exit(rec.esptool_args(1))


def _assert_exit(args):
    _common(args)
    # Clear FORCE_DOWNLOAD_BOOT (RTC_CNTL_OPTION1_REG bit 0), then reset with
    # the RTC watchdog.  An RTS "hard reset" does not reset the chip over
    # USB-Serial-JTAG while the stub runs, and --no-stub would talk ROM
    # protocol to the stub left by an earlier write (2026-10-20).
    assert args[6:] == ['--after', 'watchdog-reset',
                        'write-mem', '0x6000812c', '0x0', '0x1']


def test_flash_firmware_layout(tmp_path):
    tool, rec = _tool()
    files = {}
    for name in ('bootloader.bin', 'partition-table.bin', 'app.bin'):
        files[name] = tmp_path / name
        files[name].write_bytes(b'\x00')
    tool.flash_firmware(files['bootloader.bin'], files['partition-table.bin'], files['app.bin'])
    write, erase = rec.esptool_args(0), rec.esptool_args(1)
    _common(write)
    assert write[6:] == ['--after', 'no-reset', 'write-flash',
                         '0x0', str(files['bootloader.bin']),
                         '0x8000', str(files['partition-table.bin']),
                         '0x10000', str(files['app.bin'])]
    _common(erase)
    assert erase[6:] == ['--after', 'no-reset', 'erase-region', '0xe000', '0x2000']
    _assert_exit(rec.esptool_args(2))


def test_flash_firmware_rejects_oversize_app(tmp_path):
    tool, rec = _tool()
    app = tmp_path / 'app.bin'
    app.write_bytes(b'\x00' * (esp.APP_SLOT_SIZE + 1))
    boot = tmp_path / 'b.bin'
    boot.write_bytes(b'\x00')
    with pytest.raises(ValueError):
        tool.flash_firmware(boot, boot, app)
    assert [] == rec.calls


def test_failure_raises_with_output():
    tool, rec = _tool(rc=2)
    with pytest.raises(esp.EsptoolError) as ex:
        tool.exit_bootloader()
    assert 'err' in str(ex.value)


def test_missing_file_raises_before_running(tmp_path):
    tool, rec = _tool()
    with pytest.raises(FileNotFoundError):
        tool.restore(tmp_path / 'nope.bin')
    assert [] == rec.calls


def test_chip_mac_parses_output():
    class MacRunner(Recorder):
        def __call__(self, cmd, **kwargs):
            self.calls.append(cmd)
            return esp.RunResult(0, 'Chip type: ESP32-S3\nMAC: b4:3a:45:b7:bf:bc\n', '')
    rec = MacRunner()
    tool = esp.Esptool(PORT, runner=rec)
    assert 'B43A45B7BFBC' == tool.read_mac()
    _common(rec.esptool_args(0))
    assert rec.esptool_args(0)[6:] == ['--after', 'no-reset', 'read-mac']


def test_touch_1200_handles_broken_pipe():
    events = []

    class FakeSerial:
        def __init__(self):
            self.port = None
            self.baudrate = None
            self.dtr = True
            self.rts = True

        def open(self):
            events.append(('open', self.port, self.baudrate))
            raise BrokenPipeError(32, 'Broken pipe')

        def close(self):
            events.append(('close',))

    esp.touch_1200(PORT, serial_factory=FakeSerial)
    assert events[0] == ('open', PORT, 1200)


def test_output_is_logged():
    lines = []
    rec = Recorder()
    tool = esp.Esptool(PORT, runner=rec, log=lines.append)
    tool.exit_bootloader()
    assert lines == ['out']
