#pragma once
#include <cstdint>

// Pure, deterministic simulation. No device drivers or actuator interface exist here.
namespace roocore_sim {
enum class State : uint8_t { Stopped = 0, Running = 1, Paused = 2, SeekingCharger = 0x40, Docked = 0x42 };
enum class Result { Ok, InvalidState, Offline, UnsupportedMode };

class Vacuum {
 public:
  // Test fixtures, not Roomba timings or charging measurements.
  static constexpr uint32_t kCleaningMs = 30000;
  static constexpr uint32_t kReturnMs = 5000;
  State state() const { return state_; }
  uint8_t runMode() const {
    return state_ == State::Running || (state_ == State::Paused && resume_ == State::Running) ? 1 : 0;
  }
  bool online() const { return online_; }
  void network(bool available) {
    online_ = available;
    if (!available) {
      state_ = State::Stopped;
      remaining_ = 0;
      resume_ = State::Stopped;
    }
  }
  Result mode(uint8_t mode, uint32_t now) {
    if (mode > 1) return Result::UnsupportedMode;
    if (!online_) return Result::Offline;
    // Matter ModeBase acknowledges an unchanged mode without calling its delegate.
    // Returning home already has Idle run mode; another Idle request cannot cancel it.
    if (mode == runMode()) return Result::Ok;
    if (mode == 0) {
      if (state_ != State::Docked) state_ = State::Stopped;
      remaining_ = 0;
      return Result::Ok;
    }
    // Repeated start never extends a running job or silently resumes a paused one.
    if (state_ != State::Stopped && state_ != State::Docked) return Result::InvalidState;
    enter(State::Running, now, kCleaningMs);
    return Result::Ok;
  }
  Result pause(uint32_t now) {
    if (!online_) return Result::Offline;
    if (state_ == State::Paused) return Result::Ok;
    if (state_ != State::Running && state_ != State::SeekingCharger) return Result::InvalidState;
    const uint32_t elapsed = now - since_;
    remaining_ = elapsed >= remaining_ ? 0 : remaining_ - elapsed;
    resume_ = state_;
    state_ = State::Paused;
    return Result::Ok;
  }
  Result resume(uint32_t now) {
    if (!online_) return Result::Offline;
    if (state_ == State::Running || state_ == State::SeekingCharger) return Result::Ok;
    if (state_ != State::Paused) return Result::InvalidState;
    state_ = resume_;
    since_ = now;
    return Result::Ok;
  }
  Result goHome(uint32_t now) {
    if (!online_) return Result::Offline;
    if (state_ == State::Docked || state_ == State::SeekingCharger) return Result::Ok;
    enter(State::SeekingCharger, now, kReturnMs);
    return Result::Ok;
  }
  bool tick(uint32_t now) {
    if (!online_ || (state_ != State::Running && state_ != State::SeekingCharger)) return false;
    if (uint32_t(now - since_) < remaining_) return false;
    if (state_ == State::Running) enter(State::SeekingCharger, now, kReturnMs);
    else enter(State::Docked, now, 0);
    return true;
  }
 private:
  void enter(State state, uint32_t now, uint32_t duration) {
    state_ = state;
    since_ = now;
    remaining_ = duration;
  }
  State state_ = State::Docked;
  State resume_ = State::Stopped;
  bool online_ = false;
  uint32_t since_ = 0;
  uint32_t remaining_ = 0;
};
}  // namespace roocore_sim
