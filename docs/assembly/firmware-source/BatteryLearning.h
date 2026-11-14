#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace roonav {
// OI-referenced estimates, never an independent capacity measurement or EN control.
class BatteryLearning {
public:
  static constexpr unsigned CycleCount = 32, TraceCount = 256;
  enum Quality : uint32_t {
    Gap = 1, Gauge = 2, Unverified = 4, Interrupted = 8, Telemetry = 16,
    Critical = 32, Frame = 64, ChargingFault = 128
  };
  struct Config {
    uint32_t packKey = 0, calibrationKey = 0;
    uint16_t ratedMah = 3200; // user-reported label, never evidence of available capacity
    bool currentVerified = false, gaugeVerified = false, fullReferenceVerified = false;
    bool adaptiveReturn = false;
    float currentErrorMa = 0, gaugeErrorMah = 0, reserveMarginMah = 0;
    int16_t minimumC = 0, maximumC = 0;
    uint32_t maxGapMs = 200, traceMs = 30000, referenceMs = 5000;
    unsigned minimumCycles = 3, minimumSpanPermille = 250;
    bool qualified() const {
      return packKey && calibrationKey && currentVerified && gaugeVerified &&
          isfinite(currentErrorMa) && currentErrorMa > 0 &&
          isfinite(gaugeErrorMah) && gaugeErrorMah > 0 && minimumC < maximumC &&
          minimumCycles >= 3 && minimumCycles <= CycleCount && minimumSpanPermille >= 250 &&
          minimumSpanPermille <= 1000 && maxGapMs > 0 && maxGapMs <= 200;
    }
  } cfg;
  struct Sample {
    uint32_t now = 0, stamp = 0, generation = 0, boot = 0, frame = 0;
    uint16_t mv = 0, chargeMah = 0, capacityMah = 0;
    int16_t ma = 0, temperatureC = 0;
    uint8_t charging = 0, sources = 0, mode = 0;
    bool valid = false, gaugeValid = false, stationary = false, dockConfirmed = false;
    bool critical = false;
  };
  struct Cycle {
    uint32_t id = 0, boot = 0, startMs = 0, endMs = 0, quality = 0, gaps = 0;
    uint64_t coveredMs = 0, missingMs = 0;
    double drawMah = 0, chargeMah = 0, drawMwh = 0;
    uint16_t minimumMv = 65535, maximumMv = 0, startGaugeMah = 0, endGaugeMah = 0;
    uint16_t gaugeCapacityMah = 0;
    int16_t minimumC = 127, maximumC = -128;
    double capacityCandidateMah = 0, capacityLowerMah = 0;
    double returnMah = 0, returnErrorMah = 0;
    float returnRouteM = 0;
    uint32_t returnFrame = 0;
    bool capacityAccepted = false, capacityQualified = false, returnAccepted = false;
    uint8_t thresholdMask = 0;
    uint16_t thresholdGaugeMah[4]{};
    float thresholdUsedMah[4]{};
  };
  struct Trace {
    uint32_t id = 0, cycle = 0, boot = 0, ms = 0;
    uint16_t mv = 0, chargeMah = 0, capacityMah = 0;
    int16_t ma = 0, temperatureC = 0;
    uint8_t charging = 0, sources = 0, mode = 0, gaugeValid = 0;
    double usedMah = 0;
    uint32_t quality = 0;
  };
  struct State {
    uint32_t packKey = 0, calibrationKey = 0, lineage = 0, latestCycle = 0, latestTrace = 0;
    uint32_t cycleSize = 0, traceSize = 0, completedCycles = 0, invalidSamples = 0, gaps = 0;
    uint64_t revision = 0, coveredMs = 0, missingMs = 0;
    double drawMah = 0, chargeMah = 0, drawMwh = 0, chargeMwh = 0;
    Cycle cycles[CycleCount];
    Trace traces[TraceCount];
    Cycle open;
    bool active = false;
  } data;
  struct Model {
    unsigned advisoryCycles = 0, qualifiedCycles = 0, returnCycles = 0;
    double capacityMah = 0, capacityMinimumMah = 0, capacityMaximumMah = 0, capacityLowerMah = 0;
    float maximumReturnRouteM = 0;
  };
  struct Estimate {
    bool gaugeValid = false, anchored = false, learnedCapacity = false;
    double remainingMah = 0, remainingLowerMah = 0;
  };
  struct Reserve {
    bool available = false, earlyReturn = false;
    double requiredMah = 0, remainingLowerMah = 0;
    unsigned trips = 0;
  };
  uint64_t savedRevision = 0;
  bool profileMismatch = false;
  void begin(uint32_t boot) {
    if (!data.packKey && !data.calibrationKey) {
      data.packKey = cfg.packKey; data.calibrationKey = cfg.calibrationKey; data.lineage = boot;
    }
    profileMismatch = data.packKey != cfg.packKey || data.calibrationKey != cfg.calibrationKey;
    savedRevision = data.revision;
    if (data.active) {
      data.open.quality |= Interrupted;
      data.open.capacityAccepted = data.open.capacityQualified = data.open.returnAccepted = false;
      finishCycle(false); // a reboot never fills the unobserved interval
    }
    havePrevious_ = broken_ = anchored_ = referenceDone_ = returning_ = false;
    referenceTracking_ = haveDischargeGauge_ = forceTrace_ = false;
    fitUsed_ = 0; fitMs_ = 0;
  }
  void reset(uint32_t boot) {
    data = State{}; savedRevision = 0; begin(boot); ++data.revision;
  }
  bool dirty() const { return data.revision != savedRevision; }
  bool fresh(uint32_t now) const {
    return havePrevious_ && !broken_ && uint32_t(now - previous_.stamp) <= cfg.maxGapMs;
  }
  bool currentQualified(const Sample &s) const {
    return cfg.qualified() && s.temperatureC >= cfg.minimumC && s.temperatureC <= cfg.maximumC;
  }
  void breakContinuity(uint32_t why = Gap) {
    broken_ = true; anchored_ = false; referenceTracking_ = referenceDone_ = false;
    if (data.active) data.open.quality |= why;
  }
  void observe(const Sample &s) {
    if (profileMismatch) return;
    const bool valid = s.valid && uint32_t(s.now - s.stamp) <= cfg.maxGapMs &&
        s.mv >= 5000 && s.mv <= 26000 && s.charging <= 5 && s.sources <= 3;
    if (!valid) {
      ++data.invalidSamples; breakContinuity(Telemetry); ++data.revision; return;
    }
    if (havePrevious_ && s.generation == previous_.generation && s.boot == previous_.boot) return;
    const bool gauge = s.gaugeValid && s.capacityMah && s.chargeMah <= s.capacityMah;
    const bool cycleWasActive = data.active;
    if (!data.active && !s.sources && s.ma < 0 && data.latestCycle != UINT32_MAX) {
      data.open = Cycle{}; data.active = true;
      data.open.id = data.latestCycle + 1; data.open.boot = s.boot;
      data.open.startMs = s.stamp; data.open.startGaugeMah = s.chargeMah;
      data.open.gaugeCapacityMah = s.capacityMah;
      if (!gauge) data.open.quality |= Gauge;
    }
    uint32_t dt = 0;
    if (havePrevious_) {
      dt = uint32_t(s.stamp - previous_.stamp);
      if (s.boot != previous_.boot || broken_ || !dt || dt > cfg.maxGapMs) {
        if (dt < 0x80000000u) {
          data.missingMs += dt;
          if (data.active) { data.open.missingMs += dt; ++data.open.gaps; }
        }
        ++data.gaps; breakContinuity(Gap);
      } else {
        const double drawn = area(-double(previous_.ma), -double(s.ma), dt) / 3600000.0;
        const double charged = area(double(previous_.ma), double(s.ma), dt) / 3600000.0;
        const double drawnWh = area(-double(previous_.ma) * previous_.mv,
                                   -double(s.ma) * s.mv, dt) / 3600000000.0;
        const double chargedWh = area(double(previous_.ma) * previous_.mv,
                                     double(s.ma) * s.mv, dt) / 3600000000.0;
        data.drawMah += drawn; data.chargeMah += charged;
        data.drawMwh += drawnWh; data.chargeMwh += chargedWh; data.coveredMs += dt;
        if (data.active && cycleWasActive) {
          data.open.drawMah += drawn; data.open.chargeMah += charged;
          data.open.drawMwh += drawnWh; data.open.coveredMs += dt;
        }
        if (anchored_) { anchorDraw_ += drawn; anchorTime_ += dt; }
      }
    }
    if (data.active) {
      Cycle &c = data.open;
      if (!currentQualified(s)) c.quality |= Unverified;
      if (s.critical) c.quality |= Critical;
      if (s.mv < c.minimumMv) c.minimumMv = s.mv;
      if (s.mv > c.maximumMv) c.maximumMv = s.mv;
      if (s.temperatureC < c.minimumC) c.minimumC = s.temperatureC;
      if (s.temperatureC > c.maximumC) c.maximumC = s.temperatureC;
      if (!s.sources) {
        if ((cycleWasActive && havePrevious_ && previous_.sources) || s.ma > cfg.currentErrorMa)
          c.quality |= Gauge; // unmeasured NiMH charge efficiency forbids fitting partial-charge windows
        if (!gauge || s.capacityMah != c.gaugeCapacityMah ||
            (haveDischargeGauge_ && s.chargeMah > c.endGaugeMah + cfg.gaugeErrorMah)) c.quality |= Gauge;
        c.endGaugeMah = s.chargeMah;
        fitUsed_ = c.drawMah - c.chargeMah;
        fitMs_ = c.coveredMs; haveDischargeGauge_ = true;
        const uint16_t thresholds[] = {13800, 13500, 13000, 12500};
        for (unsigned i = 0; i < 4; ++i)
          if (!(c.thresholdMask & (1u << i)) && s.mv <= thresholds[i] && gauge) {
            c.thresholdMask |= uint8_t(1u << i); c.thresholdGaugeMah[i] = s.chargeMah;
            c.thresholdUsedMah[i] = float(c.drawMah); forceTrace_ = true;
          }
      }
      c.endMs = s.stamp;
      if (returning_ && s.frame != returnFrame_) c.quality |= Frame;
    }
    if (s.charging == 5 && data.active) {
      data.open.quality |= ChargingFault; finishCycle(false);
    }
    const bool charging = s.sources && s.mode == 1 && s.stationary &&
                          (s.charging == 2 || s.charging == 3);
    if (!charging) { referenceTracking_ = false; referenceDone_ = false; }
    else if (!referenceTracking_) { referenceTracking_ = true; referenceAt_ = s.stamp; }
    const bool stableCharge = charging && referenceTracking_ && uint32_t(s.stamp - referenceAt_) >= cfg.referenceMs;
    if (stableCharge && data.active) {
      if (returning_) {
        data.open.returnMah = data.open.drawMah - returnDraw_;
        data.open.returnErrorMah = cfg.currentErrorMa * double(uint32_t(s.stamp - returnAt_)) / 3600000.0;
        data.open.returnRouteM = returnRoute_;
        data.open.returnFrame = returnFrame_;
        data.open.returnAccepted = s.dockConfirmed && (s.sources & 2) &&
            isfinite(returnRoute_) && returnRoute_ >= 0 && data.open.returnMah > 0 &&
            !(data.open.quality & (Gap | Telemetry | Interrupted | Critical | Frame | Unverified));
      }
      finishCycle(true);
    }
    if (anchored_ && (!gauge || !currentQualified(s) ||
        (havePrevious_ && (s.capacityMah != previous_.capacityMah ||
         (!s.sources && s.chargeMah > previous_.chargeMah + cfg.gaugeErrorMah))))) {
      anchored_ = false; referenceDone_ = false;
    }
    if (s.sources && !referenceDone_) anchored_ = false; // partial charging efficiency is unknown
    if (stableCharge && s.charging == 3 && gauge && s.chargeMah == s.capacityMah &&
        currentQualified(s) && cfg.fullReferenceVerified && !referenceDone_) {
      const auto m = model();
      anchorMah_ = m.qualifiedCycles >= cfg.minimumCycles ? m.capacityMah : s.capacityMah;
      anchorLower_ = m.qualifiedCycles >= cfg.minimumCycles ? m.capacityLowerMah : 0;
      anchorDraw_ = 0; anchorTime_ = 0; anchored_ = true; referenceDone_ = true;
    }
    if (forceTrace_ || !havePrevious_ || uint32_t(s.stamp - traceAt_) >= cfg.traceMs)
      appendTrace(s, gauge);
    previous_ = s; havePrevious_ = true; broken_ = false;
    ++data.revision;
  }
  bool beginReturn(uint32_t now, uint32_t frame, float routeM) {
    if (!data.active || returning_ || !frame || !fresh(now) || !isfinite(routeM) || routeM < 0) return false;
    returning_ = true; returnAt_ = previous_.stamp; returnDraw_ = data.open.drawMah;
    returnRoute_ = routeM; returnFrame_ = frame; forceTrace_ = true;
    return true;
  }
  Model model() const {
    Model m; double values[CycleCount]{}, trusted[CycleCount]{};
    for (unsigned i = 0; i < data.cycleSize; ++i) {
      const Cycle &c = data.cycles[i];
      if (c.capacityAccepted) values[m.advisoryCycles++] = c.capacityCandidateMah;
      if (c.capacityQualified) {
        if (!m.qualifiedCycles || c.capacityLowerMah < m.capacityLowerMah) m.capacityLowerMah = c.capacityLowerMah;
        trusted[m.qualifiedCycles++] = c.capacityCandidateMah;
      }
      if (c.returnAccepted) {
        ++m.returnCycles;
        if (c.returnRouteM > m.maximumReturnRouteM) m.maximumReturnRouteM = c.returnRouteM;
      }
    }
    for (unsigned i = 1; i < m.advisoryCycles; ++i) {
      const double v = values[i]; unsigned j = i;
      while (j && values[j-1] > v) { values[j] = values[j-1]; --j; } values[j] = v;
    }
    for (unsigned i = 1; i < m.qualifiedCycles; ++i) {
      const double v = trusted[i]; unsigned j = i;
      while (j && trusted[j-1] > v) { trusted[j] = trusted[j-1]; --j; } trusted[j] = v;
    }
    if (m.advisoryCycles) {
      m.capacityMinimumMah = values[0]; m.capacityMaximumMah = values[m.advisoryCycles-1];
      m.capacityMah = (values[(m.advisoryCycles-1)/2] + values[m.advisoryCycles/2]) * .5;
    }
    if (m.qualifiedCycles >= cfg.minimumCycles)
      m.capacityMah = (trusted[(m.qualifiedCycles-1)/2] + trusted[m.qualifiedCycles/2]) * .5;
    return m;
  }
  Estimate estimate(uint32_t now) const {
    Estimate e;
    if (!fresh(now)) return e;
    e.gaugeValid = previous_.gaugeValid && previous_.capacityMah && previous_.chargeMah <= previous_.capacityMah;
    e.remainingMah = e.gaugeValid ? previous_.chargeMah : 0;
    if (anchored_) {
      e.anchored = true; e.remainingMah = fmax(0.0, anchorMah_ - anchorDraw_);
      const auto m = model();
      e.learnedCapacity = m.qualifiedCycles >= cfg.minimumCycles && anchorLower_ > 0;
      if (e.learnedCapacity && e.gaugeValid) {
        const double lower = anchorLower_ - anchorDraw_ - cfg.currentErrorMa * anchorTime_ / 3600000.0;
        e.remainingLowerMah = fmax(0.0, fmin(lower, previous_.chargeMah - cfg.gaugeErrorMah));
      }
    }
    return e;
  }
  Reserve reserve(uint32_t now, uint32_t frame, float routeM) const {
    Reserve r;
    const auto e = estimate(now); const auto m = model();
    if (!currentQualified(previous_) || !cfg.fullReferenceVerified || !e.learnedCapacity || !e.gaugeValid ||
        !e.anchored || !isfinite(routeM) || routeM < 0 || routeM > m.maximumReturnRouteM ||
        !isfinite(cfg.reserveMarginMah) || cfg.reserveMarginMah <= 0 || previous_.sources) return r;
    for (unsigned i = 0; i < data.cycleSize; ++i) {
      const Cycle &c = data.cycles[i];
      if (!c.returnAccepted || c.returnFrame != frame || (c.returnRouteM == 0 && routeM > 0)) continue;
      const double scale = c.returnRouteM > 0 ? fmax(1.0, double(routeM) / c.returnRouteM) : 1;
      r.requiredMah = fmax(r.requiredMah, (c.returnMah + c.returnErrorMah) * scale);
      ++r.trips;
    }
    if (r.trips < cfg.minimumCycles) return r;
    float matchingMaximum = 0;
    for (unsigned i = 0; i < data.cycleSize; ++i)
      if (data.cycles[i].returnAccepted && data.cycles[i].returnFrame == frame)
        matchingMaximum = fmaxf(matchingMaximum, data.cycles[i].returnRouteM);
    if (routeM > matchingMaximum) return Reserve{}; // no extrapolation beyond this frame's observations
    r.requiredMah += cfg.reserveMarginMah; r.remainingLowerMah = e.remainingLowerMah;
    r.available = true;
    r.earlyReturn = cfg.adaptiveReturn && r.remainingLowerMah <= r.requiredMah;
    return r;
  }
  uint32_t oldestCycle() const { return data.latestCycle - data.cycleSize + 1; }
  uint32_t oldestTrace() const { return data.latestTrace - data.traceSize + 1; }
  const Cycle *cycle(uint32_t id) const {
    if (!id || id < oldestCycle() || id > data.latestCycle) return nullptr;
    return &data.cycles[(id-1) % CycleCount];
  }
  const Trace *trace(uint32_t id) const {
    if (!id || id < oldestTrace() || id > data.latestTrace) return nullptr;
    return &data.traces[(id-1) % TraceCount];
  }
  static bool validState(const State &s) {
    if (s.cycleSize > CycleCount || s.traceSize > TraceCount || s.cycleSize > s.latestCycle ||
        s.traceSize > s.latestTrace || !finiteTotals(s.drawMah,s.chargeMah,s.drawMwh,s.chargeMwh)) return false;
    for (unsigned i = 0; i < CycleCount; ++i) {
      const auto &c = s.cycles[i];
      if (!finiteTotals(c.drawMah,c.chargeMah,c.drawMwh,c.capacityCandidateMah) ||
          !isfinite(c.capacityLowerMah) || c.capacityLowerMah < 0 ||
          !isfinite(c.returnMah) || c.returnMah < 0 || !isfinite(c.returnErrorMah) || c.returnErrorMah < 0 ||
          !isfinite(c.returnRouteM) || c.returnRouteM < 0 || c.thresholdMask > 15) return false;
      for (float used : c.thresholdUsedMah) if (!isfinite(used) || used < 0) return false;
    }
    for (unsigned i = 0; i < TraceCount; ++i)
      if (!isfinite(s.traces[i].usedMah) || s.traces[i].usedMah < 0 || s.traces[i].gaugeValid > 1)
        return false;
    return !s.active || finiteTotals(s.open.drawMah,s.open.chargeMah,s.open.drawMwh,0);
  }
private:
  Sample previous_;
  bool havePrevious_ = false, broken_ = false, anchored_ = false, referenceDone_ = false;
  bool referenceTracking_ = false, returning_ = false, forceTrace_ = false, haveDischargeGauge_ = false;
  uint32_t referenceAt_ = 0, traceAt_ = 0, returnAt_ = 0, returnFrame_ = 0;
  uint64_t anchorTime_ = 0, fitMs_ = 0;
  double anchorMah_ = 0, anchorLower_ = 0, anchorDraw_ = 0, fitUsed_ = 0, returnDraw_ = 0;
  float returnRoute_ = 0;
  static bool finiteTotals(double a,double b,double c,double d) {
    return isfinite(a) && a >= 0 && isfinite(b) && b >= 0 && isfinite(c) && c >= 0 && isfinite(d) && d >= 0;
  }
  // Exact area above zero of a linearly interpolated current/power interval.
  static double area(double a, double b, uint32_t dt) {
    if (a <= 0 && b <= 0) return 0;
    if (a >= 0 && b >= 0) return (a+b)*.5*dt;
    const double positive = a > 0 ? a : b;
    return .5 * positive * positive / fabs(a-b) * dt;
  }
  void appendTrace(const Sample &s, bool gauge) {
    if (data.latestTrace == UINT32_MAX) return;
    Trace t; t.id = ++data.latestTrace; t.cycle = data.active ? data.open.id : data.latestCycle;
    t.boot = s.boot; t.ms = s.stamp; t.mv = s.mv; t.ma = s.ma; t.temperatureC = s.temperatureC;
    t.chargeMah = s.chargeMah; t.capacityMah = s.capacityMah; t.gaugeValid = gauge;
    t.charging = s.charging; t.sources = s.sources; t.mode = s.mode;
    t.usedMah = data.active ? data.open.drawMah : 0; t.quality = data.active ? data.open.quality : 0;
    data.traces[(t.id-1) % TraceCount] = t;
    if (data.traceSize < TraceCount) ++data.traceSize;
    forceTrace_ = false; traceAt_ = s.stamp;
  }
  void finishCycle(bool charged) {
    if (!data.active) return;
    Cycle &c = data.open;
    const double drop = double(c.startGaugeMah) - c.endGaugeMah;
    const double minimumDrop = double(c.gaugeCapacityMah) * cfg.minimumSpanPermille / 1000;
    if (charged && !(c.quality & (Gap | Gauge | Interrupted | Telemetry | ChargingFault)) &&
        haveDischargeGauge_ && c.gaugeCapacityMah && drop >= minimumDrop && drop > 0 && fitUsed_ > 0) {
      c.capacityCandidateMah = fitUsed_ * c.gaugeCapacityMah / drop;
      c.capacityAccepted = isfinite(c.capacityCandidateMah) && c.capacityCandidateMah > 0 && c.capacityCandidateMah <= 65535;
      const double usedError = cfg.currentErrorMa * fitMs_ / 3600000.0;
      c.capacityLowerMah = fmax(0.0, fitUsed_ - usedError) * c.gaugeCapacityMah / (drop + 2*cfg.gaugeErrorMah);
      c.capacityQualified = c.capacityAccepted && cfg.qualified() && !(c.quality & (Unverified | Critical)) && c.capacityLowerMah > 0;
    }
    data.latestCycle = c.id;
    data.cycles[(c.id-1) % CycleCount] = c;
    if (data.cycleSize < CycleCount) ++data.cycleSize;
    ++data.completedCycles; ++data.revision;
    data.active = false; returning_ = haveDischargeGauge_ = false; fitUsed_ = 0; fitMs_ = 0;
    forceTrace_ = true;
  }
};
} // namespace roonav
