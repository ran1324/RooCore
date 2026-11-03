# RooCore TCP protocol v2

The ESP initiates a TCP connection to the configured server IPv4 address, port 8765 by default.
Messages are UTF-8 lines terminated by LF. Maximum ESP record size accepted by the server: 2048 bytes.
The transport is plaintext on a trusted LAN. A shared 48-character hex token authenticates the device;
it does not encrypt traffic or provide mutual authentication. The server never stores the token in run logs.

## Handshake and server commands

ESP sends a JSON object with `type: hello`, `protocol: 2`, `device: roocore-<chip-id>`, a random uint32 `boot` ID and `token`.
After validating it, the server sends:

```text
READY
START wheels
```

Commands are exact lines: `READY`, `START basic`, `START wheels`, `PING`, `STOP`.
There is no arbitrary-serial command or driving API. The server sends PING every two seconds.
The ESP pauses telemetry after ten seconds without a heartbeat or on TCP link loss, then reconnects.
STOP stops the sensor stream only. BRC continues while the ESP is powered.

START sends OI 128 (Passive), waits 50ms without blocking the loop, then sends 148 (Stream) and 150/1 (Resume).
STOP sends 150/0. No baud-change, Safe, Full, Clean, Dock, Drive or Reset commands are sent.
A repeated START for the active profile is idempotent. A new capture increments `capture` and resets `seq`.

## ESP records

Serial example:

```json
{"type":"serial","boot":123,"capture":1,"seq":0,"ms":1000,"overruns":0,"dropped":0,"hex":"13020701e3"}
```

This sample encodes OI packet 7 with value 1 and a valid checksum.

| Field | Meaning |
|---|---|
| boot | Random boot ID, fixed until ESP restart |
| capture | Stream session counter within that boot |
| seq | Serial-record sequence, increments even if that record is discarded |
| ms | ESP millis when that chunk is forwarded; uint32 rollover applies |
| overruns | Cumulative observed UART receive-overrun events in this capture |
| dropped | Cumulative serial bytes discarded when TCP had no send space |
| hex | Up to 256 bytes read from the serial port |

The ESP batches reads for up to 5ms, with a 2048-byte UART buffer. It checks TCP send space before writing;
there is no flash storage or replay queue. Dropped bytes cannot be recovered by reconnecting.
The server records gaps on disconnects, boot/capture changes, sequence gaps, UART overruns, drops and checksum errors.
Sensor frames that share a chunk share a timestamp; this is not the robot's exact sampling time.

A status record is sent approximately every second with `type: status`, `boot`, `capture`, `ms`, `recording`, `overruns` and `dropped`.
The server closes a silent connection after six seconds. It keeps the run open for a reconnect until Ctrl-C or an explicit time limit.

## Recorded fields

Basic: packets 7–15, 17, 18, 21–26 and 35.
Wheels: basic plus 43 and 44. No values are synthesized for unavailable packets.
The parser preserves encoder bit patterns as uint16 and handles rollover when deriving differences.
The old binary RC01 decoder remains only for compatibility with old captured data/tests; live v2 uses JSON over TCP.
