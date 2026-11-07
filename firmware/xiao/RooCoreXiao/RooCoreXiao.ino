#include <Arduino.h>
#include <ControlArbiter.h>
#include <LittleFS.h>
#include <NavigationHelp.h>
#include <RooNavigation.h>
#include <RoombaOIArduino.h>
#include <TminiPlus.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <new>
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

// Real hardware UARTs. USB CDC is diagnostic only, never the Roomba UART.
HardwareSerial roombaSerial(1), lidarSerial(2);
roocore::Esp32Transport robotLink(roombaSerial, UART_NUM_1, 8);
roocore::Capabilities capabilities;
roocore::Roomba *robot;
roonav::Navigator *navigator = nullptr; // large maps belong in external PSRAM
roonav::Navigator *primaryNavigator = nullptr, *provisionalNavigator = nullptr;
roonav::NavigationHelp assistance;
roonav::Odometry odometry;
roonav::TminiPlus lidar;
roonav::ControlArbiter arbiter;
WebServer web(80);
QueueHandle_t requests;
SemaphoreHandle_t statusMutex;
String publishedStatus, publishedHelpMap;

struct Request {
  char action[24];
  uint32_t epoch, sequence, expires;
  int left, right, pwm;
  roonav::Pose candidate, reference;
};
struct SavedMission {
  uint32_t magic = 0x524f4f31, version = 4, sequence = 0, crc = 0;
  roonav::Config config;
  roonav::Pose dock, checkpoint, pose;
  uint8_t state, pass, passes, recoveries;
  uint32_t skipped, completed;
  bool finished;
  int8_t cells[roonav::Navigator::MaxCells];
  uint8_t covered[roonav::Navigator::MaxCells], excluded[roonav::Navigator::MaxCells];
  uint8_t sideCovered[roonav::Navigator::MaxCells], edgeSeen[roonav::Navigator::MaxCells];
  uint8_t contacts[roonav::Navigator::MaxCells], retries[roonav::Navigator::MaxCells];
  uint32_t edgeSegments, edgeIncomplete;
};
SavedMission saved;
bool undocking = false, startAfterUndock = false, deepAfterUndock = false;
uint32_t undockAt = 0, undockProgressAt = 0;
float undockBest = 0;
bool storageReady = false, anchored = false, nativeDock = false, enabledLidar = false,
     linkReady = false;
uint32_t lidarOnMs = 0, lastScan = 0, lastGeneration = 0, sequence = 0, armingSince = 0;
uint32_t lastControl = 0, lastPublish = 0, lastSaved = 0, anchorAt = 0;
float alignmentScore = 0;
uint8_t appliedPwm = 0, appliedSidePwm = 0;
bool ownsRobot = false;
String lastError = "not commissioned";
roonav::Pose trail[16];
uint32_t trailMs[16]{};
uint32_t wheelDropSince = 0;
unsigned trailAt = 0;
roonav::Input latest;

bool terminal(roonav::State s) {
  return s == roonav::State::Idle || s == roonav::State::Fault || s == roonav::State::Paused ||
         s == roonav::State::Complete;
}
bool active() { return !terminal(navigator->state); }
bool movingSession() { return active() || undocking; }
void lidarPower(bool on) {
  if (on == enabledLidar)
    return;
  enabledLidar = on;
  lidar.reset();
  lastScan = 0;
  if (on) {
    digitalWrite(4, HIGH);
    lidarOnMs = millis();
  } else {
    digitalWrite(7, LOW);
    digitalWrite(4, LOW);
  }
}
void stopMotion() {
  appliedPwm = appliedSidePwm = 0;
  // Passive Drive packets are not an emergency stop for native docking.
  // Safe stops native cleaning/docking without disabling cliff protection.
  if (nativeDock || (ownsRobot && robot->requestedMode() != roocore::Mode::Safe)) {
    robot->enterSafe();
    nativeDock = false;
  }
  if (robot->requestedMode() == roocore::Mode::Safe)
    robot->releaseControl();
  armingSince = 0;
  ownsRobot = false;
}
uint32_t checksum(const SavedMission &value) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof value; ++i) {
    if (i >= offsetof(SavedMission, crc) && i < offsetof(SavedMission, crc) + 4)
      continue;
    h = (h ^ bytes[i]) * 16777619u;
  }
  return h;
}
bool saveMission() {
  if (!storageReady)
    return false;
  stopMotion();
  if (!robotLink.finishTransmit(50))
    return false; // never write flash while motors are commanded
  saved.config = navigator->cfg;
  saved.dock = navigator->dock;
  saved.checkpoint = navigator->checkpoint;
  saved.pose = odometry.pose;
  saved.state = uint8_t(navigator->savedState());
  saved.pass = navigator->pass;
  saved.passes = navigator->passes;
  saved.recoveries = navigator->recoveries;
  saved.skipped = navigator->skipped;
  saved.completed = navigator->completedCells;
  saved.finished = navigator->finished;
  saved.edgeSegments = navigator->edgeSegments;
  saved.edgeIncomplete = navigator->edgeIncomplete;
  memcpy(saved.contacts, navigator->contacts, sizeof saved.contacts);
  memcpy(saved.retries, navigator->retries, sizeof saved.retries);
  memcpy(saved.sideCovered, navigator->sideCovered, sizeof saved.sideCovered);
  memcpy(saved.edgeSeen, navigator->edgeSeen, sizeof saved.edgeSeen);
  memcpy(saved.cells, navigator->cells, sizeof saved.cells);
  memcpy(saved.covered, navigator->covered, sizeof saved.covered);
  memcpy(saved.excluded, navigator->excluded, sizeof saved.excluded);
  ++saved.sequence;
  saved.crc = checksum(saved);
  // Alternating complete snapshots: a torn write leaves the other slot intact.
  const bool provisional = navigator != primaryNavigator;
  const char *path = provisional ? (saved.sequence & 1 ? "/local-a" : "/local-b")
                                 : (saved.sequence & 1 ? "/mission-a" : "/mission-b");
  File file = LittleFS.open(path, "w");
  if (!file)
    return false;
  bool ok = file.write(reinterpret_cast<uint8_t *>(&saved), sizeof saved) == sizeof saved;
  file.flush();
  file.close();
  lastSaved = millis();
  return ok;
}
bool readSlot(const char *path, SavedMission &out) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != sizeof out)
    return false;
  bool ok = file.read(reinterpret_cast<uint8_t *>(&out), sizeof out) == sizeof out;
  file.close();
  return ok && out.magic == 0x524f4f31 && out.version == 4 && out.crc == checksum(out) &&
         out.pass < out.passes && out.passes >= 1 && out.passes <= 2 &&
         out.state <= uint8_t(roonav::State::Yielding);
}
void loadMission() {
  if (!storageReady)
    return;
  auto *other = static_cast<SavedMission *>(
      heap_caps_malloc(sizeof(SavedMission), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!other)
    return;
  bool a = readSlot("/mission-a", saved), b = readSlot("/mission-b", *other);
  if (b && (!a || int32_t(other->sequence - saved.sequence) > 0))
    saved = *other;
  heap_caps_free(other);
  if (!a && !b)
    return;
  if (!isfinite(saved.dock.x) || !isfinite(saved.dock.y) || !isfinite(saved.dock.yaw) ||
      !isfinite(saved.checkpoint.x) || !isfinite(saved.checkpoint.y) ||
      !isfinite(saved.checkpoint.yaw))
    return;
  // Reject checkpoint after geometry/calibration changes.
  if (saved.config.cols != navigator->cfg.cols || saved.config.rows != navigator->cfg.rows ||
      saved.config.resolution != navigator->cfg.resolution ||
      saved.config.metersPerTick != navigator->cfg.metersPerTick ||
      saved.config.wheelSeparation != navigator->cfg.wheelSeparation ||
      saved.config.brushWidth != navigator->cfg.brushWidth ||
      saved.config.brushDepth != navigator->cfg.brushDepth ||
      saved.config.brushOffset != navigator->cfg.brushOffset ||
      saved.config.sideX != navigator->cfg.sideX || saved.config.sideY != navigator->cfg.sideY ||
      saved.config.sideRadius != navigator->cfg.sideRadius ||
      saved.config.originX != navigator->cfg.originX ||
      saved.config.originY != navigator->cfg.originY ||
      saved.config.radius != navigator->cfg.radius || saved.config.margin != navigator->cfg.margin)
    return;
  for (int i = 0; i < navigator->count(); ++i)
    if (saved.cells[i] < -1 || saved.cells[i] > 1)
      return;
  navigator->edgeSegments = saved.edgeSegments;
  navigator->edgeIncomplete = saved.edgeIncomplete;
  memcpy(navigator->contacts, saved.contacts, sizeof saved.contacts);
  memcpy(navigator->retries, saved.retries, sizeof saved.retries);
  memcpy(navigator->sideCovered, saved.sideCovered, sizeof saved.sideCovered);
  memcpy(navigator->edgeSeen, saved.edgeSeen, sizeof saved.edgeSeen);
  navigator->dock = saved.dock;
  navigator->checkpoint = saved.checkpoint;
  navigator->pass = saved.pass;
  navigator->passes = saved.passes;
  navigator->recoveries = saved.recoveries;
  navigator->skipped = saved.skipped;
  navigator->completedCells = saved.completed;
  navigator->finished = saved.finished;
  memcpy(navigator->cells, saved.cells, sizeof saved.cells);
  memcpy(navigator->covered, saved.covered, sizeof saved.covered);
  memcpy(navigator->excluded, saved.excluded, sizeof saved.excluded);
  navigator->restorePaused(roonav::State(saved.state), millis());
  anchored = false;
  arbiter.hold();
  lastError = "checkpoint restored; physical dock reanchor required";
}
void publish(uint32_t now) {
  String s = "{\"now\":" + String(now) + ",\"epoch\":" + String(arbiter.epoch) +
             ",\"owner\":" + String(uint8_t(arbiter.owner));
  s += ",\"help_id\":" + String(assistance.id) + ",\"frame\":" + String(assistance.frame);
  s += ",\"help_status\":" + String(uint8_t(assistance.status));
  s += ",\"help_reason\":" + String(uint8_t(assistance.reason));
  s += ",\"help_pending\":" + String(assistance.pending() ? "true" : "false");
  s += ",\"global_localized\":" +
       String(anchored && navigator == primaryNavigator ? "true" : "false");
  s += ",\"provisional\":" + String(navigator != primaryNavigator ? "true" : "false");
  const auto ir = robot->sensor(roocore::Sensor::InfraredOmni);
  s += ",\"dock_ir_raw\":" + String(ir.fresh(now, 200) ? ir.value : -1);
  s += ",\"state\":" + String(uint8_t(navigator->state)) +
       ",\"reason\":" + String(uint8_t(navigator->reason));
  s += ",\"undocking\":" + String(undocking ? "true" : "false");
  s += ",\"commissioned\":" + String(ROO_COMMISSIONED && ROO_ENCODERS_VERIFIED ? "true" : "false");
  s += ",\"localized\":" + String(latest.localized ? "true" : "false") +
       ",\"x\":" + String(odometry.pose.x, 4) + ",\"y\":" + String(odometry.pose.y, 4) +
       ",\"yaw\":" + String(odometry.pose.yaw, 5);
  s += ",\"battery\":" + String(latest.battery, 1) + ",\"pose_ms\":" + String(latest.poseMs) +
       ",\"scan_ms\":" + String(latest.scanMs) + ",\"sensors_ms\":" + String(latest.sensorsMs);
  s += ",\"scan_valid\":" + String(latest.scanValid ? "true" : "false") +
       ",\"docked\":" + String(latest.docked ? "true" : "false") +
       ",\"charging\":" + String(latest.charging ? "true" : "false");
  s += ",\"cliff\":" + String(latest.cliff ? "true" : "false") +
       ",\"wheel_drop\":" + String(latest.wheelDrop ? "true" : "false") +
       ",\"alignment\":" + String(alignmentScore, 3);
  unsigned known = 0, main = 0, side = 0, pending = 0;
  for (int c = 0; c < navigator->count(); ++c)
    if (navigator->cells[c] == -1) {
      for (int sub = 0; sub < 4; ++sub) {
        ++known;
        bool central = navigator->covered[c] & (1u << (sub + navigator->pass * 4));
        main += central;
        side += !central && (navigator->sideCovered[c] & (1u << sub));
        pending += navigator->safe[c] && !central;
      }
    }
  s += ",\"known_floor_samples\":" + String(known);
  s += ",\"main_cleaned_samples\":" + String(main);
  s += ",\"side_only_samples\":" + String(side);
  s += ",\"pending_safe_samples\":" + String(pending);
  s += ",\"cleared_obstacles\":" + String(navigator->clearedObstacles);
  s += ",\"obstacle_waits\":" + String(navigator->obstacleWaits);
  s += ",\"contact_points\":[";
  unsigned contactCount = 0;
  for (int c = 0; c < navigator->count(); ++c) {
    if (!navigator->contacts[c])
      continue;
    if (contactCount < 32) {
      if (contactCount)
        s += ',';
      auto p = navigator->center(c);
      s += "[" + String(p.x, 3) + "," + String(p.y, 3) + "]";
    }
    ++contactCount;
  }
  s += "],\"contact_count\":" + String(contactCount);
  s += ",\"frontier_visits\":" + String(navigator->frontierVisits);
  s += ",\"brush_width_m\":" + String(navigator->cfg.brushWidth, 3);
  s += ",\"geometry_calibrated\":" + String(ROO_BRUSH_GEOMETRY_CALIBRATED ? "true" : "false");
  s += ",\"pass_axis\":\"" + String(navigator->pass == 0 ? "horizontal" : "vertical") + "\"";
  s += ",\"wall_ir_valid\":" + String(latest.wallIRValid ? "true" : "false");
  s += ",\"wall_signal\":" + String(latest.wallSignal);
  s += ",\"rooms_completed\":" + String(navigator->roomsCompleted);
  s += ",\"edge_incomplete\":" + String(navigator->edgeIncomplete);
  s += ",\"passes\":" + String(navigator->passes);
  s += ",\"pass\":" + String(navigator->pass + 1) + ",\"skipped\":" + String(navigator->skipped) +
       ",\"error\":\"" + lastError + "\",\"ranges\":[";
  for (unsigned i = 0; i < 180; ++i) {
    if (i)
      s += ',';
    s += String(latest.ranges[i], 3);
  }
  s += "]";
  int target = navigator->target();
  if (target >= 0) {
    auto p = navigator->targetPose();
    s += ",\"target\":[" + String(p.x, 4) + "," + String(p.y, 4) + "]";
  } else
    s += ",\"target\":null";
  s += "}";
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  publishedStatus = s;
  xSemaphoreGive(statusMutex);
}
void sample(uint32_t now) {
  const auto safety = robot->safety(now, 200);
  const auto battery = robot->battery(now, 1000);
  auto left = robot->sensor(roocore::Sensor::LeftEncoder),
       right = robot->sensor(roocore::Sensor::RightEncoder);
  if (left.generation != lastGeneration && left.fresh(now, 200) && right.fresh(now, 200) &&
      left.generation == right.generation) {
    lastGeneration = left.generation;
    if (odometry.valid &&
        !odometry.update(left.value, right.value, left.timestampMs, navigator->cfg))
      anchored = false;
  }
  latest.cleaningActive = appliedPwm > 0 &&
                          robot->sensor(roocore::Sensor::OiMode).fresh(now, 200) &&
                          robot->sensor(roocore::Sensor::OiMode).value == 2;
  const auto wallSignal = robot->sensor(roocore::Sensor::WallSignal);
  const auto wall = robot->sensor(roocore::Sensor::Wall);
  latest.wallIRValid = wall.fresh(now, 200) && wallSignal.fresh(now, 200);
  latest.wallDetected = latest.wallIRValid && wall.value != 0;
  latest.wallSignal = latest.wallIRValid ? wallSignal.value : 0;
  latest.sideBrushActive = appliedSidePwm > 0 &&
                           robot->sensor(roocore::Sensor::OiMode).fresh(now, 200) &&
                           robot->sensor(roocore::Sensor::OiMode).value == 2;
  latest.now = now;
  latest.pose = odometry.pose;
  latest.poseMs = odometry.stamp;
  latest.sensorsMs = robot->sensor(roocore::Sensor::BumpsAndDrops).timestampMs;
  latest.sensorsValid = safety.valid && battery.valid && battery.percentageValid;
  latest.cliff = safety.cliffLeft || safety.cliffFrontLeft || safety.cliffFrontRight ||
                 safety.cliffRight || safety.virtualWall;
  latest.wheelDrop = safety.wheelDropLeft || safety.wheelDropRight;
  latest.bumper = safety.bumpLeft || safety.bumpRight;
  if (latest.wheelDrop) {
    if (!wheelDropSince)
      wheelDropSince = now;
    if (uint32_t(now - wheelDropSince) >= 500)
      memset(trailMs, 0, sizeof trailMs);
  } else
    wheelDropSince = 0;
  const auto overcurrent = robot->sensor(roocore::Sensor::Overcurrents);
  latest.overcurrent = !overcurrent.fresh(now, 200) || overcurrent.value != 0;
  latest.battery = battery.percentage;
  const auto charger = robot->sensor(roocore::Sensor::ChargingSources);
  latest.docked = charger.fresh(now, 200) && (charger.value & 2);
  latest.charging = battery.charging == roocore::Charging::FullCharging ||
                    battery.charging == roocore::Charging::Trickle ||
                    battery.charging == roocore::Charging::Waiting;
  latest.dirt = uint8_t(robot->sensor(roocore::Sensor::DirtDetect).value);
  if (lidar.scans != lastScan && lidar.valid) {
    lastScan = lidar.scans;
    memset(latest.ranges, 0, sizeof latest.ranges);
    for (int i = 0; i < 180; ++i) {
      float r = lidar.ranges[i];
      if (r <= 0)
        continue;
      float a = (ROO_LIDAR_CLOCKWISE ? -i : i) * roonav::Pi / 90 +
                ROO_LIDAR_YAW_DEGREES * roonav::Pi / 180;
      float x = r * cosf(a) + ROO_LIDAR_X_METERS, y = r * sinf(a) + ROO_LIDAR_Y_METERS;
      float angle = atan2f(y, x);
      if (angle < 0)
        angle += 2 * roonav::Pi;
      unsigned j = unsigned(angle * 90 / roonav::Pi) % 180;
      float range = hypotf(x, y);
      if (latest.ranges[j] == 0 || range < latest.ranges[j])
        latest.ranges[j] = range;
    }
    if (assistance.status == roonav::NavigationHelp::Status::Checking) {
      roonav::Pose candidate = assistance.proposed;
      float score = 0;
      bool ok = roonav::alignToMap(candidate, latest.ranges, *primaryNavigator, score) &&
                score >= .85f &&
                roonav::distance(candidate.x, candidate.y, assistance.proposed.x,
                                 assistance.proposed.y) <= .04f &&
                fabsf(roonav::wrap(candidate.yaw - assistance.proposed.yaw)) <= .03f &&
                latest.sensorsValid && !latest.cliff && !latest.wheelDrop && !latest.bumper &&
                !latest.overcurrent;
      const bool accepted = assistance.confirm(ok);
      if (!ok && navigator == provisionalNavigator && !assistance.operatorHeld &&
          latest.sensorsValid && !latest.cliff && !latest.wheelDrop && !latest.bumper &&
          !latest.overcurrent) {
        // A rejected global hypothesis does not invalidate an otherwise healthy local map.
        latest.scanValid = true;
        latest.scanMs = now;
        latest.localized = anchored && odometry.valid;
        if (navigator->resume(latest)) {
          assistance.status = roonav::NavigationHelp::Status::LocalOnly;
          arbiter.owner = roonav::Owner::Local;
          lastError = "global candidate rejected; continuing isolated local cleaning";
        }
      }
      if (accepted) {
        stopMotion();
        navigator = primaryNavigator;
        odometry.reset(assistance.proposed, left.value, right.value, now);
        anchored = true;
        latest.pose = odometry.pose;
        latest.poseMs = now;
        latest.localized = true;
        latest.scanValid = true;
        latest.scanMs = now;
        navigator->resume(latest);
        arbiter.owner = roonav::Owner::Local;
        lastError = "relocalized; provisional coverage retained separately";
        saveMission();
      }
    }
    if (anchored && odometry.valid &&
        assistance.status != roonav::NavigationHelp::Status::Checking &&
        !roonav::alignToMap(odometry.pose, latest.ranges, *navigator, alignmentScore))
      anchored = false;
    latest.pose = odometry.pose;
  }
  latest.scanMs = lidar.scanMs;
  latest.scanValid = lidar.valid && uint32_t(now - lidar.scanMs) <= 400;
  latest.localized = anchored && odometry.valid;
  latest.retreatVerified = false;
  for (unsigned j = 0; j < 16; ++j)
    if (uint32_t(now - trailMs[j]) < 5000 && trailMs[j] &&
        roonav::distance(trail[j].x, trail[j].y, latest.pose.x - .06f * cosf(latest.pose.yaw),
                         latest.pose.y - .06f * sinf(latest.pose.yaw)) < .03f)
      latest.retreatVerified = true;
  if (latest.localized && latest.sensorsValid && !latest.cliff && !latest.wheelDrop &&
      !latest.bumper && !latest.overcurrent && uint32_t(now - trailMs[(trailAt + 15) % 16]) > 200) {
    trail[trailAt] = latest.pose;
    trailMs[trailAt] = now;
    trailAt = (trailAt + 1) % 16;
  }
}
bool healthy() {
  const uint32_t now = millis();
  return uint32_t(now - latest.sensorsMs) <= 200 && uint32_t(now - latest.poseMs) <= 300 &&
         uint32_t(now - latest.scanMs) <= 400 && latest.sensorsValid && latest.localized &&
         latest.scanValid && !latest.cliff && !latest.wheelDrop &&
         (!latest.bumper || navigator->wallRetreat()) && !latest.overcurrent && latest.battery > 6;
}
void request(const Request &r, uint32_t now) {
  if (!strcmp(r.action, "pause")) {
    assistance.operatorPause();
    undocking = false;
    stopMotion();
    navigator->pause(now);
    arbiter.hold();
    saveMission();
    return;
  }
  if (!strcmp(r.action, "init-storage")) {
    if (movingSession()) {
      lastError = "stop before formatting";
      return;
    }
    storageReady = LittleFS.format() && LittleFS.begin(false);
    return;
  }
  if (!ROO_COMMISSIONED || !ROO_ENCODERS_VERIFIED) {
    lastError = "commissioning required; motion disabled";
    return;
  }
  if (!storageReady || !linkReady) {
    lastError = "storage or robot link unavailable";
    return;
  }
  if (!strncmp(r.action, "help-", 5)) {
    if (!assistance.validReply(r.epoch, r.sequence, r.expires, now)) {
      lastError = "stale or wrong-frame assistance reply";
      return;
    }
    if (!strcmp(r.action, "help-hold")) {
      assistance.hold();
      navigator->pause(now);
      stopMotion();
      return;
    }
    if (!strcmp(r.action, "help-continue")) {
      // Acknowledgement only: cannot reset a safety fault, extend a budget or re-arm remote
      // ownership.
      lastError = "server analyzing; local safety and battery limits remain active";
      return;
    }
    if (!strcmp(r.action, "help-relocalize")) {
      if (assistance.operatorHeld) {
        lastError = "operator paused; resume or reanchor explicitly";
        return;
      }
      if (nativeDock || !latest.scanValid || !latest.sensorsValid || latest.cliff ||
          latest.wheelDrop || latest.bumper || latest.overcurrent || latest.battery <= 6 ||
          uint32_t(now - latest.scanMs) > 400 || uint32_t(now - latest.sensorsMs) > 200) {
        lastError = "candidate requires healthy local observations";
        return;
      }
      float yaw = roonav::wrap(r.candidate.yaw - r.reference.yaw);
      roonav::Pose proposal = r.candidate;
      float dx = latest.pose.x - r.reference.x, dy = latest.pose.y - r.reference.y;
      proposal.x += dx * cosf(yaw) - dy * sinf(yaw);
      proposal.y += dx * sinf(yaw) + dy * cosf(yaw);
      proposal.yaw = roonav::wrap(latest.pose.yaw + yaw);
      int c = primaryNavigator->cell(proposal.x, proposal.y);
      if (c < 0 || !primaryNavigator->safe[c] || !assistance.candidate(proposal)) {
        lastError = "candidate outside known safe map";
        return;
      }
      stopMotion();
      navigator->pause(now);
      lidarPower(true);
      lastError = "checking candidate against three fresh scans";
      return;
    }
    lastError = "unknown assistance action";
    return;
  }
  if (!strcmp(r.action, "resume") && assistance.operatorHeld && assistance.pending()) {
    if (navigator == provisionalNavigator && healthy() && navigator->resume(latest)) {
      assistance.operatorHeld = false;
      assistance.status = roonav::NavigationHelp::Status::LocalOnly;
      arbiter.owner = roonav::Owner::Local;
    } else
      lastError = "local assistance resume requires healthy observations";
    return;
  }
  if (!strcmp(r.action, "relocate") && !assistance.pending() && navigator == primaryNavigator &&
      navigator->state != roonav::State::Idle && navigator->state != roonav::State::Complete) {
    assistance.operatorResume();
    stopMotion();
    navigator->fault(roonav::Reason::Localization);
    anchored = false;
    return;
  }
  if (assistance.pending() && strcmp(r.action, "anchor")) {
    lastError = "assistance active; use help response or pause";
    return;
  }
  if (!strcmp(r.action, "anchor")) {
    if (movingSession() || !latest.docked || !latest.charging || !latest.sensorsValid) {
      lastError = "anchor requires stopped robot on powered dock";
      return;
    }
    const auto l = robot->sensor(roocore::Sensor::LeftEncoder),
               rr = robot->sensor(roocore::Sensor::RightEncoder);
    if (!l.fresh(now, 200) || !rr.fresh(now, 200)) {
      lastError = "encoders missing";
      return;
    }
    if (assistance.pending()) {
      navigator = primaryNavigator;
      assistance.operatorHeld = false;
      assistance.status = roonav::NavigationHelp::Status::Resolved;
    }
    roonav::Pose p = navigator->dock;
    p.x += ROO_DOCK_EXIT_METERS * cosf(p.yaw);
    p.y += ROO_DOCK_EXIT_METERS * sinf(p.yaw);
    odometry.reset(p, l.value, rr.value, now);
    anchored = true;
    assistance.operatorResume();
    anchorAt = now;
    lidarPower(true);
    lastError = "";
    return;
  }
  if (!strcmp(r.action, "velocity")) {
    if (!healthy() || !arbiter.command(r.epoch, r.sequence, r.expires, r.left, r.right, r.pwm, now))
      lastError = "remote command rejected";
    return;
  }
  if (!strcmp(r.action, "resume")) {
    lidarPower(true);
    anchorAt = now;
  }
  if (!healthy()) {
    lastError = "fresh healthy sensors and dock anchor required";
    return;
  }
  if (!strcmp(r.action, "daily") || !strcmp(r.action, "deep")) {
    if (movingSession()) {
      lastError = "pause before starting a new mission";
      return;
    }
    assistance.attempts = 0;
    assistance.operatorResume();
    if (latest.docked) {
      if (!ROO_DOCK_EXIT_VERIFIED) {
        lastError = "dock exit corridor not commissioned";
        return;
      }
      startAfterUndock = true;
      deepAfterUndock = !strcmp(r.action, "deep");
      undocking = true;
      undockAt = undockProgressAt = now;
      undockBest = 1e9f;
    } else
      navigator->start(!strcmp(r.action, "deep"), navigator->dock, now);
    arbiter.owner = roonav::Owner::Local;
    lastError = "";
  } else if (!strcmp(r.action, "home")) {
    navigator->goHome(latest);
    arbiter.local(now);
  } else if (!strcmp(r.action, "resume")) {
    if (navigator->resume(latest)) {
      assistance.operatorResume();
      arbiter.local(now);
    } else
      lastError = "resume rejected";
  } else if (!strcmp(r.action, "remote")) {
    if (!arbiter.takeover(now, healthy(), active()))
      lastError = "takeover rejected";
  } else if (!strcmp(r.action, "local")) {
    arbiter.local(now);
  } else
    lastError = "unknown action";
}
void apply(roonav::Output out, uint32_t now) {
  now = millis();
  if (out.action == roonav::Action::SeekDock) {
    if (!nativeDock) {
      stopMotion();
      if (robot->seekDock() == roocore::Result::Ok) {
        nativeDock = true;
        ownsRobot = true;
      } else
        navigator->fault(roonav::Reason::Telemetry);
    }
    return;
  }
  if (out.action != roonav::Action::Drive) {
    stopMotion();
    return;
  }
  if (!healthy()) {
    stopMotion();
    navigator->fault(roonav::Reason::Telemetry);
    return;
  }
  if (!navigator->allowsMotion(latest.pose, out.speed, out.turn)) {
    stopMotion();
    navigator->fault(roonav::Reason::NoRoute);
    return;
  }
  if (fabsf(out.speed) > .001f) {
    const float direction = out.speed > 0 ? 0 : roonav::Pi;
    bool clear = out.speed >= 0 || latest.retreatVerified || (undocking && ROO_DOCK_EXIT_VERIFIED);
    for (unsigned i = 0; i < 180; ++i)
      if (fabsf(roonav::wrap(i * roonav::Pi / 90 - direction)) < .5f) {
        float range = latest.ranges[i];
        if (!isfinite(range) || range < (navigator->wallApproach() ? navigator->cfg.radius - .015f
                                         : navigator->state == roonav::State::Edging
                                             ? navigator->cfg.radius + navigator->cfg.margin + .025f
                                             : navigator->cfg.radius + navigator->cfg.margin +
                                                   .02f + fabsf(out.speed) * .2f))
          clear = false;
      }
    if (!clear) {
      stopMotion();
      if (out.speed > 0 && !undocking && navigator->state == roonav::State::Cleaning)
        navigator->yieldForObstacle(latest);
      else
        navigator->fault(roonav::Reason::NoRoute);
      return;
    }
  }
  const auto mode = robot->sensor(roocore::Sensor::OiMode);
  if (robot->requestedMode() != roocore::Mode::Safe) {
    if (!armingSince) {
      robot->enterSafe();
      armingSince = now;
    }
    return;
  }
  if (!mode.fresh(now, 200) || mode.value != 2) {
    if (!armingSince)
      armingSince = now;
    if (uint32_t(now - armingSince) > 1500) {
      stopMotion();
      navigator->fault(roonav::Reason::Telemetry);
    }
    return;
  }
  armingSince = 0;
  const int l = constrain(
      lroundf((out.speed - out.turn * navigator->cfg.wheelSeparation / 2) * 1000), -250, 250);
  const int r = constrain(
      lroundf((out.speed + out.turn * navigator->cfg.wheelSeparation / 2) * 1000), -250, 250);
  appliedPwm = out.pwm;
  appliedSidePwm = out.sidePwm == 255 ? out.pwm : out.sidePwm;
  ownsRobot = true;
  if (robot->driveWheels(l, r, now, 300) != roocore::Result::Ok ||
      robot->setMotorPwm(out.pwm, appliedSidePwm, out.pwm) != roocore::Result::Ok) {
    stopMotion();
    navigator->fault(roonav::Reason::Telemetry);
  }
}
void tryAssistance(uint32_t now) {
  if (!ROO_ASSISTANCE_ENABLED || assistance.operatorHeld || assistance.pending() ||
      navigator != primaryNavigator || navigator->state != roonav::State::Fault ||
      (navigator->reason != roonav::Reason::Localization &&
       navigator->reason != roonav::Reason::NoRoute))
    return;
  stopMotion();
  const auto left = robot->sensor(roocore::Sensor::LeftEncoder),
             right = robot->sensor(roocore::Sensor::RightEncoder);
  bool localSafe = latest.sensorsValid && latest.scanValid && latest.battery > 6 && !latest.cliff &&
                   !latest.wheelDrop && !latest.bumper && !latest.overcurrent &&
                   uint32_t(now - latest.sensorsMs) <= 200 &&
                   uint32_t(now - latest.scanMs) <= 400 && left.fresh(now, 200) &&
                   right.fresh(now, 200);
  if (!saveMission()) {
    navigator->fault(roonav::Reason::Telemetry);
    lastError = "cannot preserve global mission";
    return;
  }
  if (!assistance.begin(navigator->reason, now, localSafe))
    return;
  assistance.operatorHeld = false;
  arbiter.hold();
  // Freeze the global mission. The provisional map/coverage uses separate files and frame.
  String map = "{\"id\":" + String(assistance.id) + ",\"frame\":" + String(assistance.frame) +
               ",\"cols\":" + String(navigator->cfg.cols) +
               ",\"rows\":" + String(navigator->cfg.rows) +
               ",\"resolution\":" + String(navigator->cfg.resolution, 3) +
               ",\"origin_x\":" + String(navigator->cfg.originX, 3) +
               ",\"origin_y\":" + String(navigator->cfg.originY, 3) + ",\"cells\":\"";
  map.reserve(2 * navigator->count() + 512);
  for (int c = 0; c < navigator->count(); ++c)
    map += navigator->cells[c] == 1 ? 'o' : navigator->cells[c] == -1 ? 'f' : 'u';
  map += "\",\"safe\":\"";
  for (int c = 0; c < navigator->count(); ++c)
    map += navigator->safe[c] ? '1' : '0';
  map += "\"}";
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  publishedHelpMap = map;
  xSemaphoreGive(statusMutex);
  lidarPower(true);
  anchorAt = now;
  if (!localSafe) {
    lastError = "help requested; waiting for healthy sensors";
    return;
  }
  if (!provisionalNavigator) {
    void *memory = heap_caps_malloc(sizeof(roonav::Navigator), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
      assistance.hold();
      lastError = "no PSRAM for provisional map";
      return;
    }
    provisionalNavigator = new (memory) roonav::Navigator();
  }
  provisionalNavigator->cfg = primaryNavigator->cfg;
  provisionalNavigator->cfg.homeKnown = false;
  provisionalNavigator->cfg.perimeter = false;
  provisionalNavigator->cfg.wallTouch = false;
  navigator = provisionalNavigator;
  roonav::Pose origin;
  odometry.reset(origin, left.value, right.value, now);
  anchored = true;
  navigator->start(false, origin, now);
  latest.pose = origin;
  latest.poseMs = now;
  latest.localized = true;
  arbiter.owner = roonav::Owner::Local;
  lastError = "global pose unavailable; cleaning in separate local frame; help requested";
}
void controlTask(void *) {
  robot->beginPassive();
  if (ROO_COMMISSIONED) {
    robot->enterSafe();
    robot->stopAll();
    robot->beginPassive();
  }
  linkReady = robot->verifyConnection() == roocore::Result::Ok;
  if (linkReady) {
    const roocore::Sensor ids[] = {roocore::Sensor::Safety,          roocore::Sensor::Battery,
                                   roocore::Sensor::ChargingSources, roocore::Sensor::OiMode,
                                   roocore::Sensor::WallSignal,      roocore::Sensor::InfraredOmni,
                                   roocore::Sensor::LeftEncoder,     roocore::Sensor::RightEncoder};
    linkReady = robot->startStream(ids, ROO_ENCODERS_VERIFIED ? 8 : 6) == roocore::Result::Ok;
  }
  storageReady = LittleFS.begin(false);
  loadMission();
  for (;;) {
    const uint32_t now = millis();
    robot->poll(now);
    if (enabledLidar && uint32_t(now - lidarOnMs) >= 500 && digitalRead(7) == LOW) {
      digitalWrite(7, HIGH);
      const uint8_t start[] = {0xa5, 0x60};
      lidarSerial.write(start, 2);
    }
    for (unsigned i = 0; i < 2048 && lidarSerial.available(); ++i)
      lidar.feed(uint8_t(lidarSerial.read()), now);
    if (uint32_t(now - lastControl) >= 50) {
      lastControl = now;
      sample(now);
      Request req;
      for (unsigned i = 0; i < 4 && xQueueReceive(requests, &req, 0) == pdTRUE; ++i)
        request(req, now);
      const auto before = navigator->state;
      arbiter.poll(now, healthy(), active());
      if (assistance.pending() && !assistance.allowLocal(latest)) {
        navigator->pause(now);
        stopMotion();
      }
      roonav::Output out;
      if (undocking) {
        const float remaining =
            roonav::distance(latest.pose.x, latest.pose.y, navigator->dock.x, navigator->dock.y);
        if (remaining < undockBest - .01f) {
          undockBest = remaining;
          undockProgressAt = now;
        }
        if (!healthy() || !ROO_DOCK_EXIT_VERIFIED || uint32_t(now - undockAt) > 15000 ||
            uint32_t(now - undockProgressAt) > 3000 ||
            fabsf(roonav::wrap(latest.pose.yaw - navigator->dock.yaw)) > .25f) {
          undocking = false;
          navigator->fault(roonav::Reason::NoProgress);
        } else if (remaining < .04f) {
          undocking = false;
          if (startAfterUndock) {
            navigator->start(deepAfterUndock, navigator->dock, now);
            startAfterUndock = false;
          }
        } else {
          out.action = roonav::Action::Drive;
          out.speed = -.06f;
        }
      } else if (active()) {
        // Always maintain a local map/mission during remote control for fallback.
        out = navigator->tick(latest, arbiter.owner == roonav::Owner::Remote);
        if (navigator->state != roonav::State::Cleaning && arbiter.owner == roonav::Owner::Remote)
          arbiter.local(now);
        const bool dirtHold = out.action == roonav::Action::Drive && out.pwm == 127 &&
                              out.speed == 0 && out.turn == 0;
        if (arbiter.owner == roonav::Owner::Remote && healthy() &&
            navigator->state == roonav::State::Cleaning && !dirtHold) {
          out.action = roonav::Action::Drive;
          out.speed = (arbiter.left + arbiter.right) / 2000.f;
          out.turn = (arbiter.right - arbiter.left) / (1000 * navigator->cfg.wheelSeparation);
          out.pwm = arbiter.pwm;
        }
        if (arbiter.owner == roonav::Owner::Stopping || arbiter.owner == roonav::Owner::Held)
          out = roonav::Output();
      }
      arbiter.poll(millis(), healthy(), movingSession());
      if (arbiter.owner == roonav::Owner::Stopping || arbiter.owner == roonav::Owner::Held)
        out = roonav::Output();
      if (ROO_COMMISSIONED && ROO_ENCODERS_VERIFIED)
        apply(out, now);
      else
        stopMotion();
      if (navigator->state == roonav::State::Fault)
        undocking = false;
      if (before == roonav::State::Charging && navigator->state == roonav::State::Resuming) {
        if (ROO_DOCK_EXIT_VERIFIED) {
          undocking = true;
          startAfterUndock = false;
          undockAt = undockProgressAt = now;
          undockBest = 1e9f;
        } else
          navigator->fault(roonav::Reason::NoRoute);
      }
      if (before != navigator->state) {
        if (!saveMission()) {
          stopMotion();
          navigator->fault(roonav::Reason::Telemetry);
          lastError = "checkpoint write failed";
        }
      }
      // Do not interrupt and restart the robot's native docking routine for a
      // periodic save. Entering Docking was already checkpointed.
      if (active() && !undocking && navigator->state != roonav::State::Docking &&
          uint32_t(now - lastSaved) >= 30000 && !saveMission()) {
        stopMotion();
        navigator->fault(roonav::Reason::Telemetry);
        lastError = "checkpoint write failed";
      }
      if (navigator->state == roonav::State::Charging)
        lidarPower(latest.battery >= 78);
      tryAssistance(now);
      if (!assistance.pending() && !undocking && terminal(navigator->state) &&
          uint32_t(now - anchorAt) > 10000)
        lidarPower(false);
    }
    if (uint32_t(now - lastPublish) >= 100) {
      lastPublish = now;
      publish(now);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
bool authenticated() {
  return strlen(ROO_API_TOKEN) >= 24 &&
         web.header("Authorization") == String("Bearer ") + ROO_API_TOKEN;
}
bool number(const char *name, int64_t &out, int64_t minimum, int64_t maximum) {
  if (!web.hasArg(name))
    return false;
  String value = web.arg(name);
  char *end = nullptr;
  out = strtoll(value.c_str(), &end, 10);
  return end != value.c_str() && *end == 0 && out >= minimum && out <= maximum;
}
void setup() {
  Serial.begin(115200);
  for (int pin : {4, 7, 9}) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  } // LiDAR ON, OE, power-off NPN
  roombaSerial.setRxBufferSize(4096);
  roombaSerial.begin(115200, SERIAL_8N1, 2, 1);
  robotLink.beginBrc();
  lidarSerial.setRxBufferSize(8192);
  lidarSerial.begin(230400, SERIAL_8N1, 44, 43);
  capabilities.extendedSensors = ROO_ENCODERS_VERIFIED;
  robot = new roocore::Roomba(robotLink, capabilities);
  void *navMemory =
      heap_caps_malloc(sizeof(roonav::Navigator), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!navMemory) {
    Serial.println("PSRAM allocation failed; motors disabled");
    return;
  }
  navigator = new (navMemory) roonav::Navigator();
  primaryNavigator = navigator;
  assistance.id = esp_random();
  navigator->cfg.brushWidth = ROO_BRUSH_WIDTH_METERS;
  navigator->cfg.brushDepth = ROO_BRUSH_DEPTH_METERS;
  navigator->cfg.brushOffset = ROO_BRUSH_OFFSET_METERS;
  navigator->cfg.sideX = ROO_SIDE_BRUSH_X_METERS;
  navigator->cfg.sideY = ROO_SIDE_BRUSH_Y_METERS;
  navigator->cfg.sideRadius = ROO_SIDE_BRUSH_RADIUS_METERS;
  navigator->cfg.perimeter = ROO_PERIMETER_ENABLED;
  navigator->cfg.wallTouch = ROO_WALL_TOUCH_VERIFIED;
  navigator->cfg.wallIRTarget = ROO_WALL_IR_TARGET;
  navigator->dock.x = -ROO_DOCK_EXIT_METERS;
  navigator->cfg.metersPerTick = ROO_METERS_PER_TICK;
  navigator->cfg.wheelSeparation = ROO_WHEEL_SEPARATION;
  requests = xQueueCreate(8, sizeof(Request));
  statusMutex = xSemaphoreCreateMutex();
  if (!requests || !statusMutex) {
    Serial.println("allocation failure");
    return;
  }
  xTaskCreatePinnedToCore(controlTask, "robot-control", 16384, nullptr, 3, nullptr, 1);
  if (strlen(ROO_WIFI_SSID) && strlen(ROO_API_TOKEN) >= 24) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ROO_WIFI_SSID, ROO_WIFI_PASSWORD);
  }
  const char *headers[] = {"Authorization"};
  web.collectHeaders(headers, 1);
  web.on("/status", HTTP_GET, [] {
    if (!authenticated()) {
      web.send(401);
      return;
    }
    xSemaphoreTake(statusMutex, portMAX_DELAY);
    String s = publishedStatus;
    xSemaphoreGive(statusMutex);
    web.send(200, "application/json", s);
  });
  web.on("/help/map", HTTP_GET, [] {
    if (!authenticated()) {
      web.send(401);
      return;
    }
    xSemaphoreTake(statusMutex, portMAX_DELAY);
    String data = publishedHelpMap;
    xSemaphoreGive(statusMutex);
    if (!data.length()) {
      web.send(409);
      return;
    }
    web.send(200, "application/json", data);
  });
  web.on("/command", HTTP_POST, [] {
    if (!authenticated()) {
      web.send(401);
      return;
    }
    Request req{};
    String action = web.arg("action");
    if (action.length() == 0 || action.length() >= sizeof req.action) {
      web.send(400);
      return;
    }
    action.toCharArray(req.action, sizeof req.action);
    if (action == "velocity") {
      int64_t epoch, seq, expires, left, right, pwm;
      if (!number("epoch", epoch, 0, 4294967295LL) || !number("sequence", seq, 0, 4294967295LL) ||
          !number("expires", expires, 0, 4294967295LL) || !number("left", left, -250, 250) ||
          !number("right", right, -250, 250) || !number("pwm", pwm, 0, 127)) {
        web.send(400);
        return;
      }
      req.epoch = epoch;
      req.sequence = seq;
      req.expires = expires;
      req.left = left;
      req.right = right;
      req.pwm = pwm;
    }
    if (action.startsWith("help-")) {
      int64_t id, frame, expires;
      if (!number("id", id, 0, 4294967295LL) || !number("frame", frame, 0, 4294967295LL) ||
          !number("expires", expires, 0, 4294967295LL)) {
        web.send(400);
        return;
      }
      req.epoch = id;
      req.sequence = frame;
      req.expires = expires;
      if (action == "help-relocalize") {
        float *values[] = {&req.candidate.x, &req.candidate.y, &req.candidate.yaw,
                           &req.reference.x, &req.reference.y, &req.reference.yaw};
        const char *names[] = {"x", "y", "yaw", "ref_x", "ref_y", "ref_yaw"};
        for (int j = 0; j < 6; ++j) {
          String v = web.arg(names[j]);
          char *end = nullptr;
          *values[j] = strtof(v.c_str(), &end);
          if (!v.length() || end == v.c_str() || *end || !isfinite(*values[j]) ||
              fabsf(*values[j]) > 50) {
            web.send(400);
            return;
          }
        }
      }
    }
    if (xQueueSend(requests, &req, 0) != pdTRUE) {
      web.send(503, "text/plain", "queue full");
      return;
    }
    web.send(202, "text/plain", "queued; inspect status for result");
  });
  web.begin();
}
void loop() {
  if (!navigator) {
    delay(100);
    return;
  }
  web.handleClient();
  delay(1);
}
