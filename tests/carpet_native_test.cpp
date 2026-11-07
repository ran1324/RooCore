#include "CarpetBoost.h"
#include <cassert>
#include <cstdio>
using roonav::CarpetBoost;
// Synthetic thresholds exercise the policy; NEVER use these to commission a robot.
static CarpetBoost configured() {
  CarpetBoost b;
  b.cfg.enabled = true;
  b.cfg.daily.minimumMa = 50;
  b.cfg.daily.exitMa = 200;
  b.cfg.daily.enterMa = 300;
  b.cfg.daily.maximumMa = 600;
  b.cfg.deep.minimumMa = 80;
  b.cfg.deep.exitMa = 350;
  b.cfg.deep.enterMa = 500;
  b.cfg.deep.maximumMa = 900;
  b.cfg.minimumMv = 14000;
  b.cfg.maximumMv = 16000;
  return b;
}
int main() {
  CarpetBoost b = configured();
  CarpetBoost::Sample s;
  s.valid = s.safe = s.movingStraight = true;
  s.brushPwm = 85;
  s.batteryMv = 15000;
  s.currentMa = 350;
  auto tick = [&](unsigned ms) {
    s.now = s.stamp = ms;
    b.update(s);
  };
  tick(100);
  tick(200);
  assert(b.vacuum(85, 85, 200) == 85);
  for (unsigned t = 300; t <= 2600; t += 100)
    tick(t);
  assert(b.surface == CarpetBoost::Surface::CarpetEstimate && b.entries == 1);
  assert(b.vacuum(85, 85, 2600) == 127 && b.vacuum(0, 85, 2600) == 0);
  assert(b.vacuum(85, 127, 2600) == 85); // do not reuse a different PWM's baseline
  s.currentMa = 250;
  tick(2700);
  assert(b.surface == CarpetBoost::Surface::CarpetEstimate);
  s.currentMa = 150;
  for (unsigned t = 2800; t < 5300; t += 100)
    tick(t);
  assert(b.surface == CarpetBoost::Surface::CarpetEstimate && b.exits == 0);
  tick(5300);
  assert(b.surface == CarpetBoost::Surface::HardEstimate && b.exits == 1);
  assert(b.vacuum(85, 85, 5300) == 85);
  s.currentMa = 350;
  tick(5400);
  for (unsigned k = 0; k < 100; ++k)
    b.update(s);
  assert(b.entries == 1); // repeated poll is not fresh evidence
  s.now = 5700;
  b.update(s);
  assert(b.surface == CarpetBoost::Surface::Unknown);
  s.currentMa = 700;
  tick(5800);
  assert(b.surface == CarpetBoost::Surface::Unknown); // overload, not carpet
  s.currentMa = 350;
  s.safe = false;
  tick(5900);
  assert(b.vacuum(85, 85, 5900) == 85);
  s.safe = true;
  s.movingStraight = false;
  tick(6000);
  assert(b.surface == CarpetBoost::Surface::Unknown);
  s.movingStraight = true;
  s.batteryMv = 13000;
  tick(6100);
  assert(b.surface == CarpetBoost::Surface::Unknown);
  s.batteryMv = 15000;
  s.brushPwm = 127;
  s.currentMa = 650;
  for (unsigned t = 6200; t <= 8800; t += 100)
    tick(t);
  assert(b.surface == CarpetBoost::Surface::CarpetEstimate && b.entries == 2);
  assert(b.vacuum(127, 127, 8800) == 127); // deep mode already runs maximum vacuum
  assert(b.vacuum(85, 127, 9100) == 85);   // expiry does not depend on another update call
  b.invalidate();
  assert(b.exits == 1); // losing evidence is not a fabricated carpet exit
  b.cfg.daily.exitMa = b.cfg.daily.enterMa;
  assert(!b.configured());
  CarpetBoost disabled;
  assert(!disabled.configured() && disabled.vacuum(85, 85, 0) == 85);
  puts("Carpet estimation policy tests passed");
}
