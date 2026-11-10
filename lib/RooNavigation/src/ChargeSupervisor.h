#pragma once
#include <math.h>
#include <stdint.h>

namespace roonav {
// Local charger observer. It never sends charger/motor/BRC commands.
class ChargeSupervisor {
public:
  enum class Policy : uint8_t { NativeCycleComplete, MissionEnergyReady };
  enum class Phase : uint8_t { Unknown, Contact, Waiting, Bulk, Reconditioning, Maintenance,
                               Preparing, Held };
  enum class Why : uint8_t { None, Telemetry, NoSource, ChargerFault, NotPassive,
                             Unqualified, WaitingTimeout, PreparationTimeout };
  struct Config {
    Policy policy = Policy::NativeCycleComplete;
    // Real completion/temperature predicates must be qualified on the installed pack.
    bool departureVerified = false;
    float resumePercent = 80, nativeMinimumPercent = 100;
    int16_t minimumC = 0, maximumC = 0; // zero-width range is deliberately unconfigured
    uint32_t contactMs = 1000, readyMs = 3000;
    // Engineering hold limits, not battery chemistry limits or charger termination.
    uint32_t waitingMs = 600000, preparationMs = 30000;
    bool valid() const {
      return departureVerified && uint8_t(policy) <= 1 && isfinite(resumePercent) &&
             resumePercent >= 50 && resumePercent <= 100 &&
             isfinite(nativeMinimumPercent) && nativeMinimumPercent >= 90 &&
             nativeMinimumPercent <= 100 && minimumC < maximumC && minimumC >= -20 &&
             maximumC <= 60 && contactMs >= 200 && contactMs <= 10000 && readyMs >= 500 &&
             readyMs <= 60000 && waitingMs >= 1000 && waitingMs < 0x80000000u &&
             preparationMs >= 1000 && preparationMs <= 120000;
    }
  } cfg;
  struct Sample {
    uint32_t now = 0, stamp = 0, generation = 0;
    bool valid = false, percentageValid = false, stopped = false;
    uint8_t sources = 0, state = 0, mode = 0;
    float percentage = 0;
    int16_t currentMa = 0, temperatureC = 0;
  };
  Phase phase = Phase::Unknown;
  Why why = Why::None;
  bool contactConfirmed = false, preparation = false, ready = false;
  void reset() {
    phase = Phase::Unknown;
    why = Why::None;
    contactConfirmed = preparation = ready = false;
    haveContact_ = haveReady_ = waiting_ = haveGeneration_ = latched_ = false;
  }
  void update(const Sample &s, bool pendingMission) {
    ready = false;
    if (latched_) { phase = Phase::Held; return; }
    if (!s.valid || uint32_t(s.now - s.stamp) > 200 || s.state > 5 || s.sources > 3) {
      contactConfirmed = false;
      haveContact_ = haveReady_ = false;
      phase = Phase::Held; why = Why::Telemetry;
      return;
    }
    if (!(s.sources & 2)) {
      reset(); why = Why::NoSource;
      return;
    }
    if (s.state == 5) { hold(Why::ChargerFault); return; }
    if (preparation && pendingMission && uint32_t(s.now - preparingAt_) >= cfg.preparationMs) {
      hold(Why::PreparationTimeout); return;
    }
    if (s.mode != 1 || !s.stopped) {
      contactConfirmed = false;
      haveContact_ = haveReady_ = false;
      phase = Phase::Contact; why = Why::NotPassive;
      return;
    }
    if (haveGeneration_ && s.generation == generation_) return;
    if (haveGeneration_ && uint32_t(s.stamp - lastStamp_) > 200) {
      haveContact_ = haveReady_ = false;
      contactConfirmed = false;
    }
    generation_ = s.generation; lastStamp_ = s.stamp; haveGeneration_ = true;
    if (!haveContact_) { contactAt_ = s.stamp; haveContact_ = true; }
    contactConfirmed = uint32_t(s.stamp - contactAt_) >= cfg.contactMs;
    phase = s.state == 1 ? Phase::Reconditioning : s.state == 2 ? Phase::Bulk :
            s.state == 3 ? Phase::Maintenance : Phase::Waiting;
    why = Why::None;
    if (s.state == 0 || s.state == 4) {
      if (!waiting_) { waiting_ = true; waitingAt_ = s.stamp; }
      if (uint32_t(s.stamp - waitingAt_) >= cfg.waitingMs) hold(Why::WaitingTimeout);
      haveReady_ = false;
      return;
    }
    waiting_ = false;
    if (!pendingMission) { preparation = false; haveReady_ = false; return; }
    if (!cfg.valid()) { why = Why::Unqualified; haveReady_ = false; return; }
    const bool energy = s.percentageValid && isfinite(s.percentage) && s.percentage <= 100 &&
        s.temperatureC >= cfg.minimumC && s.temperatureC <= cfg.maximumC &&
        (cfg.policy == Policy::NativeCycleComplete
          ? s.state == 3 && s.percentage >= cfg.nativeMinimumPercent
          : (s.state == 2 || s.state == 3) && s.percentage >= cfg.resumePercent);
    if (!energy || !contactConfirmed) { haveReady_ = false; return; }
    if (!haveReady_) { readyAt_ = s.stamp; haveReady_ = true; }
    if (uint32_t(s.stamp - readyAt_) < cfg.readyMs) return;
    if (!preparation) { preparation = true; preparingAt_ = s.now; }
    phase = Phase::Preparing;
    if (uint32_t(s.now - preparingAt_) >= cfg.preparationMs) {
      hold(Why::PreparationTimeout); return;
    }
    ready = true; // consumer still requires fresh localization, scans, geometry and power guard
  }
  bool held() const { return latched_; }
private:
  bool haveContact_ = false, haveReady_ = false, waiting_ = false;
  bool haveGeneration_ = false, latched_ = false;
  uint32_t contactAt_ = 0, readyAt_ = 0, waitingAt_ = 0, preparingAt_ = 0;
  uint32_t generation_ = 0, lastStamp_ = 0;
  void hold(Why why_) { why = why_; phase = Phase::Held; ready = false; latched_ = true; }
};
} // namespace roonav
