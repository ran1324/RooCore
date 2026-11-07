#pragma once
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
  Complete
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
enum class Action : uint8_t { Stop, Drive, SeekDock, Charge };
struct Input {
  uint32_t now = 0, poseMs = 0, scanMs = 0, sensorsMs = 0;
  Pose pose;
  float ranges[180]{}; // CCW, robot-relative radians: bin * 2 degrees
  float battery = 0;
  bool localized = false, scanValid = false, sensorsValid = false;
  bool cliff = false, wheelDrop = false, bumper = false, overcurrent = false;
  bool docked = false, charging = false;
  bool cleaningActive = false;
  bool retreatVerified = false; // rear corridor was recently traversed with valid floor sensors
  uint8_t dirt = 0;
};
struct Output {
  float speed = 0, turn = 0;
  uint8_t pwm = 0;
  Action action = Action::Stop;
};
struct Config {
  int cols = 64, rows = 64;
  float resolution = .2f;
  float originX = -6.4f, originY = -6.4f;
  float radius = .18f, margin = .05f;
  float wheelSeparation = .235f, metersPerTick = .000444565f;
  uint32_t targetTimeoutMs = 120000, missionTimeoutMs = 7200000;
  unsigned maxSkipped = 8;
};

// No heap allocations, no blocking retry loops, max 4096 cells.
class Navigator {
public:
  static constexpr int MaxCells = 4096;
  Config cfg;
  State state = State::Idle;
  Reason reason = Reason::None;
  Pose dock, checkpoint;
  int8_t cells[MaxCells]{};
  uint8_t covered[MaxCells]{}, safe[MaxCells]{}, excluded[MaxCells]{};
  uint8_t pass = 0, passes = 1, recoveries = 0;
  unsigned skipped = 0, completedCells = 0;
  bool finished = false;
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
           cfg.maxSkipped <= 100;
  }
  void start(bool deep, Pose home, uint32_t now) {
    if (!configureValid()) {
      fault(Reason::MapBoundary);
      return;
    }
    memset(cells, 0, sizeof cells);
    memset(covered, 0, sizeof covered);
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
    dirtCount_ = 0;
    dirtUntil_ = 0;
    missionStart_ = now;
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
    switchState(resume_, in.now);
    reason = Reason::None;
    return true;
  }
  void goHome(const Input &in, bool done = false) {
    checkpoint = in.pose;
    finished = done;
    switchState(State::Returning, in.now);
  }
  void fault(Reason why) {
    if (state != State::Fault)
      resume_ = state;
    state = State::Fault;
    reason = why;
    pathLength_ = 0;
    target_ = -1;
  }
  int target() const { return target_; }
  unsigned pathLength() const { return pathLength_; }
  int pathCell(unsigned i) const { return i < pathLength_ ? path_[i] : -1; }
  void restorePaused(State prior, uint32_t now) {
    resume_ = prior == State::Docking ? State::Returning : prior;
    switchState(State::Paused, now);
    reason = Reason::Restore;
    missionStart_ = now;
  }

  Output tick(const Input &in, bool remote = false) {
    Output stop;
    if (state == State::Idle || state == State::Paused || state == State::Fault ||
        state == State::Complete)
      return stop;
    Reason bad = invalid(in);
    if (bad != Reason::None) {
      fault(bad);
      return stop;
    }
    if (uint32_t(in.now - missionStart_) > cfg.missionTimeoutMs) {
      fault(Reason::NoProgress);
      return stop;
    }
    // A drop is NOT classified as a cliff. Neither permits motion while active.
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
      if (in.docked && in.charging) {
        switchState(finished ? State::Complete : State::Charging, in.now);
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
      if (!in.docked || !in.charging) {
        fault(Reason::ChargeLost);
        return stop;
      }
      if (in.battery >= 80 && in.localized && in.scanValid && uint32_t(in.now - in.scanMs) <= 400 &&
          uint32_t(in.now - in.poseMs) <= 300) {
        switchState(State::Resuming, in.now);
        return stop;
      }
      Output out;
      out.action = Action::Charge;
      return out;
    }
    updateMap(in);
    const int current = cell(in.pose.x, in.pose.y), home = cell(dock.x, dock.y);
    if (current < 0 || home < 0) {
      fault(Reason::MapBoundary);
      return stop;
    }
    if (!safe[current]) {
      fault(Reason::NoRoute);
      return stop;
    }
    if ((state == State::Cleaning || state == State::Resuming) && in.battery <= 25)
      goHome(in);
    if (state == State::Returning || state == State::Resuming) {
      const int goal = state == State::Returning ? home : cell(checkpoint.x, checkpoint.y);
      if (goal < 0 || !safe[goal]) {
        fault(Reason::NoRoute);
        return stop;
      }
      bool reached = false;
      Output out = follow(in, goal, reached, 0);
      if (reached) {
        switchState(state == State::Returning ? State::Docking : State::Cleaning, in.now);
        return stop;
      }
      return out;
    }
    if (!in.cleaningActive) {
      Output out;
      if (!remote) {
        out.action = Action::Drive;
        out.pwm = passes == 2 ? 127 : 85;
      }
      return out;
    }
    // Only count distinct cells in the current pass. All earlier passes persist.
    const uint8_t bit = uint8_t(1u << pass);
    for (int c = 0; c < count(); ++c)
      if (safe[c] && !(covered[c] & bit)) {
        Pose p = center(c);
        if (distance(p.x, p.y, in.pose.x, in.pose.y) <= .17f) {
          covered[c] |= bit;
          ++completedCells;
        }
      }
    // At most 16 dirt zones, one bounded stationary extra clean per zone.
    if (in.dirt >= 90 && !dirtSeen(in.pose) && dirtCount_ < 16) {
      dirtZones_[dirtCount_++] = in.pose;
      dirtUntil_ = in.now + 1500;
    }
    if (int32_t(dirtUntil_ - in.now) > 0) {
      Output out;
      out.action = Action::Drive;
      out.pwm = 127;
      return out;
    }
    if (target_ >= 0 && (covered[target_] & bit)) {
      target_ = -1;
      pathLength_ = 0;
    }
    if (target_ < 0) {
      target_ = search(current, -1, bit);
      if (target_ < 0) {
        if (pass + 1 < passes) {
          ++pass;
          target_ = -1;
          pathLength_ = 0;
          return stop;
        }
        goHome(in, true);
        return stop;
      }
      targetSince_ = progressSince_ = in.now;
      bestDistance_ = 1e9f;
    }
    if (remote) {
      pathLength_ = 0;
      targetSince_ = progressSince_ = in.now;
      if (!safe[target_])
        skip(target_, in.now);
      return stop;
    }
    bool reached = false;
    Output out = follow(in, target_, reached, passes == 2 ? 127 : 85);
    if (reached) {
      target_ = -1;
      pathLength_ = 0;
    }
    return out;
  }

  void updateMap(const Input &in) {
    memset(hits_, 0, sizeof hits_);
    memset(free_, 0, sizeof free_);
    for (unsigned i = 0; i < 180; ++i) {
      const float r = in.ranges[i];
      if (!isfinite(r) || r < .05f || r > 12)
        continue;
      float a = in.pose.yaw + i * Pi / 90, ux = cosf(a), uy = sinf(a);
      int end = cell(in.pose.x + ux * r, in.pose.y + uy * r);
      if (end >= 0)
        hits_[end] = 1;
      const float limit = fminf(r - cfg.resolution, 20.f);
      for (float d = 0; d < limit; d += cfg.resolution / 3) {
        int c = cell(in.pose.x + ux * d, in.pose.y + uy * d);
        if (c >= 0)
          free_[c] = 1;
      }
    }
    // Obstacles latch for this mission. Clearing/removing furniture requires a
    // new map. Prevents tangent-ray flicker from endlessly alternating targets.
    for (int c = 0; c < count(); ++c) {
      if (hits_[c])
        cells[c] = 1;
      else if (free_[c] && cells[c] != 1)
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
            if (hypotf(float(dx), float(dy)) * cfg.resolution > radius)
              continue;
            if (x + dx < 0 || y + dy < 0 || x + dx >= cfg.cols || y + dy >= cfg.rows ||
                cells[(y + dy) * cfg.cols + x + dx] != -1) {
              ok = false;
              break;
            }
          }
        safe[c] = ok;
      }
  }

private:
  int16_t parent_[MaxCells]{}, queue_[MaxCells]{}, path_[MaxCells]{};
  uint8_t hits_[MaxCells]{}, free_[MaxCells]{};
  unsigned pathLength_ = 0;
  int target_ = -1;
  uint32_t stateSince_ = 0, targetSince_ = 0, progressSince_ = 0, missionStart_ = 0,
           stableSince_ = 0, dirtUntil_ = 0;
  float bestDistance_ = 1e9f;
  State resume_ = State::Cleaning;
  Pose recoveryOrigin_, dirtZones_[16];
  unsigned dirtCount_ = 0;
  bool dirtSeen(Pose p) const {
    for (unsigned i = 0; i < dirtCount_; ++i)
      if (distance(p.x, p.y, dirtZones_[i].x, dirtZones_[i].y) < .6f)
        return true;
    return false;
  }
  void switchState(State s, uint32_t now) {
    state = s;
    stateSince_ = now;
    pathLength_ = 0;
    target_ = -1;
    targetSince_ = progressSince_ = now;
    bestDistance_ = 1e9f;
  }
  Reason invalid(const Input &in) const {
    if (!isfinite(in.pose.x) || !isfinite(in.pose.y) || !isfinite(in.pose.yaw) ||
        !isfinite(in.battery) || in.battery < 0 || in.battery > 100)
      return Reason::Telemetry;
    if (!in.sensorsValid || uint32_t(in.now - in.sensorsMs) > 200)
      return Reason::Telemetry;
    if (in.cliff)
      return Reason::Cliff;
    if (in.overcurrent)
      return Reason::Overcurrent;
    if (in.battery <= 6 && !(in.docked && in.charging))
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
      if ((goal >= 0 && c == goal) || (goal < 0 && !(covered[c] & bit) && !excluded[c]))
        return c;
      const int x = c % cfg.cols, y = c / cfg.cols;
      const int ns[4] = {x + 1 < cfg.cols ? c + 1 : -1, y + 1 < cfg.rows ? c + cfg.cols : -1,
                         x ? c - 1 : -1, y ? c - cfg.cols : -1};
      for (int n : ns)
        if (n >= 0 && safe[n] && parent_[n] == -2) {
          parent_[n] = int16_t(c);
          queue_[tail++] = int16_t(n);
        }
    }
    return -1;
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
        skip(goal, in.now);
      } else
        fault(Reason::NoRoute);
      return out;
    }
    while (pathLength_) {
      Pose p = center(path_[pathLength_ - 1]);
      if (distance(p.x, p.y, in.pose.x, in.pose.y) > .03f)
        break;
      --pathLength_;
      bestDistance_ = 1e9f;
      progressSince_ = in.now;
    }
    if (!pathLength_) {
      reached = true;
      return out;
    }
    Pose p = center(path_[pathLength_ - 1]);
    const float d = distance(p.x, p.y, in.pose.x, in.pose.y);
    if (d < bestDistance_ - .015f) {
      bestDistance_ = d;
      progressSince_ = in.now;
    }
    if (uint32_t(in.now - progressSince_) > 8000) {
      fault(Reason::NoProgress);
      return out;
    }
    if (uint32_t(in.now - targetSince_) > cfg.targetTimeoutMs) {
      if (state == State::Cleaning)
        skip(goal, in.now);
      else
        fault(Reason::NoProgress);
      return out;
    }
    float error = wrap(atan2f(p.y - in.pose.y, p.x - in.pose.x) - in.pose.yaw);
    out.turn = fmaxf(-1.f, fminf(1.f, error * 3));
    out.speed = fabsf(error) < .2f ? fminf(passes == 2 ? .12f : .18f, d * 2.5f) : 0;
    if (out.speed > 0 && !clearSector(in, 0, .5f, cfg.radius + cfg.margin + .06f)) {
      if (state == State::Cleaning)
        skip(goal, in.now);
      else
        fault(Reason::NoRoute);
      return Output();
    }
    out.pwm = pwm;
    out.action = Action::Drive;
    return out;
  }
  void skip(int goal, uint32_t now) {
    if (goal >= 0)
      excluded[goal] = 1;
    ++skipped;
    target_ = -1;
    pathLength_ = 0;
    targetSince_ = progressSince_ = now;
    if (skipped >= cfg.maxSkipped)
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
