#include "ChargeSupervisor.h"
#include "RooNavigation.h"
#include <cassert>
#include <cstdio>
using namespace roonav;

static Input input(uint32_t now) {
  Input i;
  i.now = i.sensorsMs = i.poseMs = i.scanMs = i.chargerMs = now;
  i.sensorsValid = i.localized = i.scanValid = i.chargerValid = true;
  i.battery = 50;
  i.chargerSources = 2; i.chargerState = 2;
  i.docked = i.charging = i.chargeContactConfirmed = true;
  for (float &r : i.ranges) r = 2;
  return i;
}
static ChargeSupervisor configured() {
  ChargeSupervisor c;
  c.cfg.departureVerified = true;
  c.cfg.minimumC = 10; c.cfg.maximumC = 40; // synthetic fixtures, never pack calibration
  return c;
}
static ChargeSupervisor::Sample sample(uint32_t now, uint8_t state = 3) {
  ChargeSupervisor::Sample s;
  s.now = s.stamp = now; s.generation = now;
  s.valid = s.percentageValid = s.stopped = true;
  s.sources = 2; s.state = state; s.mode = 1;
  s.percentage = 100; s.temperatureC = 25;
  return s;
}
static void advance(ChargeSupervisor &c, uint32_t start, uint32_t duration, uint8_t state = 3) {
  for (uint32_t delta = 0; delta <= duration; delta += 100)
    c.update(sample(start + delta, state), true);
}
static void chargeStates() {
  ChargeSupervisor unqualified;
  advance(unqualified, 100, 5000);
  assert(unqualified.contactConfirmed && !unqualified.preparation && !unqualified.ready);
  assert(unqualified.why == ChargeSupervisor::Why::Unqualified);
  for (uint8_t state : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(4)}) {
    auto c = configured(); advance(c, 100, 5000, state);
    assert(c.contactConfirmed && !c.ready && !c.preparation);
  }
  auto c = configured(); advance(c, 100, 5000);
  assert(c.ready && c.preparation);
  auto s = sample(5200); s.percentageValid = false;
  c.update(s, true);
  assert(!c.ready && c.preparation); // warm-up latches without permitting a stale/invalid exit
  s = sample(5300, 1); c.update(s, true);
  assert(!c.ready); // native reconditioning may never be interrupted by mission SOC
  s = sample(5400, 5); c.update(s, true);
  assert(c.held() && !c.ready);
  advance(c, 5500, 5000);
  assert(c.held() && !c.ready); // operator reset/revalidation required after fault
  c = configured(); c.cfg.policy = ChargeSupervisor::Policy::MissionEnergyReady;
  for (uint32_t t = 100; t <= 5100; t += 100) {
    s = sample(t, 2); s.percentage = 80; c.update(s, true);
  }
  assert(c.ready && c.preparation);
  s = sample(5200, 2); s.percentage = 79; c.update(s, true);
  assert(!c.ready && c.preparation); // no percentage-driven LiDAR chatter
  s = sample(40000, 2); s.percentage = 79; c.update(s, true);
  assert(c.held() && c.why == ChargeSupervisor::Why::PreparationTimeout);
  c = configured(); c.cfg.waitingMs = 1000; advance(c, 100, 1500, 4);
  assert(c.held() && c.why == ChargeSupervisor::Why::WaitingTimeout);
  c = configured(); advance(c, 0xfffff000u, 5000);
  assert(c.ready); // wrap-safe dwell timestamps
  s = sample(2000); s.stamp = 1000; c.update(s, true);
  assert(!c.ready && !c.contactConfirmed);
  c = configured(); s = sample(100); c.update(s, true);
  s.now = 200; c.update(s, true); // cached generation does not advance the dwell
  assert(!c.contactConfirmed && !c.ready);
  c = configured(); s = sample(100); s.sources = 1; c.update(s, true);
  assert(!c.contactConfirmed && !c.ready); // an internal charger is not a Home Base
}
static void missionClockAndValidity() {
  Navigator n; n.start(false, {}, 100);
  n.waitOnDock(200);
  const uint32_t active = n.activeMs;
  for (uint8_t state : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4)}) {
    auto i = input(300 + uint32_t(state) * 10000000);
    i.chargerState = state; i.batteryValid = false; i.scanValid = i.localized = false;
    assert(n.tick(i).action == Action::Charge);
    assert(n.state == State::Charging && n.activeMs == active);
  }
  auto i = input(50000000); i.chargeReady = true; i.chargerState = 4;
  n.tick(i); assert(n.state == State::Charging);
  i.chargerState = 3; i.scanValid = false;
  n.tick(i); assert(n.state == State::Charging);
  i.scanValid = true; n.tick(i);
  assert(n.state == State::Resuming && n.activeMs == active);
  n.pause(i.now + 100);
  const uint32_t prior = n.activeMs;
  i = input(i.now + 20000000);
  assert(n.resume(i));
  assert(n.activeMs == prior); // pause time does not restore/reset or consume active budget
  n.waitOnDock(i.now);
  i.chargerMs = i.now - 201; n.tick(i);
  assert(n.state == State::Fault && n.reason == Reason::Telemetry);
  n.start(false, {}, 100); n.waitOnDock(100);
  i = input(200); i.chargerState = 5; n.tick(i);
  assert(n.state == State::Fault);
  n.start(false, {}, 100); n.waitOnDock(100);
  i = input(200); i.chargerSources = 0; n.tick(i);
  assert(n.state == State::Fault && n.reason == Reason::ChargeLost);
}
static bool forbiddenHalf(void *, float x, float) { return x < 0; }
static void mapAndEvents() {
  Navigator n; n.start(false, {}, 100);
  int c = n.cell(.1f, .1f);
  n.cells[c] = -1; n.covered[c] = 255; n.sideCovered[c] = 15; n.excluded[c] = 2;
  n.start(true, {}, 200, true);
  assert(n.cells[c] == -1 && n.covered[c] == 0 && n.sideCovered[c] == 0 && n.excluded[c] == 0);
  assert(n.passes == 2);
  auto i = input(300);
  n.goHome(i, false, ReturnCause::Battery);
  assert(n.returnCause == ReturnCause::Battery && n.returnAt == 300);
  auto seq = n.returnSequence;
  n.goHome(input(400));
  assert(n.returnSequence == seq && n.returnAt == 300); // retry cannot rewrite provenance
  n.start(false, {}, 500, true); n.goHome(input(600));
  assert(n.returnCause == ReturnCause::Manual);
  n.start(false, {}, 700, true); n.goHome(input(800), true);
  assert(n.returnCause == ReturnCause::Completed);
  n.start(false, {}, 900, true);
  n.cfg.originX -= .1f;
  n.start(false, {}, 1000, true);
  assert(n.state == State::Fault && n.reason == Reason::Restore);
  assert(n.cells[c] == -1); // reject stale config without erasing the evidence
  n.clearMap(1100); assert(n.state == State::Idle && n.cells[c] == 0);
  n.ordinaryCenterAllowed = forbiddenHalf;
  for (State state : {State::Cleaning, State::Edging, State::Recovering, State::Resuming}) {
    n.state = state;
    Pose p; p.x = .1f;
    assert(!n.allowsMotion(p, 0, 1)); // all command-source paths respect ordinary exclusion
  }
  Output out; out.speed = .25f; out.turn = 2;
  int left = 0, right = 0;
  assert(quantizeMotion(out, .235f, left, right));
  assert(left == 15 && right == 250);
  assert(fabsf(out.speed - .1325f) < .0001f && fabsf(out.turn - 1.f) < .0001f);
  out.turn = NAN;
  assert(!quantizeMotion(out, .235f, left, right));
  Navigator paused;
  paused.start(false, {}, 100);
  paused.state = State::Resuming;
  paused.pause(200);
  auto docked = input(300);
  assert(paused.resume(docked) && paused.state == State::Charging);
}
int main() {
  chargeStates(); missionClockAndValidity(); mapAndEvents();
  std::puts("Charge supervisor, mission clock, saved-map reuse and return-event tests passed (synthetic).");
}
