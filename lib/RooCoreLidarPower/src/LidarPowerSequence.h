#pragma once
#include <stddef.h>
#include <stdint.h>

// One task owns this object and the three pins. No concurrent calls, callbacks,
// direct Serial1 use, or deep sleep while active. IO is an internal adapter.
template <typename IO> class LidarPowerSequence {
 public:
  enum class State { Off, Warming, Ready, Fault };
  explicit LidarPowerSequence(IO &io) : io_(io) {}

  void begin() {
    state_ = State::Off;
    io_.endUart();
    io_.safeSignals();           // TX = 0, RX input pulldown; no pullups
    io_.setPower(false);         // only AFTER signals are safe
    begun_ = true;
  }

  // Delay must be qualified on the assembled rail, not guessed from baud rate.
  // Zero is deliberately unusable: the example stays off until commissioned.
  bool requestOn(uint32_t now, uint32_t qualifiedRiseDelayMs) {
    if (!begun_ || qualifiedRiseDelayMs == 0 || qualifiedRiseDelayMs > INT32_MAX ||
        state_ == State::Fault) return false;
    if (state_ == State::Warming || state_ == State::Ready) return true;
    io_.safeSignals();
    io_.setPower(true);
    riseMs_ = qualifiedRiseDelayMs;
    onAt_ = now;
    state_ = State::Warming;
    return true;
  }

  // Nonblocking: the robot control task must continue polling sensors and stops
  // while the LiDAR supply rises. Returns true only on the transition to Ready.
  bool poll(uint32_t now) {
    if (state_ != State::Warming || uint32_t(now - onAt_) < riseMs_) return false;
    if (!io_.startUart()) {
      abort();
      return false;
    }
    state_ = State::Ready;
    return true;
  }

  // false reports a drain timeout, but still completes the ordered shutdown.
  bool powerOff() {
    if (state_ == State::Off) return true;
    const bool wasReady = ready();
    state_ = State::Off;         // stop admitting application writes
    const bool drained = !wasReady || io_.drainTx();
    disable();
    return drained;
  }

  // Latch initialization/command failure. Repeated requestOn cannot cycle power;
  // an explicit powerOff (or begin) must clear the fault before a new request.
  void abort() {
    state_ = State::Fault;
    disable();
  }

  size_t write(const uint8_t *data, size_t count) {
    // Commands only, not an arbitrary bulk/console channel. Bounds time spent
    // in the unbuffered writer: 64 * 10 / 230400 = 2.78 ms on the wire.
    if (!ready() || !data || count == 0 || count > 64) return 0;
    return io_.write(data, count);
  }
  int available() { return ready() ? io_.available() : 0; }
  int read() { return ready() ? io_.read() : -1; }
  bool ready() const { return state_ == State::Ready; }
  bool powered() const { return state_ == State::Warming || ready(); }
  State state() const { return state_; }

 private:
  IO &io_;
  void disable() {
    io_.endUart();               // detach GPIO matrix; cancel queued output
    io_.safeSignals();           // GPIO TX LOW, RX input pulldown
    io_.setPower(false);         // only now lower Pololu ON
  }
  bool begun_ = false;
  State state_ = State::Off;
  uint32_t riseMs_ = 0, onAt_ = 0;
};

