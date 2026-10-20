<!--
# SPDX-FileCopyrightText: Copyright 2026 Jetperch LLC
# SPDX-License-Identifier: Apache-2.0
-->

# Serial protocol v1

The host talks to the hub firmware over the ESP32-S3 USB-Serial-JTAG port
(USB 303a:1001, serial number = the hub's MAC address).  This document is
normative: the firmware (`firmware/esp32s3/components/uih_core/proto.c`)
and the host library (`host/python/src/usb_insight_hub/protocol.py`) both
implement it and test against the shared vectors in `test/vectors/`.


## Framing

Every message is one line:

```
<json>*<crc>\n
```

- `<json>` is a single JSON object, UTF-8, with no line breaks.
- `<crc>` is the CRC-32 (IEEE 802.3, as `zlib.crc32`) of the `<json>` bytes,
  as exactly 8 lowercase hexadecimal digits.
- The separator is the **last** `*` on the line, so `*` may appear inside
  JSON strings.
- A trailing `\r` before `\n` is ignored.
- The hub accepts request lines up to 256 bytes, excluding the line ending.

Receivers handle bad lines as follows:

| Condition | Hub (receiving a request) | Host (receiving from the hub) |
| --- | --- | --- |
| No `*` or not 8 hex digits | `error` message, code `format` | skip (for example ROM boot output) |
| CRC mismatch | `error` message, code `crc` | count and skip |
| CRC ok, JSON invalid | `error` message, code `parse` | count and skip |
| Line too long | `error` message, code `overflow` | n/a |

At boot, the ESP32-S3 ROM prints unframed text on this port.  Hosts must
skip lines that are not valid frames.


## Messages from the host: requests

Each request is an object with:

- `op`: the operation name (string, required).
- `seq`: a request sequence number, an integer 0 to 4294967295 (required).
  The hub echoes it in the response.  The host chooses it; incrementing
  per request is recommended.

Unknown keys are ignored.  Requests are absolute and idempotent: repeating
a request has the same effect as sending it once, so a host may retry
after a lost response.

| `op` | Arguments | Effect |
| --- | --- | --- |
| `ping` | none | none |
| `info` | none | report identity and versions |
| `status` | none | report the current state of all ports |
| `set` | `port`, and `power` and/or `data` | set port outputs, then verify |
| `stream` | `rate_hz` (integer 0..50) | set the status stream rate; 0 stops it |
| `bootloader` | none | respond, then reboot into the ESP32-S3 ROM bootloader |
| `reset` | none | respond, then reboot the firmware (port state is kept) |

`set` arguments:

- `port`: `1`, `2`, `3`, or `"all"`.
- `power`: `true` or `false` (optional).
- `data`: `true` or `false` (optional).  At least one of `power` and `data`
  is required.

Examples:

```
{"op":"set","seq":7,"port":2,"power":false}*<crc>
{"op":"set","seq":8,"port":"all","power":true,"data":true}*<crc>
{"op":"stream","seq":9,"rate_hz":10}*<crc>
```


## Messages from the hub

Every hub message has `type`: `rsp`, `status`, `event`, or `error`.

### Port state

Responses, status and events describe a port with this object:

| Key | Type | Meaning |
| --- | --- | --- |
| `port` | integer | 1..3 |
| `power` | bool | power output enabled, read back from the STM8 |
| `data` | bool | USB data enabled, read back from the STM8 |
| `fault` | bool | the port's load switch reports a fault |
| `mV` | integer | port voltage in mV (0 until the meter has a reading) |
| `mA` | number | port current in mA, one decimal; positive flows to the device |

### `rsp`: response to a request

```
{"type":"rsp","seq":7,"op":"set","status":"ok","ports":[{...}]}
```

- `status` is `"ok"` or `"error"`.  On error, `code` gives the reason
  and `msg` may add detail.
- Error codes: `op` (unknown operation), `arg` (missing or invalid
  argument), `io` (I2C failure), `not_applied` (the STM8 readback did not
  match after retries), `busy` (request queue full).

Successful responses carry these keys:

| `op` | Keys |
| --- | --- |
| `ping` | none |
| `info` | see below |
| `status` | `ports` (all three), `aux` (see below) |
| `set` | `ports` (the ports addressed by the request) |
| `stream` | `rate_hz` |
| `bootloader`, `reset` | none; the action follows about 100 ms later |

`info` keys:

- `fw`: firmware version, for example `"0.1.0"`.
- `git`: the firmware's git commit (short hash).
- `proto`: protocol version, `1`.
- `idf`: ESP-IDF version.
- `serial`: the MAC as 12 uppercase hex digits (the USB serial number
  without colons).
- `stm8`: STM8 firmware version, or `null` if the STM8 did not answer.
- `meter`: bool, whether the PAC1943 answered.
- `reset`: the reset reason (`poweron`, `sw`, `panic`, `wdt`, `usb`,
  `brownout`, `other`).
- `uptime_ms`: milliseconds since boot.

`aux` reports the STM8 auxiliary register: `vhost` and `vext` (bool:
supply present), `usb3` (bool: USB3 mux enabled).

### `status`: periodic status

Sent at the `stream` rate:

```
{"type":"status","n":42,"t":12345,"ports":[{...},{...},{...}]}
```

- `n` counts status messages since boot, including messages dropped
  because the host was not reading.  A gap in `n` means lost messages.
- `t` is milliseconds since boot.

### `event`: asynchronous event

```
{"type":"event","n":3,"t":5021,"event":"button","port":2,"power":false}
```

- `n` counts events since boot.
- `event` is one of:
  - `boot`: sent once at startup, with `reset` (reset reason) and
    `defaults` (bool: whether the firmware applied the power-up defaults).
  - `button`: a button toggled a port; includes `port` and `power`.
  - `fault`: a port's fault flag changed; includes `port` and `fault`.

The firmware reports faults only: it does not turn a faulted port off.
The load switch limits the current on its own.

### `error`: a request line that could not be handled

```
{"type":"error","code":"crc"}
```

`seq` is included when the request parsed far enough to provide it.


## Behavior

- At hub power-up, the firmware turns every port's power and data on.
  After a firmware-only reset (watchdog, `reset`, update), it keeps the
  ports as the STM8 holds them.  Nothing persists across power cycles.
- `set` writes the STM8, reads it back, and retries up to 3 times.  The
  response reports the read-back state, so a successful response means
  the hardware is in the requested state.
- Buttons toggle their port's power and produce a `button` event.
- Responses are never dropped.  Status messages are dropped when the
  USB transmit buffer is full (no host reading); `n` shows the gap.
- The current limit is fixed at 1.0 A per port.
