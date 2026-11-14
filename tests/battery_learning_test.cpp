#include "BatteryLearning.h"
#include "BatteryLearningStore.h"
#include "RooNavigation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using B = roonav::BatteryLearning;
using S = roonav::BatteryLearningStore;
struct Harness {
  B b;
  B::Sample s;
  Harness(uint32_t now = 100) {
    b.cfg.packKey = 1; b.cfg.calibrationKey = 2;
    b.cfg.currentVerified = b.cfg.gaugeVerified = b.cfg.fullReferenceVerified = true;
    b.cfg.currentErrorMa = 20; b.cfg.gaugeErrorMah = 5; b.cfg.reserveMarginMah = 50;
    b.cfg.minimumC = 10; b.cfg.maximumC = 40;
    b.cfg.adaptiveReturn = true;
    b.begin(3);
    s.now = s.stamp = now; s.boot = 3; s.frame = 9;
    s.valid = s.gaugeValid = true; s.stationary = true;
    s.mv = 15000; s.capacityMah = s.chargeMah = 3200; s.temperatureC = 25; s.mode = 1;
  }
  void tick(unsigned ms = 100, bool distinct = true) {
    s.now += ms;
    if (distinct) { s.stamp = s.now; ++s.generation; }
    b.observe(s);
  }
  void run(unsigned ms) { for (unsigned n = 0; n < ms; n += 100) tick(); }
  void chargeFull() {
    s.sources = 2; s.charging = 3; s.ma = 500; s.mode = 1;
    s.chargeMah = s.capacityMah; s.dockConfirmed = true; s.stationary = true;
    run(6000);
  }
  void discharge(double mah, bool learnReturn = true) {
    s.sources = s.charging = 0; s.mode = 2; s.ma = -2000;
    s.dockConfirmed = false; s.stationary = false; s.chargeMah = 3200;
    tick();
    const unsigned duration = unsigned(mah * 3600000 / 2000);
    for (unsigned elapsed = 100; elapsed <= duration; elapsed += 100) {
      // Synthetic gauge with correct SOC but a deliberately different nominal mAh scale.
      const double used = 2000.0 * elapsed / 3600000;
      s.chargeMah = uint16_t(std::lround(3200 * (1-used/2000)));
      if (learnReturn && !returnStarted_ && used >= mah - 100) {
        assert(b.beginReturn(s.now, s.frame, 5)); returnStarted_ = true;
      }
      s.mv = uint16_t(15000 - used);
      tick();
    }
    returnStarted_ = false;
  }
private:
  bool returnStarted_ = false;
};
struct Memory : S::Port {
  std::vector<uint8_t> files[2];
  bool failWrite = false, failRemove = false;
  bool read(unsigned slot, void *p, size_t n) override {
    if (files[slot].size() != n) return false;
    memcpy(p,files[slot].data(),n); return true;
  }
  bool write(unsigned slot, const void *p, size_t n) override {
    const auto *q = static_cast<const uint8_t *>(p);
    files[slot].assign(q,q+(failWrite?n/2:n)); return !failWrite;
  }
  bool exists(unsigned slot) override { return !files[slot].empty(); }
  bool remove(unsigned slot) override { if (failRemove) return false; files[slot].clear(); return true; }
};
static void integrationAndGaps() {
  Harness h; h.s.ma = -1000; h.tick(); h.run(3600000);
  assert(std::fabs(h.b.data.drawMah-1000) < .001);
  assert(std::fabs(h.b.data.drawMwh-15000) < .001);
  const auto used = h.b.data.drawMah;
  h.tick(100,false); assert(h.b.data.drawMah == used);
  h.tick(1000); assert(h.b.data.drawMah == used && h.b.data.gaps == 1);
  assert(h.b.data.missingMs == 1100 && (h.b.data.open.quality & B::Gap));
  h.chargeFull(); assert(h.b.data.latestCycle == 1 && !h.b.cycle(1)->capacityAccepted);
  Harness cross; cross.s.ma = 1000; cross.tick(); cross.s.ma = -1000; cross.tick();
  assert(std::fabs(cross.b.data.drawMah - 25000.0/3600000) < 1e-8);
  assert(std::fabs(cross.b.data.chargeMah - 25000.0/3600000) < 1e-8);
  Harness wrap(0xfffff000); wrap.s.ma = -1000; wrap.tick(); wrap.run(10000);
  assert(std::fabs(wrap.b.data.drawMah - 1000.0*10000/3600000) < 1e-8);
}
static void learnedCapacityAndReserve() {
  Harness h; h.chargeFull();
  for (unsigned cycle = 0; cycle < 3; ++cycle) {
    h.discharge(1000); h.chargeFull();
  }
  const auto m = h.b.model();
  assert(m.advisoryCycles == 3 && m.qualifiedCycles == 3 && m.returnCycles == 3);
  assert(std::fabs(m.capacityMah - 2000) < 2 && m.capacityLowerMah < m.capacityMah);
  auto e = h.b.estimate(h.s.now);
  assert(e.anchored && e.learnedCapacity && e.remainingLowerMah > 1900);
  h.s.sources = h.s.charging = 0; h.s.ma = -2000; h.s.stationary = false; h.s.mode = 2;
  h.tick(); auto r = h.b.reserve(h.s.now,9,5);
  assert(r.available && !r.earlyReturn && r.requiredMah > 140 && r.requiredMah < 170);
  assert(!h.b.reserve(h.s.now,10,5).available); // another calibration frame
  assert(!h.b.reserve(h.s.now,9,6).available); // outside observed route lengths
  for (unsigned n = 100; n <= 3350000; n += 100) {
    const double used = 2000.0*n/3600000;
    h.s.chargeMah = uint16_t(std::lround(3200*(1-used/2000))); h.tick();
  }
  r = h.b.reserve(h.s.now,9,5); assert(r.available && r.earlyReturn);
  h.b.cfg.adaptiveReturn = false;
  assert(!h.b.reserve(h.s.now,9,5).earlyReturn);
  h.tick(500); assert(!h.b.estimate(h.s.now).anchored && !h.b.reserve(h.s.now,9,5).available);
  h.chargeFull(); assert(h.b.estimate(h.s.now).anchored);
  h.b.breakContinuity(); h.tick(); assert(!h.b.estimate(h.s.now).anchored);
  h.run(6000); assert(h.b.estimate(h.s.now).anchored); // new full reference after checkpoint gap
  h.s.temperatureC = 50; h.tick();
  assert(!h.b.estimate(h.s.now).anchored && !h.b.reserve(h.s.now,9,5).available);
  h.s.temperatureC = 25; h.chargeFull(); assert(h.b.estimate(h.s.now).anchored);
  h.s.sources = h.s.charging = 0; h.s.ma = -100; h.tick();
  h.s.capacityMah = 3000; h.s.chargeMah = 2000; h.tick();
  assert(!h.b.estimate(h.s.now).anchored);
}
static void rejectedLearning() {
  Harness partial; partial.chargeFull(); partial.discharge(100,false); partial.chargeFull();
  assert(!partial.b.cycle(1)->capacityAccepted);
  Harness unverified; unverified.b.cfg.currentVerified = false;
  unverified.discharge(1000); unverified.chargeFull();
  assert(unverified.b.cycle(1)->capacityAccepted && !unverified.b.cycle(1)->capacityQualified);
  assert(!unverified.b.cycle(1)->returnAccepted && !unverified.b.reserve(unverified.s.now,9,5).available);
  Harness resetGauge; resetGauge.s.ma = -2000; resetGauge.tick(); resetGauge.run(1000);
  resetGauge.s.capacityMah = 3000; resetGauge.s.chargeMah = 1000; resetGauge.run(2000);
  resetGauge.chargeFull(); assert(!resetGauge.b.cycle(1)->capacityAccepted);
  Harness invalid; invalid.s.ma = -1000; invalid.tick(); invalid.s.valid = false; invalid.tick();
  invalid.s.valid = true; invalid.tick(); assert(invalid.b.data.gaps && (invalid.b.data.open.quality & B::Telemetry));
  Harness critical; critical.discharge(1000); critical.s.critical = true; critical.tick(); critical.chargeFull();
  assert(!critical.b.cycle(1)->returnAccepted && !critical.b.cycle(1)->capacityQualified);
  Harness frame; frame.discharge(1000); frame.s.frame = 0; frame.tick(); frame.chargeFull();
  assert(!frame.b.cycle(1)->returnAccepted && (frame.b.cycle(1)->quality & B::Frame));
  Harness temp; temp.discharge(1000); temp.s.temperatureC = 50; temp.tick(); temp.chargeFull();
  assert(!temp.b.cycle(1)->capacityQualified);
}
static void persistenceAndRings() {
  Harness learned;
  for (unsigned i = 0; i < 3; ++i) { learned.discharge(1000); learned.chargeFull(); }
  Memory durable; S completedStore; S::Snapshot completedScratch;
  assert(completedStore.save(learned.b,durable,completedScratch));
  Harness loaded; loaded.s.boot = 4; S reloader;
  assert(reloader.load(loaded.b,durable,completedScratch,4));
  assert(loaded.b.model().qualifiedCycles == 3 && loaded.b.model().returnCycles == 3);
  assert(std::fabs(loaded.b.model().capacityMah-2000) < 1);
  assert(!loaded.b.estimate(loaded.s.now).anchored && !loaded.b.reserve(loaded.s.now,9,5).available);
  loaded.chargeFull(); assert(loaded.b.estimate(loaded.s.now).learnedCapacity);
  Harness h; Memory memory; S store; S::Snapshot scratch;
  h.s.ma = -1000; h.tick(); h.run(1000);
  assert(store.save(h.b,memory,scratch)); assert(!h.b.dirty());
  h.run(1000); memory.failWrite = true;
  assert(!store.save(h.b,memory,scratch) && h.b.dirty());
  memory.failWrite = false;
  Harness restored; S load;
  assert(load.load(restored.b,memory,scratch,4));
  assert(restored.b.data.latestCycle == 1 && (restored.b.cycle(1)->quality & B::Interrupted));
  assert(!restored.b.estimate(restored.s.now).anchored && restored.b.dirty());
  Harness wrong; wrong.b.cfg.packKey = 99;
  assert(!load.load(wrong.b,memory,scratch,5) && wrong.b.profileMismatch);
  assert(!load.save(wrong.b,memory,scratch));
  memory.files[1][30] ^= 1;
  Harness corrupt; assert(!load.load(corrupt.b,memory,scratch,6) && corrupt.b.profileMismatch);
  assert(load.reset(corrupt.b,memory,6));
  assert(load.save(corrupt.b,memory,scratch));
  scratch.state.traces[0].usedMah = NAN; scratch.checksum = S::crc(scratch);
  assert(!S::valid(scratch));
  Harness ring;
  for (unsigned i = 0; i < B::CycleCount+3; ++i) {
    ring.discharge(1,false); ring.chargeFull();
  }
  assert(ring.b.data.cycleSize == B::CycleCount && !ring.b.cycle(1));
  assert(ring.b.oldestCycle() == 4 && ring.b.cycle(B::CycleCount+3));
  assert(ring.b.data.traceSize <= B::TraceCount);
}
static void routeBudgeting() {
  roonav::Config cfg; cfg.cols = cfg.rows = 8; cfg.originX = cfg.originY = 0;
  roonav::Navigator n(cfg);
  for (int k = 0; k < n.count(); ++k) n.safe[k] = true;
  const auto start = n.center(9); n.dock = n.center(14);
  const float direct = n.routeToDockMeters(start);
  assert(std::fabs(direct-.5f) < .0001f);
  for (int y = 0; y < 7; ++y) n.safe[y*8+4] = false;
  assert(n.routeToDockMeters(start) > direct + 1);
  n.safe[7*8+4] = false;
  assert(!std::isfinite(n.routeToDockMeters(start)));
  n.cfg.homeKnown = false; assert(!std::isfinite(n.routeToDockMeters(n.dock)));
  n.cfg.homeKnown = true; assert(n.routeToDockMeters(n.dock) == 0);
}
int main() {
  routeBudgeting(); integrationAndGaps(); learnedCapacityAndReserve(); rejectedLearning(); persistenceAndRings();
  std::printf("PASS battery learning: integration, gaps, gauge-referenced capacity, bounded reserve, reboot and torn snapshots (%zu-byte state)\n",sizeof(B::State));
}
