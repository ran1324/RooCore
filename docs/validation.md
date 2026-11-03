# Validation — 2026-11-04

- Firmware compiled successfully for ESP-01 1 MB using PlatformIO espressif8266 4.2.1 and the Arduino ESP8266 software framework. No extra Arduino board is required.
- Build usage: RAM 28,832 bytes; flash 278,747 bytes.
- Eleven Python tests passed, including stream parsing, signed values, corrupted frames, sequence loss, capture changes, TCP authentication, fragmented messages and reconnecting into the same recording.
- A standalone C++ test passed for pin assignments, BRC timing and timer rollover, command parsing and sensor-stream command construction.
- The connection uses the project's own TCP recorder server, without MQTT.
- No ESP or robot was connected or flashed. No real telemetry was captured.
- Hardware validation remains pending: selected transistor pinout and signal levels, power stability, Roomba 620 packet support, BRC behavior, Wi-Fi continuity and agreement between wheel counts and observed movement.

Work is in `/Users/ransaymour/Documents/RooCore-recorder`, a separate local clone because `/Volumes/T9` was unavailable. Nothing was pushed to GitHub.
