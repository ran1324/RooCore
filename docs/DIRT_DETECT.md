# DirtDetect: collect data before enabling automatic responses

The current stage is **collection and analysis only**. The old response to a single reading above 90 has been removed from both the XIAO core and the reference engine. There is currently no switch that enables automatic cleaning in response to dirt. No physical Roomba data was collected and no firmware was flashed as part of this change.

The user's requested automatic mode, persistent cleaning history and maintenance recommendations are now accepted requirements in the [implementation plan](SMART_CLEANING_PLAN.md) ([Hebrew](SMART_CLEANING_PLAN-he.md)). That plan defines the next behavior and validation gates; it does not enable the observation-only detector described here.

## Stored data

Packet 15 is decoded as an unsigned value from 0–255; packet 16 is a separate byte, not a dirt measurement. The library already stored the latest reading with its reception time and generation. `setSampleObserver` now receives every measurement from every valid frame after the entire frame has been decoded, including multiple frames in one `poll`. An invalid checksum or payload produces no reading. The reading is not a calibrated quantity of dirt.

Each record includes a sequence number, MCU reception time, generation, session, value, ordinary action being performed and PWM. When RooCore owns control and has a valid anchored global pose, an estimated pose within 100 ms of the reading is attached. Otherwise, `pose` and `frame` are `null`: a provisional local map during localization loss is not automatically associated with the global map either. Reception time is not the sensor's internal sampling time. Resetting/correcting the anchor changes the frame; frames are not automatically joined. Coordinates use `roocore_odom`: anchored odometry with local LiDAR matching, not AMCL localization in the server map. Displaying them on a SLAM map requires a valid transform for that frame and time.

The XIAO keeps 256 records in a RAM ring, rather than unlimited history or a Flash write per sample. The receive queue holds 64 measurements; queue loss or corrupt frames break detection continuity and generate a gap record. A client that does not fetch in time receives explicit `lost_records`.

## Detection and proposed extra pass

Settings in `firmware/xiao/RooCoreXiao/config.example.h` (copy to `config.local.h`):

| Setting | Default | Meaning |
|---|---:|---|
| `ROO_DIRT_THRESHOLD` | 0 | Collection only; 1–255 enables experimental detection without changing motion |
| `ROO_DIRT_CONSECUTIVE` | 5 | At least five consecutive high readings |
| `ROO_DIRT_MIN_HIGH_MS` | 300 | The sequence must last at least 300 ms |
| `ROO_DIRT_MAX_GAP_MS` | 200 | A longer gap cancels the sequence |
| `ROO_DIRT_REGION_RADIUS_M` | 0.30 | Local sequence and comparison region, in meters |
| `ROO_DIRT_REVISIT_BUDGET_MS` | 20000 | Time limit for the proposal and revisit comparison |

A low value, frame/PWM change, leaving the region, invalid information, safety event, battery at 25% or below, or communication loss interrupts active analysis. Invalid localization does not erase raw data. The collection tool records server disconnections; lost Roomba transport or expired server ownership stops comparison on the MCU. Motion safety always takes precedence.

Detection creates `region_detected` with `proposed_action=one_extra_pass`, state `deferred_calibration`, a radius, a 20-second limit and one extra pass per region. The proposal executes no motor command or route. Each session supports up to 16 regions; the same region does not generate repeated proposals.

When the **ordinary** route leaves the region and returns, the start and result of one natural revisit are recorded. Comparison requires enough readings and sampling time, the same frame and the same PWM values; otherwise the result is inconclusive. The detection average, revisit average and whether readings are lower are stored. Safety/battery/communication/localization interrupts comparison with a reason. Lower readings do not prove cleanliness: the first average was selected from a high sequence, and floor, motion and sensor conditions can change.

## API and local collection

`GET /dirt?after=0`, with the existing Bearer token, returns up to 32 records, `boot_id`, `oldest`, `latest`, `next`, `has_more`, loss counters and analysis settings. Continue from `next`; `(boot_id, seq)` identifies a record. The proposal and reason describe what was not performed automatically; `action` describes ordinary cleaning/driving. For region events, the position is the previously recorded region center, not necessarily the robot's position at event time.

```sh
export ROOCORE_URL=http://ROBOT_IP
read -s ROOCORE_TOKEN
export ROOCORE_TOKEN
python3 -m navigation.dirt_capture --output recordings/dirt.jsonl --label clean-floor
python3 -m navigation.dirt_capture --report recordings/dirt.jsonl
```

The tool sends GET requests only. It saves JSONL with server reception time, source data and connection/record gaps, and stores a cursor beside the file to resume after restart. Data is written before advancing the cursor; duplicates after a crash are identified by record ID. The summary includes reading count, how many have associated positions, median/p95 and range, detections and comparisons. It does not choose a threshold automatically. Files under `recordings/` are excluded from Git.

## Stasis and maintenance

Separate diagnostics were added for packet 58, disabled until `ROO_STASIS_VERIFIED=true` after verification on our model. `0` means no forward progress and is also valid while stopped, turning or reversing; `1` means forward progress; `2` indicates a caster wheel too dirty to read; `3` or a stale measurement is not a valid progress source. Value 2 does not mean stuck or slipping. The library returns `usableForProgress=false`, and firmware displays a maintenance message in `/status` and logs: inspect and clean the caster wheel, and dry it before reinstalling. This is a possible reason to check general maintenance, not proof that the entire robot is dirty. Current navigation does not use Stasis as a localization source anyway.

## Validation and next steps

Simulated transport tests cover: the 0–255 range, query and stream, sequence/threshold/gaps, checksum, multiple frames in one poll, no pose association with invalid localization, rollover, natural comparison, safety/battery/communication precedence, region and time limits, ring overflow, Stasis decoding and HTTP with cursor/restart/gaps. A regression compares route and PWM with and without dirt, ensuring no automatic response was added. Builds for both XIAO configurations are also required; results are recorded in `VALIDATION_XIAO.md`.

**Not tested on hardware:** availability/meaning of the measurement on this 620, actual rate, time-to-position alignment, memory during Wi-Fi operation, collection resilience, threshold, false-positive rate and extra-pass effectiveness. Next come labeled recordings on clean and dirty floors under several operating conditions. Automatic cleaning behavior will only be added after analyzing them, and its stopping behavior will be tested on the robot.

Source: [iRobot specification, packet 15 on p. 24 and packet 58 on p. 32](https://cdn.hackaday.io/files/1835247851890816/2021-04-13_iRobot_Roomba_600_Open_Interface_Spec-3.pdf). Protocol availability is not hardware validation of our Roomba.
