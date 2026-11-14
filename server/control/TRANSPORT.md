# Controller and cloud transport boundary

2026-11-14. Source inspection only; no hardware connection, flash or operation.

| Existing source | Present behavior | Cloud integration |
|---|---|---|
| `server/recorder.py` | Token-authenticated TCP recording, default 8765; receives serial capture, sends recording START/PING controls | Separate legacy local tool; not an encrypted public gateway and not part of native cloud service |
| `firmware/xiao/RooCoreXiao/RooCoreXiao.ino` | Local HTTP status/progress/recording/dirt APIs; bounded snapshots copied out of the control task | Firmware API is not publicly exposed by this deployment |
| `navigation/monitor.py` | Read-only, token-authenticated GET /progress; validates map, sensor validity, pose and coverage; replay/recording | Local dashboard reader, not a server-to-home route |
| `server/control/app/mqtt.py` | Receive-only MQTT with per-device ACL and strict bounded telemetry schema | Broker is loopback-only; no home gateway/client integrated |
| UDP | No application sensor UDP stream identified in inspected firmware/server entry points | No public UDP sensor listener is deployed |

MQTT is useful for regular status, events and small messages, independently of
voice assistants. It runs over TCP; it is not a replacement for TCP at the
transport layer. Large LiDAR scans/map data need an explicit bandwidth and queue
budget; measure actual scan sizes/rates before selecting MQTT chunking or a
separate TLS stream. A UDP sensor path would additionally need authentication,
encryption, sequencing, reassembly bounds and loss handling. No unprotected TCP
or UDP port should be opened on the home router or Linux server for this.

## Required controller behavior for the next integration

- Control, collision/cliff/fault checks and bounded navigation decisions stay on
  the controller. Network and JSON serialization must not block its control task.
- Telemetry uses boot identity, sequence, acquisition time and sensor validity;
  server receipt alone cannot make stale data fresh. Keep map and pose identity
  together. Large-frame processing belongs on the server where appropriate.
- Bounded queues replace old telemetry during outages; no unbounded buffering,
  retained motion commands or replay of a previous mission after reconnect.
- Cloud results are advisory, versioned and time-bounded. The controller may
  reject them. Required barriers/mission policy must be persisted and validated
  locally before a mission can rely on them during a network outage.
- Normal local cleaning should continue when healthy and sufficiently localized
  under its already accepted policy. If required local evidence or policy is
  missing, hold safely rather than guessing. Network loss alone does not waive
  sensor faults, map restrictions or local safety.
- Existing `ControlArbiter.h` expires remote commands, zeros drive, changes epoch
  and transitions through Stopping before a healthy active mission can return
  to Local. Its 250 ms command horizon and 500 ms settling are source constants,
  not measured stopping times. New network integration must preserve this
  behavior and verify it on the actual 620 before actuator commissioning.

No claim of working sensor streaming, cloud SLAM, synchronized physical barriers
or autonomous operation follows from publishing the web console. Next gates are
an authenticated private controller route, firmware/client integration, measured
transport/CPU budgets, map-policy acknowledgement, then physical validation.
