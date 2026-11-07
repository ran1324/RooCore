#include <Arduino.h>
#include <ControlArbiter.h>
#include <LittleFS.h>
#include <RooNavigation.h>
#include <RoombaOIArduino.h>
#include <TminiPlus.h>
#include <WebServer.h>
#include <WiFi.h>
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
roonav::Navigator navigator;
roonav::Odometry odometry;
roonav::TminiPlus lidar;
roonav::ControlArbiter arbiter;
WebServer web(80);
QueueHandle_t requests;
SemaphoreHandle_t statusMutex;
String publishedStatus;

struct Request {
  char action[24];
  uint32_t epoch, sequence, expires;
  int left, right, pwm;
};
struct SavedMission {
  uint32_t magic = 0x524f4f31, version = 1, sequence = 0, crc = 0;
  roonav::Config config;
  roonav::Pose dock, checkpoint, pose;
  uint8_t state, pass, passes, recoveries;
  uint32_t skipped, completed;
  bool finished;
  int8_t cells[roonav::Navigator::MaxCells];
  uint8_t covered[roonav::Navigator::MaxCells], excluded[roonav::Navigator::MaxCells];
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
uint8_t appliedPwm = 0;
bool ownsRobot = false;
String lastError = "not commissioned";
roonav::Pose trail[16];
uint32_t trailMs[16]{};
unsigned trailAt = 0;
roonav::Input latest;

bool terminal(roonav::State s) {
  return s == roonav::State::Idle || s == roonav::State::Fault || s == roonav::State::Paused ||
         s == roonav::State::Complete;
}
bool active() { return !terminal(navigator.state); }
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
  appliedPwm = 0;
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
  saved.config = navigator.cfg;
  saved.dock = navigator.dock;
  saved.checkpoint = navigator.checkpoint;
  saved.pose = odometry.pose;
  saved.state = uint8_t(navigator.state);
  saved.pass = navigator.pass;
  saved.passes = navigator.passes;
  saved.recoveries = navigator.recoveries;
  saved.skipped = navigator.skipped;
  saved.completed = navigator.completedCells;
  saved.finished = navigator.finished;
  memcpy(saved.cells, navigator.cells, sizeof saved.cells);
  memcpy(saved.covered, navigator.covered, sizeof saved.covered);
  memcpy(saved.excluded, navigator.excluded, sizeof saved.excluded);
  ++saved.sequence;
  saved.crc = checksum(saved);
  // Alternating complete snapshots: a torn write leaves the other slot intact.
  File file = LittleFS.open(saved.sequence & 1 ? "/mission-a" : "/mission-b", "w");
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
  return ok && out.magic == 0x524f4f31 && out.version == 1 && out.crc == checksum(out) &&
         out.pass < out.passes && out.passes >= 1 && out.passes <= 2 &&
         out.state <= uint8_t(roonav::State::Complete);
}
void loadMission() {
  if (!storageReady)
    return;
  static SavedMission other;
  bool a = readSlot("/mission-a", saved), b = readSlot("/mission-b", other);
  if (b && (!a || other.sequence > saved.sequence))
    saved = other;
  else if (!a)
    return;
  if (!isfinite(saved.dock.x) || !isfinite(saved.dock.y) || !isfinite(saved.dock.yaw) ||
      !isfinite(saved.checkpoint.x) || !isfinite(saved.checkpoint.y) ||
      !isfinite(saved.checkpoint.yaw))
    return;
  // Reject checkpoint after geometry/calibration changes.
  if (saved.config.cols != navigator.cfg.cols || saved.config.rows != navigator.cfg.rows ||
      saved.config.resolution != navigator.cfg.resolution ||
      saved.config.metersPerTick != navigator.cfg.metersPerTick ||
      saved.config.wheelSeparation != navigator.cfg.wheelSeparation)
    return;
  for (int i = 0; i < navigator.count(); ++i)
    if (saved.cells[i] < -1 || saved.cells[i] > 1 || saved.covered[i] > 3)
      return;
  navigator.dock = saved.dock;
  navigator.checkpoint = saved.checkpoint;
  navigator.pass = saved.pass;
  navigator.passes = saved.passes;
  navigator.recoveries = saved.recoveries;
  navigator.skipped = saved.skipped;
  navigator.completedCells = saved.completed;
  navigator.finished = saved.finished;
  memcpy(navigator.cells, saved.cells, sizeof saved.cells);
  memcpy(navigator.covered, saved.covered, sizeof saved.covered);
  memcpy(navigator.excluded, saved.excluded, sizeof saved.excluded);
  navigator.restorePaused(roonav::State(saved.state), millis());
  anchored = false;
  arbiter.hold();
  lastError = "checkpoint restored; physical dock reanchor required";
}
void publish(uint32_t now) {
  String s = "{\"now\":" + String(now) + ",\"epoch\":" + String(arbiter.epoch) +
             ",\"owner\":" + String(uint8_t(arbiter.owner));
  s += ",\"state\":" + String(uint8_t(navigator.state)) +
       ",\"reason\":" + String(uint8_t(navigator.reason));
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
  s += ",\"passes\":" + String(navigator.passes);
  s += ",\"pass\":" + String(navigator.pass + 1) + ",\"skipped\":" + String(navigator.skipped) +
       ",\"error\":\"" + lastError + "\",\"ranges\":[";
  for (unsigned i = 0; i < 180; ++i) {
    if (i)
      s += ',';
    s += String(latest.ranges[i], 3);
  }
  s += "]";
  int target = navigator.target();
  if (target >= 0) {
    auto p = navigator.center(target);
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
        !odometry.update(left.value, right.value, left.timestampMs, navigator.cfg))
      anchored = false;
  }
  latest.cleaningActive = appliedPwm > 0 &&
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
    if (anchored && odometry.valid &&
        !roonav::alignToMap(odometry.pose, latest.ranges, navigator, alignmentScore))
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
         latest.scanValid && !latest.cliff && !latest.wheelDrop && !latest.bumper &&
         !latest.overcurrent && latest.battery > 6;
}
void request(const Request &r, uint32_t now) {
  if (!strcmp(r.action, "pause")) {
    undocking = false;
    stopMotion();
    navigator.pause(now);
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
    roonav::Pose p = navigator.dock;
    p.x += ROO_DOCK_EXIT_METERS * cosf(p.yaw);
    p.y += ROO_DOCK_EXIT_METERS * sinf(p.yaw);
    odometry.reset(p, l.value, rr.value, now);
    anchored = true;
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
      navigator.start(!strcmp(r.action, "deep"), navigator.dock, now);
    arbiter.owner = roonav::Owner::Local;
    lastError = "";
  } else if (!strcmp(r.action, "home")) {
    navigator.goHome(latest);
    arbiter.local(now);
  } else if (!strcmp(r.action, "resume")) {
    if (navigator.resume(latest))
      arbiter.local(now);
    else
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
        navigator.fault(roonav::Reason::Telemetry);
    }
    return;
  }
  if (out.action != roonav::Action::Drive) {
    stopMotion();
    return;
  }
  if (!healthy()) {
    stopMotion();
    navigator.fault(roonav::Reason::Telemetry);
    return;
  }
  if (fabsf(out.speed) > .001f) {
    const float direction = out.speed > 0 ? 0 : roonav::Pi;
    bool clear = out.speed >= 0 || latest.retreatVerified || (undocking && ROO_DOCK_EXIT_VERIFIED);
    for (unsigned i = 0; i < 180; ++i)
      if (fabsf(roonav::wrap(i * roonav::Pi / 90 - direction)) < .5f) {
        float range = latest.ranges[i];
        if (!isfinite(range) || range < .30f)
          clear = false;
      }
    if (!clear) {
      stopMotion();
      navigator.fault(roonav::Reason::NoRoute);
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
      navigator.fault(roonav::Reason::Telemetry);
    }
    return;
  }
  armingSince = 0;
  const int l = constrain(
      lroundf((out.speed - out.turn * navigator.cfg.wheelSeparation / 2) * 1000), -250, 250);
  const int r = constrain(
      lroundf((out.speed + out.turn * navigator.cfg.wheelSeparation / 2) * 1000), -250, 250);
  appliedPwm = out.pwm;
  ownsRobot = true;
  if (robot->driveWheels(l, r, now, 300) != roocore::Result::Ok ||
      robot->setMotorPwm(out.pwm, out.pwm, out.pwm) != roocore::Result::Ok) {
    stopMotion();
    navigator.fault(roonav::Reason::Telemetry);
  }
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
                                   roocore::Sensor::LeftEncoder,     roocore::Sensor::RightEncoder};
    linkReady = robot->startStream(ids, ROO_ENCODERS_VERIFIED ? 6 : 4) == roocore::Result::Ok;
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
      const auto before = navigator.state;
      arbiter.poll(now, healthy(), active());
      roonav::Output out;
      if (undocking) {
        const float remaining =
            roonav::distance(latest.pose.x, latest.pose.y, navigator.dock.x, navigator.dock.y);
        if (remaining < undockBest - .01f) {
          undockBest = remaining;
          undockProgressAt = now;
        }
        if (!healthy() || !ROO_DOCK_EXIT_VERIFIED || uint32_t(now - undockAt) > 15000 ||
            uint32_t(now - undockProgressAt) > 3000 ||
            fabsf(roonav::wrap(latest.pose.yaw - navigator.dock.yaw)) > .25f) {
          undocking = false;
          navigator.fault(roonav::Reason::NoProgress);
        } else if (remaining < .04f) {
          undocking = false;
          if (startAfterUndock) {
            navigator.start(deepAfterUndock, navigator.dock, now);
            startAfterUndock = false;
          }
        } else {
          out.action = roonav::Action::Drive;
          out.speed = -.06f;
        }
      } else if (active()) {
        // Always maintain a local map/mission during remote control for fallback.
        out = navigator.tick(latest, arbiter.owner == roonav::Owner::Remote);
        if (navigator.state != roonav::State::Cleaning && arbiter.owner == roonav::Owner::Remote)
          arbiter.local(now);
        if (arbiter.owner == roonav::Owner::Remote && healthy() &&
            navigator.state == roonav::State::Cleaning) {
          out.action = roonav::Action::Drive;
          out.speed = (arbiter.left + arbiter.right) / 2000.f;
          out.turn = (arbiter.right - arbiter.left) / (1000 * navigator.cfg.wheelSeparation);
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
      if (navigator.state == roonav::State::Fault)
        undocking = false;
      if (before == roonav::State::Charging && navigator.state == roonav::State::Resuming) {
        if (ROO_DOCK_EXIT_VERIFIED) {
          undocking = true;
          startAfterUndock = false;
          undockAt = undockProgressAt = now;
          undockBest = 1e9f;
        } else
          navigator.fault(roonav::Reason::NoRoute);
      }
      if (before != navigator.state) {
        if (!saveMission()) {
          stopMotion();
          navigator.fault(roonav::Reason::Telemetry);
          lastError = "checkpoint write failed";
        }
      }
      if (active() && uint32_t(now - lastSaved) >= 30000)
        saveMission();
      if (navigator.state == roonav::State::Charging)
        lidarPower(latest.battery >= 78);
      if (!undocking && terminal(navigator.state) && uint32_t(now - anchorAt) > 10000)
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
  navigator.dock.x = -ROO_DOCK_EXIT_METERS;
  navigator.cfg.metersPerTick = ROO_METERS_PER_TICK;
  navigator.cfg.wheelSeparation = ROO_WHEEL_SEPARATION;
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
    if (xQueueSend(requests, &req, 0) != pdTRUE) {
      web.send(503, "text/plain", "queue full");
      return;
    }
    web.send(202, "text/plain", "queued; inspect status for result");
  });
  web.begin();
}
void loop() {
  web.handleClient();
  delay(1);
}
