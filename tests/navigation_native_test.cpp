#include "ControlArbiter.h"
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
static void clockAt(Input &i, uint32_t now) { i.now = i.poseMs = i.scanMs = i.sensorsMs = now; }
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
  if (p.x < .18 || p.y < .18 || p.x > 4.62 || p.y > 3.42)
    return true;
  for (auto b : boxes)
    if (distance(p.x, p.y, fmaxf(b.x1, fminf(b.x2, p.x)), fmaxf(b.y1, fminf(b.y2, p.y))) < .18)
      return true;
  return false;
}
static void scenario(bool furniture, bool deep, bool battery) {
  Config cfg;
  cfg.cols = 32;
  cfg.rows = 24;
  cfg.resolution = .15;
  cfg.originX = cfg.originY = 0;
  cfg.margin = .04;
  Navigator n(cfg);
  Input i = healthy();
  i.pose.x = i.pose.y = .675;
  n.start(deep, i.pose, i.now);
  std::vector<Box> boxes;
  if (furniture)
    boxes = {{1.8f, 1.4f, 2.4f, 2.1f}, {3.5f, .6f, 4.1f, 1.2f}};
  bool charged = false, resumed = false;
  float travelled = 0;
  for (int step = 0; step < 18000; ++step) {
    clockAt(i, 100 + step * 200);
    if (battery && step == 200)
      i.battery = 18;
    for (int j = 0; j < 180; ++j)
      i.ranges[j] = ray(i.pose, i.pose.yaw + j * Pi / 90, boxes);
    auto out = n.tick(i);
    if (n.state == State::Fault) {
      std::printf("scenario fault %d at %d skip %u\n", int(n.reason), step, n.skipped);
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
    if (out.action == Action::Drive) {
      for (int sub = 0; sub < 10; ++sub) {
        i.pose.x += out.speed * cosf(i.pose.yaw + out.turn * .01f) * .02f;
        i.pose.y += out.speed * sinf(i.pose.yaw + out.turn * .01f) * .02f;
        i.pose.yaw = wrap(i.pose.yaw + out.turn * .02f);
        assert(!collision(i.pose, boxes));
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
  assert(n.skipped < cfg.maxSkipped);
  assert(n.completedCells > 200);
  if (deep)
    assert(n.pass == 1);
  if (battery)
    assert(charged && resumed);
}
static void noProgress() {
  Config cfg;
  cfg.cols = 32;
  cfg.rows = 24;
  cfg.resolution = .15;
  cfg.originX = cfg.originY = 0;
  cfg.margin = .04;
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
  noProgress();
  arbitration();
  safety();
  lidar();
  odometry();
  scenario(false, false, false);
  scenario(true, false, false);
  scenario(true, true, false);
  scenario(false, false, true);
  puts("Native navigation tests passed");
}
