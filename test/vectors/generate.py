#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0

"""Generate the protocol v1 test vectors shared by the Python and C tests.

Writes three tab-separated files next to this script:

- frames.txt:   <expect>\\t<line>  where expect is ok, format, crc or parse.
- requests.txt: <line>\\t<summary>  the canonical decode of a request line.
- messages.txt: <name>\\t<line>  hub output lines the firmware must produce
  byte for byte and the host must parse.

The summary is ``key=value`` pairs separated by spaces, in the order op,
seq, port, power, data, rate_hz, with booleans as 0/1.  Errors are
``error=<code>``, followed by ``seq=<n>`` when the request provided a valid
one.  Rerun this script after changing doc/protocol.md.
"""

import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def frame(json_text):
    return f'{json_text}*{zlib.crc32(json_text.encode()):08x}'


def bad_crc(json_text):
    good = zlib.crc32(json_text.encode())
    return f'{json_text}*{good ^ 1:08x}'


FRAMES = [
    ('ok', frame('{"op":"ping","seq":1}')),
    ('ok', frame('{"op":"ping","seq":1}') + '\r'),
    ('ok', frame('{"msg":"a*b","op":"ping","seq":2}')),  # '*' inside a string
    ('crc', bad_crc('{"op":"ping","seq":1}')),
    ('crc', frame('{"op":"ping","seq":1}').upper().replace('{"OP":"PING","SEQ":1}',
                                                           '{"op":"ping","seq":9}')),
    ('format', '{"op":"ping","seq":1}'),                 # no CRC
    ('format', '{"op":"ping","seq":1}*1234567'),          # 7 digits
    ('format', '{"op":"ping","seq":1}*123456789'),        # 9 digits
    ('format', '{"op":"ping","seq":1}*1234567g'),         # not hex
    ('format', ''),
    ('format', 'ESP-ROM:esp32s3-20210327'),               # ROM boot output
    ('parse', frame('{"op":"ping",')),
    ('parse', frame('[1,2,3]')),
    ('parse', frame('not json')),
]

REQUESTS = [
    ('{"op":"ping","seq":1}', 'op=ping seq=1'),
    ('{"seq":4294967295,"op":"info"}', 'op=info seq=4294967295'),
    ('{"op":"status","seq":0,"extra":[1,{"a":"b"}],"x":null}', 'op=status seq=0'),
    ('{"op":"set","seq":7,"port":2,"power":false}', 'op=set seq=7 port=2 power=0'),
    ('{"op":"set","seq":8,"port":"all","power":true,"data":true}',
     'op=set seq=8 port=all power=1 data=1'),
    ('{"op":"set","seq":9,"port":3,"data":false}', 'op=set seq=9 port=3 data=0'),
    ('{ "op" : "set" , "seq" : 10 , "port" : 1 , "power" : true }',
     'op=set seq=10 port=1 power=1'),
    ('{"op":"stream","seq":11,"rate_hz":10}', 'op=stream seq=11 rate_hz=10'),
    ('{"op":"stream","seq":12,"rate_hz":0}', 'op=stream seq=12 rate_hz=0'),
    ('{"op":"stream","seq":13,"rate_hz":50}', 'op=stream seq=13 rate_hz=50'),
    ('{"op":"bootloader","seq":14}', 'op=bootloader seq=14'),
    ('{"op":"reset","seq":15}', 'op=reset seq=15'),
    ('{"op":"pi\\u006eg","seq":16}', 'op=ping seq=16'),   # escaped characters
    ('{"op":"ping","seq":17,"op":"info"}', 'op=info seq=17'),  # last key wins
    # Errors
    ('{"op":"fly","seq":20}', 'error=op seq=20'),
    ('{"seq":21}', 'error=arg seq=21'),
    ('{"op":"ping"}', 'error=arg'),
    ('{"op":"ping","seq":-1}', 'error=arg'),
    ('{"op":"ping","seq":4294967296}', 'error=arg'),
    ('{"op":"ping","seq":1.5}', 'error=arg'),
    ('{"op":"ping","seq":"1"}', 'error=arg'),
    ('{"op":5,"seq":22}', 'error=arg seq=22'),
    ('{"op":"set","seq":23,"power":true}', 'error=arg seq=23'),
    ('{"op":"set","seq":24,"port":0,"power":true}', 'error=arg seq=24'),
    ('{"op":"set","seq":25,"port":4,"power":true}', 'error=arg seq=25'),
    ('{"op":"set","seq":26,"port":2.0,"power":true}', 'error=arg seq=26'),
    ('{"op":"set","seq":27,"port":"two","power":true}', 'error=arg seq=27'),
    ('{"op":"set","seq":28,"port":2}', 'error=arg seq=28'),
    ('{"op":"set","seq":29,"port":2,"power":1}', 'error=arg seq=29'),
    ('{"op":"set","seq":30,"port":2,"data":"on"}', 'error=arg seq=30'),
    ('{"op":"stream","seq":31}', 'error=arg seq=31'),
    ('{"op":"stream","seq":32,"rate_hz":51}', 'error=arg seq=32'),
    ('{"op":"stream","seq":33,"rate_hz":-1}', 'error=arg seq=33'),
    ('{"op":"stream","seq":34,"rate_hz":2.5}', 'error=arg seq=34'),
]

_PORTS = ('{"port":1,"power":true,"data":true,"fault":false,"mV":4797,"mA":176.7},'
          '{"port":2,"power":false,"data":true,"fault":false,"mV":12,"mA":0.0},'
          '{"port":3,"power":true,"data":false,"fault":true,"mV":4788,"mA":-0.3}')

MESSAGES = [
    ('rsp_ping', '{"type":"rsp","seq":1,"op":"ping","status":"ok"}'),
    ('rsp_set',
     '{"type":"rsp","seq":7,"op":"set","status":"ok","ports":['
     '{"port":2,"power":false,"data":true,"fault":false,"mV":12,"mA":0.0}]}'),
    ('rsp_status',
     '{"type":"rsp","seq":5,"op":"status","status":"ok","ports":[' + _PORTS + '],'
     '"aux":{"vhost":true,"vext":false,"usb3":true}}'),
    ('rsp_stream', '{"type":"rsp","seq":11,"op":"stream","status":"ok","rate_hz":10}'),
    ('rsp_info',
     '{"type":"rsp","seq":2,"op":"info","status":"ok","fw":"0.1.0","git":"c813f74",'
     '"proto":1,"idf":"v6.1","serial":"B43A45B7BFBC","stm8":4,"meter":true,'
     '"reset":"poweron","uptime_ms":1234}'),
    ('rsp_info_no_stm8',
     '{"type":"rsp","seq":3,"op":"info","status":"ok","fw":"0.1.0","git":"c813f74",'
     '"proto":1,"idf":"v6.1","serial":"B43A45B7BFBC","stm8":null,"meter":false,'
     '"reset":"wdt","uptime_ms":5}'),
    ('rsp_error', '{"type":"rsp","seq":7,"op":"set","status":"error","code":"not_applied"}'),
    ('rsp_error_op', '{"type":"rsp","seq":20,"op":"fly","status":"error","code":"op"}'),
    ('status', '{"type":"status","n":42,"t":12345,"ports":[' + _PORTS + ']}'),
    ('event_boot',
     '{"type":"event","n":0,"t":12,"event":"boot","reset":"poweron","defaults":true}'),
    ('event_button',
     '{"type":"event","n":3,"t":5021,"event":"button","port":2,"power":false}'),
    ('event_fault', '{"type":"event","n":4,"t":6000,"event":"fault","port":3,"fault":true}'),
    ('error_crc', '{"type":"error","code":"crc"}'),
    ('error_arg_seq', '{"type":"error","code":"arg","seq":21}'),
]


def _write(name, rows):
    path = os.path.join(HERE, name)
    with open(path, 'w', newline='\n') as f:
        for a, b in rows:
            assert '\t' not in a and '\t' not in b
            f.write(f'{a}\t{b}\n')


def main():
    _write('frames.txt', FRAMES)
    _write('requests.txt', [(frame(j), s) for j, s in REQUESTS])
    _write('messages.txt', [(n, frame(j)) for n, j in MESSAGES])


if __name__ == '__main__':
    main()
