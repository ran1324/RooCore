#include "PowerManagement.h"
#include "PowerShutdownOI.h"
#include "RooNavigation.h"
#include <cassert>
#include <cstdio>
#include <vector>
using P = roonav::PowerManagement;
struct Harness : P::Port {
  P p;
  P::Input in;
  bool saveOk = true, stopOk = true, clearOk = true, safe = true, dockOk = true, returnOk = true;
  bool marker = false;
  unsigned stops = 0, saves = 0, returns = 0, docks = 0, offs = 0;
  Harness(uint32_t now = 100) {
    p.cfg.verified = p.cfg.passiveStopVerified = true;
    p.cfg.fallbackVerified = p.cfg.clearanceVerified = true;
    // Synthetic qualification data, never values for flashing a real robot.
    p.cfg.hardwareOffMaxMv = 12200; p.cfg.saveMinMv = 12400; p.cfg.recoverMv = 14000;
    in.now = in.stamp = now; in.valid = true; in.millivolts = 15000; in.mode = 1;
    p.begin(now, false);
  }
  bool stop(bool passive) override { ++stops; in.mode = passive ? 1 : 2; return stopOk; }
  bool persistIntent() override { ++saves; marker = saveOk; return saveOk; }
  bool clearIntent() override { if (clearOk) marker = false; return clearOk; }
  bool returnHome() override { ++returns; return returnOk; }
  void lidarOff() override { ++offs; }
  bool fallbackSafe(bool) override { return safe; }
  bool seekDock() override { ++docks; in.mode = 1; return dockOk; }
  void tick(unsigned dt = 100, bool distinct = true) {
    in.now += dt;
    if (distinct) { in.stamp = in.now; ++in.generation; }
    p.tick(in, *this);
  }
  void run(unsigned ms) { for (unsigned n = 0; n < ms; n += 100) tick(); }
  void ready() { run(7000); assert(p.state == P::State::Running); }
  void arm() {
    assert(p.armMission(7, in.now, *this));
    run(7000); assert(p.state == P::State::Running && marker && p.scope == 7);
  }
  bool approve() { return p.approve(7, p.approvalRevision, in.now, in.now + 1000); }
  void critical() { in.millivolts = 12500; tick(); assert(p.state == P::State::CriticalHold); }
};
static void thresholdsAndRebound() {
  Harness h; h.ready(); h.arm();
  h.in.millivolts = 13800; h.tick();
  assert(h.p.state == P::State::Warning && !h.p.canStartMission() && !h.p.blocksCleaning());
  h.in.millivolts = 13500; h.run(4900);
  assert(!h.p.returning());
  h.run(200); assert(h.p.state == P::State::Returning && h.returns == 1 && h.p.blocksCleaning());
  h.in.millivolts = 15100; h.run(6000);
  assert(h.p.state == P::State::Returning && h.returns == 1);
  h.in.millivolts = 13000; h.tick(); assert(h.p.state == P::State::UrgentReturn);
  h.in.millivolts = 14500; h.tick(); assert(h.p.state == P::State::UrgentReturn);
  h.critical(); const auto stopped = h.stops;
  assert(h.p.blocksMotion() && !h.p.stopConfirmed && stopped > 0);
  h.run(3000);
  assert(h.p.fallbackOffered() && h.docks == 0 && h.p.stopConfirmed && h.offs == 1);
  h.in.millivolts = 15100; h.run(6000);
  assert(h.p.state == P::State::CriticalHold && h.docks == 0);
  Harness urgent; urgent.ready(); urgent.arm(); urgent.in.millivolts = 13000; urgent.tick();
  assert(urgent.p.state == P::State::UrgentReturn && urgent.returns == 1);
  Harness dips; dips.ready(); dips.arm();
  for (unsigned n = 0; n < 100; ++n) { dips.in.millivolts = n % 2 ? 14500 : 13500; dips.tick(); }
  assert(!dips.p.returning());
}
static void approvalsAndSafety() {
  Harness h; h.ready(); h.arm();
  assert(!h.p.approve(8, h.p.approvalRevision, h.in.now, h.in.now + 100));
  assert(!h.p.approve(7, h.p.approvalRevision, h.in.now, h.in.now));
  assert(!h.p.approve(7, h.p.approvalRevision, h.in.now, h.in.now + 5001));
  auto revision = h.p.approvalRevision;
  assert(h.approve());
  assert(!h.p.approve(7, revision, h.in.now, h.in.now + 100)); // replay
  h.critical(); h.run(3000);
  assert(h.p.state == P::State::NativeFallback && h.docks == 1 && !h.p.approved);
  h.run(5000); assert(h.docks == 1);
  h.safe = false; h.tick();
  assert(h.p.state == P::State::Held && h.p.reason == P::Reason::Safety);
  assert(!h.approve()); h.run(5000); assert(h.docks == 1);
  Harness noSafety; noSafety.ready(); noSafety.arm(); assert(noSafety.approve());
  noSafety.safe = false; noSafety.critical(); noSafety.run(3000);
  assert(noSafety.docks == 0 && !noSafety.p.approved && noSafety.p.reason == P::Reason::Safety);
  noSafety.safe = true; noSafety.run(1000); assert(noSafety.docks == 0);
  assert(noSafety.approve()); noSafety.tick(); assert(noSafety.docks == 1);
  Harness atEvent; atEvent.ready(); atEvent.arm(); atEvent.critical(); atEvent.run(3000);
  assert(atEvent.docks == 0); assert(atEvent.approve()); atEvent.tick(); assert(atEvent.docks == 1);
  atEvent.p.revoke(atEvent); assert(atEvent.p.state == P::State::Held && atEvent.stops > 1);
  Harness unqualified; unqualified.ready(); unqualified.arm();
  unqualified.p.cfg.clearanceVerified = false;
  assert(!unqualified.approve()); unqualified.critical(); unqualified.run(5000);
  assert(unqualified.docks == 0);
}
static void lossFailureAndPersistence() {
  Harness disabled; disabled.p.cfg.verified = false; disabled.p.begin(100, false);
  disabled.run(10000); assert(disabled.stops == 0 && disabled.saves == 0 && disabled.docks == 0);
  assert(disabled.p.blocksMotion() && !disabled.approve());
  Harness stale; stale.ready(); stale.arm(); assert(stale.approve()); stale.critical();
  stale.tick(100, false); stale.tick(200, false); stale.run(5000);
  assert(stale.docks == 0 && !stale.p.approved);
  Harness moving; moving.ready(); moving.arm(); assert(moving.approve()); moving.critical();
  for (unsigned n = 0; n < 100; ++n) { ++moving.in.left; moving.tick(); }
  assert(!moving.p.stopConfirmed && moving.docks == 0);
  Harness lost; lost.ready(); lost.arm(); lost.in.valid = false; lost.tick();
  assert(lost.p.state == P::State::Held && !lost.approve());
  Harness save; save.ready(); save.saveOk = false;
  assert(!save.p.armMission(7, save.in.now, save));
  assert(save.p.blocksMotion() && save.p.intentPending && !save.p.canInitializeStorage());
  Harness stop; stop.ready(); stop.arm(); stop.stopOk = false;
  stop.in.millivolts = 12500; stop.tick();
  assert(stop.p.state == P::State::Held && stop.p.reason == P::Reason::StopFailed);
  Harness fail; fail.ready(); fail.arm(); fail.dockOk = false;
  assert(fail.approve()); fail.critical(); fail.run(5000);
  assert(fail.docks == 1 && fail.p.state == P::State::Held && !fail.approve());
  Harness resync; resync.ready();
  assert(resync.p.armMission(7, resync.in.now, resync));
  resync.in.valid = false; resync.run(2000);
  assert(resync.p.state == P::State::BootGuard && resync.p.blocksMotion());
  resync.in.valid = true; resync.run(7000); assert(!resync.p.blocksMotion());
  Harness paused; paused.ready(); assert(paused.p.armMission(7, paused.in.now, paused));
  paused.in.sources = 2; paused.in.charging = 2;
  paused.p.operatorPause(paused); paused.run(10000);
  assert(paused.p.state == P::State::Held && paused.p.blocksMotion() && paused.docks == 0);
}
static void plannerCannotBypassApproval() {
  Harness h; h.ready(); h.arm();
  assert(h.p.offerHandoff(h.in.now, h));
  h.run(5000); assert(h.p.state == P::State::HandoffHold && h.docks == 0);
  assert(h.approve()); h.tick(); assert(h.docks == 1);
  h.run(61000); assert(h.p.state == P::State::Held && h.docks == 1);
  auto *n = new roonav::Navigator;
  roonav::Input in;
  in.now = in.poseMs = in.scanMs = in.sensorsMs = 100;
  in.sensorsValid = in.scanValid = in.localized = true;
  in.battery = 1;
  for (float &r : in.ranges) r = 2;
  n->start(false, in.pose, in.now); n->tick(in);
  assert(n->reason == roonav::Reason::Battery);
  n->externalVoltagePolicy = true;
  n->start(false, in.pose, in.now); n->tick(in);
  assert(n->reason != roonav::Reason::Battery);
  in.cliff = true; n->tick(in);
  assert(n->reason == roonav::Reason::Cliff); // other local protections remain authoritative
  delete n;
}
static void learnedReserveOnlyAdvancesReturn() {
  Harness h; h.ready(); h.arm();
  assert(h.p.requestReserveReturn(h.in.now,h));
  assert(h.p.returning() && h.p.reason == P::Reason::EnergyReserve && h.returns == 1);
  assert(h.p.cfg.criticalMv == 12500 && h.p.cfg.returnMv == 13500);
  assert(!h.p.requestReserveReturn(h.in.now,h));
  h.critical(); h.run(3000); assert(h.docks == 0);
}
static void chargeAndBoot() {
  Harness h; h.ready(); h.arm(); h.critical(); h.run(3000);
  h.in.sources = 2; h.in.charging = 2; h.in.millivolts = 15000; h.run(8000);
  assert(h.p.state == P::State::DockedHold && h.docks == 0 && h.marker);
  assert(h.p.resetAfterCharge(h.in.now, h));
  assert(!h.marker && !h.p.scope && !h.p.approved && h.p.state == P::State::Running);
  // New explicit mission can arm on a powered dock without deadlocking departure.
  assert(h.p.armMission(9, h.in.now, h)); h.run(8000);
  assert(h.p.state == P::State::Running && h.p.scope == 9);
  h.in.sources = h.in.charging = 0;
  h.in.millivolts = 13500; h.tick(); assert(!h.p.returning());
  const auto oldOffs = h.offs; h.critical(); h.run(3000);
  assert(h.offs > oldOffs); // a second mission must not retain the old LiDAR-off latch
  Harness dockWarning; dockWarning.ready(); dockWarning.in.sources = 2;
  dockWarning.in.charging = 2; dockWarning.in.millivolts = 13800; dockWarning.tick();
  assert(!dockWarning.p.canStartMission());
  assert(!dockWarning.p.armMission(8, dockWarning.in.now, dockWarning));
  Harness boot; boot.p.begin(boot.in.now, true); boot.run(15000);
  assert(boot.p.blocksMotion() && !boot.approve() && boot.p.intentPending);
  boot.in.sources = 2; boot.in.charging = 2; boot.run(8000);
  assert(boot.p.resetAfterCharge(boot.in.now, boot));
  Harness fault; fault.ready(); fault.arm(); fault.critical(); fault.run(3000);
  fault.in.sources = 2; fault.in.charging = 5; fault.run(10000);
  assert(!fault.p.resetAfterCharge(fault.in.now, fault));
}
struct Link : roocore::Transport {
  std::vector<uint8_t> bytes;
  size_t write(const uint8_t *p, size_t n) override { bytes.insert(bytes.end(), p, p+n); return n; }
  int read() override { return -1; }
  bool finishTransmit(uint32_t) override { return true; }
};
static void actualStopSequence() {
  Link link; roocore::Roomba robot(link);
  robot.beginPassive(); link.bytes.clear();
  assert(roonav::stopRoombaForPowerHold(robot, link, false));
  assert(link.bytes == std::vector<uint8_t>({131,145,0,0,0,0,138,0}));
  link.bytes.clear();
  assert(roonav::stopRoombaForPowerHold(robot, link, true));
  assert(link.bytes == std::vector<uint8_t>({131,145,0,0,0,0,138,0,128}));
}
int main() {
  thresholdsAndRebound(); approvalsAndSafety();
  // Stop-failure assertion is checked without expecting CriticalHold.
  lossFailureAndPersistence(); chargeAndBoot(); actualStopSequence(); plannerCannotBypassApproval();
  learnedReserveOnlyAdvancesReturn();
  Harness wrap(0xffffe000u); wrap.ready(); wrap.arm(); assert(wrap.approve());
  wrap.critical(); wrap.run(3000); assert(wrap.docks == 1);
  std::puts("Power management: thresholds, scoped approval, safety, persistence and OI stop passed");
}
