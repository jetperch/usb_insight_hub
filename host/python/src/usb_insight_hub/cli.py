# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""The ``uih`` command line tool."""

import argparse
import json
import os
import sys
import time

from . import bundled, discover, esp, ops, soak, stream_check, update_loop, usb_soak
from .version import __version__

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


def _build_files(build_dir):
    try:
        return bundled.build_files(build_dir)
    except LookupError as ex:
        raise CliError(str(ex))


def _cmd_flash(args):
    if args.build_dir:
        files = _build_files(args.build_dir)
    else:
        try:
            files = bundled.files()
        except LookupError as ex:
            raise CliError(str(ex))
        m = bundled.manifest()
        print(f'installing bundled firmware {m["version"]} ({m["git"]})', flush=True)
    _tool(args.serial).flash_firmware(*files)


def _cmd_update_loop(args):
    files = _build_files(args.build_dir)
    with open(os.path.join(args.build_dir, 'project_description.json')) as f:
        version = json.load(f)['project_version']

    def progress(r):
        status = 'ok' if r.ok else f'FAIL {r.error}'
        print(f'cycle {r.cycle + 1}: {status}; update {r.update_s:.1f} s, '
              f'restore {r.restore_s:.1f} s, |dV| {r.max_delta_mv()} mV, '
              f'|dI| {r.max_delta_ma():.1f} mA', flush=True)
    results = update_loop.run(_select(args.serial), files, args.backup, args.cycles,
                              expect_fw=version, progress=progress)
    print(update_loop.summary(results))
    if not all(r.ok for r in results):
        raise CliError('update loop failed')


def _onoff(text):
    if text in ('on', '1', 'true'):
        return True
    if text in ('off', '0', 'false'):
        return False
    raise argparse.ArgumentTypeError(f'expected on or off, got {text!r}')


def _port_arg(text):
    if text == 'all':
        return 'all'
    if text in ('1', '2', '3'):
        return int(text)
    raise argparse.ArgumentTypeError(f'expected 1, 2, 3 or all, got {text!r}')


def _print_ports(ports):
    print(f'{"port":>4} {"power":>5} {"data":>5} {"fault":>5} {"mV":>6} {"mA":>8}')
    for p in ports:
        print(f'{p.port:>4} {("on" if p.power else "off"):>5} '
              f'{("on" if p.data else "off"):>5} {("yes" if p.fault else "no"):>5} '
              f'{p.mV:>6} {p.mA:>8.1f}')


def _hub(serial):
    return ops.open_any(_select(serial))


def _cmd_info(args):
    hub = _hub(args.serial)
    try:
        print(json.dumps(hub.info(), indent=2))
    finally:
        hub.close()


def _cmd_status(args):
    hub = _hub(args.serial)
    try:
        ports, _ = hub.status()
        _print_ports(ports)
    finally:
        hub.close()


def _cmd_set(args):
    if args.power is None and args.data is None:
        raise CliError('set requires --power and/or --data')
    hub = _hub(args.serial)
    try:
        _print_ports(hub.set(args.port, power=args.power, data=args.data))
    finally:
        hub.close()


def _cmd_stream(args):
    hub = _hub(args.serial)
    if not hasattr(hub, 'stream'):
        hub.close()
        raise CliError('stream requires the Jetperch firmware')
    try:
        hub.on_status(lambda m: print(m['n'], m['t'], ' '.join(
            f'{p.port}:{"on" if p.power else "off"}:{p.mV}mV:{p.mA:.1f}mA'
            for p in m['ports']), flush=True))
        hub.on_event(lambda m: print('event', json.dumps(m), flush=True))
        hub.stream(args.rate)
        time.sleep(args.duration)
        hub.stream(0)
        print(f'counters: {hub.counters}')
    finally:
        hub.close()


def _cmd_stream_check(args):
    hub = _hub(args.serial)
    if not hasattr(hub, 'stream'):
        hub.close()
        raise CliError('stream-check requires the Jetperch firmware')
    last = [time.monotonic()]

    def progress(r):
        if time.monotonic() - last[0] >= 60:
            last[0] = time.monotonic()
            print(r.summary(), flush=True)
    try:
        result = stream_check.run(hub, args.rate, args.duration, progress=progress)
    finally:
        hub.close()
    print(result.summary())
    if not result.ok:
        raise CliError('stream lost messages or ran slow')


def _cmd_usb_soak(args):
    try:
        device = usb_soak.parse_device(args.device)
        watch = [usb_soak.parse_device(w) for w in args.watch or []]
    except ValueError as ex:
        raise CliError(str(ex))
    hub = _hub(args.serial)

    def progress(cycle, r):
        if args.verbose_progress and (cycle + 1) % args.verbose_progress == 0:
            print(f'cycle {cycle + 1}: {r.summary()}', flush=True)
    try:
        result = usb_soak.run(hub, args.port, device, args.cycles, off_s=args.off,
                              on_s=args.on, off_timeout=args.off_timeout, on_timeout=args.on_timeout,
                              watch=watch, progress=progress)
    finally:
        hub.close()
    for failure in result.failures:
        print(failure)
    print(result.summary())
    if not result.ok:
        raise CliError('the USB device did not disconnect or reconnect as expected')


def _cmd_soak(args):
    hub = _hub(args.serial)
    ports = tuple(args.ports) if args.ports else (1, 2, 3)

    def progress(cycle, result):
        if args.verbose_progress and (cycle + 1) % args.verbose_progress == 0:
            print(f'cycle {cycle + 1}: {result.summary()}', flush=True)
    try:
        result = soak.run(hub, args.cycles, ports=ports, dwell=args.dwell,
                          settle=args.settle, raw=args.raw, progress=progress)
    finally:
        hub.close()
    print(result.summary())
    if not result.ok:
        raise CliError('soak saw dropped commands, errors or timeouts')


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
    add('flash', _cmd_flash,
        'install the bundled firmware, or one from an ESP-IDF build directory').add_argument(
        'build_dir', nargs='?', help='default: the firmware bundled in this package')
    s = add('update-loop', _cmd_update_loop,
            'repeat: flash this firmware, verify, restore the stock backup, verify')
    s.add_argument('build_dir')
    s.add_argument('backup', help='whole-flash stock backup')
    s.add_argument('--cycles', type=int, default=20)
    add('info', _cmd_info, 'show firmware and hub information')
    add('status', _cmd_status, 'show the port states')
    s = add('set', _cmd_set, 'set port power and/or data')
    s.add_argument('port', type=_port_arg, help='1, 2, 3 or all')
    s.add_argument('--power', type=_onoff, help='on or off')
    s.add_argument('--data', type=_onoff, help='on or off')
    s = add('stream', _cmd_stream, 'print the status stream (Jetperch firmware)')
    s.add_argument('--rate', type=int, default=10, help='status messages per second')
    s.add_argument('--duration', type=float, default=10.0, help='seconds')
    s = add('stream-check', _cmd_stream_check,
            'stream and verify that no status message is lost (Jetperch firmware)')
    s.add_argument('--rate', type=int, default=10, help='status messages per second')
    s.add_argument('--duration', type=float, default=60.0, help='seconds')
    s = add('usb-soak', _cmd_usb_soak,
            'power-cycle a port and check that its USB device disconnects and returns '
            '(Linux)')
    s.add_argument('--port', type=int, required=True, choices=(1, 2, 3))
    s.add_argument('--device', required=True, help='VID:PID[:SERIAL] on the port, hex')
    s.add_argument('--watch', action='append',
                   help='VID:PID[:SERIAL] on another port that must stay connected')
    s.add_argument('--cycles', type=int, default=100)
    s.add_argument('--off', type=float, default=2.0, help='minimum seconds off')
    s.add_argument('--on', type=float, default=3.0,
                   help='seconds the device must stay connected after it enumerates')
    s.add_argument('--off-timeout', type=float, default=5.0,
                   help='seconds allowed to disconnect')
    s.add_argument('--on-timeout', type=float, default=20.0,
                   help='seconds allowed to enumerate')
    s.add_argument('--progress', dest='verbose_progress', type=int, default=0,
                   help='print a summary every N cycles')
    s = add('soak', _cmd_soak, 'toggle port power and count dropped commands')
    s.add_argument('--cycles', type=int, default=100)
    s.add_argument('--ports', type=int, nargs='+', choices=(1, 2, 3))
    s.add_argument('--dwell', type=float, default=0.05, help='seconds after each batch')
    s.add_argument('--settle', type=float, default=0.25,
                   help='seconds before reading the ports back')
    s.add_argument('--raw', action='store_true',
                   help='unverified commands (measure the stock firmware drop rate)')
    s.add_argument('--progress', dest='verbose_progress', type=int, default=0,
                   help='print a summary every N cycles')
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
