#include <cassert>
#include "recorder_core.h"
using namespace roocore;
int main() {
  static_assert(TX_PIN==1 && RX_PIN==3 && BRC_PIN==2,"upstream pin mapping");
  BrcClock clock;
  assert(!clock.update(59999));
  assert(clock.update(60000) && clock.low);
  assert(!clock.update(60999));
  assert(clock.update(61000) && !clock.low);
  assert(!clock.update(119999));
  assert(clock.update(120000) && clock.low);
  BrcClock rollover; rollover.cycle=0xffff0000;
  assert(rollover.update(uint32_t(0xffff0000u+60000)) && rollover.low);
  assert(rollover.update(uint32_t(0xffff0000u+61000)) && !rollover.low);
  assert(parseCommand("START wheels")==Command::StartWheels);
  assert(parseCommand("START basic")==Command::StartBasic);
  assert(parseCommand("READY")==Command::Ready);
  assert(parseCommand("STOP")==Command::Stop);
  assert(parseCommand("PING")==Command::Ping);
  assert(parseCommand("DRIVE 200")==Command::Invalid);
  assert(parseCommand("START wheels extra")==Command::Invalid);
  uint8_t out[32];
  size_t n=streamCommand(true,out);
  assert(out[0]==148 && out[1]==20 && n==24);
  assert(out[20]==43 && out[21]==44 && out[22]==150 && out[23]==1);
  n=streamCommand(false,out);
  assert(n==22 && out[1]==18 && out[20]==150 && out[21]==1);
}
