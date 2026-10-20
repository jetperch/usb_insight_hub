# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Client for hubs running the Jetperch firmware (protocol v1)."""

import logging
import threading

from . import discover, protocol
from .protocol import PortState

_log = logging.getLogger(__name__)


class HubError(RuntimeError):
    """The hub answered a request with an error."""

    def __init__(self, code, msg=''):
        super().__init__(f'{code}: {msg}' if msg else code)
        self.code = code


class Hub:
    """A hub on the Jetperch firmware.

    Requests are retried with a new ``seq`` on timeout.  That is safe because
    every request is idempotent.  A reader thread dispatches responses,
    status messages and events.

    :param device: the serial port device.
    :param timeout: seconds to wait for each response.
    :param retries: total attempts per request.
    :param serial_factory: ``serial.Serial`` or a stand-in (tests).
    """

    def __init__(self, device, timeout=1.0, retries=3, serial_factory=None):
        if serial_factory is None:
            import serial
            serial_factory = serial.Serial
        self._serial = serial_factory(device, baudrate=115200, timeout=0.05)
        self.timeout = timeout
        self.retries = max(1, retries)
        self.counters = {'skipped': 0, 'crc': 0, 'parse': 0, 'errors': 0,
                         'status_gap': 0, 'late': 0}
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._seq = 0
        self._pending = {}
        self._status_cbs = []
        self._event_cbs = []
        self._last_status_n = None
        self._quit = False
        self._thread = threading.Thread(target=self._run, name='uih-reader', daemon=True)
        self._thread.start()
        # End any partial line in the hub's receive buffer (the firmware
        # ignores empty lines), such as esptool bytes left after a flash.
        self._serial.write(b'\n')

    # --- lifecycle --------------------------------------------------------------

    def close(self):
        if self._quit:
            return
        self._quit = True
        self._thread.join(timeout=2.0)
        self._serial.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # --- reader -----------------------------------------------------------------

    def _run(self):
        buf = bytearray()
        while not self._quit:
            try:
                # Block for one byte, then take what arrived with it: read(n)
                # would wait out the timeout for n bytes.
                data = self._serial.read(1)
                if data:
                    data += self._serial.read(self._serial.in_waiting)
            except OSError:
                _log.warning('serial read failed; reader stops')
                return
            if not data:
                continue
            buf += data
            while b'\n' in buf:
                idx = buf.index(b'\n')
                line, buf = bytes(buf[:idx + 1]), buf[idx + 1:]
                self._dispatch(line)

    def _dispatch(self, line):
        try:
            msg = protocol.decode_frame(line)
        except protocol.FrameError as ex:
            self.counters['skipped' if ex.code == 'format' else ex.code] += 1
            return
        kind = msg.get('type')
        if kind == 'rsp' or (kind == 'error' and 'seq' in msg):
            self._resolve(msg.get('seq'), msg)
        elif kind == 'status':
            self._on_status(msg)
        elif kind == 'event':
            for cb in list(self._event_cbs):
                cb(msg)
        elif kind == 'error':
            # Expected once after opening: the flush reports leftover bytes.
            self.counters['errors'] += 1
            _log.debug('hub reported error %s', msg.get('code'))

    def _resolve(self, seq, msg):
        with self._lock:
            waiter = self._pending.get(seq)
        if waiter is None:
            self.counters['late'] += 1  # a response after its request timed out
            return
        waiter[1] = msg
        waiter[0].set()

    def _on_status(self, msg):
        n = msg.get('n')
        if self._last_status_n is not None and isinstance(n, int):
            gap = n - self._last_status_n - 1
            if gap > 0:
                self.counters['status_gap'] += gap
        self._last_status_n = n
        msg = dict(msg)
        msg['ports'] = [PortState.from_dict(p) for p in msg.get('ports', [])]
        for cb in list(self._status_cbs):
            cb(msg)

    # --- requests ---------------------------------------------------------------

    def request(self, op, **args):
        """Send a request and return its successful response.

        :raise HubError: if the hub responds with an error.
        :raise TimeoutError: if no response arrives after all attempts.
        """
        for _ in range(self.retries):
            with self._lock:
                self._seq = (self._seq + 1) & protocol.SEQ_MAX
                seq = self._seq
                waiter = [threading.Event(), None]
                self._pending[seq] = waiter
            try:
                with self._write_lock:
                    self._serial.write(protocol.encode({'op': op, 'seq': seq, **args}))
                if waiter[0].wait(self.timeout):
                    msg = waiter[1]
                    if msg.get('type') == 'error' or msg.get('status') != 'ok':
                        raise HubError(msg.get('code', 'unknown'), msg.get('msg', ''))
                    return msg
            finally:
                with self._lock:
                    self._pending.pop(seq, None)
        raise TimeoutError(f'no response to {op}')

    def ping(self):
        self.request('ping')

    def info(self):
        msg = self.request('info')
        return {k: v for k, v in msg.items() if k not in ('type', 'seq', 'op', 'status')}

    def status(self):
        """Return ``(ports, aux)``: three PortState and the aux dict."""
        msg = self.request('status')
        return [PortState.from_dict(p) for p in msg['ports']], msg['aux']

    def set(self, port, power=None, data=None):
        """Set port outputs.  Returns the read-back PortState list."""
        if port != 'all' and port not in protocol.PORTS:
            raise ValueError(f'invalid port {port!r}')
        if power is None and data is None:
            raise ValueError('set requires power and/or data')
        args = {'port': port}
        if power is not None:
            args['power'] = bool(power)
        if data is not None:
            args['data'] = bool(data)
        msg = self.request('set', **args)
        return [PortState.from_dict(p) for p in msg['ports']]

    def set_power(self, port, on):
        return self.set(port, power=on)

    def set_data(self, port, on):
        return self.set(port, data=on)

    def stream(self, rate_hz):
        self.request('stream', rate_hz=int(rate_hz))

    def on_status(self, cb):
        self._status_cbs.append(cb)

    def on_event(self, cb):
        self._event_cbs.append(cb)

    def enter_bootloader(self):
        self.request('bootloader')

    def reset(self):
        self.request('reset')


def open_hub(serial, **kwargs):
    """Open the Jetperch-firmware hub with the given serial number."""
    ports = [p for p in discover.find(serial) if p.kind == discover.KIND_JTAG]
    if not ports:
        raise LookupError(f'hub {discover.normalize_serial(serial)} not found on '
                          f'USB-Serial-JTAG')
    return Hub(ports[0].device, **kwargs)
