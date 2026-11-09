# RooCore LiDAR power sequence

Integrated into the full `firmware/xiao/RooCoreXiao/RooCoreXiao.ino` application. The small OffByDefault example is only a bench starting point. Arduino-ESP32 3.3.12 was used for full application builds; no hardware was flashed.

UART2 belongs to LiDAR; UART1 remains the Roomba interface. XIAO D3/GPIO4 drives Pololu 2810 ON, D4/GPIO5 drives sensor RX, and D5/GPIO6 receives sensor TX. All methods and pins have one owner: setup before task creation, then the firmware control task. HTTP only enqueues requests.

`begin()` establishes the off state. `requestOn(now, qualifiedRiseDelayMs)` powers the rail without blocking; `poll(now)` attaches UART after the qualified interval and returns true once on the transition to ready. Zero or invalid intervals are refused. The application then sends its scan command. Firmware defaults `ROO_LIDAR_POWER_VERIFIED=false` and `ROO_LIDAR_RISE_MS=0` require actual bench qualification before activation.

`powerOff()` disallows writes, checks TX completion without waiting, detaches/cancels UART, drives TX LOW and RX input pulldown, and only then lowers ON. It returns false if TX had to be cancelled; shutdown still completes. Startup cancellation cannot be resurrected by a later tick. `abort()` follows the same shutdown order and latches a fault until a normal off/reset. Reads/writes are refused while off or warming; queued RX is discarded on detach.

The firmware routes idle, charging and battery-shutdown power-off through this sequence. No GPIO7/OE or GPIO43/44 LiDAR wiring remains. It resets parser validity when the rail goes off or UART fails. Ready indicates UART readiness, not valid range data.

This removes an active host signal source during intentional LiDAR shutdown. It is not hardware isolation and does not establish behavior during unexpected host power loss, brownout, reset, external rail removal or sleep. Hardware qualification remains required.

Run `sh tests/run.sh` and `sh tests/build_xiao.sh` in the full firmware checkout. The host model checks 100 cycles and failure cases; the build script compiles default and synthetic commissioned configurations. Synthetic settings are never calibration or a flash instruction.
