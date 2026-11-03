# Upstream baseline

Reference: [thehookup/MQTT-Roomba-ESP01](https://github.com/thehookup/MQTT-Roomba-ESP01), commit `7ad7cd09321a43e0b74d5bf430566a7bcb32051b`.

The owner selected its battery/buck/PNP arrangement and 600-series pinout as RooCore's starting point.
This is recorded as a project decision, not an option to reopen with each change.

## Retained

- ESP-01S powered by the Roomba battery through a buck converter.
- PNP receive interface, using a 2N3906 already in the owner's BOJACK assortment.
- Normal UART0: GPIO1 TX, GPIO3 RX at 115200 baud.
- GPIO2 connected to BRC: low for one second every minute, high otherwise.

The upstream comment calls GPIO2 "D4" because it refers to a NodeMCU label. Our board is the ESP-01S.
Its shared schematic image does not include the BRC wire added in the 600-series sketch.

## RooCore changes

- Direct Wi-Fi/TCP link to our own server; no MQTT broker or MQTT commands.
- One serial reader and a continuous OI stream, replacing the independent five-second battery queries.
- Passive-only telemetry. Native cleaning is started with the robot button.
- All received sensor bytes, decoded frames and reconnect events are recorded for later analysis.
- Timed BRC and start sequencing, without blocking reconnect loops or timer callbacks writing to Serial.
- The specific PNP uses E to ESP RX, B to Roomba TX, C to ground, plus an explicit RX pull-up.
  The generic upstream Fritzing C/E connector labels differ; see [wiring](wiring.md#pnp-pin-names).

The earlier AP/UDP recorder and two-level-shifter proposal have been replaced. GPIO2 is no longer TX.
Arduino remains the ESP8266 software framework used to build the firmware; there is no extra Arduino board.

## Next stage

Record full native cleaning cycles and compare the observations with video/notes.
Then evaluate odometry and the planned LiDAR path before adding a custom planner and motor commands.
The LiDAR design is [saved separately](lidar-plan-he.md); it is not implemented in this release.
