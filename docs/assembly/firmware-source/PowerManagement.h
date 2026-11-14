#pragma once
#include <stdint.h>

namespace roonav {
// Trial voltage policy. Hardware EN remains independent; this class never drives D10.
class PowerManagement {
public:
  enum class State : uint8_t {
    Unconfigured, BootGuard, Running, Warning, Returning, UrgentReturn,
    CriticalHold, HandoffHold, NativeFallback, DockedHold, Held
  };
  enum class Reason : uint8_t {
    Calibration, Boot, None, LowVoltage, Telemetry, Charging, StopFailed,
    Persistence, RestartGuard, ApprovalRequired, Safety, NativeFailed, Revoked, Operator, EnergyReserve
  };
  struct Config {
    bool verified = false, fallbackVerified = false, clearanceVerified = false;
    bool passiveStopVerified = false;
    uint32_t warningMv = 13800, returnMv = 13500, urgentMv = 13000, criticalMv = 12500;
    // Measured on the installed system; zero values deliberately prevent activation.
    uint32_t hardwareOffMaxMv = 0, recoverMv = 0, saveMinMv = 0;
    uint32_t returnMs = 5000, stopMs = 1000, recoverMs = 5000, nativeMaxMs = 60000;
    bool valid() const {
      return verified && hardwareOffMaxMv >= 5000 && hardwareOffMaxMv < saveMinMv &&
             saveMinMv <= criticalMv && criticalMv < urgentMv && urgentMv < returnMv &&
             returnMv < warningMv && warningMv < recoverMv && recoverMv <= 25000 &&
             returnMs == 5000 && stopMs >= 1000 && stopMs <= 5000 &&
             recoverMs >= 5000 && recoverMs <= 30000 && nativeMaxMs > 0 && nativeMaxMs <= 60000;
    }
  } cfg;
  struct Input {
    uint32_t now = 0, stamp = 0, generation = 0, millivolts = 0;
    uint16_t left = 0, right = 0;
    uint8_t mode = 0, charging = 0, sources = 0;
    bool valid = false;
  };
  struct Port {
    virtual ~Port() {}
    // Stop in Safe. Passive is permitted only after physical qualification.
    virtual bool stop(bool passive) = 0;
    virtual bool persistIntent() = 0;
    virtual bool clearIntent() = 0;
    virtual bool returnHome() = 0; // zero cleaning motors, retain LiDAR/navigation
    virtual void lidarOff() = 0;
    virtual bool fallbackSafe(bool starting) = 0;
    virtual bool seekDock() = 0;
  };
  State state = State::Unconfigured;
  Reason reason = Reason::Calibration;
  bool intentPending = false, approved = false, stopConfirmed = false;
  uint32_t scope = 0, approvalRevision = 1, fallbackAttempts = 0;
  void begin(uint32_t now, bool pending) {
    intentPending = pending;
    state = cfg.valid() ? State::BootGuard : State::Unconfigured;
    reason = pending ? Reason::RestartGuard : cfg.valid() ? Reason::Boot : Reason::Calibration;
    scope = 0; approved = stopConfirmed = false; ++approvalRevision;
    haveSample_ = haveWheels_ = prepared_ = lidarOff_ = chargeReady_ = arming_ = telemetryReady_ = false;
    fallbackAttempts = 0;
    epoch_ = now; low_.reset(); stopped_.reset(); recovered_.reset(); recoveredBoot_.reset();
  }
  bool returning() const { return state == State::Returning || state == State::UrgentReturn; }
  bool blocksMotion() const {
    return state != State::Running && state != State::Warning && !returning();
  }
  bool blocksCleaning() const { return state != State::Running && state != State::Warning; }
  bool canStartMission() const { return state == State::Running; }
  bool requestReserveReturn(uint32_t now, Port &port) {
    if (!cfg.valid() || (state != State::Running && state != State::Warning) ||
        !scope || !intentPending || !current(now) || last_.sources) return false;
    if (!port.returnHome()) { hold(Reason::Safety,port); return false; }
    state = State::Returning; reason = Reason::EnergyReserve;
    return true; // may advance return; cannot lower/delay any voltage threshold
  }
  bool canInitializeStorage() const { return !intentPending && !scope && blocksMotion(); }
  bool fallbackOffered() const {
    return (state == State::CriticalHold || state == State::HandoffHold) && !fallbackAttempts;
  }
  bool offerHandoff(uint32_t now, Port &port) {
    if (!cfg.valid() || blocksMotion() || !scope || !intentPending || !current(now)) return false;
    state = State::HandoffHold; reason = Reason::ApprovalRequired;
    prepared_ = true; epoch_ = now; haveWheels_ = false;
    stopConfirmed = false; stopped_.reset();
    if (!port.stop(false)) { state = State::Held; reason = Reason::StopFailed; return false; }
    return true;
  }
  bool armMission(uint32_t newScope, uint32_t now, Port &port) {
    if (!canStartMission() || !newScope || scope || !current(now) ||
        last_.millivolts <= cfg.warningMv || last_.millivolts < cfg.saveMinMv)
      return false;
    if (!port.stop(cfg.passiveStopVerified)) { hold(Reason::StopFailed, port); return false; }
    intentPending = true; // even a partial/torn marker must inhibit formatting and reboot motion
    if (!port.persistIntent()) { hold(Reason::Persistence, port); return false; }
    intentPending = true; scope = newScope; approved = false; ++approvalRevision;
    fallbackAttempts = 0;
    state = State::BootGuard; reason = Reason::Boot; arming_ = prepared_ = true;
    epoch_ = now; haveWheels_ = stopConfirmed = lidarOff_ = false;
    low_.reset(); stopped_.reset(); recoveredBoot_.reset();
    return true;
  }
  // Called only for an authenticated explicit decision bound to boot/mission/revision.
  bool approve(uint32_t requestedScope, uint32_t revision, uint32_t now, uint32_t expires) {
    const int32_t remaining = int32_t(expires - now);
    if (!cfg.valid() || !cfg.fallbackVerified || !cfg.clearanceVerified ||
        remaining <= 0 || remaining > 5000 ||
        !scope || scope != requestedScope || revision != approvalRevision ||
        !current(now) || fallbackAttempts ||
        (blocksMotion() && state != State::CriticalHold && state != State::HandoffHold))
      return false;
    approved = true;
    ++approvalRevision; // replaying the same request cannot re-authorize a later attempt
    return true;
  }
  void revoke(Port &port) {
    approved = false; ++approvalRevision;
    if (state == State::NativeFallback) hold(Reason::Revoked, port);
    else if (state == State::CriticalHold || state == State::HandoffHold) reason = Reason::Revoked;
  }
  void operatorPause(Port &port) {
    revoke(port);
    if (returning() || state == State::CriticalHold || state == State::HandoffHold ||
        (scope && state == State::BootGuard))
      hold(Reason::Operator, port);
  }
  bool resetAfterCharge(uint32_t now, Port &port) {
    if (!cfg.valid() || !current(now) || !chargeReady_ ||
        (state != State::DockedHold && state != State::BootGuard && state != State::Held))
      return false;
    if (!port.clearIntent()) { reason = Reason::Persistence; return false; }
    intentPending = approved = false; scope = 0; ++approvalRevision;
    fallbackAttempts = 0; state = State::Running; reason = Reason::None;
    prepared_ = arming_ = lidarOff_ = false;
    low_.reset(); recoveredBoot_.reset();
    return true; // no mission or motor command is started
  }
  void tick(const Input &in, Port &port) {
    if (state == State::Unconfigured) return; // no hardware side effects before qualification
    if (!cfg.valid()) { hold(Reason::Calibration, port); return; }
    if (!prepared_ && blocksMotion()) {
      prepared_ = true;
      if (!port.stop(cfg.passiveStopVerified && state != State::CriticalHold && state != State::HandoffHold)) {
        state = State::Held; reason = Reason::StopFailed; return;
      }
      epoch_ = in.now; stopped_.reset(); haveWheels_ = false;
      return; // never use a sample taken before the stop
    }
    const bool fresh = in.valid && uint32_t(in.now - in.stamp) <= 200 &&
        in.millivolts >= 5000 && in.millivolts <= 26000 && in.charging <= 5 && in.sources <= 3;
    if (!fresh) {
      telemetryReady_ = false;
      low_.reset(); stopped_.reset(); recovered_.reset(); recoveredBoot_.reset();
      chargeReady_ = stopConfirmed = false;
      if (!blocksMotion() || state == State::NativeFallback) hold(Reason::Telemetry, port);
      else if (approved) { approved = false; ++approvalRevision; reason = Reason::Telemetry; }
      return;
    }
    telemetryReady_ = true;
    if (haveSample_ && in.generation == last_.generation) return;
    const bool gap = haveSample_ && uint32_t(in.stamp - last_.stamp) > 200;
    if (gap) {
      low_.reset(); stopped_.reset(); recovered_.reset(); recoveredBoot_.reset(); chargeReady_ = false;
      if (!blocksMotion() || state == State::NativeFallback) { hold(Reason::Telemetry, port); return; }
    }
    const bool unchanged = haveWheels_ && in.left == last_.left && in.right == last_.right;
    haveWheels_ = true; haveSample_ = true; last_ = in;
    const bool afterStop = int32_t(in.stamp - epoch_) > 0;
    stopConfirmed = stopped_.update(unchanged && afterStop &&
        (in.mode == 1 || in.mode == 2), in.stamp, cfg.stopMs);
    chargeReady_ = recovered_.update(stopConfirmed && in.mode == 1 && (in.sources & 2) &&
        (in.charging == 2 || in.charging == 3) && in.millivolts >= cfg.recoverMv,
        in.stamp, cfg.recoverMs);
    if (in.charging == 5) {
      if (state != State::Held) hold(Reason::Charging, port);
      return;
    }
    if (in.sources != 0) {
      if (arming_) {
        if (chargeReady_) {
          state = State::Running; reason = Reason::None; prepared_ = arming_ = false;
        }
        return;
      }
      if (returning() || state == State::CriticalHold || state == State::HandoffHold || state == State::NativeFallback ||
          (state == State::BootGuard && !intentPending)) {
        state = State::DockedHold; reason = Reason::Charging;
        approved = false; ++approvalRevision; prepared_ = false; port.lidarOff();
      }
      else if (state == State::Running || state == State::Warning) {
        state = in.millivolts <= cfg.warningMv ? State::Warning : State::Running;
        reason = state == State::Warning ? Reason::LowVoltage : Reason::None;
      }
      return;
    }
    if (state == State::BootGuard) {
      if (intentPending && !arming_) { reason = Reason::RestartGuard; return; }
      if (recoveredBoot_.update(stopConfirmed && in.millivolts >= cfg.recoverMv,
                               in.stamp, cfg.recoverMs)) {
        state = State::Running; reason = Reason::None; prepared_ = arming_ = false;
      }
      return;
    }
    if (state == State::Held || state == State::DockedHold) return;
    if (state == State::NativeFallback) {
      if (in.mode != 1 || uint32_t(in.now - nativeAt_) >= cfg.nativeMaxMs ||
          !port.fallbackSafe(false)) hold(Reason::Safety, port);
      return;
    }
    if (state == State::CriticalHold || state == State::HandoffHold) {
      if (!stopConfirmed) return;
      if (!lidarOff_) { port.lidarOff(); lidarOff_ = true; }
      if (!ensureIntent(port)) { hold(Reason::Persistence, port); return; }
      if (!approved) return; // retain the safety/revocation reason for status and a new decision
      approved = false; ++approvalRevision;
      if (!cfg.fallbackVerified || !cfg.clearanceVerified || !scope || !intentPending ||
          !port.fallbackSafe(true)) { reason = Reason::Safety; return; }
      ++fallbackAttempts; // attempted once, including partial UART write/failure
      if (!port.seekDock()) { hold(Reason::NativeFailed, port); return; }
      state = State::NativeFallback; reason = Reason::None;
      nativeAt_ = in.now;
      return;
    }
    if (in.millivolts <= cfg.criticalMv) {
      state = State::CriticalHold; reason = Reason::ApprovalRequired;
      prepared_ = false; stopConfirmed = false; stopped_.reset();
      // Same tick: no extra debounce or loop interval before the stop command.
      prepared_ = true; epoch_ = in.now; haveWheels_ = false;
      if (!port.stop(false)) { state = State::Held; reason = Reason::StopFailed; }
      return;
    }
    const bool urgent = in.millivolts <= cfg.urgentMv;
    const bool sustained = low_.update(in.millivolts <= cfg.returnMv, in.stamp, cfg.returnMs);
    if ((urgent || sustained) && !returning()) {
      if (!ensureIntent(port)) { hold(Reason::Persistence, port); return; }
      if (!port.returnHome()) { hold(Reason::Safety, port); return; }
      state = urgent ? State::UrgentReturn : State::Returning; reason = Reason::LowVoltage;
    } else if (returning()) {
      if (urgent) state = State::UrgentReturn; // never downgrade after voltage rebound
    } else {
      state = in.millivolts <= cfg.warningMv ? State::Warning : State::Running;
      reason = state == State::Warning ? Reason::LowVoltage : Reason::None;
    }
  }
private:
  struct Window {
    uint32_t since = 0; unsigned count = 0;
    void reset() { count = 0; }
    bool update(bool yes, uint32_t stamp, uint32_t duration) {
      if (!yes) { reset(); return false; }
      if (!count) since = stamp;
      if (count < 100000) ++count;
      return count >= 5 && uint32_t(stamp - since) >= duration;
    }
  } low_, stopped_, recovered_, recoveredBoot_;
  Input last_;
  bool haveSample_ = false, haveWheels_ = false, prepared_ = false;
  bool lidarOff_ = false, chargeReady_ = false, arming_ = false, telemetryReady_ = false;
  uint32_t epoch_ = 0, nativeAt_ = 0;
  bool current(uint32_t now) const {
    return telemetryReady_ && haveSample_ && uint32_t(now - last_.stamp) <= 200;
  }
  bool ensureIntent(Port &port) {
    if (intentPending) return true;
    if (last_.millivolts < cfg.saveMinMv || !port.persistIntent()) return false;
    intentPending = true;
    return true;
  }
  void hold(Reason why, Port &port) {
    state = State::Held; reason = why; approved = false; ++approvalRevision;
    arming_ = false;
    const bool ok = port.stop(cfg.passiveStopVerified);
    prepared_ = true; port.lidarOff(); lidarOff_ = true;
    epoch_ = last_.now; stopped_.reset(); recovered_.reset(); recoveredBoot_.reset();
    chargeReady_ = stopConfirmed = false;
    if (!ok) reason = Reason::StopFailed;
  }
};
} // namespace roonav
