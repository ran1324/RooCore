#pragma once
#include <stdint.h>
namespace roonav {
enum class Owner : uint8_t { Local, Remote, Stopping, Held };
// Only this arbiter chooses the motor-command owner. All times are MCU millis.
class ControlArbiter {
public:
  Owner owner = Owner::Local;
  uint32_t epoch = 1;
  int left = 0, right = 0, pwm = 0;
  bool takeover(uint32_t now, bool healthy, bool missionActive) {
    if (!healthy || !missionActive || owner != Owner::Local)
      return false;
    ++epoch;
    lastSequence_ = 0;
    haveSequence_ = false;
    deadline_ = now + 200;
    owner = Owner::Remote;
    zero();
    return true;
  }
  bool command(uint32_t requestedEpoch, uint32_t sequence, uint32_t expires, int l, int r,
               int power, uint32_t now) {
    const int32_t remaining = int32_t(expires - now);
    if (owner != Owner::Remote || requestedEpoch != epoch || remaining <= 0 || remaining > 250 ||
        (haveSequence_ &&
         (sequence == lastSequence_ || uint32_t(sequence - lastSequence_) >= 0x80000000u)) ||
        l < -250 || l > 250 || r < -250 || r > 250 || power < 0 || power > 127)
      return false;
    lastSequence_ = sequence;
    haveSequence_ = true;
    deadline_ = expires;
    left = l;
    right = r;
    pwm = power;
    return true;
  }
  // Require a settled stop before automatic fallback. Remote reconnect never
  // re-arms itself: a new explicit takeover increments the session epoch.
  void poll(uint32_t now, bool localHealthy, bool missionActive) {
    if (owner == Owner::Remote && (!localHealthy || int32_t(now - deadline_) >= 0)) {
      zero();
      ++epoch;
      owner = Owner::Stopping;
      stoppedAt_ = now;
    }
    if (owner == Owner::Stopping && uint32_t(now - stoppedAt_) >= 500) {
      owner = localHealthy && missionActive ? Owner::Local : Owner::Held;
      zero();
    }
  }
  void local(uint32_t now) {
    zero();
    ++epoch;
    stoppedAt_ = now;
    owner = Owner::Stopping;
  }
  void hold() {
    zero();
    ++epoch;
    owner = Owner::Held;
  }

private:
  void zero() { left = right = pwm = 0; }
  uint32_t deadline_ = 0, lastSequence_ = 0, stoppedAt_ = 0;
  bool haveSequence_ = false;
};
} // namespace roonav
