#include "ControlArbiter.h"
#include "PowerShutdown.h"
#include "PowerShutdownOI.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>
using P = roonav::PowerShutdown;

struct Harness : P::Port {
  P policy;
  P::Input input;
  roonav::ControlArbiter arbiter;
  unsigned stops = 0, saves = 0, cuts = 0, clears = 0;
  bool stopOk = true, intentOk = true, checkpointOk = true, clearOk = true, marker = false;
  uint32_t flashDelay = 0;
  std::vector<std::string> events;
  Harness(uint32_t start = 100) {
    policy.cfg.verified = true;
    policy.cfg.cutoffMv = 13200;
    policy.cfg.recoverMv = 13800;
    policy.cfg.restartMinMv = 14400;
    policy.cfg.reboundMarginMv = 400;
    policy.cfg.saveMinMv = 13000;
    input.valid = true;
    input.millivolts = 15000;
    input.mode = 1;
    input.now = input.stamp = start;
    policy.begin(start, false);
  }
  uint32_t nowMs() const override { return input.now; }
  bool quiesce() override {
    ++stops;
    arbiter.hold();
    events.push_back("stop");
    return stopOk;
  }
  P::PersistResult persist() override {
    ++saves;
    events.push_back("checkpoint");
    events.push_back("intent");
    marker = intentOk;
    input.now += flashDelay;
    P::PersistResult r;
    r.intent = intentOk;
    r.checkpoint = checkpointOk;
    return r;
  }
  bool clearIntent() override {
    ++clears;
    if (clearOk)
      marker = false;
    return clearOk;
  }
  void assertD10() override {
    assert(marker && policy.blocksMotion());
    ++cuts;
    events.push_back("D10_HIGH");
  }
  void tick(uint32_t step = 100, bool distinct = true) {
    input.now += step;
    if (distinct) {
      input.stamp = input.now;
      ++input.generation;
    }
    policy.tick(input, *this);
  }
  void run(unsigned ms) {
    for (unsigned t = 0; t < ms; t += 100)
      tick();
  }
  void ready() {
    run(7000);
    assert(policy.state == P::State::Running);
  }
  void low() {
    input.millivolts = 13150;
    run(2300);
  }
  void until(P::State s, unsigned limit = 150) {
    for (unsigned i = 0; i < limit && policy.state != s; ++i)
      tick();
    assert(policy.state == s);
  }
};

static void normalAndPersistentGuard() {
  Harness h;
  h.ready();
  h.arbiter.owner = roonav::Owner::Local; // explicit new cleaning command after boot guard
  assert(h.arbiter.takeover(h.input.now, true, true));
  auto oldEpoch = h.arbiter.epoch;
  assert(h.arbiter.command(oldEpoch, 1, h.input.now + 200, 100, 100, 85, h.input.now));
  h.low();
  h.until(P::State::Asserted);
  assert(h.cuts == 1 && h.saves == 1 && h.policy.checkpointSaved);
  assert(h.arbiter.owner == roonav::Owner::Held);
  assert(!h.arbiter.command(oldEpoch, 2, h.input.now + 200, 100, 100, 85, h.input.now));
  assert(h.events ==
         std::vector<std::string>({"stop", "stop", "checkpoint", "intent", "D10_HIGH"}));
  h.input.valid = false;
  h.run(10000);
  assert(h.cuts == 1 && h.policy.reason == P::Reason::CutoffNotObserved);
  assert(!h.policy.canInitializeStorage());
  // A power cycle with the durable intent never repeats the cut at low voltage.
  Harness reboot;
  reboot.marker = true;
  reboot.input.millivolts = 13300;
  reboot.policy.begin(reboot.input.now, true);
  reboot.run(60000);
  assert(reboot.cuts == 0 && reboot.saves == 0 && reboot.policy.intentPending);
  assert(!reboot.policy.canInitializeStorage());
  reboot.input.millivolts = 14300;
  reboot.run(10000);
  assert(reboot.policy.blocksMotion()); // below measured restart minimum
  reboot.input.millivolts = 14500;
  reboot.run(7000);
  assert(reboot.policy.state == P::State::Running && reboot.clears == 1 && !reboot.marker);
  assert(reboot.arbiter.owner == roonav::Owner::Held); // no auto navigation restart
}
static void calibrationAndDebounce() {
  Harness h;
  h.policy.cfg.verified = false;
  h.policy.begin(100, false);
  h.run(10000);
  assert(h.stops == 0 && h.cuts == 0 && h.policy.blocksMotion());
  h.policy.cfg.verified = true;
  h.policy.cfg.restartMinMv = 13300;
  assert(!h.policy.cfg.valid());
  Harness n;
  n.ready();
  for (unsigned i = 0; i < 100; ++i) {
    n.input.millivolts = i % 2 ? 15000 : 13100;
    n.tick();
  }
  assert(n.policy.state == P::State::Running && n.cuts == 0);
  n.input.millivolts = 13100;
  n.tick();
  for (unsigned i = 0; i < 40; ++i)
    n.tick(50, false);
  assert(n.cuts == 0 && n.policy.blocksMotion()); // old cached sample never proves sustained low
  Harness zero;
  zero.ready();
  zero.input.millivolts = 0;
  zero.run(5000);
  assert(zero.saves == 0 && zero.cuts == 0);
}
static void interruptionAndRebound() {
  for (unsigned which = 0; which < 5; ++which) {
    Harness h;
    h.ready();
    h.low();
    h.until(P::State::Verify);
    switch (which) {
    case 0:
      h.input.sources = 2;
      break;
    case 1:
      h.input.charging = 5;
      break; // charger fault is not permission to cut
    case 2:
      h.input.millivolts = 14000;
      break; // equality at margin boundary also blocked
    case 3:
      h.input.valid = false;
      break;
    case 4:
      h.input.mode = 2;
      break; // stop command is not a fresh Passive confirmation
    }
    h.run(40000);
    assert(h.cuts == 0 && h.saves == 1 && h.policy.blocksMotion());
  }
  Harness moving;
  moving.ready();
  moving.low();
  for (unsigned i = 0; i < 400; ++i) {
    ++moving.input.left;
    moving.tick();
  }
  assert(moving.policy.state == P::State::Held && moving.policy.reason == P::Reason::Timeout);
  assert(moving.cuts == 0 && moving.saves == 0);
  Harness charged;
  charged.input.sources = 2;
  charged.input.millivolts = 13100;
  charged.run(10000);
  assert(charged.saves == 0 && charged.cuts == 0);
  charged.input.sources = 0;
  charged.tick();
  assert(charged.policy.state ==
         P::State::BootGuard); // no accumulation of low samples while charging
}
static void storageAndStopFailure() {
  Harness stop;
  stop.stopOk = false;
  stop.run(10000);
  assert(stop.policy.reason == P::Reason::StopFailed && stop.stops == 1 && stop.cuts == 0);
  Harness persist;
  persist.ready();
  persist.intentOk = false;
  persist.low();
  persist.run(15000);
  assert(persist.policy.reason == P::Reason::Persistence && persist.saves == 1 &&
         persist.cuts == 0);
  Harness checkpoint;
  checkpoint.ready();
  checkpoint.checkpointOk = false;
  checkpoint.low();
  checkpoint.until(P::State::Asserted);
  assert(!checkpoint.policy.checkpointSaved && checkpoint.cuts == 1);
  Harness depleted;
  depleted.ready();
  depleted.input.millivolts = 12900;
  depleted.run(15000);
  assert(depleted.policy.reason == P::Reason::SaveVoltage && depleted.saves == 0 &&
         depleted.cuts == 0);
  Harness clear;
  clear.marker = true;
  clear.clearOk = false;
  clear.policy.begin(100, true);
  clear.run(10000);
  assert(clear.policy.reason == P::Reason::Persistence && clear.cuts == 0 && clear.marker);
}
static void timing() {
  Harness flash;
  flash.ready();
  flash.flashDelay = 1500;
  flash.low();
  flash.until(P::State::Verify);
  assert(flash.cuts == 0);
  // Simulate cached pre-flash observations, even with a different generation.
  auto stale = flash.input;
  for (unsigned i = 0; i < 10; ++i) {
    flash.input.now += 100;
    ++flash.input.generation;
    flash.policy.tick(flash.input, flash);
  }
  assert(flash.cuts == 0 && stale.stamp == flash.input.stamp);
  flash.until(P::State::Asserted);
  assert(flash.cuts == 1);
  Harness wrap(0xffffe000u);
  wrap.ready();
  wrap.low();
  wrap.until(P::State::Asserted);
  assert(wrap.cuts == 1);
  Harness noise;
  noise.ready();
  noise.low();
  for (unsigned i = 0; i < 350; ++i) {
    noise.input.millivolts = i % 2 ? 13350 : 13100;
    noise.tick();
  }
  assert(noise.cuts == 0 && noise.policy.state == P::State::Held);
}

struct Wire : roocore::Transport {
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  bool fail = false, drain = true;
  size_t write(const uint8_t *p, size_t n) override {
    if (fail)
      return 0;
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
  bool finishTransmit(uint32_t timeout) override {
    assert(timeout == 50);
    return drain;
  }
};
static void transportSequence() {
  Wire w;
  roocore::Roomba robot(w);
  robot.beginPassive();
  robot.seekDock();
  w.tx.clear();
  assert(roonav::stopRoombaForPowerCutoff(robot, w));
  assert(w.tx == std::vector<uint8_t>({131, 145, 0, 0, 0, 0, 138, 0, 128}));
  // Failed stop/drain never reports that it is safe to proceed.
  w.fail = true;
  assert(!roonav::stopRoombaForPowerCutoff(robot, w));
  Wire d;
  roocore::Roomba r(d);
  r.beginPassive();
  d.drain = false;
  assert(!roonav::stopRoombaForPowerCutoff(r, d));
  // Post-flash resync invalidates cached voltage and discards buffered old frames.
  Wire stream;
  roocore::Roomba rr(stream);
  rr.beginPassive();
  roocore::Sensor ids[] = {roocore::Sensor::VoltageMv};
  assert(rr.startStream(ids, 1) == roocore::Result::Ok);
  const std::vector<uint8_t> frame = {19,   3,    22,
                                      0x33, 0x44, uint8_t(0 - 19 - 3 - 22 - 0x33 - 0x44)};
  stream.rx.insert(stream.rx.end(), frame.begin(), frame.end());
  rr.poll(100);
  assert(rr.sensor(ids[0]).valid);
  stream.rx.insert(stream.rx.end(), frame.begin(), frame.end());
  assert(rr.resynchronizeInput(200) == roocore::Result::Ok);
  rr.poll(201);
  assert(!rr.sensor(ids[0]).valid && !rr.inputReady());
  rr.poll(302);
  assert(rr.inputReady());
  assert(rr.startStream(ids, 1) == roocore::Result::Ok);
  stream.rx.insert(stream.rx.end(), frame.begin(), frame.end());
  rr.poll(400);
  assert(rr.sensor(ids[0]).valid && rr.sensor(ids[0]).timestampMs == 400);
}
int main() {
  normalAndPersistentGuard();
  calibrationAndDebounce();
  interruptionAndRebound();
  storageAndStopFailure();
  timing();
  transportSequence();
  puts("Power cutoff policy and OI stop/resync transport tests passed (synthetic; no hardware)");
}
