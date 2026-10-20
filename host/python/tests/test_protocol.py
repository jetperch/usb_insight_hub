# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

import json
import os

import pytest

from usb_insight_hub import protocol

VECTORS = os.path.join(os.path.dirname(__file__), '..', '..', '..', 'test', 'vectors')


def _rows(name):
    with open(os.path.join(VECTORS, name), newline='\n') as f:
        return [line.rstrip('\n').split('\t', 1) for line in f]


@pytest.mark.parametrize('expect, line', _rows('frames.txt'))
def test_frame_vectors(expect, line):
    if expect == 'ok':
        obj = protocol.decode_frame(line.encode())
        assert isinstance(obj, dict)
    else:
        with pytest.raises(protocol.FrameError) as ex:
            protocol.decode_frame(line.encode())
        assert ex.value.code == expect


@pytest.mark.parametrize('line, summary', _rows('requests.txt'))
def test_request_vectors(line, summary):
    assert summary == protocol.summarize_request(line.encode())


@pytest.mark.parametrize('name, line', _rows('messages.txt'))
def test_message_vectors_decode(name, line):
    msg = protocol.decode_frame(line.encode())
    assert msg['type'] in ('rsp', 'status', 'event', 'error')


def test_message_port_values():
    rows = dict(_rows('messages.txt'))
    msg = protocol.decode_frame(rows['status'].encode())
    ports = [protocol.PortState.from_dict(p) for p in msg['ports']]
    assert ports[0] == protocol.PortState(1, True, True, False, 4797, 176.7)
    assert ports[2].fault and ports[2].mA == pytest.approx(-0.3)


def test_encode_round_trip():
    line = protocol.encode({'op': 'set', 'seq': 7, 'port': 2, 'power': False})
    assert line.endswith(b'\n')
    text = line[:-1].decode()
    body, crc = text.rsplit('*', 1)
    assert body == '{"op":"set","seq":7,"port":2,"power":false}'
    assert len(crc) == 8 and crc == crc.lower()
    assert protocol.decode_frame(line) == json.loads(body)


def test_encode_rejects_newline_free_check():
    # json.dumps escapes control characters, so a frame is always one line.
    line = protocol.encode({'msg': 'a\nb'})
    assert line.count(b'\n') == 1
