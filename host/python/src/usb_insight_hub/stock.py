# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Backend for hubs on the stock firmware 1.0.0 JSON API.

The stock firmware silently drops about 1 in 50 ``set`` commands (a race in
its ``Intercomms`` task, doc/plans/implement.md), and its ``set`` reply
does not report the resulting state.  ``StockHub.set`` therefore reads the
port back after ``settle`` seconds and retries.  ``set_raw`` sends one
unverified command, for measuring the drop rate.
"""

import json
import time

from . import esp, protocol
from .client import HubError
from .protocol import PortState


def _bool(value):
    return 'true' if value else 'false'


class StockHub:

    def __init__(self, device, timeout=1.0, retries=3, settle=0.25, serial_factory=None):
        if serial_factory is None:
            import serial
            serial_factory = serial.Serial
        self.device = device
        self._serial = serial_factory(device, baudrate=115200, timeout=0.05)
        self.timeout = timeout
        self.retries = max(1, retries)
        self.settle = settle

    def close(self):
        self._serial.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _request(self, obj):
        """Send one command and return its JSON reply (skipping log lines)."""
        self._serial.reset_input_buffer()
        self._serial.write(json.dumps(obj, separators=(',', ':')).encode() + b'\n')
        buf = bytearray()
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            buf += self._serial.read(max(1, self._serial.in_waiting))
            while b'\n' in buf:
                idx = buf.index(b'\n')
                line, buf = bytes(buf[:idx]).strip(), buf[idx + 1:]
                if not line.startswith(b'{'):
                    continue
                try:
                    reply = json.loads(line)
                except ValueError:
                    continue
                if reply.get('status') != 'ok':
                    raise HubError('stock', str(reply.get('data')))
                return reply.get('data', {})
        raise TimeoutError(f'no reply to {obj.get("action")}')

    def info(self):
        d = self._request({'action': 'get', 'params': ['state']})
        return {'fw': d.get('cpu_ver'), 'stm8': d.get('base_ver'),
                'serial': d.get('mac'), 'meter': d.get('meterInit') == 'ok',
                'proto': None}

    def _ports(self, numbers):
        d = self._request({'action': 'get', 'params': [f'CH{p}' for p in numbers]})
        out = []
        for p in numbers:
            ch = d[f'CH{p}']
            out.append(PortState(p, bool(ch['powerEn']), bool(ch['dataEn']),
                                 bool(ch.get('shortAlert', False)),
                                 int(float(ch['voltage'])), float(ch['current'])))
        return out

    def status(self):
        return self._ports(protocol.PORTS), {}

    def set_raw(self, port, power=None, data=None):
        """Send one unverified set command."""
        numbers = protocol.PORTS if port == 'all' else (port,)
        fields = {}
        if power is not None:
            fields['powerEn'] = _bool(power)
        if data is not None:
            fields['dataEn'] = _bool(data)
        self._request({'action': 'set',
                       'params': {f'CH{p}': dict(fields) for p in numbers}})

    def set(self, port, power=None, data=None):
        """Set port outputs, read them back and retry dropped commands."""
        if port != 'all' and port not in protocol.PORTS:
            raise ValueError(f'invalid port {port!r}')
        if power is None and data is None:
            raise ValueError('set requires power and/or data')
        numbers = protocol.PORTS if port == 'all' else (port,)
        for _ in range(self.retries):
            self.set_raw(port, power=power, data=data)
            if self.settle:
                time.sleep(self.settle)
            ports = self._ports(numbers)
            if all((power is None or p.power == power) and (data is None or p.data == data)
                   for p in ports):
                return ports
        raise HubError('not_applied', f'port {port} did not take the command')

    def set_power(self, port, on):
        return self.set(port, power=on)

    def set_data(self, port, on):
        return self.set(port, data=on)

    def enter_bootloader(self):
        esp.touch_1200(self.device)
