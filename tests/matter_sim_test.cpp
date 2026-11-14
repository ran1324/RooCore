#include "../firmware/xiao/RooCoreMatterSim/SimVacuum.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace roocore_sim;

static void cycle() {
  Vacuum v;
  assert(v.state() == State::Docked && v.runMode() == 0);
  assert(v.mode(1, 0) == Result::Offline);
  v.network(true);
  assert(v.mode(1, 100) == Result::Ok);
  assert(v.state() == State::Running && v.runMode() == 1);
  assert(!v.tick(30099));
  assert(v.tick(30100));
  assert(v.state() == State::SeekingCharger && v.runMode() == 0);
  assert(!v.tick(35099));
  assert(v.tick(35100));
  assert(v.state() == State::Docked);
  assert(!v.tick(99999));
}
static void pauseResume() {
  Vacuum v;
  v.network(true);
  assert(v.pause(0) == Result::InvalidState);
  assert(v.resume(0) == Result::InvalidState);
  v.mode(1, 0);
  assert(v.pause(12000) == Result::Ok);
  assert(v.state() == State::Paused && v.runMode() == 1);
  assert(v.pause(19000) == Result::Ok);
  assert(!v.tick(90000));
  assert(v.mode(1, 90000) == Result::Ok && v.state() == State::Paused);
  assert(v.resume(100000) == Result::Ok);
  assert(v.resume(100100) == Result::Ok);
  assert(!v.tick(117999));
  assert(v.tick(118000));
  assert(v.pause(119000) == Result::Ok && v.runMode() == 0);
  assert(v.resume(200000) == Result::Ok);
  assert(!v.tick(203999));
  assert(v.tick(204000) && v.state() == State::Docked);
}
static void cancelAndDock() {
  Vacuum v;
  v.network(true);
  v.mode(1, 0);
  assert(v.mode(1, 20000) == Result::Ok);
  assert(v.tick(30000));  // duplicate start must not extend the run
  assert(v.mode(1, 30001) == Result::InvalidState);
  assert(v.mode(0, 30002) == Result::Ok);
  assert(v.state() == State::SeekingCharger);  // already Idle; same-mode requests do not invoke the delegate
  assert(v.tick(35000) && v.state() == State::Docked);
  v.mode(1, 40000);
  assert(v.mode(0, 41000) == Result::Ok);
  assert(v.state() == State::Stopped);
  assert(!v.tick(100000));
  assert(v.goHome(100000) == Result::Ok);
  assert(v.goHome(103000) == Result::Ok);
  assert(v.tick(105000));  // duplicate GoHome must not extend return
  assert(v.goHome(105001) == Result::Ok && v.state() == State::Docked);
  assert(v.mode(0, 105002) == Result::Ok && v.state() == State::Docked);
  v.mode(1, 110000);
  v.pause(111000);
  assert(v.goHome(112000) == Result::Ok && v.runMode() == 0);
  assert(v.tick(117000) && v.state() == State::Docked);
  assert(v.mode(255, 120000) == Result::UnsupportedMode);
  assert(v.state() == State::Docked);
}
static void networkAndRestart() {
  for (int scenario = 0; scenario < 3; ++scenario) {
    Vacuum v;
    v.network(true);
    v.mode(1, 0);
    if (scenario == 1) v.pause(1000);
    if (scenario == 2) v.goHome(1000);
    v.network(false);
    assert(v.state() == State::Stopped && v.runMode() == 0);
    assert(v.mode(1, 2000) == Result::Offline);
    assert(v.pause(2000) == Result::Offline);
    assert(v.resume(2000) == Result::Offline);
    assert(v.goHome(2000) == Result::Offline);
    assert(!v.tick(90000));
    v.network(true);
    assert(v.state() == State::Stopped && !v.tick(120000));
    assert(v.resume(120000) == Result::InvalidState);
    assert(v.mode(1, 120000) == Result::Ok);  // explicit fresh start is required
    v.network(true);
    assert(v.state() == State::Running);  // duplicate connected event is harmless
    v = Vacuum();
    assert(v.state() == State::Docked && v.runMode() == 0 && !v.online());
  }
}
static void clockWrap() {
  Vacuum v;
  v.network(true);
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 10000;
  v.mode(1, start);
  assert(!v.tick(start + 29999));
  assert(v.tick(start + 30000));
  assert(v.tick(start + 35000));
  assert(v.state() == State::Docked);
  v.mode(1, start);
  v.pause(start + 5000);
  v.resume(start + 20000);
  assert(!v.tick(start + 44999));
  assert(v.tick(start + 45000));
}
int main() {
  cycle(); pauseResume(); cancelAndDock(); networkAndRestart(); clockWrap();
  std::cout << "PASS: cycle, pause/resume, cancel/dock, disconnect/restart, clock wrap\n";
}
