#include "DirtMonitor.h"
#include "RoombaOI.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
using roonav::DirtMonitor;
struct Wire : roocore::Transport {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  size_t write(const uint8_t *p, size_t n) override {
    tx.insert(tx.end(), p, p + n);
    return n;
  }
  int read() override {
    if (rx.empty())
      return -1;
    int b = rx.front();
    rx.pop_front();
    return b;
  }
  void dirt(uint8_t value, bool corrupt = false) {
    std::vector<uint8_t> bytes = {19, 13, 1, 0, 0, 0, 0, 0, 0, 0, 0, value, 0, 35, 2};
    uint8_t sum = 0;
    for (auto b : bytes)
      sum += b;
    bytes.push_back(uint8_t(0 - sum + (corrupt ? 1 : 0)));
    rx.insert(rx.end(), bytes.begin(), bytes.end());
  }
};
static DirtMonitor::Context context(uint32_t now = 100) {
  DirtMonitor::Context c;
  c.now = c.poseMs = now;
  c.frame = 1;
  c.pose.x = c.pose.y = .5f;
  c.managed = c.poseValid = c.cleaning = c.safe = c.link = true;
  c.battery = 90;
  c.brush = c.vacuum = 85;
  c.action = DirtMonitor::Action::RoutineCleaning;
  return c;
}
static unsigned kinds(const DirtMonitor &m, DirtMonitor::Kind kind) {
  DirtMonitor::Record rows[DirtMonitor::Capacity];
  unsigned count = m.copyAfter(0, rows, DirtMonitor::Capacity), total = 0;
  for (unsigned i = 0; i < count; ++i)
    total += rows[i].kind == kind;
  return total;
}
struct Rig {
  Wire wire;
  roocore::Roomba robot;
  DirtMonitor monitor;
  DirtMonitor::Context c = context();
  unsigned received = 0;
  Rig() : robot(wire) {
    robot.beginPassive();
    roocore::Sensor ids[] = {roocore::Sensor::Safety, roocore::Sensor::OiMode};
    assert(robot.startStream(ids, 2) == roocore::Result::Ok);
    robot.setSampleObserver(
        [](void *p, roocore::Sensor id, const roocore::Sample &sample) {
          Rig &r = *static_cast<Rig *>(p);
          if (id != roocore::Sensor::DirtDetect)
            return;
          assert(r.robot.sensor(roocore::Sensor::OiMode).generation == sample.generation);
          assert(r.robot.sensor(roocore::Sensor::OiMode).value == 2);
          ++r.received;
          r.monitor.observe(uint8_t(sample.value), sample.timestampMs, sample.generation, r.c);
        },
        this);
  }
  void send(int value, uint32_t now, bool corrupt = false) {
    c.now = c.poseMs = now;
    wire.dirt(uint8_t(value), corrupt);
    robot.poll(now);
  }
};
static void transport() {
  Rig r;
  r.monitor.cfg.threshold = 100;
  r.send(255, 100);
  assert(r.received == 1 && r.robot.sensor(roocore::Sensor::DirtDetect).value == 255);
  r.send(200, 200, true);
  assert(r.received == 1); // rejected checksum never reaches observer
  r.wire.dirt(50);
  r.wire.dirt(60);
  r.c.now = r.c.poseMs = 250;
  r.robot.poll(250);
  assert(r.received == 3); // two frames in one poll are both kept, with different generations
  DirtMonitor::Record rows[4];
  assert(r.monitor.copyAfter(0, rows, 4) == 3);
  assert(rows[0].value == 255 && rows[0].stamp == 100 && rows[0].located);
  assert(rows[1].stamp == 250 && rows[2].stamp == 250 && rows[1].generation != rows[2].generation);
  assert(kinds(r.monitor, DirtMonitor::Kind::Region) == 0);
  r.robot.setSampleObserver(nullptr);
  r.send(150, 300);
  assert(r.received == 3);
  // Standalone query also notifies, once the complete reply is committed.
  Wire w;
  roocore::Roomba q(w);
  q.beginPassive();
  unsigned calls = 0;
  q.setSampleObserver(
      [](void *p, roocore::Sensor id, const roocore::Sample &s) {
        assert(id == roocore::Sensor::DirtDetect && s.value == 200 && s.timestampMs == 77);
        ++*static_cast<unsigned *>(p);
      },
      &calls);
  assert(q.request(roocore::Sensor::DirtDetect, 0) == roocore::Result::Ok);
  w.rx.push_back(200);
  q.poll(77);
  assert(calls == 1);
}
static void detectionAndRevisit() {
  Rig r;
  r.monitor.cfg.threshold = 100;
  for (unsigned t = 100; t <= 500; t += 100)
    r.send(120, t);
  assert(r.monitor.regionCount == 1 && kinds(r.monitor, DirtMonitor::Kind::Region) == 1);
  r.c.pose.x = 1.2f;
  r.send(20, 600);
  r.c.pose.x = .5f;
  for (unsigned t = 700; t <= 1200; t += 100)
    r.send(10, t);
  r.c.pose.x = 1.2f;
  r.send(20, 1300);
  assert(kinds(r.monitor, DirtMonitor::Kind::RevisitStart) == 1);
  assert(kinds(r.monitor, DirtMonitor::Kind::RevisitResult) == 1);
  DirtMonitor::Record rows[32];
  unsigned n = r.monitor.copyAfter(0, rows, 32);
  bool compared = false;
  for (unsigned i = 0; i < n; ++i)
    if (rows[i].kind == DirtMonitor::Kind::RevisitResult) {
      assert(rows[i].before == 120 && rows[i].after == 10 && rows[i].comparable &&
             rows[i].decreased);
      compared = true;
    }
  assert(compared && !DirtMonitor::automaticCleaningEnabled());
  r.c.pose.x = .5f;
  for (unsigned t = 1400; t <= 2200; t += 100)
    r.send(140, t);
  assert(r.monitor.regionCount == 1 && kinds(r.monitor, DirtMonitor::Kind::RevisitStart) == 1);
  assert(r.wire.tx == std::vector<uint8_t>({128, 148, 2, 1, 35})); // NO actuator commands
}
static void guards() {
  for (unsigned reason = 0; reason < 5; ++reason) {
    Rig r;
    r.monitor.cfg.threshold = 100;
    for (unsigned t = 100; t <= 500; t += 100)
      r.send(120, t);
    r.c.pose.x = 1.2f;
    r.send(10, 600);
    r.c.pose.x = .5f;
    r.send(10, 700);
    r.c.now = 750;
    if (reason == 0)
      r.c.safe = false;
    if (reason == 1)
      r.c.battery = 25;
    if (reason == 2)
      r.c.link = false;
    if (reason == 3)
      r.c.poseValid = false;
    if (reason == 4)
      r.c.frame = 2;
    r.monitor.tick(r.c);
    assert(kinds(r.monitor, DirtMonitor::Kind::Interrupted) == 1);
    assert(kinds(r.monitor, DirtMonitor::Kind::RevisitResult) == 0);
  }
  Rig r;
  r.monitor.cfg.threshold = 100;
  r.send(120, 100);
  r.send(120, 200);
  r.send(1, 300);
  r.send(120, 400);
  r.send(120, 500);
  r.send(120, 1000);
  r.send(120, 1100);
  assert(r.monitor.regionCount == 0);
  r.c.poseValid = false;
  for (unsigned t = 1200; t <= 2000; t += 100)
    r.send(130, t);
  assert(r.monitor.regionCount == 0);
  DirtMonitor::Record last[2];
  assert(r.monitor.copyAfter(r.monitor.latestSequence - 1, last, 2) == 1 && !last[0].located);
  r.c.poseValid = true;
  r.c.managed = false;
  for (unsigned t = 2100; t <= 3000; t += 100)
    r.send(130, t);
  assert(r.monitor.regionCount == 0);
  Rig off;
  for (unsigned t = 100; t <= 2000; t += 100)
    off.send(255, t);
  assert(off.monitor.regionCount == 0);
}
static void boundaries() {
  DirtMonitor m;
  m.cfg.threshold = 100;
  auto c = context();
  unsigned generation = 1;
  auto send = [&](unsigned value, uint32_t t) {
    c.now = c.poseMs = t;
    m.observe(uint8_t(value), t, generation++, c);
  };
  for (unsigned k = 0; k < 5; ++k)
    send(120, 0xffffff00u + k * 100);
  assert(m.regionCount == 1); // uptime rollover
  c.pose.x = 1.2f;
  send(0, 300);
  c.pose.x = .5f;
  send(20, 400);
  c.brush = 127;
  for (unsigned t = 500; t <= 1000; t += 100)
    send(20, t);
  c.pose.x = 1.2f;
  send(20, 1100);
  DirtMonitor::Record rows[64];
  unsigned n = m.copyAfter(0, rows, 64);
  for (unsigned i = 0; i < n; ++i)
    if (rows[i].kind == DirtMonitor::Kind::RevisitResult)
      assert(!rows[i].comparable && !rows[i].decreased);
  m.beginSession(2, c);
  assert(m.regionCount == 0);
  for (unsigned k = 0; k < 300; ++k)
    send(0, 1200 + k * 100);
  assert(m.oldestSequence() > 1);
  assert(m.copyAfter(0, rows, 64) == 64);
  assert(rows[0].sequence == m.oldestSequence());
  assert(m.copyAfter(m.latestSequence, rows, 64) == 0);
}
static void budgets() {
  DirtMonitor m;
  m.cfg.threshold = 100;
  m.cfg.revisitBudgetMs = 1000;
  auto c = context();
  uint32_t t = 0, g = 0;
  auto send = [&](int value) {
    t += 100;
    c.now = c.poseMs = t;
    m.observe(uint8_t(value), t, ++g, c);
  };
  for (int n = 0; n < 5; ++n)
    send(120);
  c.pose.x = 1.2f;
  send(0);
  c.pose.x = .5f;
  for (int n = 0; n < 15; ++n)
    send(10);
  assert(kinds(m, DirtMonitor::Kind::RevisitResult) == 1);
  DirtMonitor::Record rows[64];
  unsigned count = m.copyAfter(0, rows, 64);
  bool timeout = false;
  for (unsigned n = 0; n < count; ++n)
    if (rows[n].kind == DirtMonitor::Kind::RevisitResult)
      timeout = rows[n].why == DirtMonitor::Why::TimeLimit;
  assert(timeout);
  for (int zone = 1; zone < 18; ++zone) {
    c.pose.x = .5f + zone;
    for (int n = 0; n < 5; ++n)
      send(130);
  }
  assert(m.regionCount == DirtMonitor::MaxRegions);
  assert(kinds(m, DirtMonitor::Kind::Region) == DirtMonitor::MaxRegions);
  assert(!DirtMonitor::automaticCleaningEnabled());
}
static void stasis() {
  Wire w;
  roocore::Capabilities caps;
  caps.extendedSensors = true;
  roocore::Roomba robot(w, caps);
  robot.beginPassive();
  for (unsigned value = 0; value < 4; ++value) {
    unsigned t = value * 50;
    assert(robot.request(roocore::Sensor::Stasis, t) == roocore::Result::Ok);
    w.rx.push_back(uint8_t(value));
    robot.poll(t + 1);
    auto s = robot.stasis(t + 1);
    assert(s.usableForProgress == (value < 2));
    assert(s.maintenanceSuggested == (value == 2));
    assert(unsigned(s.meaning) == value);
    assert(!robot.stasis(t + 202).usableForProgress && !robot.stasis(t + 202).maintenanceSuggested);
  }
}
int main() {
  transport();
  detectionAndRevisit();
  guards();
  boundaries();
  budgets();
  stasis();
  puts("Dirt observation / simulated OI traffic and Stasis tests passed");
}
