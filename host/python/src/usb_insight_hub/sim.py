# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""An in-memory hub that speaks protocol v1, for testing hosts.

``SimHub.serial_factory`` stands in for ``serial.Serial``.  The hub answers
requests as doc/protocol.md specifies and can inject faults: dropped
responses, failed sets, noise lines, button presses and status messages.
"""

from dataclasses import dataclass
import threading
import time

from . import protocol


@dataclass
class SimPort:
    power: bool = True
    data: bool = True
    fault: bool = False
    mV: int = 5000
    mA: float = 100.0

    def to_dict(self, port):
        mV = self.mV if self.power else 0
        mA = round(self.mA, 1) if self.power else 0.0
        return {'port': port, 'power': self.power, 'data': self.data,
                'fault': self.fault, 'mV': mV, 'mA': mA}


class SimSerial:
    """The host side of the simulated serial link (subset of serial.Serial)."""

    def __init__(self, hub, timeout):
        self._hub = hub
        self.timeout = timeout
        self._rx = bytearray()

    def write(self, data):
        self._rx += data
        while b'\n' in self._rx:
            idx = self._rx.index(b'\n')
            line, self._rx = bytes(self._rx[:idx + 1]), self._rx[idx + 1:]
            self._hub.receive(line)
        return len(data)

    def read(self, size=1):
        return self._hub.take(size, self.timeout)

    @property
    def in_waiting(self):
        return self._hub.waiting()

    def close(self):
        self._hub.closed = True


class SimHub:

    def __init__(self, serial='B43A45B7BFBC'):
        self.serial = serial
        self.ports = {p: SimPort() for p in protocol.PORTS}
        self.aux = {'vhost': True, 'vext': False, 'usb3': True}
        self.rate_hz = 0
        self.status_n = 0
        self.event_n = 0
        self.t0 = time.monotonic()
        self.closed = False
        self.requests_seen = 0
        self.last_op = None
        self.drop_responses = 0       # drop the next N responses
        self.fail_next_set = None     # error code for the next set
        self._out = bytearray()
        self._cv = threading.Condition()

    # --- the link -----------------------------------------------------------

    def serial_factory(self, device=None, baudrate=115200, timeout=0.05, **kwargs):
        return SimSerial(self, timeout)

    def take(self, size, timeout):
        """Read like pyserial: wait for ``size`` bytes or the timeout."""
        with self._cv:
            if timeout:
                self._cv.wait_for(lambda: len(self._out) >= size, timeout)
            data = bytes(self._out[:size])
            del self._out[:size]
            return data

    def waiting(self):
        with self._cv:
            return len(self._out)

    def inject(self, raw: bytes):
        with self._cv:
            self._out += raw
            self._cv.notify_all()

    def _send(self, obj):
        self.inject(protocol.encode(obj))

    def _t(self):
        return int((time.monotonic() - self.t0) * 1000)

    # --- protocol -----------------------------------------------------------

    def receive(self, line):
        if not line.strip(b'\r\n'):
            return  # the firmware ignores empty lines
        try:
            obj = protocol.decode_frame(line)
        except protocol.FrameError as ex:
            self._send({'type': 'error', 'code': ex.code})
            return
        try:
            req = protocol.parse_request(obj)
        except protocol.RequestError as ex:
            if ex.code == 'op':
                self._respond(obj.get('op'), ex.seq, error=ex.code)
            else:
                msg = {'type': 'error', 'code': ex.code}
                if ex.seq is not None:
                    msg['seq'] = ex.seq
                self._send(msg)
            return
        self.requests_seen += 1
        self.last_op = req.op
        handler = getattr(self, f'_op_{req.op}')
        handler(req)

    def _respond(self, op, seq, error=None, **fields):
        if self.drop_responses:
            self.drop_responses -= 1
            return
        msg = {'type': 'rsp', 'seq': seq, 'op': op}
        if error:
            msg.update(status='error', code=error)
        else:
            msg['status'] = 'ok'
            msg.update(fields)
        self._send(msg)

    def _ports(self, numbers):
        return [self.ports[p].to_dict(p) for p in numbers]

    def _op_ping(self, req):
        self._respond('ping', req.seq)

    def _op_info(self, req):
        self._respond('info', req.seq, fw='0.0.0-sim', git='sim', proto=1, idf='sim',
                      serial=self.serial, stm8=4, meter=True, display='ok', reset='poweron',
                      uptime_ms=self._t())

    def _op_status(self, req):
        self._respond('status', req.seq, ports=self._ports(protocol.PORTS), aux=self.aux)

    def _op_set(self, req):
        if self.fail_next_set:
            code, self.fail_next_set = self.fail_next_set, None
            self._respond('set', req.seq, error=code)
            return
        numbers = protocol.PORTS if req.port == 'all' else (req.port,)
        for p in numbers:
            if req.power is not None:
                self.ports[p].power = req.power
            if req.data is not None:
                self.ports[p].data = req.data
        self._respond('set', req.seq, ports=self._ports(numbers))

    def _op_stream(self, req):
        self.rate_hz = req.rate_hz
        self._respond('stream', req.seq, rate_hz=req.rate_hz)

    def _op_bootloader(self, req):
        self._respond('bootloader', req.seq)

    def _op_reset(self, req):
        self._respond('reset', req.seq)

    # --- hub-side activity ----------------------------------------------------

    def emit_status(self):
        self._send({'type': 'status', 'n': self.status_n, 't': self._t(),
                    'ports': self._ports(protocol.PORTS)})
        self.status_n += 1

    def press_button(self, port):
        p = self.ports[port]
        p.power = not p.power
        self._send({'type': 'event', 'n': self.event_n, 't': self._t(),
                    'event': 'button', 'port': port, 'power': p.power})
        self.event_n += 1


class StockSimSerial:
    """The host side of a simulated stock-firmware serial link."""

    def __init__(self, hub, timeout):
        self._hub = hub
        self.timeout = timeout
        self._rx = bytearray()

    def write(self, data):
        self._rx += data
        while b'\n' in self._rx:
            idx = self._rx.index(b'\n')
            line, self._rx = bytes(self._rx[:idx]), self._rx[idx + 1:]
            self._hub.receive(line)
        return len(data)

    def read(self, size=1):
        return self._hub.take(size, self.timeout)

    @property
    def in_waiting(self):
        return self._hub.waiting()

    def reset_input_buffer(self):
        self._hub.drain()

    def close(self):
        self._hub.closed = True


class StockSimHub(SimHub):
    """Simulated stock firmware 1.0.0 JSON API, including its dropped sets.

    :param drop_sets: silently drop the next N ``set`` commands.
    :param drop_every: silently drop every Nth ``set`` command.
    """

    def __init__(self, serial='B43A45B7BFBC', drop_sets=0, drop_every=0):
        super().__init__(serial)
        self.drop_sets = drop_sets
        self.drop_every = drop_every
        self.set_count = 0
        self.sent = []
        self.noise = []
        self.mute = False
        self.channels = {
            p: {'voltage': '4797.9', 'current': '176.7', 'fwdAlert': False,
                'backAlert': False, 'shortAlert': False, 'dataEn': True, 'powerEn': True}
            for p in (1, 2, 3)}

    def serial_factory(self, device=None, baudrate=115200, timeout=0.05, **kwargs):
        return StockSimSerial(self, timeout)

    def drain(self):
        with self._cv:
            self._out.clear()

    def _reply(self, obj):
        import json
        for n in self.noise:
            self.inject(n)
        if not self.mute:
            self.inject(json.dumps(obj).encode() + b'\r\n')

    def receive(self, line):
        import json
        req = json.loads(line)
        self.sent.append(req)
        if req['action'] == 'get':
            data = {}
            for name in req['params']:
                if name == 'state':
                    data.update(cpu_ver='1.0.0', base_ver=4, mac=self.serial,
                                meterInit='ok')
                else:
                    data[name] = dict(self.channels[int(name[2])])
            self._reply({'status': 'ok', 'data': data})
        elif req['action'] == 'set':
            self.set_count += 1
            drop = False
            if self.drop_sets:
                self.drop_sets -= 1
                drop = True
            elif self.drop_every and self.set_count % self.drop_every == 0:
                drop = True
            if not drop:
                for name, fields in req['params'].items():
                    ch = self.channels[int(name[2])]
                    for key in ('powerEn', 'dataEn'):
                        if key in fields:
                            ch[key] = fields[key] == 'true'
            self._reply({'status': 'ok', 'data': {'valid': '1 of 1'}})
