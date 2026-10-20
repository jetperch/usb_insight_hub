# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""The ``uih`` command line tool."""

import argparse
import os
import sys

from . import discover, esp, ops
from .version import __version__

# ESP-IDF build outputs, relative to the build directory.
_BUILD_FILES = ('bootloader/bootloader.bin', 'partition_table/partition-table.bin',
                'uih_firmware.bin')


class CliError(Exception):
    pass


def _log(text):
    if _verbose:
        print(text, file=sys.stderr, end='' if text.endswith('\n') else '\n')


_verbose = False


def _tool(serial):
    """Enter the bootloader on the selected hub and return its Esptool."""
    return esp.Esptool(ops.enter_bootloader(_select(serial)), log=_log)


def _select(serial):
    """Return the serial number of the one hub to use."""
    hubs = discover.find(serial)
    serials = sorted({h.serial for h in hubs})
    if not serials:
        raise CliError(f'no hub found{" with serial " + serial if serial else ""}')
    if len(serials) > 1:
        raise CliError(f'several hubs found ({", ".join(serials)}); use --serial')
    return serials[0]


def _cmd_list(args):
    for h in discover.find(args.serial):
        print(f'{h.serial}  {h.kind:5s}  {h.device}')


def _cmd_bootloader(args):
    print(ops.enter_bootloader(_select(args.serial)))


def _cmd_exit_bootloader(args):
    _tool(args.serial).exit_bootloader()


def _cmd_backup(args):
    tool = _tool(args.serial)
    tool.backup(args.path)
    tool.exit_bootloader()


def _cmd_restore(args):
    _tool(args.serial).restore(args.path)


def _cmd_flash(args):
    files = [os.path.join(args.build_dir, f) for f in _BUILD_FILES]
    for f in files:
        if not os.path.isfile(f):
            raise CliError(f'missing build output: {f}')
    _tool(args.serial).flash_firmware(*files)


def parser():
    # --serial works before or after the command (tools/ wrappers pass it after).
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--serial', default=argparse.SUPPRESS,
                        help='hub serial number (MAC), with or without colons')
    common.add_argument('-v', '--verbose', action='store_true', default=argparse.SUPPRESS,
                        help='show esptool output')
    p = argparse.ArgumentParser(prog='uih', description='USB Insight Hub tool',
                                parents=[common])
    p.add_argument('--version', action='version', version=__version__)
    sub = p.add_subparsers(dest='command', required=True)

    def add(name, func, help):
        s = sub.add_parser(name, help=help, parents=[common])
        s.set_defaults(func=func)
        return s

    add('list', _cmd_list, 'list connected hubs')
    add('bootloader', _cmd_bootloader, 'enter the ROM bootloader')
    add('exit-bootloader', _cmd_exit_bootloader, 'leave the ROM bootloader')
    add('backup', _cmd_backup, 'back up the whole flash').add_argument('path')
    add('restore', _cmd_restore, 'write a whole-flash backup and boot it').add_argument('path')
    add('flash', _cmd_flash, 'install firmware from an ESP-IDF build directory').add_argument(
        'build_dir')
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    global _verbose
    args.serial = getattr(args, 'serial', None)
    _verbose = getattr(args, 'verbose', False)
    try:
        args.func(args)
    except (CliError, LookupError, RuntimeError, OSError) as ex:
        print(f'error: {ex}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
