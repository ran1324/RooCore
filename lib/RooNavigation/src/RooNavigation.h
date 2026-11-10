#pragma once
#include <initializer_list>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace roonav {
constexpr float Pi = 3.14159265358979323846f;
inline float wrap(float a) {
  if (!isfinite(a))
    return NAN;
  a = fmodf(a + Pi, 2 * Pi);
  if (a < 0)
    a += 2 * Pi;
  return a - Pi;
}
inline float distance(float x, float y, float a, float b) { return hypotf(x - a, y - b); }
struct Pose {
  float x = 0, y = 0, yaw = 0;
};
enum class State : uint8_t {
  Idle,
  Cleaning,
  Returning,
  Docking,
  Charging,
  Resuming,
  Settling,
  Recovering,
  Paused,
  Fault,
  Complete,
  Edging,
  Touchup,
  Yielding
};
enum class Reason : uint8_t {
  None,
  Telemetry,
  Localization,
  Cliff,
  WheelDrop,
  Bumper,
  Overcurrent,
  Battery,
  NoRoute,
  NoProgress,
  RecoveryExhausted,
  DockTimeout,
  ChargeLost,
  MapBoundary,
  Operator,
  Restore
};
enum class ReturnCause : uint8_t { None, Manual, Battery, Completed };
enum class Action : uint8_t { Stop, Drive, SeekDock, Charge };
struct Input {
  uint32_t now = 0, poseMs = 0, scanMs = 0, sensorsMs = 0;
  Pose pose;
  float ranges[180]{}; // CCW, robot-relative radians: bin * 2 degrees
  float battery = 0;
  bool localized = false, scanValid = false, sensorsValid = false;
  bool cliff = false, wheelDrop = false, bumper = false, overcurrent = false;
  bool docked = false, charging = false;
  bool batteryValid = true;
  bool chargerValid = false, chargeContactConfirmed = false, chargeReady = false;
  uint8_t chargerState = 0, chargerSources = 0;
  uint32_t chargerMs = 0;
  bool cleaningActive = false, sideBrushActive = false;
  bool retreatVerified = false; // rear corridor was recently traversed with valid floor sensors
  uint8_t dirt = 0;
  bool wallIRValid = false, wallDetected = false;
  uint16_t wallSignal = 0;
};
struct Output {
  float speed = 0, turn = 0;
  uint8_t pwm = 0, sidePwm = 255; // 255 means follow main-brush PWM
  Action action = Action::Stop;
};
// Authorize the trajectory corresponding to the integer wheel command, including saturation.
inline bool quantizeMotion(Output &out, float separation, int &left, int &right) {
  if (!isfinite(out.speed) || !isfinite(out.turn) || !isfinite(separation) || separation <= 0)
    return false;
  const float l = (out.speed - out.turn * separation / 2) * 1000;
  const float r = (out.speed + out.turn * separation / 2) * 1000;
  if (!isfinite(l) || !isfinite(r)) return false;
  left = int(lroundf(fmaxf(-250, fminf(250, l))));
  right = int(lroundf(fmaxf(-250, fminf(250, r))));
  out.speed = (left + right) / 2000.f;
  out.turn = (right - left) / (1000 * separation);
  return true;
}
struct Config {
  int cols = 128, rows = 128;
  float resolution = .1f;
  float originX = -6.4f, originY = -6.4f;
  float radius = .17f, margin = .03f;
  // Provisional brush geometry, NOT manufacturer dimensions. Calibrate on robot.
  float brushWidth = .16f, brushDepth = .04f, brushOffset = 0;
  float sideX = .10f, sideY = -.13f, sideRadius = .10f;
  float overlap = .04f;
  uint16_t wallIRTarget = 0; // zero disables uncalibrated signal-to-steering correction
  bool homeKnown = true;
  bool perimeter = true, wallTouch = false; // touch requires measured bumper/stop latency
  uint32_t perimeterTimeoutMs = 900000;
  float wheelSeparation = .235f, metersPerTick = .000444565f;
  uint32_t targetTimeoutMs = 120000, missionTimeoutMs = 7200000;
  unsigned maxSkipped = 8;
};
inline bool sameMapConfiguration(const Config &a, const Config &b) {
  return a.cols == b.cols && a.rows == b.rows && a.resolution == b.resolution &&
      a.originX == b.originX && a.originY == b.originY && a.radius == b.radius &&
      a.margin == b.margin && a.metersPerTick == b.metersPerTick &&
      a.wheelSeparation == b.wheelSeparation && a.brushWidth == b.brushWidth &&
      a.brushDepth == b.brushDepth && a.brushOffset == b.brushOffset &&
      a.sideX == b.sideX && a.sideY == b.sideY && a.sideRadius == b.sideRadius;
}

// No runtime allocations or unbounded retries. Allocate this object in PSRAM on ESP32.
class Navigator {
public:
  static constexpr int MaxCells = 16384;
  Config cfg;
  State state = State::Idle;
  Reason reason = Reason::None;
  Pose dock, checkpoint;
  int8_t cells[MaxCells]{};
  // Four 5 cm samples per 10 cm navigation cell, four bits per pass.
  uint8_t covered[MaxCells]{}, sideCovered[MaxCells]{}, safe[MaxCells]{}, excluded[MaxCells]{};
  uint8_t contacts[MaxCells]{}, retries[MaxCells]{};
  unsigned clearedObstacles = 0, obstacleWaits = 0;
  uint8_t gapTried[MaxCells]{}, edgeSeen[MaxCells]{}, roomMask[MaxCells]{}, portal[MaxCells]{},
      explored[MaxCells]{};
  unsigned roomsCompleted = 0, frontierVisits = 0, wallContacts = 0;
  unsigned edgeSegments = 0, edgeIncomplete = 0;
  uint8_t pass = 0, passes = 1, recoveries = 0;
  unsigned skipped = 0, completedCells = 0;
  bool finished = false;
  uint32_t activeMs = 0, returnAt = 0, returnSequence = 0;
  ReturnCause returnCause = ReturnCause::None;
  bool (*ordinaryCenterAllowed)(void *, float, float) = nullptr;
  void *ordinaryGuardContext = nullptr;
  explicit Navigator(Config config = Config()) : cfg(config) {}
  int count() const { return cfg.cols * cfg.rows; }
  int cell(float x, float y) const {
    int col = int(floorf((x - cfg.originX) / cfg.resolution)),
        row = int(floorf((y - cfg.originY) / cfg.resolution));
    return col >= 0 && row >= 0 && col < cfg.cols && row < cfg.rows ? row * cfg.cols + col : -1;
  }
  Pose center(int c) const {
    Pose p;
    p.x = cfg.originX + (c % cfg.cols + .5f) * cfg.resolution;
    p.y = cfg.originY + (c / cfg.cols + .5f) * cfg.resolution;
    return p;
  }
  bool configureValid() const {
    return cfg.cols >= 4 && cfg.rows >= 4 && count() <= MaxCells && cfg.resolution >= .1f &&
           cfg.resolution <= .3f && cfg.radius >= .16f && cfg.radius <= .25f &&
           cfg.margin >= .03f && cfg.margin <= .15f && isfinite(cfg.originX) &&
           isfinite(cfg.originY) && cfg.wheelSeparation >= .15f && cfg.wheelSeparation <= .4f &&
           cfg.metersPerTick > .00001f && cfg.metersPerTick < .005f && cfg.maxSkipped > 0 &&
           cfg.maxSkipped <= 100 && cfg.brushWidth >= .1f && cfg.brushWidth <= .3f &&
           cfg.brushDepth >= .02f && cfg.brushDepth <= .1f && fabsf(cfg.brushOffset) <= .1f &&
           cfg.overlap >= .02f && cfg.resolution <= cfg.brushWidth - cfg.overlap + .0001f &&
           cfg.sideX >= 0 && cfg.sideX <= .2f && cfg.sideY < 0 && cfg.sideY >= -.2f &&
           cfg.sideRadius >= .05f && cfg.sideRadius <= .15f;
  }
  void start(bool deep, Pose home, uint32_t now, bool reuseMap = false) {
    if (!configureValid()) {
      fault(Reason::MapBoundary);
      return;
    }
    if (reuseMap && mapConfigKnown_ && !sameMapConfiguration(mapConfig_, cfg)) {
      fault(Reason::Restore);
      return;
    }
    mapConfig_ = cfg;
    mapConfigKnown_ = true;
    if (!reuseMap)
      memset(cells, 0, sizeof cells);
    memset(covered, 0, sizeof covered);
    memset(sideCovered, 0, sizeof sideCovered);
    memset(edgeSeen, 0, sizeof edgeSeen);
    memset(gapTried, 0, sizeof gapTried);
    memset(contacts, 0, sizeof contacts);
    memset(retries, 0, sizeof retries);
    memset(lastHitMs_, 0, sizeof lastHitMs_);
    memset(firstHitMs_, 0, sizeof firstHitMs_);
    memset(freeVotes_, 0, sizeof freeVotes_);
    memset(clearBits_, 0, sizeof clearBits_);
    haveMapScan_ = false;
    clearedObstacles = obstacleWaits = 0;
    skipStreak_ = skipCoverage_ = waitStreak_ = waitCoverage_ = 0;
    wasBumper_ = false;
    touchupDone_ = false;
    touchupCount_ = 0;
    memset(explored, 0, sizeof explored);
    exploring_ = false;
    frontierVisits = wallContacts = 0;
    edgeSegments = edgeIncomplete = roomsCompleted = 0;
    roomSeed_ = home;
    roomReady_ = false;
    roomTransit_ = false;
    roomGoal_ = -1;
    edgePhase_ = 0;
    sweepValid_ = false;
    cleaningReturn_ = State::Cleaning;
    memset(excluded, 0, sizeof excluded);
    memset(safe, 0, sizeof safe);
    pass = 0;
    passes = deep ? 2 : 1;
    dock = home;
    checkpoint = home;
    recoveries = 0;
    skipped = 0;
    completedCells = 0;
    finished = false;
    activeMs = 0;
    clockAt_ = now;
    returnCause = ReturnCause::None;
    returnAt = 0;
    dockAligning_ = false;
    switchState(State::Cleaning, now);
    reason = Reason::None;
  }
  void pause(uint32_t now) {
    if (state != State::Paused && state != State::Fault) {
      resume_ = state;
      switchState(State::Paused, now);
      reason = Reason::Operator;
    }
  }
  bool resume(const Input &in) {
    if ((state != State::Paused && state != State::Fault) || invalid(in) != Reason::None ||
        in.wheelDrop || in.bumper)
      return false;
    if (reason == Reason::RecoveryExhausted || reason == Reason::NoProgress)
      return false;
    // No reset of recovery budget. A new mission is an explicit operator action.
    switchState(in.chargerValid && (in.chargerSources & 2) && resume_ == State::Resuming
                    ? State::Charging : resume_, in.now);
    reason = Reason::None;
    return true;
  }
  void goHome(const Input &in, bool done = false, ReturnCause cause = ReturnCause::Manual) {
    if (state == State::Returning || state == State::Docking || state == State::Charging)
      return; // an HTTP retry must not overwrite the original event or checkpoint
    returnCause = done ? ReturnCause::Completed : cause;
    returnAt = in.now;
    ++returnSequence;
    dockAligning_ = false;
    if (!cfg.homeKnown) {
      finished = done;
      switchState(State::Docking, in.now);
      return;
    }
    checkpoint = in.pose;
    if (state == State::Cleaning || state == State::Edging || state == State::Touchup)
      cleaningReturn_ = state;
    finished = done;
    switchState(State::Returning, in.now);
  }
  void waitOnDock(uint32_t now) { switchState(State::Charging, now); }
  void clearMap(uint32_t now) {
    if (!configureValid()) { fault(Reason::MapBoundary); return; }
    start(false, dock, now, false);
    switchState(State::Idle, now);
  }
  void restoredMapConfiguration() { mapConfig_ = cfg; mapConfigKnown_ = true; }
  void fault(Reason why) {
    if (state != State::Fault)
      resume_ = state;
    state = State::Fault;
    sweepValid_ = false;
    reason = why;
    pathLength_ = 0;
    target_ = -1;
  }
  bool wallRetreat() const { return state == State::Edging && edgePhase_ == 7; }
  bool wallApproach() const { return state == State::Edging && edgePhase_ == 6; }
  bool roomTransit() const { return roomTransit_; }
  bool allowsMotion(Pose p, float speed, float turn, bool qualifiedDockAccess = false) const {
    Pose checked = p;
    if (ordinaryCenterAllowed && !qualifiedDockAccess)
      for (int j = 0; j <= 5; ++j) {
        if (!ordinaryCenterAllowed(ordinaryGuardContext, checked.x, checked.y))
          return false;
        checked.x += speed * cosf(checked.yaw) * .1f;
        checked.y += speed * sinf(checked.yaw) * .1f;
        checked.yaw += turn * .1f;
      }
    if (state != State::Cleaning || roomTransit_ || !roomReady_)
      return true;
    for (int j = 0; j < 5; ++j) {
      p.x += speed * cosf(p.yaw) * .1f;
      p.y += speed * sinf(p.yaw) * .1f;
      p.yaw += turn * .1f;
      int c = cell(p.x, p.y);
      if (c < 0 || !roomMask[c])
        return false;
    }
    return true;
  }
  unsigned edgePhase() const { return edgePhase_; }
  int target() const { return target_; }
  Pose samplePoint(int c, int sub) const {
    Pose p = center(c);
    p.x += (sub & 1 ? .25f : -.25f) * cfg.resolution;
    p.y += (sub & 2 ? .25f : -.25f) * cfg.resolution;
    return p;
  }
  Pose targetPose() const {
    return target_ < 0 ? Pose()
           : state == State::Cleaning && !roomTransit_ && !exploring_
               ? samplePoint(target_, targetSub_)
               : center(target_);
  }
  // Only estimated floor samples inside the moving brush footprint receive credit.
  // Interpolation is bounded and never connects across stopped motors or stale/teleported poses.
  void recordSweep(const Input &in) {
    bool valid = isfinite(in.pose.x) && isfinite(in.pose.y) && isfinite(in.pose.yaw) &&
                 in.localized && in.scanValid && in.sensorsValid &&
                 uint32_t(in.now - in.sensorsMs) <= 200 && uint32_t(in.now - in.scanMs) <= 400 &&
                 uint32_t(in.now - in.poseMs) <= 300 && !in.cliff && !in.wheelDrop && !in.bumper;
    if (!valid || (!in.cleaningActive && !in.sideBrushActive)) {
      sweepValid_ = false;
      return;
    }
    float travel = distance(in.pose.x, in.pose.y, sweepPose_.x, sweepPose_.y);
    float angle = wrap(in.pose.yaw - sweepPose_.yaw);
    bool join = sweepValid_ && uint32_t(in.now - sweepMs_) <= 250 && travel <= .08f &&
                fabsf(angle) <= .5f && sweepMain_ == in.cleaningActive &&
                sweepSide_ == in.sideBrushActive;
    int steps = join ? int(ceilf(fmaxf(travel / .01f, fabsf(angle) / .05f))) : 1;
    if (steps < 1)
      steps = 1;
    for (int step = 1; step <= steps; ++step) {
      Pose pose = in.pose;
      if (join) {
        float t = float(step) / steps;
        pose.x = sweepPose_.x + t * (in.pose.x - sweepPose_.x);
        pose.y = sweepPose_.y + t * (in.pose.y - sweepPose_.y);
        pose.yaw = sweepPose_.yaw + t * angle;
      }
      const float cs = cosf(pose.yaw), sn = sinf(pose.yaw);
      const int cx = int(floorf((pose.x - cfg.originX) / cfg.resolution));
      const int cy = int(floorf((pose.y - cfg.originY) / cfg.resolution));
      const int extent = int(ceilf(.4f / cfg.resolution));
      for (int y = cy - extent; y <= cy + extent; ++y)
        for (int x = cx - extent; x <= cx + extent; ++x) {
          if (x < 0 || y < 0 || x >= cfg.cols || y >= cfg.rows)
            continue;
          int c = y * cfg.cols + x;
          if (cells[c] != -1)
            continue;
          for (int sub = 0; sub < 4; ++sub) {
            Pose p = samplePoint(c, sub);
            float dx = p.x - pose.x, dy = p.y - pose.y;
            float forward = dx * cs + dy * sn, left = -dx * sn + dy * cs;
            uint8_t bit = uint8_t(1u << (sub + pass * 4));
            if (in.cleaningActive && fabsf(forward - cfg.brushOffset) <= cfg.brushDepth / 2 &&
                fabsf(left) <= cfg.brushWidth / 2 && !(covered[c] & bit)) {
              covered[c] |= bit;
              ++completedCells;
            }
            if (in.sideBrushActive &&
                distance(forward, left, cfg.sideX, cfg.sideY) <= cfg.sideRadius)
              sideCovered[c] |= uint8_t(1u << sub);
          }
        }
    }
    sweepPose_ = in.pose;
    sweepMs_ = in.now;
    sweepValid_ = true;
    sweepMain_ = in.cleaningActive;
    sweepSide_ = in.sideBrushActive;
  }
  unsigned pathLength() const { return pathLength_; }
  int pathCell(unsigned i) const { return i < pathLength_ ? path_[i] : -1; }
  State savedState() const {
    State s = state == State::Fault || state == State::Paused ? resume_ : state;
    return s == State::Yielding ? waitResume_ : s;
  }
  State savedCleaningPhase() const { return cleaningReturn_; }
  void yieldForObstacle(const Input &in) { beginYield(in, target_); }
  bool nearbyContact(Pose pose) const {
    for (int c = 0; c < count(); ++c) {
      if (!contacts[c])
        continue;
      Pose p = center(c);
      if (distance(pose.x, pose.y, p.x, p.y) <= .6f)
        return true;
    }
    return false;
  }
  void restorePaused(State prior, uint32_t now, uint32_t elapsed = 0,
                     State cleaningPhase = State::Cleaning) {
    resume_ = prior == State::Docking ? State::Returning : prior;
    if (prior == State::Edging || cleaningPhase == State::Edging) {
      edgePhase_ = 0;
      edgeStarted_ = now;
    }
    cleaningReturn_ = prior == State::Edging ? State::Edging
        : cleaningPhase == State::Edging || cleaningPhase == State::Touchup ? cleaningPhase : State::Cleaning;
    switchState(State::Paused, now);
    reason = Reason::Restore;
    activeMs = elapsed;
    clockAt_ = now;
    roomSeed_ = checkpoint;
    roomReady_ = false;
  }

  Output tick(const Input &in, bool remote = false) {
    Output stop;
    accountTime(in.now);
    if (state == State::Idle || state == State::Paused || state == State::Fault ||
        state == State::Complete)
      return stop;
    Reason bad = invalid(in);
    if (bad != Reason::None) {
      fault(bad);
      return stop;
    }
    if (activeMs > cfg.missionTimeoutMs) {
      fault(Reason::NoProgress);
      return stop;
    }
    if (in.bumper && !wasBumper_) {
      int c = cell(in.pose.x + cfg.radius * cosf(in.pose.yaw),
                   in.pose.y + cfg.radius * sinf(in.pose.yaw));
      if (c >= 0) {
        contacts[c] |= 1;
        cells[c] = 1;
        lastHitMs_[c] = in.now;
      }
    }
    wasBumper_ = in.bumper;
    // A drop is NOT classified as a cliff. Neither permits motion while active.
    if (state == State::Edging && cfg.wallTouch && !in.wheelDrop &&
        (edgePhase_ == 6 || edgePhase_ == 7))
      return contactTick(in);
    if (in.wheelDrop || in.bumper) {
      if (state == State::Recovering || recoveries >= 1) {
        fault(Reason::RecoveryExhausted);
        return stop;
      }
      if (state != State::Settling) {
        resume_ = state;
        switchState(State::Settling, in.now);
        reason = in.wheelDrop ? Reason::WheelDrop : Reason::Bumper;
        stableSince_ = 0;
      }
    }
    if (state == State::Settling) {
      if (uint32_t(in.now - stateSince_) > 5000) {
        fault(Reason::RecoveryExhausted);
        return stop;
      }
      if (in.wheelDrop || in.bumper) {
        stableSince_ = 0;
        return stop;
      }
      if (!stableSince_)
        stableSince_ = in.now;
      if (uint32_t(in.now - stableSince_) < 1000)
        return stop;
      if (!in.retreatVerified || !clearSector(in, Pi, .5f, .40f)) {
        fault(Reason::RecoveryExhausted);
        return stop;
      }
      recoveryOrigin_ = in.pose;
      ++recoveries;
      switchState(State::Recovering, in.now);
      return stop;
    }
    if (state == State::Recovering) {
      if (!in.retreatVerified || !clearSector(in, Pi, .5f, .35f)) {
        fault(Reason::RecoveryExhausted);
        return stop;
      }
      if (distance(in.pose.x, in.pose.y, recoveryOrigin_.x, recoveryOrigin_.y) >= .05f) {
        switchState(resume_, in.now);
        reason = Reason::None;
        return stop;
      }
      if (uint32_t(in.now - stateSince_) >= 1500) {
        fault(Reason::RecoveryExhausted);
        return stop;
      }
      Output out;
      out.action = Action::Drive;
      out.speed = -.04f;
      return out;
    }
    if (state == State::Docking) {
      if (in.chargerValid && uint32_t(in.now - in.chargerMs) <= 200 &&
          (in.chargerSources & 2) && in.chargerState == 5) {
        fault(Reason::ChargeLost);
        return stop;
      }
      if (in.chargerValid && uint32_t(in.now - in.chargerMs) <= 200 &&
          in.chargeContactConfirmed && (in.chargerSources & 2)) {
        switchState(finished || !cfg.homeKnown ? State::Complete : State::Charging, in.now);
        return stop;
      }
      if (uint32_t(in.now - stateSince_) >= 60000) {
        fault(Reason::DockTimeout);
        return stop;
      }
      Output out;
      out.action = Action::SeekDock;
      return out;
    }
    if (state == State::Charging) {
      if (!in.chargerValid || uint32_t(in.now - in.chargerMs) > 200) {
        fault(Reason::Telemetry);
        return stop;
      }
      if (!(in.chargerSources & 2) || in.chargerState == 5) {
        fault(Reason::ChargeLost);
        return stop;
      }
      if (in.chargeReady && in.batteryValid && (in.chargerState == 2 || in.chargerState == 3) &&
          in.localized && in.scanValid && uint32_t(in.now - in.scanMs) <= 400 &&
          uint32_t(in.now - in.poseMs) <= 300) {
        switchState(State::Resuming, in.now);
        return stop;
      }
      Output out;
      out.action = Action::Charge;
      return out;
    }
    if (!haveMapScan_ || in.scanMs != lastMapScan_) {
      updateMap(in);
      haveMapScan_ = true;
      lastMapScan_ = in.scanMs;
    }
    recordSweep(in);
    const int current = cell(in.pose.x, in.pose.y), home = cell(dock.x, dock.y);
    if (current < 0 || home < 0) {
      fault(Reason::MapBoundary);
      return stop;
    }
    if (state == State::Yielding) {
      if (uint32_t(in.now - stateSince_) >= 1000 && safe[current] &&
          clearSector(in, 0, .5f, cfg.radius + cfg.margin + .07f)) {
        switchState(waitResume_, in.now);
        if (waitResume_ == State::Edging)
          edgePhase_ = 0;
        if (waitResume_ == State::Touchup)
          touchupPhase_ = 0;
        return stop;
      }
      if (uint32_t(in.now - stateSince_) < 8000)
        return stop;
      if (!safe[current]) {
        fault(Reason::NoRoute);
        return stop;
      }
      State prior = waitResume_;
      switchState(prior, in.now);
      if (prior == State::Edging)
        edgePhase_ = 0;
      if (prior == State::Touchup)
        touchupPhase_ = 0;
      if (prior == State::Cleaning && blockedGoal_ >= 0)
        skip(blockedGoal_, in);
      return stop;
    }
    if (state != State::Edging && !safe[current]) {
      beginYield(in, target_);
      return stop;
    }
    if (state == State::Edging && in.battery <= 25 && !safe[current])
      edgePhase_ = 4;
    if ((state == State::Cleaning || state == State::Edging || state == State::Touchup ||
         state == State::Resuming) &&
        in.battery <= 25 && safe[current])
      goHome(in, false, ReturnCause::Battery);
    if (state == State::Returning || state == State::Resuming) {
      const int goal = state == State::Returning ? home : cell(checkpoint.x, checkpoint.y);
      if (goal < 0 || !safe[goal]) {
        fault(Reason::NoRoute);
        return stop;
      }
      bool reached = false;
      Output out = follow(in, goal, reached, 0);
      if (reached) {
        if (state == State::Returning && fabsf(wrap(dock.yaw - in.pose.yaw)) > .08f) {
          if (!dockAligning_) { dockAligning_ = true; dockAlignAt_ = in.now; }
          if (uint32_t(in.now - dockAlignAt_) >= 15000) {
            fault(Reason::NoProgress);
            return stop;
          }
          out.action = Action::Drive;
          out.speed = 0;
          out.turn = wrap(dock.yaw - in.pose.yaw) > 0 ? .4f : -.4f;
          return out;
        }
        switchState(state == State::Returning ? State::Docking : cleaningReturn_, in.now);
        return stop;
      }
      return out;
    }
    if (!roomReady_ ||
        (state == State::Cleaning && !roomTransit_ && uint32_t(in.now - roomUpdated_) >= 10000))
      partitionRoom(in.now);
    if (roomTransit_) {
      bool reached = false;
      Output out = follow(in, roomGoal_, reached, passes == 2 ? 127 : 85);
      if (reached) {
        roomSeed_ = in.pose;
        roomTransit_ = false;
        roomReady_ = false;
        pass = 0;
        touchupDone_ = false;
        touchupCount_ = 0;
        switchState(State::Cleaning, in.now);
      }
      return out;
    }
    if (state == State::Edging)
      return edgeTick(in);
    if (state == State::Touchup)
      return touchupTick(in);
    if (!in.cleaningActive) {
      Output out;
      if (!remote) {
        out.action = Action::Drive;
        out.pwm = passes == 2 ? 127 : 85;
      }
      return out;
    }
    const uint8_t bit = uint8_t(15u << (pass * 4));
    // DirtDetect is observation-only until physical calibration. It must not
    // change coverage, motor PWM or trajectory here.
    if (!exploring_ && target_ >= 0 && (covered[target_] & (1u << (targetSub_ + pass * 4)))) {
      target_ = -1;
      pathLength_ = 0;
    }
    if (target_ < 0) {
      target_ = coverageTarget(current, bit);
      if (target_ < 0 && frontierVisits < 32) {
        target_ = frontierTarget(current);
        if (target_ >= 0) {
          exploring_ = true;
          explored[target_] = 1;
          ++frontierVisits;
        }
      }
      if (target_ < 0) {
        if (pass + 1 < passes) {
          ++pass;
          target_ = -1;
          pathLength_ = 0;
          return stop;
        }
        if (cfg.perimeter) {
          edgePhase_ = 0;
          edgeStarted_ = in.now;
          edgeRoomSegments_ = 0;
          switchState(State::Edging, in.now);
        } else
          finishRoom(in);
        return stop;
      }
      float best = 1e9f;
      for (int sub = 0; sub < 4; ++sub) {
        if (covered[target_] & (1u << (sub + pass * 4)))
          continue;
        Pose p = samplePoint(target_, sub);
        float d = distance(p.x, p.y, in.pose.x, in.pose.y);
        if (d < best) {
          best = d;
          targetSub_ = sub;
        }
      }
      targetSince_ = progressSince_ = in.now;
      bestDistance_ = 1e9f;
    }
    if (remote) {
      pathLength_ = 0;
      targetSince_ = progressSince_ = in.now;
      if (!safe[target_])
        skip(target_, in);
      return stop;
    }
    bool reached = false;
    Output out = follow(in, target_, reached, passes == 2 ? 127 : 85);
    if (reached) {
      exploring_ = false;
      target_ = -1;
      pathLength_ = 0;
    }
    return out;
  }

  void updateMap(const Input &in) {
    memset(hits_, 0, sizeof hits_);
    memset(free_, 0, sizeof free_);
    memset(centerFree_, 0, sizeof centerFree_);
    for (unsigned i = 0; i < 180; ++i) {
      const float r = in.ranges[i];
      if (!isfinite(r) || r < .05f || r > 12)
        continue;
      float a = in.pose.yaw + i * Pi / 90, ux = cosf(a), uy = sinf(a);
      // Quantize the hit just beyond its surface, so a boundary does not
      // alternate between the free-side and obstacle-side cells on rotation.
      float hitX = in.pose.x + ux * (r + .005f), hitY = in.pose.y + uy * (r + .005f);
      hitX =
          fmaxf(cfg.originX + .001f, fminf(cfg.originX + cfg.cols * cfg.resolution - .001f, hitX));
      hitY =
          fmaxf(cfg.originY + .001f, fminf(cfg.originY + cfg.rows * cfg.resolution - .001f, hitY));
      int end = cell(hitX, hitY);
      if (end >= 0)
        hits_[end] = 1;
      const float limit = fminf(r - cfg.resolution, 20.f);
      for (float d = 0; d < limit; d += cfg.resolution / 3) {
        int c = cell(in.pose.x + ux * d, in.pose.y + uy * d);
        if (c >= 0) {
          free_[c] = 1;
          if (cells[c] == 1) {
            Pose middle = center(c);
            for (int sub = 0; sub < 4; ++sub) {
              float dx = middle.x + (sub & 1 ? .4f : -.4f) * cfg.resolution - in.pose.x;
              float dy = middle.y + (sub & 2 ? .4f : -.4f) * cfg.resolution - in.pose.y;
              float along = dx * ux + dy * uy, across = fabsf(dx * uy - dy * ux);
              if (across <= cfg.resolution * .15f && r > along + .03f)
                centerFree_[c] |= uint8_t(1u << sub);
            }
          }
        }
      }
    }
    // Block immediately. Reopen only after repeated centre-crossing free rays.
    // A bumper-only object may be below the laser and cannot be erased by it.
    for (int c = 0; c < count(); ++c) {
      if (hits_[c]) {
        if (cells[c] != 1 || !firstHitMs_[c])
          firstHitMs_[c] = in.now;
        cells[c] = 1;
        lastHitMs_[c] = in.now;
        freeVotes_[c] = 0;
        clearBits_[c] = 0;
        if (contacts[c] & 1)
          contacts[c] |= 2;
      } else if (cells[c] == 1) {
        if (centerFree_[c] && free_[c]) {
          clearBits_[c] |= centerFree_[c];
          if (freeVotes_[c] < 255)
            ++freeVotes_[c];
        } else {
          freeVotes_[c] = 0;
          clearBits_[c] = 0;
        }
        if (clearBits_[c] == 15 && freeVotes_[c] >= 20 &&
            uint32_t(in.now - lastHitMs_[c]) >= 2500 && (!(contacts[c] & 1) || (contacts[c] & 2))) {
          cells[c] = -1;
          contacts[c] = 0;
          firstHitMs_[c] = 0;
          ++clearedObstacles;
          // Retry only goals whose clearance could have changed here.
          const int reach = int(ceilf((cfg.radius + cfg.margin + .15f) / cfg.resolution));
          for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx) {
              int x = c % cfg.cols + dx, y = c / cfg.cols + dy;
              if (x < 0 || y < 0 || x >= cfg.cols || y >= cfg.rows)
                continue;
              int g = y * cfg.cols + x;
              if (excluded[g] == 1 && retries[g] < 2)
                excluded[g] = 0;
            }
        }
      } else if (free_[c])
        cells[c] = -1;
      safe[c] = 0;
    }
    const float radius = cfg.radius + cfg.margin + cfg.resolution * .707107f;
    const int n = int(ceilf(radius / cfg.resolution));
    for (int c = 0; c < count(); ++c)
      if (cells[c] == -1) {
        bool ok = true;
        const int x = c % cfg.cols, y = c / cfg.cols;
        for (int dy = -n; dy <= n && ok; ++dy)
          for (int dx = -n; dx <= n; ++dx) {
            if ((dx * dx + dy * dy) * cfg.resolution * cfg.resolution > radius * radius)
              continue;
            if (x + dx < 0 || y + dy < 0 || x + dx >= cfg.cols || y + dy >= cfg.rows ||
                cells[(y + dy) * cfg.cols + x + dx] != -1) {
              ok = false;
              break;
            }
          }
        const Pose candidate = center(c);
        safe[c] = ok && (!ordinaryCenterAllowed ||
            ordinaryCenterAllowed(ordinaryGuardContext, candidate.x, candidate.y));
      }
  }

private:
  Config mapConfig_;
  bool mapConfigKnown_ = false;
  bool dockAligning_ = false;
  uint32_t dockAlignAt_ = 0;
  int16_t parent_[MaxCells]{}, queue_[MaxCells]{}, path_[MaxCells]{};
  uint8_t hits_[MaxCells]{}, free_[MaxCells]{}, centerFree_[MaxCells]{}, freeVotes_[MaxCells]{},
      clearBits_[MaxCells]{};
  uint32_t lastHitMs_[MaxCells]{}, firstHitMs_[MaxCells]{};
  uint32_t lastMapScan_ = 0;
  bool haveMapScan_ = false, wasBumper_ = false;
  State waitResume_ = State::Cleaning;
  int blockedGoal_ = -1;
  unsigned pathLength_ = 0;
  unsigned skipStreak_ = 0, skipCoverage_ = 0, waitStreak_ = 0, waitCoverage_ = 0;
  int target_ = -1, targetSub_ = 0;
  Pose sweepPose_;
  uint32_t sweepMs_ = 0;
  bool sweepValid_ = false, sweepMain_ = false, sweepSide_ = false;
  State cleaningReturn_ = State::Cleaning;
  Pose roomSeed_;
  bool roomReady_ = false, roomTransit_ = false, exploring_ = false;
  int roomGoal_ = -1;
  uint32_t roomUpdated_ = 0, contactAt_ = 0, edgeExitAt_ = 0;
  int edgeExitGoal_ = -1;
  bool touchupDone_ = false;
  unsigned touchupCount_ = 0, touchupPhase_ = 0;
  Pose gapPoint_;
  uint32_t touchupAt_ = 0, touchupHold_ = 0;
  Pose contactPose_;
  unsigned edgePhase_ = 0, corners_ = 0, edgeRoomSegments_ = 0;
  uint32_t edgeStarted_ = 0, edgeComponentAt_ = 0, cornerAt_ = 0;
  Pose edgeStart_, edgePrevious_, edgeExit_;
  float edgeHeading_ = 0, edgeTravel_ = 0, cornerYaw_ = 0;
  bool edgeClosing_ = false;
  uint32_t stateSince_ = 0, targetSince_ = 0, progressSince_ = 0, clockAt_ = 0,
           stableSince_ = 0;
  float bestDistance_ = 1e9f;
  State resume_ = State::Cleaning;
  Pose recoveryOrigin_;
  void accountTime(uint32_t now) {
    const uint32_t elapsed = uint32_t(now - clockAt_);
    clockAt_ = now;
    if (state != State::Idle && state != State::Paused && state != State::Fault &&
        state != State::Complete && state != State::Charging)
      activeMs = UINT32_MAX - activeMs < elapsed ? UINT32_MAX : activeMs + elapsed;
  }
  void switchState(State s, uint32_t now) {
    accountTime(now);
    state = s;
    sweepValid_ = false;
    stateSince_ = now;
    pathLength_ = 0;
    target_ = -1;
    targetSince_ = progressSince_ = now;
    bestDistance_ = 1e9f;
  }
  Reason invalid(const Input &in) const {
    if (!isfinite(in.pose.x) || !isfinite(in.pose.y) || !isfinite(in.pose.yaw) ||
        (state != State::Charging &&
         (!in.batteryValid || !isfinite(in.battery) || in.battery < 0 || in.battery > 100)))
      return Reason::Telemetry;
    if (!in.sensorsValid || uint32_t(in.now - in.sensorsMs) > 200)
      return Reason::Telemetry;
    if (in.cliff)
      return Reason::Cliff;
    if (in.overcurrent)
      return Reason::Overcurrent;
    if (in.battery <= 6 && !(in.chargerValid && (in.chargerSources & 2)))
      return Reason::Battery;
    if (state == State::Charging)
      return Reason::None;
    if (!in.localized || uint32_t(in.now - in.poseMs) > 300)
      return Reason::Localization;
    if (!in.scanValid || uint32_t(in.now - in.scanMs) > 400)
      return Reason::Telemetry;
    return Reason::None;
  }
  bool clearSector(const Input &in, float direction, float halfWidth, float minimum) const {
    unsigned samples = 0;
    for (unsigned i = 0; i < 180; ++i)
      if (fabsf(wrap(i * Pi / 90 - direction)) <= halfWidth) {
        float r = in.ranges[i];
        if (!isfinite(r) || r < minimum)
          return false;
        ++samples;
      }
    return samples >= 10;
  }
  int search(int start, int goal, uint8_t bit) {
    if (start < 0 || !safe[start])
      return -1;
    for (int c = 0; c < count(); ++c)
      parent_[c] = -2;
    int head = 0, tail = 0;
    queue_[tail++] = int16_t(start);
    parent_[start] = -1;
    while (head < tail) {
      int c = queue_[head++];
      if ((goal >= 0 && c == goal) || (goal < 0 && (covered[c] & bit) != bit && !excluded[c]))
        return c;
      const int x = c % cfg.cols, y = c / cfg.cols;
      const int horizontal[4] = {x + 1 < cfg.cols ? c + 1 : -1, x ? c - 1 : -1,
                                 y + 1 < cfg.rows ? c + cfg.cols : -1, y ? c - cfg.cols : -1};
      const int vertical[4] = {horizontal[2], horizontal[3], horizontal[0], horizontal[1]};
      const int *ns = pass == 0 ? horizontal : vertical;
      for (int k = 0; k < 4; ++k) {
        int n = ns[k];
        if (n >= 0 && safe[n] && parent_[n] == -2 &&
            (!roomReady_ || roomTransit_ || (state != State::Cleaning && state != State::Edging) ||
             roomMask[n])) {
          parent_[n] = int16_t(c);
          queue_[tail++] = int16_t(n);
        }
      }
    }
    return -1;
  }
  int coverageTarget(int start, uint8_t mask) {
    // Flood only reachable, inflated free space. Finish a horizontal lane in
    // pass 1; transpose lane/along axes in pass 2 for an independent crosshatch.
    search(start, -1, 0); // mask zero explores the entire connected component
    int chosen = -1;
    float best = 1e9f;
    int sx = start % cfg.cols, sy = start / cfg.cols;
    for (int c = 0; c < count(); ++c) {
      if (parent_[c] == -2 || !roomMask[c] || excluded[c] || (covered[c] & mask) == mask)
        continue;
      int x = c % cfg.cols, y = c / cfg.cols;
      int across = pass == 0 ? abs(y - sy) : abs(x - sx);
      int along = pass == 0 ? abs(x - sx) : abs(y - sy);
      float score = float(across * 1000 + (across ? along : -along));
      if (score < best) {
        best = score;
        chosen = c;
      }
    }
    return chosen;
  }
  bool plan(int start, int goal) {
    if (search(start, goal, 0) < 0)
      return false;
    pathLength_ = 0;
    for (int c = goal; c >= 0; c = parent_[c])
      path_[pathLength_++] = int16_t(c);
    return true;
  }
  Output follow(const Input &in, int goal, bool &reached, uint8_t pwm) {
    Output out;
    const int current = cell(in.pose.x, in.pose.y);
    bool broken = pathLength_ == 0;
    for (unsigned i = 0; i < pathLength_; ++i)
      if (!safe[path_[i]])
        broken = true;
    if (broken && !plan(current, goal)) {
      if (state == State::Cleaning) {
        skip(goal, in);
      } else if (state == State::Edging || state == State::Touchup)
        beginYield(in, goal);
      else
        fault(Reason::NoRoute);
      return out;
    }
    while (pathLength_) {
      Pose p = pathLength_ == 1 && state == State::Cleaning && !roomTransit_ && !exploring_
                   ? targetPose()
                   : center(path_[pathLength_ - 1]);
      if (distance(p.x, p.y, in.pose.x, in.pose.y) >
          (pathLength_ == 1 && state == State::Cleaning && !roomTransit_ && !exploring_ ? .008f
                                                                                        : .012f))
        break;
      --pathLength_;
      bestDistance_ = 1e9f;
      progressSince_ = in.now;
    }
    if (!pathLength_) {
      reached = true;
      return out;
    }
    Pose p = pathLength_ == 1 && state == State::Cleaning && !roomTransit_ && !exploring_
                 ? targetPose()
                 : center(path_[pathLength_ - 1]);
    const float d = distance(p.x, p.y, in.pose.x, in.pose.y);
    if (d < bestDistance_ - .003f) {
      bestDistance_ = d;
      progressSince_ = in.now;
    }
    if (uint32_t(in.now - progressSince_) > 8000) {
      fault(Reason::NoProgress);
      return out;
    }
    if (uint32_t(in.now - targetSince_) > cfg.targetTimeoutMs) {
      if (state == State::Cleaning)
        skip(goal, in);
      else
        fault(Reason::NoProgress);
      return out;
    }
    float error = wrap(atan2f(p.y - in.pose.y, p.x - in.pose.x) - in.pose.yaw);
    out.turn = fmaxf(-1.f, fminf(1.f, error * 3));
    out.speed = fabsf(error) < .2f ? fminf(passes == 2 ? .12f : .18f, d * 2.5f) : 0;
    if (out.speed > 0 &&
        !clearSector(in, 0, .5f, cfg.radius + cfg.margin + .02f + out.speed * .2f)) {
      beginYield(in, goal);
      return Output();
    }
    out.pwm = pwm;
    out.action = Action::Drive;
    return out;
  }
  int frontierTarget(int start) {
    search(start, -1, 0);
    float best = 1e9f;
    int goal = -1;
    Pose from = center(start);
    for (int c = 0; c < count(); ++c) {
      if (parent_[c] == -2 || !roomMask[c] || explored[c])
        continue;
      Pose p = center(c);
      bool frontier = false;
      // Look toward unknown from a safe vantage, never plan THROUGH unknown.
      for (int a = 0; a < 16 && !frontier; ++a) {
        float angle = a * Pi / 8;
        for (float d = cfg.resolution; d <= .7f; d += cfg.resolution / 2) {
          int n = cell(p.x + cosf(angle) * d, p.y + sinf(angle) * d);
          if (n < 0 || cells[n] == 1)
            break;
          if (cells[n] == 0) {
            frontier = true;
            break;
          }
        }
      }
      float d = distance(from.x, from.y, p.x, p.y);
      if (frontier && d < best) {
        best = d;
        goal = c;
      }
    }
    return goal;
  }
  Output contactTick(const Input &in) {
    Output out;
    // Only a commissioned, known-wall approach can enter this controller.
    // No cliff/drop/telemetry exception: tick() checked them before entry.
    if (in.wheelDrop) {
      fault(Reason::WheelDrop);
      return out;
    }
    if (edgePhase_ == 6) {
      float travelled = distance(in.pose.x, in.pose.y, contactPose_.x, contactPose_.y);
      if (in.bumper) {
        ++wallContacts;
        edgePhase_ = 7;
        contactAt_ = in.now;
        contactPose_ = in.pose;
        return out; // stop immediately on first contact
      }
      if (uint32_t(in.now - contactAt_) > 6000 || travelled >= .08f) {
        edgePhase_ = 5;
        cornerAt_ = in.now;
        return out; // no repeated taps
      }
      if (!clearSector(in, 0, .5f, cfg.radius - .015f)) {
        edgePhase_ = 5;
        cornerAt_ = in.now;
        return out;
      }
      out.action = Action::Drive;
      out.speed = .015f;
      out.pwm = 85;
      out.sidePwm = 127;
      return out;
    }
    if (uint32_t(in.now - contactAt_) < 200)
      return out;
    if (!in.retreatVerified || !clearSector(in, Pi, .5f, .35f)) {
      fault(Reason::RecoveryExhausted);
      return out;
    }
    float travelled = distance(in.pose.x, in.pose.y, contactPose_.x, contactPose_.y);
    if (travelled >= .03f && !in.bumper) {
      edgePhase_ = 5;
      cornerAt_ = in.now;
      progressSince_ = in.now;
      return out;
    }
    if (uint32_t(in.now - contactAt_) > 2200 || travelled >= .04f) {
      fault(Reason::RecoveryExhausted);
      return out;
    }
    out.action = Action::Drive;
    out.speed = -.02f; // brushes off during release
    return out;
  }
  void partitionRoom(uint32_t now) {
    memset(portal, 0, sizeof portal);
    memset(roomMask, 0, sizeof roomMask);
    // Conservative local zone partition at observed narrow openings (<=1.4 m).
    // This is not semantic room recognition. Unknown space never joins a room.
    const int limit = int(1.4f / cfg.resolution);
    for (int c = 0; c < count(); ++c)
      if (safe[c]) {
        int x = c % cfg.cols, y = c / cfg.cols;
        for (int axis = 0; axis < 2; ++axis) {
          int widths[2] = {limit + 1, limit + 1};
          for (int side = 0; side < 2; ++side)
            for (int d = 1; d <= limit; ++d) {
              int nx = x + (axis == 0 ? d * (side ? 1 : -1) : 0);
              int ny = y + (axis == 1 ? d * (side ? 1 : -1) : 0);
              if (nx < 0 || ny < 0 || nx >= cfg.cols || ny >= cfg.rows)
                break;
              int v = cells[ny * cfg.cols + nx];
              if (v == 0)
                break;
              if (v == 1) {
                widths[side] = d;
                break;
              }
            }
          if (widths[0] + widths[1] <= limit)
            portal[c] = 1;
        }
      }
    int seed = cell(roomSeed_.x, roomSeed_.y);
    if (seed < 0 || !safe[seed]) {
      float best = 1e9f;
      for (int c = 0; c < count(); ++c)
        if (safe[c] && !portal[c]) {
          Pose p = center(c);
          float d = distance(p.x, p.y, roomSeed_.x, roomSeed_.y);
          if (d < best) {
            best = d;
            seed = c;
          }
        }
    }
    if (seed < 0 || !safe[seed])
      return;
    const bool corridor = portal[seed] != 0;
    int head = 0, tail = 0;
    queue_[tail++] = int16_t(seed);
    roomMask[seed] = 1;
    while (head < tail) {
      int c = queue_[head++], x = c % cfg.cols, y = c / cfg.cols;
      int ns[] = {x + 1 < cfg.cols ? c + 1 : -1, x ? c - 1 : -1,
                  y + 1 < cfg.rows ? c + cfg.cols : -1, y ? c - cfg.cols : -1};
      for (int n : ns)
        if (n >= 0 && safe[n] && (portal[n] != 0) == corridor && !roomMask[n]) {
          roomMask[n] = 1;
          queue_[tail++] = int16_t(n);
        }
    }
    roomReady_ = true;
    roomUpdated_ = now;
  }
  bool insideRoom(float x, float y) const {
    int c = cell(x, y);
    if (c < 0 || portal[c])
      return false;
    if (roomMask[c])
      return true;
    // Edging is deliberately closer to the wall than the inflated routing grid.
    float best = .5f;
    bool own = false;
    for (int n = 0; n < count(); ++n)
      if (safe[n]) {
        Pose p = center(n);
        float d = distance(p.x, p.y, x, y);
        if (d < best) {
          best = d;
          own = roomMask[n] != 0;
        }
      }
    return own;
  }
  void finishRoom(const Input &in) {
    if (!touchupDone_) {
      touchupDone_ = true;
      touchupPhase_ = 0;
      switchState(State::Touchup, in.now);
      return;
    }
    ++roomsCompleted;
    roomTransit_ = true; // routing can cross portals only after this room's passes + edge phase
    int start = cell(in.pose.x, in.pose.y);
    search(start, -1, 0);
    int goal = -1;
    float best = 1e9f;
    const uint8_t mask = passes == 2 ? 255 : 15;
    for (int c = 0; c < count(); ++c) {
      if (parent_[c] == -2 || roomMask[c] || excluded[c] || (covered[c] & mask) == mask)
        continue;
      // Prefer a room interior over a doorway; corridor-only leftovers are still eligible.
      Pose p = center(c);
      float d = distance(p.x, p.y, in.pose.x, in.pose.y) + (portal[c] ? 100 : 0);
      if (d < best) {
        best = d;
        goal = c;
      }
    }
    if (goal < 0 || roomsCompleted >= 32) {
      if (goal >= 0)
        ++edgeIncomplete;
      roomTransit_ = false;
      goHome(in, true);
      return;
    }
    roomGoal_ = goal;
    switchState(State::Cleaning, in.now);
    target_ = goal;
    // Corridor-only regions are treated as their own finite zone on arrival.
    if (portal[goal]) {
      roomReady_ = false;
    }
  }
  Output touchupTick(const Input &in) {
    Output out;
    if (touchupPhase_ == 0) {
      int start = cell(in.pose.x, in.pose.y);
      // Search the same connected zone. Use short point/rotation cleaning for
      // leftovers that have no full lane, without inventing a narrower body.
      bool savedTransit = roomTransit_;
      roomTransit_ = false;
      State savedState = state;
      state = State::Cleaning;
      search(start, -1, 0);
      state = savedState;
      roomTransit_ = savedTransit;
      float best = 1e9f;
      int goal = -1, sampleCell = -1, sampleSub = -1;
      const float reach = hypotf(cfg.sideX, cfg.sideY) + cfg.sideRadius - .015f;
      int n = int(ceilf(reach / cfg.resolution));
      if (touchupCount_ < 32)
        for (int c = 0; c < count(); ++c) {
          if (parent_[c] == -2 || !roomMask[c])
            continue;
          Pose p = center(c);
          float travel = distance(p.x, p.y, in.pose.x, in.pose.y);
          if (travel >= best)
            continue;
          int cx = c % cfg.cols, cy = c / cfg.cols;
          for (int dy = -n; dy <= n; ++dy)
            for (int dx = -n; dx <= n; ++dx) {
              int x = cx + dx, y = cy + dy;
              if (x < 0 || y < 0 || x >= cfg.cols || y >= cfg.rows)
                continue;
              int k = y * cfg.cols + x;
              if (cells[k] != -1)
                continue;
              for (int sub = 0; sub < 4; ++sub) {
                if ((covered[k] & (1u << (sub + pass * 4))) || (sideCovered[k] & (1u << sub)) ||
                    (gapTried[k] & (1u << sub)))
                  continue;
                Pose q = samplePoint(k, sub);
                if (distance(p.x, p.y, q.x, q.y) > reach)
                  continue;
                best = travel;
                goal = c;
                sampleCell = k;
                sampleSub = sub;
                gapPoint_ = q;
              }
            }
        }
      if (goal < 0) {
        finishRoom(in);
        return out;
      }
      gapTried[sampleCell] |= uint8_t(1u << sampleSub);
      ++touchupCount_;
      target_ = goal;
      targetSince_ = progressSince_ = in.now;
      bestDistance_ = 1e9f;
      pathLength_ = 0;
      touchupPhase_ = 1;
    }
    if (touchupPhase_ == 1) {
      bool reached = false;
      out = follow(in, target_, reached, passes == 2 ? 127 : 85);
      out.sidePwm = 127;
      if (reached) {
        touchupPhase_ = 2;
        touchupAt_ = in.now;
        touchupHold_ = 0;
      }
      return out;
    }
    if (uint32_t(in.now - touchupAt_) > 12000) {
      touchupPhase_ = 0;
      return out;
    }
    float heading =
        atan2f(gapPoint_.y - in.pose.y, gapPoint_.x - in.pose.x) - atan2f(cfg.sideY, cfg.sideX);
    float error = wrap(heading - in.pose.yaw);
    out.action = Action::Drive;
    out.pwm = passes == 2 ? 127 : 85;
    out.sidePwm = 127;
    if (fabsf(error) > .08f) {
      out.turn = fmaxf(-.5f, fminf(.5f, error * 2));
      return out;
    }
    if (!touchupHold_)
      touchupHold_ = in.now;
    if (uint32_t(in.now - touchupHold_) >= 1000) {
      touchupPhase_ = 0;
      return Output();
    }
    return out;
  }
  // Perimeter controller: wall stays on the RIGHT. Separate high side-brush
  // PWM from slow body motion; no contact-based pushing or safety override.
  int nearbyUnseenWall(int c) const {
    Pose p = center(c);
    int found = -1;
    float best = .56f;
    int radius = int(ceilf(best / cfg.resolution));
    int cx = c % cfg.cols, cy = c / cfg.cols;
    for (int dy = -radius; dy <= radius; ++dy)
      for (int dx = -radius; dx <= radius; ++dx) {
        int x = cx + dx, y = cy + dy;
        if (x < 0 || y < 0 || x >= cfg.cols || y >= cfg.rows)
          continue;
        int n = y * cfg.cols + x;
        if (cells[n] != 1 || edgeSeen[n])
          continue;
        Pose w = center(n);
        float d = distance(p.x, p.y, w.x, w.y);
        if (d < best) {
          best = d;
          found = n;
        }
      }
    return found;
  }
  int edgeStage(int start) {
    for (int c = 0; c < count(); ++c)
      parent_[c] = -2;
    int head = 0, tail = 0;
    queue_[tail++] = int16_t(start);
    parent_[start] = -1;
    while (head < tail) {
      int c = queue_[head++];
      if (roomMask[c] && nearbyUnseenWall(c) >= 0)
        return c;
      int x = c % cfg.cols, y = c / cfg.cols;
      int ns[] = {x + 1 < cfg.cols ? c + 1 : -1, y + 1 < cfg.rows ? c + cfg.cols : -1,
                  x ? c - 1 : -1, y ? c - cfg.cols : -1};
      for (int n : ns)
        if (n >= 0 && safe[n] && roomMask[n] && parent_[n] == -2) {
          parent_[n] = int16_t(c);
          queue_[tail++] = int16_t(n);
        }
    }
    return -1;
  }
  void seenWall(const Input &in) {
    // Mark only endpoints actually beside the brush, not the whole boundary.
    for (int i = 120; i <= 150; i += 3) {
      float r = in.ranges[i];
      if (!isfinite(r) || r < cfg.radius || r > .40f)
        continue;
      float a = in.pose.yaw + i * Pi / 90;
      int c = cell(in.pose.x + r * cosf(a), in.pose.y + r * sinf(a));
      if (c < 0)
        continue;
      int cx = c % cfg.cols, cy = c / cfg.cols;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          int x = cx + dx, y = cy + dy;
          if (x >= 0 && y >= 0 && x < cfg.cols && y < cfg.rows)
            edgeSeen[y * cfg.cols + x] = 1;
        }
    }
  }
  bool edgeClear(const Input &in, float heading, float distance) const {
    return clearSector(in, heading, .5f, cfg.radius + cfg.margin + distance);
  }
  Output edgeTick(const Input &in) {
    Output out;
    out.action = Action::Drive;
    out.pwm = passes == 2 ? 127 : 85;
    out.sidePwm = 127;
    int current = cell(in.pose.x, in.pose.y);
    if (current < 0) {
      fault(Reason::MapBoundary);
      return Output();
    }
    if (edgePhase_ == 0) {
      if (!safe[current]) {
        edgePhase_ = 4;
        return Output();
      }
      if (edgeRoomSegments_ >= 16 || uint32_t(in.now - edgeStarted_) >= cfg.perimeterTimeoutMs) {
        ++edgeIncomplete;
        finishRoom(in);
        return Output();
      }
      target_ = edgeStage(current);
      if (target_ < 0) {
        finishRoom(in);
        return Output();
      }
      // A staging point itself is consumed once, including an interrupted edge.
      int stageWall = nearbyUnseenWall(target_);
      edgeSeen[stageWall] = 1;
      Pose stage = center(target_), surface = center(stageWall);
      bool vertical = fabsf(surface.x - stage.x) > fabsf(surface.y - stage.y);
      for (int sign : {-1, 1})
        for (int d = 1; d < count(); ++d) {
          int x = stageWall % cfg.cols + (vertical ? 0 : sign * d);
          int y = stageWall / cfg.cols + (vertical ? sign * d : 0);
          if (x < 0 || y < 0 || x >= cfg.cols || y >= cfg.rows || cells[y * cfg.cols + x] != 1)
            break;
          edgeSeen[y * cfg.cols + x] = 1; // attempted face, NOT a claim of cleaned floor
        }
      edgeExit_ = center(target_);
      Pose wall = center(stageWall);
      // right normal = (sin(yaw), -cos(yaw))
      edgeHeading_ = atan2f(wall.y - edgeExit_.y, wall.x - edgeExit_.x) + Pi / 2;
      edgePhase_ = 1;
      targetSince_ = progressSince_ = in.now;
      bestDistance_ = 1e9f;
      pathLength_ = 0;
    }
    if (edgePhase_ == 1) {
      bool reached = false;
      out = follow(in, target_, reached, passes == 2 ? 127 : 85);
      out.sidePwm = 127;
      if (reached) {
        edgePhase_ = 2;
        edgeComponentAt_ = in.now;
        edgeTravel_ = 0;
        edgePrevious_ = edgeStart_ = in.pose;
        edgeClosing_ = false;
        corners_ = 0;
      }
      return out;
    }
    if (edgePhase_ == 2) {
      float error = wrap(edgeHeading_ - in.pose.yaw);
      if (fabsf(error) > .08f) {
        if (uint32_t(in.now - edgeComponentAt_) > 12000) {
          fault(Reason::NoProgress);
          return Output();
        }
        out.turn = fmaxf(-.4f, fminf(.4f, error * 2));
        return out;
      }
      edgePhase_ = 3;
      edgeComponentAt_ = progressSince_ = in.now;
      edgePrevious_ = in.pose;
    }
    if (edgePhase_ == 3) {
      float travelled = distance(in.pose.x, in.pose.y, edgePrevious_.x, edgePrevious_.y);
      edgeTravel_ += travelled;
      if (travelled > .002f)
        progressSince_ = in.now;
      edgePrevious_ = in.pose;
      seenWall(in);
      if (uint32_t(in.now - edgeStarted_) >= cfg.perimeterTimeoutMs ||
          uint32_t(in.now - edgeComponentAt_) > 600000 || corners_ > 24) {
        ++edgeIncomplete;
        edgePhase_ = 4;
      } else if (edgeClosing_ && edgeTravel_ > 1.5f &&
                 distance(in.pose.x, in.pose.y, edgeStart_.x, edgeStart_.y) < .12f) {
        ++edgeSegments;
        ++edgeRoomSegments_;
        edgePhase_ = 4;
      } else if (uint32_t(in.now - progressSince_) > 8000) {
        fault(Reason::NoProgress);
        return Output();
      } else {
        float front = in.ranges[0], right = in.ranges[135];
        if (!isfinite(front) || !isfinite(right) || front < .05f || right < .05f) {
          fault(Reason::Telemetry);
          return Output();
        }
        const float desired = cfg.radius + cfg.margin + .015f;
        if (!edgeClosing_ && fabsf(right - desired) < .035f) {
          edgeClosing_ = true;
          edgeStart_ = in.pose;
          edgeTravel_ = 0;
        }
        // A missing right wall is an opening, not permission to enter a new room.
        if (right > .65f || !insideRoom(in.pose.x + .15f * cosf(in.pose.yaw),
                                        in.pose.y + .15f * sinf(in.pose.yaw))) {
          ++edgeSegments;
          ++edgeRoomSegments_;
          edgePhase_ = 4;
          return Output();
        }
        if (front < cfg.radius + cfg.margin + (cfg.wallTouch ? .04f : .10f)) {
          cornerYaw_ =
              wrap(in.pose.yaw + (front < cfg.radius + cfg.margin + .10f ? Pi / 2 : -Pi / 2));
          cornerAt_ = in.now;
          ++corners_;
          edgePhase_ = 5;
          float fx = in.pose.x + (front + .005f) * cosf(in.pose.yaw),
                fy = in.pose.y + (front + .005f) * sinf(in.pose.yaw);
          fx = fmaxf(cfg.originX + .001f,
                     fminf(cfg.originX + cfg.cols * cfg.resolution - .001f, fx));
          fy = fmaxf(cfg.originY + .001f,
                     fminf(cfg.originY + cfg.rows * cfg.resolution - .001f, fy));
          int frontCell = cell(fx, fy);
          bool established = frontCell >= 0 && firstHitMs_[frontCell] &&
                             uint32_t(in.now - firstHitMs_[frontCell]) >= 10000;
          if (cfg.wallTouch && established && wallContacts < 8) {
            edgePhase_ = 6;
            contactAt_ = in.now;
            contactPose_ = in.pose;
          }
          return out;
        }
        // Right diagonal returns estimate tangent angle of the wall.
        float rf = in.ranges[150], rr = in.ranges[120];
        float tangent = 0;
        if (isfinite(rf) && isfinite(rr) && rf > .05f && rr > .05f && rf < .8f && rr < .8f)
          tangent = atan2f(.8660254f * (rr - rf), .5f * (rf + rr));
        float irCorrection = 0;
        if (cfg.wallIRTarget && in.wallIRValid && in.wallDetected)
          irCorrection = fmaxf(-.12f, fminf(.12f, .12f * (float(in.wallSignal) - cfg.wallIRTarget) /
                                                      cfg.wallIRTarget));
        out.turn = fmaxf(-.45f, fminf(.45f, 2 * tangent + 3 * (desired - right) + irCorrection));
        if (!edgeClear(in, 0, .035f)) {
          cornerYaw_ = wrap(in.pose.yaw + Pi / 2);
          cornerAt_ = in.now;
          ++corners_;
          edgePhase_ = 5;
          return Output();
        }
        out.speed = .05f;
        return out;
      }
    }
    if (edgePhase_ == 5) {
      // One bounded quarter-turn, never repeated in-place scrubbing.
      float error = wrap(cornerYaw_ - in.pose.yaw);
      if (uint32_t(in.now - cornerAt_) > 6000) {
        fault(Reason::NoProgress);
        return Output();
      }
      if (fabsf(error) < .06f) {
        edgePhase_ = 3;
        progressSince_ = in.now;
        return out;
      }
      out.turn = fmaxf(-.4f, fminf(.4f, error * 2));
      return out;
    }
    if (edgePhase_ != 4)
      edgeExitAt_ = 0;
    if (edgePhase_ == 4) {
      // Re-enter inflated free space before ordinary routing or low-battery return.
      if (safe[current]) {
        edgePhase_ = 0;
        edgeExitAt_ = 0;
        edgeExitGoal_ = -1;
        target_ = -1;
        return Output();
      }
      if (!edgeExitAt_) {
        edgeExitAt_ = in.now;
        edgeExitGoal_ = -1;
      }
      if (uint32_t(in.now - edgeExitAt_) > 20000) {
        fault(Reason::NoProgress);
        return Output();
      }
      float best = .65f;
      int goal = edgeExitGoal_;
      for (int c = 0; c < count() && edgeExitGoal_ < 0; ++c)
        if (safe[c]) {
          Pose p = center(c);
          float d = distance(p.x, p.y, in.pose.x, in.pose.y);
          float direction = wrap(atan2f(p.y - in.pose.y, p.x - in.pose.x) - in.pose.yaw);
          if (d < best && edgeClear(in, direction, d)) {
            best = d;
            goal = c;
          }
        }
      if (goal < 0) {
        fault(Reason::NoRoute);
        return Output();
      }
      edgeExitGoal_ = goal;
      Pose p = center(goal);
      float error = wrap(atan2f(p.y - in.pose.y, p.x - in.pose.x) - in.pose.yaw);
      out.turn = fmaxf(-.4f, fminf(.4f, error * 2));
      out.speed = fabsf(error) < .15f ? .04f : 0;
      if (uint32_t(in.now - stateSince_) > cfg.perimeterTimeoutMs + 30000) {
        fault(Reason::NoProgress);
        return Output();
      }
      return out;
    }
    return Output();
  }
  void beginYield(const Input &in, int goal) {
    if (state == State::Yielding)
      return;
    ++obstacleWaits;
    if (completedCells >= waitCoverage_ + 4)
      waitStreak_ = 0;
    waitCoverage_ = completedCells;
    if (++waitStreak_ > 16) {
      fault(Reason::NoProgress);
      return;
    }
    waitResume_ = state;
    blockedGoal_ = goal;
    switchState(State::Yielding, in.now);
  }
  void skip(int goal, const Input &in) {
    const uint32_t now = in.now;
    if (roomTransit_) {
      roomTransit_ = false;
      roomSeed_ = in.pose;
      roomReady_ = false;
      pass = 0;
    }
    if (goal >= 0) {
      if (retries[goal] < 2)
        ++retries[goal];
      excluded[goal] = retries[goal] >= 2 ? 2 : 1;
    }
    ++skipped;
    if (completedCells >= skipCoverage_ + 4)
      skipStreak_ = 0;
    skipCoverage_ = completedCells;
    ++skipStreak_;
    target_ = -1;
    exploring_ = false;
    pathLength_ = 0;
    targetSince_ = progressSince_ = now;
    if (skipStreak_ >= cfg.maxSkipped)
      fault(Reason::NoProgress);
  }
};

class Odometry {
public:
  Pose pose;
  bool valid = false;
  uint32_t stamp = 0;
  void reset(Pose p, uint16_t left, uint16_t right, uint32_t now) {
    pose = p;
    left_ = left;
    right_ = right;
    stamp = now;
    valid = true;
  }
  bool update(uint16_t left, uint16_t right, uint32_t now, const Config &cfg) {
    if (!valid || !isfinite(cfg.metersPerTick) || !isfinite(cfg.wheelSeparation) ||
        cfg.wheelSeparation <= 0 || cfg.metersPerTick <= 0) {
      valid = false;
      return false;
    }
    int dl = delta(left, left_), dr = delta(right, right_);
    left_ = left;
    right_ = right;
    uint32_t dt = now - stamp;
    stamp = now;
    const float l = dl * cfg.metersPerTick, r = dr * cfg.metersPerTick;
    if (dt == 0 || dt > 200 || fabsf(l) > .6f * dt / 1000 + .002f ||
        fabsf(r) > .6f * dt / 1000 + .002f) {
      valid = false;
      return false;
    }
    const float angle = (r - l) / cfg.wheelSeparation, travel = (r + l) / 2;
    pose.x += travel * cosf(pose.yaw + angle / 2);
    pose.y += travel * sinf(pose.yaw + angle / 2);
    pose.yaw = wrap(pose.yaw + angle);
    return true;
  }

private:
  uint16_t left_ = 0, right_ = 0;
  static int delta(uint16_t a, uint16_t b) {
    int v = int(a) - int(b);
    if (v > 32767)
      v -= 65536;
    if (v < -32768)
      v += 65536;
    return v;
  }
};

// Bounded local scan-to-map check/correction around odometry. No global loop
// closure or kidnapped-robot recovery. Ambiguous/poor alignment fails closed.
inline bool alignToMap(Pose &pose, const float *ranges, const Navigator &nav, float &score) {
  unsigned occupied = 0;
  for (int i = 0; i < nav.count(); ++i)
    if (nav.cells[i] == 1)
      ++occupied;
  if (occupied < 15) {
    score = 1;
    return true;
  } // initial map is anchored explicitly at dock
  float best = -1;
  Pose winner = pose;
  for (int ix = -1; ix <= 1; ++ix)
    for (int iy = -1; iy <= 1; ++iy)
      for (int ia = -1; ia <= 1; ++ia) {
        Pose p = pose;
        p.x += ix * .025f;
        p.y += iy * .025f;
        p.yaw += ia * .02f;
        unsigned hit = 0, total = 0;
        for (unsigned i = 0; i < 180; i += 3) {
          float r = ranges[i];
          if (!isfinite(r) || r < .1f || r > 8)
            continue;
          float a = p.yaw + i * Pi / 90;
          int c = nav.cell(p.x + r * cosf(a), p.y + r * sinf(a));
          if (c < 0)
            continue;
          ++total;
          bool nearby = false;
          const int x = c % nav.cfg.cols, y = c / nav.cfg.cols;
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
              if (x + dx >= 0 && y + dy >= 0 && x + dx < nav.cfg.cols && y + dy < nav.cfg.rows &&
                  nav.cells[(y + dy) * nav.cfg.cols + x + dx] == 1)
                nearby = true;
          if (nearby)
            ++hit;
        }
        float value = total >= 20 ? float(hit) / total : 0;
        // Prefer odometry in ties to prevent random drift along featureless walls.
        value -= .002f * (abs(ix) + abs(iy) + abs(ia));
        if (value > best) {
          best = value;
          winner = p;
        }
      }
  score = best;
  if (best < .55f)
    return false;
  pose = winner;
  return true;
}
} // namespace roonav
