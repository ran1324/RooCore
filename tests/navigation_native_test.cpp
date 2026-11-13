#include "ControlArbiter.h"
#include "NavigationHelp.h"
#include "RooNavigation.h"
#include "TminiPlus.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace roonav;
static Input healthy(uint32_t now = 100) {
  Input i;
  i.now = i.poseMs = i.scanMs = i.sensorsMs = now;
  i.localized = i.scanValid = i.sensorsValid = true;
  i.battery = 90;
  i.cleaningActive = true;
  for (float &r : i.ranges)
    r = 2;
  return i;
}
static void clockAt(Input &i, uint32_t now) {
  i.now = i.poseMs = i.scanMs = i.sensorsMs = i.chargerMs = now;
  i.chargerValid = true; // explicit synthetic telemetry, never supplied by a physical robot
  i.chargerSources = i.docked ? 2 : 0;
  i.chargerState = i.charging ? (i.battery >= 100 ? 3 : 2) : 0;
  i.chargeContactConfirmed = i.docked;
  i.chargeReady = i.docked && i.charging && i.battery >= 100;
}
static void safety() {
  Navigator n;
  Input i = healthy();
  n.start(false, i.pose, i.now);
  i.cliff = true;
  assert(n.tick(i).action == Action::Stop);
  assert(n.state == State::Fault && n.reason == Reason::Cliff);
  assert(!n.resume(i));
  i.cliff = false;
  assert(n.resume(i));
  n.start(false, i.pose, i.now);
  i.wheelDrop = true;
  n.tick(i);
  assert(n.state == State::Settling);
  clockAt(i, 5300);
  assert(n.tick(i).action == Action::Stop);
  assert(n.reason == Reason::RecoveryExhausted);
  assert(!n.resume(i));
  i = healthy();
  n.start(false, i.pose, i.now);
  i.wheelDrop = true;
  n.tick(i);
  i.wheelDrop = false;
  clockAt(i, 200);
  n.tick(i);
  clockAt(i, 1300);
  n.tick(i);
  assert(n.state == State::Fault); // clear rear LiDAR alone is not proof of safe floor
  i = healthy();
  i.retreatVerified = true;
  n.start(false, i.pose, i.now);
  i.bumper = true;
  n.tick(i);
  assert(n.contacts[n.cell(i.pose.x + n.cfg.radius, i.pose.y)] & 1);
  i.bumper = false;
  clockAt(i, 200);
  n.tick(i);
  clockAt(i, 1300);
  n.tick(i);
  assert(n.state == State::Recovering);
  clockAt(i, 1400);
  assert(n.tick(i).speed < 0);
  i.wheelDrop = true;
  clockAt(i, 1500);
  assert(n.tick(i).action == Action::Stop);
  assert(n.state == State::Fault);
  i = healthy();
  n.start(false, i.pose, i.now);
  i.overcurrent = true;
  n.tick(i);
  assert(n.reason == Reason::Overcurrent);
  i = healthy();
  n.start(false, i.pose, i.now);
  i.now = 600;
  n.tick(i);
  assert(n.state == State::Fault);
}
static std::vector<uint8_t> packet(bool start, int first, int count = 30) {
  std::vector<uint8_t> p(10 + count * 3);
  p[0] = 0xaa;
  p[1] = 0x55;
  p[2] = start ? 1 : 0;
  p[3] = uint8_t(count);
  auto put = [&](int index, uint16_t v) {
    p[index] = uint8_t(v);
    p[index + 1] = uint8_t(v >> 8);
  };
  put(4, uint16_t(first * 128 + 1));
  put(6, uint16_t(((first + (count - 1) * 2) % 360) * 128 + 1));
  uint16_t sum = 0x55aa ^ uint16_t(p[2] + (count << 8)) ^ uint16_t(p[4] + (p[5] << 8)) ^
                 uint16_t(p[6] + (p[7] << 8));
  for (int j = 0; j < count; ++j) {
    p[10 + 3 * j] = 80;
    put(11 + 3 * j, 4000);
    sum ^= 80 ^ 4000;
  }
  put(8, sum);
  return p;
}
static void lidar() {
  TminiPlus parser;
  for (int rev = 0; rev < 3; ++rev)
    for (int part = 0; part < 6; ++part)
      for (auto b : packet(part == 0, part * 60))
        parser.feed(b, 100 + rev * 100 + part * 10);
  assert(parser.valid && parser.scans == 2);
  for (float r : parser.ranges)
    assert(fabsf(r - 1) < .001);
  auto broken = packet(true, 0);
  broken.back() ^= 1;
  for (auto b : broken)
    parser.feed(b, 400);
  assert(!parser.valid && parser.rejected > 0);
  const uint8_t oversized[] = {0xaa, 0x55, 0, 255};
  for (auto b : oversized)
    parser.feed(b, 410);
  assert(!parser.valid);
}
static void odometry() {
  Odometry o;
  Config c;
  Pose p;
  o.reset(p, 65530, 65530, 100);
  assert(o.update(4, 4, 120, c));
  assert(o.pose.x > 0);
  assert(!o.update(30000, 30000, 140, c));
  assert(!o.valid);
}
struct Box {
  float x1, y1, x2, y2;
};
static float ray(Pose p, float a, const std::vector<Box> &boxes) {
  float ux = cosf(a), uy = sinf(a);
  float best = 100;
  if (fabsf(ux) > 1e-6)
    best = fminf(best, ((ux > 0 ? 4.8f : 0) - p.x) / ux);
  if (fabsf(uy) > 1e-6)
    best = fminf(best, ((uy > 0 ? 3.6f : 0) - p.y) / uy);
  for (auto box : boxes) {
    float lo = 0, hi = 100;
    float origins[] = {p.x, p.y}, dirs[] = {ux, uy}, lows[] = {box.x1, box.y1},
          highs[] = {box.x2, box.y2};
    for (int j = 0; j < 2; ++j) {
      if (fabsf(dirs[j]) < 1e-6) {
        if (origins[j] < lows[j] || origins[j] > highs[j])
          hi = -1;
      } else {
        float x = (lows[j] - origins[j]) / dirs[j], y = (highs[j] - origins[j]) / dirs[j];
        lo = fmaxf(lo, fminf(x, y));
        hi = fminf(hi, fmaxf(x, y));
      }
    }
    if (hi >= lo)
      best = fminf(best, lo);
  }
  return best - .001f;
}
static bool collision(Pose p, const std::vector<Box> &boxes) {
  if (p.x < .17 || p.y < .17 || p.x > 4.63 || p.y > 3.43)
    return true;
  for (auto b : boxes)
    if (distance(p.x, p.y, fmaxf(b.x1, fminf(b.x2, p.x)), fmaxf(b.y1, fminf(b.y2, p.y))) < .17)
      return true;
  return false;
}
static void dynamicOccupancy() {
  Config cfg;
  cfg.cols = 48;
  cfg.rows = 36;
  cfg.originX = cfg.originY = 0;
  Navigator n(cfg);
  Input i = healthy();
  i.pose.x = 1.05f;
  i.pose.y = 1.05f;
  n.start(false, i.pose, i.now);
  std::vector<Box> boxes = {{1.65f, .85f, 1.95f, 1.25f}};
  auto scan = [&]() {
    for (int k = 0; k < 180; ++k)
      i.ranges[k] = ray(i.pose, i.pose.yaw + k * Pi / 90, boxes);
    n.updateMap(i);
  };
  for (int k = 0; k < 8; ++k) {
    clockAt(i, 100 + k * 100);
    scan();
  }
  int c = n.cell(1.66f, 1.05f);
  assert(n.cells[c] == 1);
  boxes.clear();
  for (int k = 0; k < 10; ++k) {
    clockAt(i, 900 + k * 100);
    scan();
  }
  assert(n.cells[c] == 1); // a fleeting clear ray is not enough
  for (int k = 0; k < 40; ++k) {
    clockAt(i, 1900 + k * 100);
    scan();
  }
  assert(n.cells[c] == -1 && n.clearedObstacles > 0);
  // Low bumper-only obstacle: laser can see past it, but that proves no floor clearance.
  n.contacts[c] = 1;
  n.cells[c] = 1;
  Pose near = n.center(c);
  near.x -= .25f;
  assert(n.nearbyContact(near));
  near.x -= 1.f;
  assert(!n.nearbyContact(near));
  for (int k = 0; k < 50; ++k) {
    clockAt(i, 6000 + k * 100);
    scan();
  }
  assert(n.cells[c] == 1 && n.contacts[c] == 1);
  boxes = {{1.65f, .85f, 1.95f, 1.25f}};
  clockAt(i, 12000);
  scan();
  assert(n.contacts[c] == 3); // now actually corroborated at laser height
  boxes.clear();
  for (int k = 0; k < 50; ++k) {
    clockAt(i, 12100 + k * 100);
    scan();
  }
  assert(n.cells[c] == -1 && !n.contacts[c]);
}
static void obstacleWaitSafety() {
  Config cfg;
  cfg.cols = 48;
  cfg.rows = 36;
  cfg.originX = cfg.originY = 0;
  Navigator n(cfg);
  Input i = healthy();
  i.pose.x = i.pose.y = 1.05f;
  n.start(false, i.pose, i.now);
  for (int k = 0; k < 180; ++k)
    i.ranges[k] = ray(i.pose, i.pose.yaw + k * Pi / 90, {});
  n.tick(i);
  n.yieldForObstacle(i);
  clockAt(i, 500);
  assert(n.tick(i).action == Action::Stop && n.state == State::Yielding);
  clockAt(i, 1200);
  assert(n.tick(i).action == Action::Stop && n.state == State::Cleaning);
  n.yieldForObstacle(i);
  i.cliff = true;
  clockAt(i, 1300);
  assert(n.tick(i).action == Action::Stop && n.reason == Reason::Cliff);
  i.cliff = false;
  n.start(false, i.pose, i.now);
  n.yieldForObstacle(i);
  i.battery = 5;
  clockAt(i, 1400);
  assert(n.tick(i).action == Action::Stop && n.reason == Reason::Battery);
}
static void cornerReplanStartsAtActualPose() {
  Config c;
  c.cols = 48; c.rows = 36; c.originX = c.originY = 0;
  Navigator n(c);
  Input i = healthy();
  Pose home; home.x = 3.25f; home.y = .35f;
  n.start(false, home, i.now);
  for (int k = 0; k < n.count(); ++k) n.cells[k] = -1;
  // Recorded failure pose: recentering at (3.35, .45) points into the corner;
  // the destination is in the opposite direction and has a clear path.
  i.pose.x = 3.3441f; i.pose.y = .4361f; i.pose.yaw = 1.0875f;
  const std::vector<Box> boxes = {{3.5f, .6f, 4.1f, 1.2f}};
  n.goHome(i, false);
  for (int step = 0; step < 400 && n.state != State::Docking; ++step) {
    clockAt(i, 100 + step * 200);
    for (int k = 0; k < 180; ++k)
      i.ranges[k] = ray(i.pose, i.pose.yaw + k * Pi / 90, boxes);
    Output out = n.tick(i);
    assert(n.state != State::Fault);
    if (!step) {
      assert(n.state == State::Returning && out.action == Action::Drive);
      assert(out.speed == 0 && out.turn != 0); // turn away, do not recenter or wait
    }
    for (int sub = 0; sub < 10 && out.action == Action::Drive; ++sub) {
      i.pose.x += out.speed * cosf(i.pose.yaw + out.turn * .01f) * .02f;
      i.pose.y += out.speed * sinf(i.pose.yaw + out.turn * .01f) * .02f;
      i.pose.yaw = wrap(i.pose.yaw + out.turn * .02f);
      assert(!collision(i.pose, boxes));
    }
  }
  assert(n.state == State::Docking && n.obstacleWaits == 0);
}
static void reportedBrushFootprint() {
  Config c;
  c.cols = 40; c.rows = 30; c.originX = c.originY = 0;
  Navigator n(c);
  Input i = healthy(); i.pose.x = 1.07f; i.pose.y = 1.025f;
  i.sideBrushActive = true;
  n.start(false, i.pose, i.now);
  for (int k = 0; k < n.count(); ++k) n.cells[k] = -1;
  n.recordSweep(i);
  assert(n.covered[n.cell(1.025f, 1.025f)] & 1); // 45 mm behind axle, under brush
  assert(!(n.covered[n.cell(1.075f, 1.025f)] & 2)); // 5 mm ahead, outside brush
  assert(n.sideCovered[n.cell(1.175f, .925f)] & 2); // 30 mm from side pivot
  assert(!(n.sideCovered[n.cell(1.175f, .975f)] & 8)); // 80 mm exceeds 70 mm radius
}
static void brushGeometry() {
  Config c;
  c.brushOffset = 0; // isolate rotation/interpolation from the separate rear-offset test
  c.cols = 40;
  c.rows = 30;
  c.originX = c.originY = 0;
  Navigator n(c);
  Input i = healthy();
  i.pose.x = 1.0f;
  i.pose.y = 1.025f;
  n.start(false, i.pose, i.now);
  for (int k = 0; k < n.count(); ++k)
    n.cells[k] = -1;
  auto has = [&](float x, float y, int pass) {
    int cell = n.cell(x, y);
    for (int sub = 0; sub < 4; ++sub) {
      Pose p = n.samplePoint(cell, sub);
      if (distance(x, y, p.x, p.y) < .002f)
        return bool(n.covered[cell] & (1u << (sub + pass * 4)));
    }
    assert(false);
    return false;
  };
  n.recordSweep(i);
  assert(has(.975f, 1.025f, 0)); // +/-25 mm depth samples fit inside 70 mm
  assert(has(1.025f, 1.025f, 0));
  assert(has(1.025f, 1.075f, 0));
  assert(!has(1.125f, 1.025f, 0)); // body over it, but main brush never crossed it
  assert(!has(1.025f, 1.125f, 0));
  assert(!has(1.025f, 1.025f, 1));
  clockAt(i, 200);
  i.pose.x = 1.085f;
  n.recordSweep(i);
  assert(has(1.075f, 1.025f, 0)); // continuous ribbon, not disconnected pose stamps
  clockAt(i, 400);
  i.pose.x = 2.025f;
  n.recordSweep(i);
  assert(!has(1.525f, 1.025f, 0)); // teleport never paints a joining line
  i.cleaningActive = false;
  i.sideBrushActive = true;
  clockAt(i, 600);
  i.pose.x = 3.025f;
  unsigned before = n.completedCells;
  n.recordSweep(i);
  assert(n.completedCells == before); // side brush is NEVER counted as main brush
  bool side = false;
  for (int k = 0; k < n.count(); ++k)
    side |= n.sideCovered[k] != 0;
  assert(side);
  i.sideBrushActive = false;
  clockAt(i, 800);
  i.pose.y = 2.025f;
  n.recordSweep(i);
  assert(n.completedCells == before);
  i.cleaningActive = true;
  i.pose.x = i.pose.y = 1.025f;
  i.pose.yaw = Pi / 2;
  n.pass = 1;
  clockAt(i, 1000);
  n.recordSweep(i);
  assert(has(1.075f, 1.025f, 1));
  assert(!has(1.025f, 1.075f, 1)); // second pass footprint is rotated, not copied
  c.brushWidth = .11f;
  Navigator invalid(c);
  invalid.start(false, i.pose, i.now);
  assert(invalid.state == State::Fault); // cannot choose lanes wider than brush minus overlap
}
static void scenario(bool furniture, bool deep, bool battery, bool doorway = false,
                     bool touch = false, bool moving = false, bool zeroBrushOffset = false) {
  Config cfg;
  cfg.cols = 48;
  cfg.rows = 36;
  cfg.resolution = .1;
  cfg.originX = cfg.originY = 0;
  cfg.margin = .03;
  cfg.wallTouch = touch;
  if (zeroBrushOffset)
    cfg.brushOffset = 0; // original 70 mm-depth regression before the axle-offset report
  Navigator n(cfg);
  Input i = healthy();
  i.pose.x = i.pose.y = .675;
  n.start(deep, i.pose, i.now);
  std::vector<Box> boxes;
  if (furniture)
    boxes = {{1.8f, 1.4f, 2.4f, 2.1f}, {3.5f, .6f, 4.1f, 1.2f}};
  if (doorway)
    boxes = {{2.25f, 0, 2.55f, 1.1f}, {2.25f, 2.3f, 2.55f, 3.6f}};
  bool charged = false, resumed = false, rightRoom = false, secondStartedHere = false;
  float axisTravel[2][2]{};
  unsigned gentleContacts = 0, lastPass = 0;
  Pose lastPose = i.pose;
  bool edgeBeforeCross = false;
  float travelled = 0;
  int movingAt = -1, movingCell = -1;
  for (int step = 0; step < 36000; ++step) {
    clockAt(i, 100 + step * 200);
    if (moving && movingAt < 0 && step > 100 && i.pose.x > .8f && i.pose.x < 3.8f &&
        i.pose.y > .6f && i.pose.y < 2.8f) {
      float x = i.pose.x + .65f * cosf(i.pose.yaw), y = i.pose.y + .65f * sinf(i.pose.yaw);
      boxes.push_back({x - .15f, y - .15f, x + .15f, y + .15f});
      movingAt = step;
      movingCell = n.cell(x, y);
    }
    if (movingAt >= 0 && step == movingAt + 100)
      boxes.clear();
    if (battery && step == 200)
      i.battery = 18;
    for (int j = 0; j < 180; ++j)
      i.ranges[j] = ray(i.pose, i.pose.yaw + j * Pi / 90, boxes);
    if (n.state == State::Edging && i.pose.x < 2.25f)
      edgeBeforeCross = true;
    if (doorway && i.pose.x > 2.55f) {
      assert(edgeBeforeCross);
      rightRoom = true;
    }
    auto before = n.state;
    bool transit = n.roomTransit();
    i.retreatVerified = true; // ideal simulator has a recorded level-floor trail
    auto out = n.tick(i);
    i.cleaningActive = out.action == Action::Drive && out.pwm > 0;
    i.sideBrushActive =
        out.action == Action::Drive && (out.sidePwm == 255 ? out.pwm : out.sidePwm) > 0;
    if (n.state == State::Fault) {
      std::printf("scenario fault %d at %d skip %u edge=%u seg=%u pos=%.3f,%.3f yaw=%.2f\n",
                  int(n.reason), step, n.skipped, n.edgePhase(), n.edgeSegments, i.pose.x, i.pose.y,
                  i.pose.yaw);
      std::printf("before state=%d safe=%d target=%d\n", int(before),
                  n.safe[n.cell(i.pose.x, i.pose.y)], n.target());
      break;
    }
    if (n.state == State::Docking) {
      i.docked = i.charging = true;
    }
    if (n.state == State::Charging) {
      charged = true;
      i.battery += 1;
    }
    if (n.state == State::Resuming) {
      resumed = true;
      i.docked = i.charging = false;
    }
    if (deep && lastPass == 0 && n.pass == 1) {
      assert(distance(i.pose.x, i.pose.y, lastPose.x, lastPose.y) < .05f);
      secondStartedHere = true;
    }
    lastPass = n.pass;
    lastPose = i.pose;
    if (out.action == Action::Drive) {
      if (before == State::Cleaning && !transit) {
        axisTravel[n.pass][0] += fabsf(out.speed * cosf(i.pose.yaw)) * .2f;
        axisTravel[n.pass][1] += fabsf(out.speed * sinf(i.pose.yaw)) * .2f;
      }
      for (int sub = 0; sub < 10; ++sub) {
        Pose previous = i.pose;
        i.pose.x += out.speed * cosf(i.pose.yaw + out.turn * .01f) * .02f;
        i.pose.y += out.speed * sinf(i.pose.yaw + out.turn * .01f) * .02f;
        i.pose.yaw = wrap(i.pose.yaw + out.turn * .02f);
        if (collision(i.pose, boxes)) {
          assert(touch && n.wallApproach() && out.speed <= .0151f);
          i.pose = previous;
          if (!i.bumper)
            ++gentleContacts;
          i.bumper = true;
          break;
        }
        if (out.speed < 0)
          i.bumper = false;
      }
      travelled += fabsf(out.speed) * .2f;
    }
    if (n.state == State::Complete)
      break;
  }
  std::printf(
      "native scenario furniture=%d deep=%d battery=%d state=%d cells=%u skips=%u distance=%.1f\n",
      furniture, deep, battery, int(n.state), n.completedCells, n.skipped, travelled);
  assert(n.state == State::Complete);
  for (int c = 0; c < n.count(); ++c)
    assert(n.retries[c] <= 2);
  assert(n.roomsCompleted < 12);
  assert(n.completedCells > 200);
  if (moving) {
    assert(movingAt >= 0 && n.clearedObstacles > 0);
    assert(n.cells[movingCell] == -1);
    assert(n.covered[movingCell] != 0); // return to clean the previously occupied floor
  }
  std::printf(
      "axis horizontal %.1f/%.1f vertical %.1f/%.1f contacts %u rooms %u edges %u incomplete %u\n",
      axisTravel[0][0], axisTravel[0][1], axisTravel[1][0], axisTravel[1][1], gentleContacts,
      n.roomsCompleted, n.edgeSegments, n.edgeIncomplete);
  assert(axisTravel[0][0] > axisTravel[0][1] * 1.3f);
  if (deep) {
    assert(n.pass == 1 && secondStartedHere);
    assert(axisTravel[1][1] > axisTravel[1][0] * 1.3f);
  }
  if (doorway) {
    assert(rightRoom);
    assert(n.roomsCompleted >= 2);
  }
  if (touch) {
    assert(gentleContacts > 0 && gentleContacts <= 8);
    assert(n.wallContacts == gentleContacts);
  }
  if (battery)
    assert(charged && resumed);
}
static void noProgress() {
  Config cfg;
  cfg.cols = 48;
  cfg.rows = 36;
  cfg.resolution = .1;
  cfg.originX = cfg.originY = 0;
  cfg.margin = .03;
  Navigator n(cfg);
  Input i = healthy();
  i.pose.x = i.pose.y = .675;
  n.start(false, i.pose, i.now);
  const std::vector<Box> boxes;
  for (int tick = 0; tick < 1000 && n.state != State::Fault; ++tick) {
    clockAt(i, 100 + tick * 100);
    for (int j = 0; j < 180; ++j)
      i.ranges[j] = ray(i.pose, i.pose.yaw + j * Pi / 90, boxes);
    n.tick(i); // Frozen encoders/pose despite drive intentions.
  }
  assert(n.state == State::Fault && n.reason == Reason::NoProgress);
  assert(n.tick(i).action == Action::Stop);
  assert(!n.resume(i));
  Config invalid;
  invalid.wheelSeparation = 0;
  Navigator bad(invalid);
  bad.start(false, i.pose, 0);
  assert(bad.state == State::Fault);
  assert(isnan(wrap(INFINITY)));
}
static void assistancePolicy() {
  NavigationHelp paused;
  paused.operatorPause();
  assert(!paused.begin(Reason::Localization, 100, true));
  paused.operatorResume();
  assert(paused.begin(Reason::Localization, 100, true));
  paused.operatorPause();
  Pose candidate;
  assert(!paused.candidate(candidate));
  assert(!paused.allowLocal(healthy()));
  NavigationHelp h;
  Input i = healthy();
  assert(h.begin(Reason::Localization, 100, true));
  assert(h.allowLocal(i));
  assert(!h.begin(Reason::NoRoute, 200, true));
  assert(!h.validReply(h.id - 1, h.frame, 500, 200));
  assert(!h.validReply(h.id, h.frame + 1, 500, 200));
  assert(!h.validReply(h.id, h.frame, 200, 200));
  assert(h.validReply(h.id, h.frame, 500, 200));
  i.battery = 24;
  assert(h.allowLocal(i)); // low battery may invoke native IR search
  i.cliff = true;
  assert(!h.allowLocal(i));
  assert(h.status == NavigationHelp::Status::Held);
  Pose p;
  assert(h.candidate(p));
  assert(!h.confirm(true));
  assert(!h.confirm(true));
  assert(h.confirm(true));
  assert(!h.pending());
  assert(h.begin(Reason::Localization, 1000, false));
  assert(!h.allowLocal(i));
  assert(h.candidate(p));
  assert(!h.confirm(false));
  assert(h.status == NavigationHelp::Status::Held);
  NavigationHelp deadline;
  assert(deadline.begin(Reason::NoRoute, 0xfffffff0u, true));
  assert(deadline.validReply(deadline.id, deadline.frame, 100, 0xfffffff1u));
  i = healthy(7200100);
  assert(!deadline.allowLocal(i));
  Config cfg;
  cfg.homeKnown = false;
  Navigator n(cfg);
  i = healthy();
  n.start(false, i.pose, i.now);
  n.goHome(i);
  assert(n.state == State::Docking);
  assert(n.tick(i).action == Action::SeekDock);
  clockAt(i, 60200);
  assert(n.tick(i).action == Action::Stop);
  assert(n.state == State::Fault && n.reason == Reason::DockTimeout);
  n.start(false, i.pose, i.now);
  n.goHome(i);
  i.docked = i.charging = true;
  clockAt(i, i.now);
  n.tick(i);
  assert(n.state == State::Complete); // never guesses a global resume pose after docking
}
static void arbitration() {
  ControlArbiter wrapOwner;
  assert(wrapOwner.takeover(0xfffffff0u, true, true));
  assert(wrapOwner.command(wrapOwner.epoch, 1, 100, 0, 0, 0, 0xfffffff1u));
  wrapOwner.poll(99, true, true);
  assert(wrapOwner.owner == Owner::Remote);
  wrapOwner.poll(101, true, true);
  assert(wrapOwner.owner == Owner::Stopping);
  ControlArbiter a;
  assert(a.takeover(100, true, true));
  const auto epoch = a.epoch;
  assert(a.command(epoch, 1, 300, 100, 100, 85, 110));
  assert(!a.command(epoch, 1, 300, 100, 100, 85, 120));
  assert(!a.command(epoch, 2, 1000, 100, 100, 85, 120));
  a.poll(301, true, true);
  assert(a.owner == Owner::Stopping && a.left == 0);
  assert(!a.command(epoch, 2, 500, 100, 100, 85, 310));
  a.poll(801, true, true);
  assert(a.owner == Owner::Local);
  assert(!a.command(epoch, 2, 1000, 100, 100, 85, 820));
  assert(a.takeover(900, true, true));
  a.poll(1200, false, true);
  a.poll(1700, false, true);
  assert(a.owner == Owner::Held);
  assert(!a.takeover(1800, true, true));
}
int main() {
  dynamicOccupancy();
  obstacleWaitSafety();
  cornerReplanStartsAtActualPose();
  reportedBrushFootprint();
  assistancePolicy();
  brushGeometry();
  noProgress();
  arbitration();
  safety();
  lidar();
  odometry();
  scenario(false, false, false);
  scenario(true, false, false);
  scenario(true, true, false);
  scenario(false, false, true);
  scenario(false, false, false, true);
  scenario(false, false, false, false, true);
  scenario(false, false, false, false, false, true);
  scenario(true, false, false, false, false, false, true); // original 70 mm/zero-offset failure
  puts("Native navigation tests passed");
}
