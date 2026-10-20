# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Protocol v1 framing and messages (doc/protocol.md)."""

from dataclasses import dataclass
import json
import zlib

PROTOCOL_VERSION = 1
PORTS = (1, 2, 3)
STREAM_RATE_MAX = 50
SEQ_MAX = 0xFFFFFFFF
OPS = ('ping', 'info', 'status', 'set', 'stream', 'bootloader', 'reset')
_HEX = set('0123456789abcdefABCDEF')


class FrameError(ValueError):
    """A line that is not a valid frame; ``code`` is format, crc or parse."""

    def __init__(self, code, msg=''):
        super().__init__(f'{code}: {msg}' if msg else code)
        self.code = code


class RequestError(ValueError):
    """A framed request that is invalid; ``code`` is op or arg."""

    def __init__(self, code, seq=None):
        super().__init__(code)
        self.code = code
        self.seq = seq


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode(obj) -> bytes:
    """Encode a message object as a framed line, including the newline."""
    body = json.dumps(obj, separators=(',', ':')).encode()
    return body + b'*%08x\n' % crc32(body)


def decode_frame(line: bytes):
    """Decode one framed line (with or without its line ending).

    :return: the JSON object.
    :raise FrameError: for a line that is not a valid frame.
    """
    line = line.rstrip(b'\n')
    if line.endswith(b'\r'):
        line = line[:-1]
    idx = line.rfind(b'*')
    if idx < 0:
        raise FrameError('format', 'no CRC separator')
    body, crc_text = line[:idx], line[idx + 1:]
    if len(crc_text) != 8 or not set(crc_text.decode('latin-1')) <= _HEX:
        raise FrameError('format', 'CRC is not 8 hex digits')
    if crc32(body) != int(crc_text, 16):
        raise FrameError('crc')
    try:
        obj = json.loads(body)
    except (UnicodeDecodeError, ValueError) as ex:
        raise FrameError('parse', str(ex)) from None
    if not isinstance(obj, dict):
        raise FrameError('parse', 'not an object')
    return obj


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


@dataclass
class Request:
    op: str
    seq: int
    port: object = None      # 1..3 or 'all' (set)
    power: object = None     # bool or None (set)
    data: object = None      # bool or None (set)
    rate_hz: object = None   # int (stream)


def parse_request(obj) -> Request:
    """Validate a decoded request object, as the firmware does.

    :raise RequestError: with code ``arg`` or ``op``.
    """
    seq = obj.get('seq')
    if not _is_int(seq) or not 0 <= seq <= SEQ_MAX:
        raise RequestError('arg')
    op = obj.get('op')
    if not isinstance(op, str):
        raise RequestError('arg', seq)
    if op not in OPS:
        raise RequestError('op', seq)
    req = Request(op, seq)
    if op == 'set':
        port = obj.get('port')
        if port != 'all' and not (_is_int(port) and port in PORTS):
            raise RequestError('arg', seq)
        req.port = port
        for key in ('power', 'data'):
            if key in obj:
                if not isinstance(obj[key], bool):
                    raise RequestError('arg', seq)
                setattr(req, key, obj[key])
        if req.power is None and req.data is None:
            raise RequestError('arg', seq)
    elif op == 'stream':
        rate = obj.get('rate_hz')
        if not _is_int(rate) or not 0 <= rate <= STREAM_RATE_MAX:
            raise RequestError('arg', seq)
        req.rate_hz = rate
    return req


def summarize_request(line: bytes) -> str:
    """Canonical summary of a request line, as in test/vectors/requests.txt."""
    try:
        req = parse_request(decode_frame(line))
    except FrameError as ex:
        return f'error={ex.code}'
    except RequestError as ex:
        return f'error={ex.code}' + ('' if ex.seq is None else f' seq={ex.seq}')
    parts = [f'op={req.op}', f'seq={req.seq}']
    if req.port is not None:
        parts.append(f'port={req.port}')
    for key in ('power', 'data'):
        value = getattr(req, key)
        if value is not None:
            parts.append(f'{key}={int(value)}')
    if req.rate_hz is not None:
        parts.append(f'rate_hz={req.rate_hz}')
    return ' '.join(parts)


@dataclass(frozen=True)
class PortState:
    port: int
    power: bool
    data: bool
    fault: bool
    mV: int
    mA: float

    @staticmethod
    def from_dict(d):
        return PortState(int(d['port']), bool(d['power']), bool(d['data']),
                         bool(d['fault']), int(d['mV']), float(d['mA']))
