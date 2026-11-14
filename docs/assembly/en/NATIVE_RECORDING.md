# XIAO native-cleaning capture and wireless firmware updates

[Hebrew](../NATIVE_RECORDING.md) · [Assembly guide](RooCore-assembly.html)

This is integrated into the full RooCoreXiao application, not a replacement ESP8266 sketch. No flashing, physical operation, measurements or hardware OTA tests have been performed. Complete the level-translator mapping, harness and OI qualification first. No wiring changes or battery-connection instructions are introduced. The electronics and esp32-development skills were used.

## Capture contents and behavior

`record-start` holds RooCore navigation and switches the sensor stream. It does not start cleaning: press CLEAN on the robot. Recording can also begin during native cleaning when RooCore has no active mission and the robot reports fresh Passive mode. Starting, collecting and stopping recording sends no drive, motor, Clean, Safe or Full commands. LiDAR is powered off through the existing ordered sequence; the physical anchor is invalidated because native motion makes the prior pose unreliable.

- `base`: group 6, all IDs 7–42: safety, dirt, IR, buttons, raw distance/angle, battery voltage/current/temperature/charge/capacity, wall/cliff strengths, charging sources, OI mode and reported motion-command fields. Reserved fields are retained as raw values, not useful sensors.
- `extended`: group 100, all IDs 7–58. Adds encoders, light bumper, side IR, four wheel/brush motor currents and processed Stasis state. Requires `ROO_RECORD_EXTENDED_ENABLED=true` in the firmware configuration. This permits a support probe; it does not qualify the readings on this robot. Returned numbers can still be inapplicable or constant. Stasis is not raw caster rotation.

There is no OI packet for vacuum motor current/RPM, air pressure/flow or the internal PWM used during native Clean. IDs 39–42 report motion commands, not measured wheel velocity. Known firmware distance/angle/radius bugs remain raw. Neither the model name nor a returned number proves physical capability.

The XIAO retains 256 validated frames in PSRAM, serving up to 16 per HTTP response, without high-frequency flash logging. At a 15ms frame cadence this represents about 3.84 seconds; actual robot/network timing is not guaranteed. The collecting computer must remain running. Network loss does not stop native cleaning; overwritten records are detected by sequence gaps. Each frame includes MCU receive time, generation, sequence and loss/rejection counters. Frame length, layout and checksum are checked before commit; this is not a raw UART trace of corrupt bytes.

## One command to record

After the initial firmware installation and local Wi-Fi/API-token configuration, put the token in a local file outside Git. Passive recording permits `ROO_COMMISSIONED=false`. No second control program may own the same UART.

```sh
python3 tools/xiao_capture.py --url http://XIAO-IP --token-file .roocore-token record --profile base --output recordings/native-baseline
```

Use `--profile extended` after enabling the explicit probe flag. Optional `--seconds 900` limits computer capture duration. Ctrl-C or expiry sends `record-stop`, drains the retained frames and saves:

- `frames.jsonl`: reported OI values by numeric ID; unavailable fields remain absent.
- `sensors.csv`: named fields and calculated battery discharge watts; missing values remain blank.
- `session.json`: boot/session and firmware build identity, loss counts, sensor dictionary and estimated discharge/charge Wh.

Power is battery voltage multiplied by reported current; negative current means discharge. Integration separates charging/discharging and excludes sequence gaps, changed rejection/source-loss counters and intervals over 200ms. This is an analysis policy, not guaranteed sensor accuracy. An external power branch bypassing the Roomba shunt may be absent from reported consumption. Electrical power does not establish suction pressure or an individual motor's mechanical power.

`record-stop` ends logging only. Native cleaning may continue; RooCore remains in observation hold. Explicit exit stops native operation and requires commissioned stop control and qualified encoders:

```sh
python3 tools/xiao_capture.py --url http://XIAO-IP exit
```

Use `fetch --output recordings/recovered` instead of `record ...` to attach to an existing session or retrieve the remainder after disconnect; overwritten history cannot be recovered. Use `status` to inspect state or `stop` to stop capture from another computer. A link failure preserves partial results; different boots/sessions are never silently merged. Finish native cleaning with the robot controls if communications are lost; do not assume a command was received.

## API

All routes require `Authorization: Bearer <token>` on a trusted LAN. HTTP is unencrypted and must not be exposed to the Internet. POST `/command` accepts `action=record-start&profile=base|extended`, `record-stop`, `record-exit`, `ota-arm` and `ota-exit`. The two native-stop actions, `record-exit` and `ota-arm`, also require `confirm=stop-native-and-hold`. HTTP 202 means queued, not completed; inspect `/status` and `/recording`.

GET `/recording?after=0` returns identity, profile, state, ring bounds and rows. Subsequent requests use `after=<next>&boot_id=<id>&session=<number>`. Changed identity or a future cursor returns 409. `lost_records` counts overwritten rows before that cursor; rows also carry source-loss and rejected-frame counters. `hardware_verified=false` records project validation status.

## Wireless algorithm updates

OTA updates our XIAO application, not iRobot firmware. The first installation still needs USB with XIAO disconnected from the entire harness under the single-source power arrangement. OTA defaults off. After qualifying stop control, encoders, supply stability and flash writes, set `ROO_OTA_ENABLED=true`, a separate random password of at least 24 characters, `ROO_OTA_POWER_VERIFIED=true` and a measured `ROO_OTA_MIN_MV` above hardware-cutoff uncertainty. The enabled build fixture's 14500mV is synthetic, not a recommended installation threshold.

The `ota` command requests service hold, ends capture, stops the robot in Safe, powers LiDAR off and waits for two seconds without encoder changes while requiring fresh Safe-mode telemetry and sufficient voltage. This is a software observation window, not a physical standstill guarantee. Motion, unsafe/stale telemetry, mode changes or low voltage close the OTA gate. Navigation commands remain blocked. Failed OTA retains service hold and does not resume cleaning. Safe mode terminates charging, so the dock is not assumed to supply continuous power during updates.

```sh
sh tools/build_xiao_ota.sh
python3 tools/xiao_capture.py --url http://XIAO-IP ota --manifest output/xiao-ota/manifest.json --password-file /path/to/ota-password --espota /path/to/esp32/3.3.12/tools/espota.py
```

The tool checks the local SHA-256, ESP32-S3 application header and app-slot size, then invokes the official Arduino-ESP32 uploader. Only application OTA is accepted; filesystem/calibration/partition updates are excluded. The existing USB-installed partition layout must be `default_8MB`, with two `0x330000` app slots. Preserve local credentials and calibration in each new build.

ArduinoOTA supplies password authentication and image integrity checking; this implementation has no HTTPS, firmware signature or guaranteed post-boot automatic rollback. A failed upload before boot-slot selection is expected to retain the previous application, but power interruption and bad-image recovery have not been tested on hardware. Retain USB recovery. The tool waits for a new boot ID and displays version/build; this does not prove hardware function or verify the remote image digest.

Use `ota-exit` to leave service without updating. Physical reanchoring and an explicit new mission are required after exit/update. Algorithm changes are made in code, tested and compiled before upload; recordings do not automatically modify the algorithm.

## Evidence and checks

- iRobot Roomba 600 OI: page 7 Passive; page 15 motor PWM; pages 23–32 sensors; pages 40–41 firmware differences/bugs. [Manufacturer document copy](../sources/iRobot-Roomba-600-OI.pdf).
- [Arduino-ESP32 3.3.12 ArduinoOTA](https://github.com/espressif/arduino-esp32/tree/3.3.12/libraries/ArduinoOTA), [espota](https://github.com/espressif/arduino-esp32/blob/3.3.12/tools/espota.py), [default_8MB partition table](https://github.com/espressif/arduino-esp32/blob/3.3.12/tools/partitions/default_8MB.csv).
- Host checks cover base/extended frame commits, rejected checksums, ring loss, gaps, wraparound, charging/discharging and OTA hold. XIAO builds cover defaults and an enabled synthetic recorder/OTA fixture. Final results: [validation record](../NATIVE_RECORDING_VALIDATION.md). No physical measurements.
