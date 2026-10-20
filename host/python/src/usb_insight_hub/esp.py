# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""ESP32-S3 ROM bootloader operations through esptool.

Every operation expects the hub's ESP32-S3 to already be in the ROM
bootloader (see :func:`touch_1200` and the firmware ``bootloader`` op), so
esptool connects with ``--before no-reset``.  Flash layout follows the
stock partition table (doc/plans/implement.md).
"""

from dataclasses import dataclass
import os
import re
import subprocess
import sys

FLASH_SIZE = 0x800000
BOOTLOADER_OFFSET = 0x0
PARTITION_TABLE_OFFSET = 0x8000
OTADATA_OFFSET = 0xE000
OTADATA_SIZE = 0x2000
APP0_OFFSET = 0x10000
APP_SLOT_SIZE = 0x3B0000
# RTC_CNTL_OPTION1_REG bit 0 is FORCE_DOWNLOAD_BOOT, set by a 1200-baud
# reboot into the bootloader.  It survives a reset, so clear it to leave.
RTC_CNTL_OPTION1_REG = 0x6000812C


@dataclass
class RunResult:
    returncode: int
    stdout: str
    stderr: str


class EsptoolError(RuntimeError):
    pass


def _run(cmd, **kwargs):
    p = subprocess.run(cmd, capture_output=True, text=True, **kwargs)
    return RunResult(p.returncode, p.stdout, p.stderr)


def _exists(*paths):
    for path in paths:
        if not os.path.isfile(path):
            raise FileNotFoundError(path)


class Esptool:
    """esptool commands for one ESP32-S3 serial port in the ROM bootloader."""

    def __init__(self, port, runner=None, log=None):
        self.port = port
        self._runner = runner or _run
        self._log = log or (lambda text: None)

    def _esptool(self, *args, after='no-reset'):
        cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3',
               '--port', str(self.port), '--before', 'no-reset', '--after', after]
        cmd += [str(a) for a in args]
        result = self._runner(cmd)
        self._log(result.stdout)
        if result.returncode:
            raise EsptoolError(f'esptool {args[0]} failed ({result.returncode}): '
                               f'{result.stderr or result.stdout}')
        return result

    def read_mac(self):
        """Return the chip MAC as 12 uppercase hex digits."""
        out = self._esptool('read-mac').stdout
        m = re.search(r'MAC:\s*([0-9a-fA-F:]{17})', out)
        if not m:
            raise EsptoolError(f'no MAC in esptool output: {out}')
        return m.group(1).replace(':', '').upper()

    def backup(self, path, size=FLASH_SIZE):
        """Read the whole flash to ``path``, then verify it against the chip."""
        self._esptool('read-flash', '0', size, path)
        self._esptool('verify-flash', '0', path)

    def restore(self, path):
        """Write a full flash image (a backup), then boot it."""
        _exists(path)
        self._esptool('write-flash', '0', path)
        self.exit_bootloader()

    def flash_firmware(self, bootloader, partition_table, app):
        """Install firmware: bootloader, partition table and app into app0.

        Erases otadata so the bootloader boots app0, then boots the app.
        """
        _exists(bootloader, partition_table, app)
        if os.path.getsize(app) > APP_SLOT_SIZE:
            raise ValueError(f'{app} exceeds the {APP_SLOT_SIZE:#x} byte app slot')
        self._esptool('write-flash',
                      hex(BOOTLOADER_OFFSET), bootloader,
                      hex(PARTITION_TABLE_OFFSET), partition_table,
                      hex(APP0_OFFSET), app)
        self._esptool('erase-region', hex(OTADATA_OFFSET), hex(OTADATA_SIZE))
        self.exit_bootloader()

    def exit_bootloader(self):
        """Clear FORCE_DOWNLOAD_BOOT and reset into the application.

        Uses the RTC watchdog: an RTS reset does not reset the chip over
        USB-Serial-JTAG while the flasher stub runs.
        """
        self._esptool('write-mem', hex(RTC_CNTL_OPTION1_REG), '0x0', '0x1',
                      after='watchdog-reset')


def touch_1200(port, serial_factory=None):
    """Reboot stock firmware into the ROM bootloader with a 1200-baud open.

    The stock TinyUSB CDC reboots on the line coding alone; opening often
    raises because the device disappears mid-open, which is expected.
    """
    if serial_factory is None:
        import serial
        serial_factory = serial.Serial
    s = serial_factory()
    s.port = port
    s.baudrate = 1200
    s.dtr = False
    s.rts = False
    try:
        s.open()
        s.dtr = False
    except OSError:
        return  # the device left the bus, as intended
    finally:
        try:
            s.close()
        except OSError:
            pass
