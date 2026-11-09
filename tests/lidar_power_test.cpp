#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "LidarPowerSequence.h"

// The electrical invariant is independent of the expected call list: no host
// HIGH/pullup may survive a deliberate rail-off. Detach alone is NOT assumed safe.
struct ElectricalModel {
  bool power = false, txSources = false, rxPullup = false, uart = false;
  bool failStart = false, busy = false;
  uint32_t now = 0, earliestUart = 0;
  int writes = 0, starts = 0, disables = 0;
  std::vector<uint8_t> rx;
  void invariant() { assert(power || (!txSources && !rxPullup)); }
  void endUart() { uart = false; rx.clear(); }
  void safeSignals() { txSources = rxPullup = false; invariant(); }
  void setPower(bool on) {
    power = on;
    if (!on) ++disables;
    invariant();
  }
  bool startUart() {
    assert(power && int32_t(now - earliestUart) >= 0);
    ++starts;
    txSources = true; // even failed driver setup could have enabled idle HIGH
    invariant();
    uart = !failStart;
    return uart;
  }
  bool drainTx() { assert(power && uart); return !busy; }
  size_t write(const uint8_t *, size_t n) {
    assert(power && uart);
    ++writes;
    return n;
  }
  int available() { assert(uart); return int(rx.size()); }
  int read() {
    assert(uart);
    if (rx.empty()) return -1;
    int b = rx.back(); rx.pop_back(); return b;
  }
};

int main() {
  ElectricalModel io;
  LidarPowerSequence<ElectricalModel> power(io);
  using State = LidarPowerSequence<ElectricalModel>::State;
  const uint8_t command[] = {0xa5, 0x60}; // no hardware transport in this test
  assert(!power.requestOn(0, 10));
  assert(power.write(command, 2) == 0);
  power.begin();
  assert(!power.requestOn(0, 0));
  assert(!power.requestOn(0, uint32_t(INT32_MAX) + 1));
  assert(!io.power && io.starts == 0);

  for (unsigned i = 0; i < 100; ++i) {
    io.now = 1000 * i;
    io.earliestUart = io.now + 25; // synthetic interval, never a sensor calibration
    assert(power.requestOn(io.now, 25));
    assert(power.state() == State::Warming && !io.txSources);
    assert(power.write(command, 2) == 0);
    assert(!power.poll(io.now + 24));
    assert(power.requestOn(io.now + 24, 25)); // cannot extend/reset the rising timer
    io.now += 25;
    assert(power.poll(io.now));
    assert(!power.poll(io.now)); // exactly one start transition/scan command
    assert(power.write(command, 2) == 2);
    assert(power.write(nullptr, 2) == 0);
    assert(power.write(command, 65) == 0);
    io.rx.push_back(42);
    assert(power.available() == 1 && power.read() == 42);
    io.rx.push_back(99); // stale RX must not survive the next power cycle
    assert(power.powerOff());
    assert(!io.power && !io.txSources && !io.rxPullup && io.rx.empty());
    assert(power.write(command, 2) == 0);
    assert(power.available() == 0 && power.read() == -1);
    assert(!power.poll(io.now + 50));
    assert(power.powerOff());
  }
  assert(io.starts == 100 && io.writes == 100);

  // Cancel startup before UART attachment; a later tick must never resurrect it.
  assert(power.requestOn(5000, 25));
  assert(power.powerOff());
  assert(!power.poll(6000) && io.starts == 100);

  // Unsigned time comparison survives the millis wrap boundary.
  io.now = UINT32_MAX - 9;
  io.earliestUart = 10;
  assert(power.requestOn(io.now, 20));
  assert(!power.poll(9));
  io.now = 10;
  assert(power.poll(io.now));
  io.busy = true;
  assert(!power.powerOff()); // cancels busy TX without waiting on robot safety loop
  assert(!io.power && !io.txSources);
  io.busy = false;

  io.failStart = true;
  io.now = 100; io.earliestUart = 110;
  assert(power.requestOn(io.now, 10));
  io.now = 110;
  assert(!power.poll(io.now));
  assert(power.state() == State::Fault && !io.power && !io.txSources);
  int starts = io.starts;
  assert(!power.requestOn(200, 10) && !power.poll(300));
  assert(io.starts == starts); // a periodic charge policy cannot make a retry loop
  power.powerOff();
  io.failStart = false;
  assert(power.requestOn(200, 10));
  io.now = io.earliestUart = 210;
  assert(power.poll(io.now));
  power.abort(); // scan-command failure follows the same ordered shutdown
  assert(!io.power && power.state() == State::Fault);
  assert(!power.requestOn(220, 10));
  power.begin();
  assert(!io.power && power.state() == State::Off);
  assert(power.requestOn(300, 10));
  power.begin(); // reinitialization while warming also cancels all future activity
  assert(!power.poll(400) && !io.power);
  std::cout << "PASS: LiDAR off-source invariant; 100 nonblocking cycles; startup cancellation; "
               "wraparound; busy TX; stale RX; failure latch; write guards; reinitialization\n";
}
