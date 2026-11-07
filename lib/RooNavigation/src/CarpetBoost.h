#pragma once
#include <stdint.h>

namespace roonav {
// Load-based floor estimate, NOT a dedicated carpet detector. Defaults cannot boost.
class CarpetBoost {
public:
  enum class Surface : uint8_t { Unknown, HardEstimate, CarpetEstimate };
  struct Band {
    int minimumMa = 0, exitMa = 0, enterMa = 0, maximumMa = 0;
    bool valid() const {
      return minimumMa > 0 && minimumMa < exitMa && exitMa < enterMa && enterMa < maximumMa &&
             maximumMa <= 32767;
    }
  };
  struct Config {
    bool enabled = false;
    Band daily, deep; // independently measured at brush PWM 85 and 127
    unsigned minimumMv = 0, maximumMv = 0;
  } cfg;
  struct Sample {
    uint32_t now = 0, stamp = 0;
    int currentMa = 0;
    unsigned batteryMv = 0;
    uint8_t brushPwm = 0;
    bool valid = false, safe = false, movingStraight = false;
  };
  Surface surface = Surface::Unknown;
  uint32_t enteredAt = 0, exitedAt = 0;
  unsigned entries = 0, exits = 0;
  bool configured() const {
    return cfg.enabled && cfg.daily.valid() && cfg.deep.valid() && cfg.minimumMv >= 8000 &&
           cfg.maximumMv > cfg.minimumMv && cfg.maximumMv <= 25000;
  }
  void invalidate() {
    surface = Surface::Unknown;
    candidate_ = Surface::Unknown;
    samples_ = 0;
    eligible_ = false;
    haveSample_ = false;
    warming_ = false;
  }
  void update(const Sample &s) {
    const Band &band = s.brushPwm == 85 ? cfg.daily : cfg.deep;
    if (!configured() || !s.valid || !s.safe || !s.movingStraight ||
        (s.brushPwm != 85 && s.brushPwm != 127) || uint32_t(s.now - s.stamp) > 200 ||
        s.batteryMv < cfg.minimumMv || s.batteryMv > cfg.maximumMv ||
        s.currentMa < band.minimumMa || s.currentMa > band.maximumMa) {
      invalidate();
      return;
    }
    if (!warming_ || brushPwm_ != s.brushPwm) {
      invalidate();
      warming_ = true;
      warmAt_ = s.now;
      brushPwm_ = s.brushPwm;
    }
    if (uint32_t(s.now - warmAt_) < 1000)
      return; // ignore brush startup/PWM changes
    if (haveSample_ && s.stamp == lastSample_)
      return; // never count a polled sample twice
    if (haveSample_ && uint32_t(s.stamp - lastSample_) > 200) {
      invalidate(); // gap cannot establish a sustained transition
      return;
    }
    lastSample_ = s.stamp;
    haveSample_ = eligible_ = true;
    Surface next = s.currentMa >= band.enterMa  ? Surface::CarpetEstimate
                   : s.currentMa <= band.exitMa ? Surface::HardEstimate
                                                : Surface::Unknown;
    if (next == Surface::Unknown || next == surface) {
      candidate_ = Surface::Unknown;
      samples_ = 0;
      return; // hysteresis retains the last estimate in the intermediate band
    }
    if (next != candidate_) {
      candidate_ = next;
      candidateAt_ = s.stamp;
      samples_ = 0;
    }
    if (samples_ < 255)
      ++samples_;
    uint32_t dwell = next == Surface::CarpetEstimate ? 1500 : 2500;
    if (samples_ < 8 || uint32_t(s.stamp - candidateAt_) < dwell)
      return;
    if (next == Surface::CarpetEstimate) {
      ++entries;
      enteredAt = s.now;
    } else if (surface == Surface::CarpetEstimate) {
      ++exits;
      exitedAt = s.now;
    }
    surface = next;
    candidate_ = Surface::Unknown;
    samples_ = 0;
  }
  uint8_t vacuum(uint8_t requested, uint8_t brushPwm, uint32_t now) const {
    return requested && configured() && eligible_ && surface == Surface::CarpetEstimate &&
                   brushPwm == brushPwm_ && uint32_t(now - lastSample_) <= 200
               ? 127
               : requested;
  }

private:
  Surface candidate_ = Surface::Unknown;
  uint32_t candidateAt_ = 0, lastSample_ = 0, warmAt_ = 0;
  uint8_t brushPwm_ = 0, samples_ = 0;
  bool eligible_ = false, haveSample_ = false, warming_ = false;
};
} // namespace roonav
