# Private Wi-Fi setup — XIAO ESP32S3

Status: integrated source, host/browser simulation and compiler checks; not flashed or tested on a board. This extends the [cloud stream](CLOUD_STREAM.md). Existing commissioning gates stay disabled.

## First setup

1. Provision a different, unique `ROO_SETUP_AP_PASSWORD` (12–63 printable ASCII characters, no spaces) in the private configuration, alongside the separately provisioned device key. The public example is empty and cannot open a setup AP. Home SSID/password may remain empty.
2. Initial board verification must use USB only, with the XIAO fully disconnected from the robot, harness and other power sources. Confirm the exact board/port, image and recovery procedure before flashing. This document is not a report of flashing or a ready-to-run flash command.
3. With the intended image running and no saved/compiled home network, the controller requests setup. The control core admits it only in an idle/paused/completed state. Uncommissioned USB preparation additionally requires no verified robot link and no owned motion; the software cannot prove physical disconnection.
4. Join `RooCore-Setup-xxxxxx` using the private **setup AP** password. Open **http://192.168.4.1/setup** manually. This local HTTP page runs inside a WPA2-PSK/CCMP protected AP with a maximum of one client; it is separate from the HTTPS management console.
5. Scan and select a 2.4 GHz network or type a hidden SSID. Enter its password and choose Connect and save. SSIDs are limited to 32 bytes; password is empty for an open network or 8–63 printable ASCII characters. Enterprise Wi-Fi and 64-digit raw PSKs are not supported by this setup form. The page checks connectivity to the local Wi-Fi network, not Internet/cloud reachability.
6. The controller tests the connection before committing one versioned/checksummed NVS blob, then reads it back. Passwords never appear in API responses or serial logs. A failed join does not replace the saved record; a failed write/readback is reported as a storage error. A power failure during NVS replacement can retain the old or new complete record. This is integrity detection, not encryption or protection against physical flash extraction.
7. After success, reconnect the phone to the home network and open `https://roocore.saymour.co.il`. The setup AP closes after an 8-second response window. Setup expires after five minutes; join attempts expire after 25 seconds. These are software UX budgets. Setup completion/timeout never starts or resumes cleaning.

## Changing network

While the running robot is already stopped, hold **BOOT** for three seconds, then release. Do not hold BOOT during reset/power-on: it selects the ROM download mode. BOOT is **GPIO0** on the exact XIAO ESP32S3; the header pin **D0 is GPIO1**, used by OI, and is not the setup button. No additional wiring is required. [Manufacturer pin table and BOOT instructions](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).

The request is ignored during cleaning, undocking, native operation or OTA. A commissioned robot requires fresh qualified encoders with the existing two-second stable-wheel software window, fresh Passive OI and hazard readings, cleaning outputs off, and the qualified flash-save voltage floor. Loss of that admission closes setup. This software gate is not independent proof of standstill. Physical-button operation, RF scanning, coexistence with the home network and NVS power-loss recovery still require real-board verification.

## Isolation and offline operation

- A routine Internet or home-Wi-Fi outage never opens setup. It retries the known station network and retains the local mission and restrictions.
- Setup holds the control arbiter, blocks all local command requests and suspends cloud uploads. HTTP and Wi-Fi work stay off the control core. AP closure releases the setup hold only; a separate explicit command is needed to resume a mission.
- Setup cannot change the device token, server hostname, firmware, commissioning flags, map, no-go zones or their sticky checkpoint requirement. It uses a separate NVS namespace; it never formats LittleFS.
- Setup routes require an active AP, an AP-destination connection and AP-subnet source, exact IP Host, and POST additionally requires exact Origin plus a fresh random 128-bit nonce. The normal connection POST is streamed into a fixed 512-byte buffer; duplicate/unknown fields and malformed percent encoding are rejected. Browser responses prohibit caching/framing and remote assets. Scanned SSIDs are inserted as text, never HTML.
- There is no setup DNS server, DNS forwarding, NAT or home-router port opening. Enter the explicit local address. Existing LAN/OTA authentication remains separate. AP isolation, actual RF security and timing cannot be established by host tests.

Validation covers record limits/corruption, wraparound budgets, the actual page in a mock browser, and default/synthetic firmware builds. See the [assembly validation record](assembly/VALIDATION.md). [Arduino Wi-Fi API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html); build uses installed Arduino ESP32 **3.3.12** source for exact signatures.
