#pragma once
#include <stdint.h>

namespace roonav {
// D36V28F5 EN is hysteretic, not a latch. All voltages below must be measured
// on the installed pack/circuit. There is deliberately no guessed default.
class PowerShutdown {
public:
  enum class State : uint8_t { Unconfigured, BootGuard, Running, Settling, Verify, Held, Asserted };
  enum class Reason : uint8_t {
    Calibration,
    Boot,
    None,
    LowVoltage,
    Telemetry,
    Charging,
    Rebound,
    StopFailed,
    Persistence,
    Timeout,
    RestartGuard,
    CutoffNotObserved,
    SaveVoltage
  };
  struct Config {
    bool verified = false;
    uint32_t cutoffMv = 0, recoverMv = 0, restartMinMv = 0, reboundMarginMv = 0, saveMinMv = 0;
    uint32_t lowMs = 2000, settleMs = 2000, recoverMs = 5000, timeoutMs = 30000;
    uint32_t stableBandMv = 100;
    bool valid() const {
      return verified && saveMinMv >= 6000 && saveMinMv <= 25000 && cutoffMv <= 25000 &&
             recoverMv <= 25000 && saveMinMv + 100 <= cutoffMv && cutoffMv + 100 <= recoverMv &&
             recoverMv < restartMinMv && restartMinMv <= 25000 && reboundMarginMv >= 100 &&
             reboundMarginMv <= 3000 && cutoffMv + reboundMarginMv < restartMinMv && lowMs >= 500 &&
             lowMs <= 5000 && settleMs >= 1000 && settleMs <= 5000 && recoverMs >= 2000 &&
             recoverMs <= 30000 && timeoutMs >= 2 * settleMs + 5000 && timeoutMs <= 60000 &&
             stableBandMv >= 10 && stableBandMv <= 200;
    }
  } cfg;
  struct Input {
    uint32_t now = 0, stamp = 0, generation = 0, millivolts = 0;
    uint16_t left = 0, right = 0;
    uint8_t mode = 0, charging = 0, sources = 0;
    bool valid = false; // coherent fresh voltage/charge/mode/encoder frame
  };
  struct PersistResult {
    bool intent = false, checkpoint = false;
  };
  struct Port {
    virtual ~Port() {}
    virtual uint32_t nowMs() const = 0;
    // Freeze command ownership, stop Roomba, stop LiDAR scan, OE LOW then ON LOW.
    // True means complete TX, NOT a physical stop. Encoders/mode confirm below.
    virtual bool quiesce() = 0;
    // Save checkpoint, commit/read-back intent, then invalidate/drain old RX.
    virtual PersistResult persist() = 0;
    virtual bool clearIntent() = 0;
    virtual void assertD10() = 0; // HIGH once; never lowered in this boot
  };
  State state = State::Unconfigured;
  Reason reason = Reason::Calibration;
  bool intentPending = false, checkpointSaved = false;
  uint32_t assertedAt = 0, assertions = 0, cutoffVoltageMv = 0;
  void begin(uint32_t now, bool pending) {
    state = cfg.valid() ? State::BootGuard : State::Unconfigured;
    reason = pending ? Reason::RestartGuard : cfg.valid() ? Reason::Boot : Reason::Calibration;
    intentPending = pending;
    prepared_ = haveSample_ = false;
    epoch_ = now;
    resetWindows();
  }
  bool blocksMotion() const { return state != State::Running; }
  bool canInitializeStorage() const {
    return !intentPending &&
           (state == State::Unconfigured || state == State::BootGuard || state == State::Running);
  }
  void tick(const Input &in, Port &port) {
    if (state == State::Asserted) {
      if (uint32_t(in.now - assertedAt) >= 2000)
        reason = Reason::CutoffNotObserved;
      return; // no reset, no pulse train, no release of D10
    }
    if (state == State::Unconfigured || state == State::Held)
      return;
    if (!cfg.valid()) {
      state = State::Held;
      reason = Reason::Calibration;
      port.quiesce();
      return;
    }
    if (blocksMotion() && !prepared_) {
      prepared_ = true;
      if (!port.quiesce()) {
        fail(Reason::StopFailed);
        return;
      }
      epoch_ = port.nowMs();
      if (state == State::Settling)
        startedAt_ = epoch_;
      resetWindows();
      return; // never use the frame that predates the stop
    }
    if ((state == State::Settling || state == State::Verify) &&
        uint32_t(in.now - startedAt_) >= cfg.timeoutMs) {
      fail(Reason::Timeout);
      return;
    }
    bool fresh = in.valid && uint32_t(in.now - in.stamp) <= 200 && in.millivolts >= 5000 &&
                 in.millivolts <= 26000 && in.charging <= 5 && in.sources <= 3 &&
                 (state == State::Running || int32_t(in.stamp - epoch_) > 0);
    if (!fresh) {
      resetWindows();
      if (state == State::Running)
        guard(Reason::Telemetry);
      return;
    }
    if (haveSample_ && in.generation == lastGeneration_)
      return; // cached samples are not repeated evidence
    if (haveSample_ && uint32_t(in.stamp - lastStamp_) > 200)
      resetWindows();
    haveSample_ = true;
    lastGeneration_ = in.generation;
    lastStamp_ = in.stamp;
    const bool charger =
        in.sources != 0 || in.charging != 0; // includes faults/unknown charge state
    if (state == State::Running) {
      if (low_.update(in.millivolts <= cfg.cutoffMv, in.stamp, cfg.lowMs)) {
        state = charger ? State::BootGuard : State::Settling;
        reason = charger ? Reason::Charging : Reason::LowVoltage;
        prepared_ = false;
        resetWindows();
      }
      return;
    }
    // Require Passive mode and unchanged wheel encoders for a full second.
    const bool unchanged = haveWheels_ && in.left == lastLeft_ && in.right == lastRight_;
    lastLeft_ = in.left;
    lastRight_ = in.right;
    haveWheels_ = true;
    const bool stopped = stationary_.update(in.mode == 1 && unchanged, in.stamp, 1000);
    if (state == State::BootGuard) {
      const uint32_t recover = intentPending ? cfg.restartMinMv : cfg.recoverMv;
      const bool low = low_.update(!charger && in.millivolts <= cfg.cutoffMv, in.stamp, cfg.lowMs);
      if (recovery_.update(in.millivolts >= recover && stopped, in.stamp, cfg.recoverMs)) {
        if (intentPending && !port.clearIntent()) {
          fail(Reason::Persistence);
          return;
        }
        intentPending = false;
        state = State::Running;
        reason = Reason::None;
        prepared_ = false;
        resetWindows();
      } else if (!intentPending && low) {
        state = State::Settling;
        reason = Reason::LowVoltage;
        startedAt_ = in.now;
        resetWindows();
      } else if (intentPending)
        reason = Reason::RestartGuard;
      else if (charger)
        reason = Reason::Charging;
      return;
    }
    if (charger) {
      guard(Reason::Charging, true);
      return;
    }
    // Margin covers measured voltage error and maximum rebound AFTER XIAO is cut.
    if (in.millivolts + cfg.reboundMarginMv >= cfg.restartMinMv) {
      guard(Reason::Rebound, true);
      return;
    }
    if (!stopped) {
      stable_.reset();
      haveBand_ = false;
      return;
    }
    if (!haveBand_ || in.millivolts < bandMin_ || in.millivolts > bandMax_) {
      if (!haveBand_) {
        bandMin_ = bandMax_ = in.millivolts;
        haveBand_ = true;
      } else {
        if (in.millivolts < bandMin_)
          bandMin_ = in.millivolts;
        if (in.millivolts > bandMax_)
          bandMax_ = in.millivolts;
        if (bandMax_ - bandMin_ > cfg.stableBandMv) {
          bandMin_ = bandMax_ = in.millivolts;
          stable_.reset();
        }
      }
    }
    if (!stable_.update(true, in.stamp, cfg.settleMs))
      return;
    if (state == State::Settling) {
      if (in.millivolts < cfg.saveMinMv) {
        fail(Reason::SaveVoltage); // hardware EN remains independent
        return;
      }
      const PersistResult result = port.persist();
      checkpointSaved = result.checkpoint;
      if (!result.intent) {
        fail(Reason::Persistence);
        return;
      }
      intentPending = true;
      state = State::Verify;
      epoch_ = port.nowMs();
      resetWindows();
      return; // fresh post-flash frames are required before any power removal
    }
    cutoffVoltageMv = in.millivolts;
    state = State::Asserted;
    assertedAt = port.nowMs();
    ++assertions;
    port.assertD10();
  }

private:
  struct Window {
    uint32_t since = 0;
    unsigned count = 0;
    void reset() { count = 0; }
    bool update(bool yes, uint32_t stamp, uint32_t duration) {
      if (!yes) {
        reset();
        return false;
      }
      if (!count)
        since = stamp;
      if (count < 100000)
        ++count;
      return count >= 5 && uint32_t(stamp - since) >= duration;
    }
  } low_, recovery_, stationary_, stable_;
  uint32_t epoch_ = 0, startedAt_ = 0, lastGeneration_ = 0, lastStamp_ = 0;
  uint32_t bandMin_ = 0, bandMax_ = 0;
  uint16_t lastLeft_ = 0, lastRight_ = 0;
  bool prepared_ = false, haveSample_ = false, haveWheels_ = false, haveBand_ = false;
  void resetWindows() {
    low_.reset();
    recovery_.reset();
    stationary_.reset();
    stable_.reset();
    haveWheels_ = haveBand_ = false;
  }
  void guard(Reason why, bool alreadyStopped = false) {
    state = State::BootGuard;
    reason = why;
    prepared_ = alreadyStopped;
    resetWindows();
  }
  void fail(Reason why) {
    state = State::Held;
    reason = why;
    resetWindows();
  }
};
} // namespace roonav
