#pragma once
#include <RoombaOI.h>

namespace roonav {
// Revised policy: stop in Safe first. Releasing to Passive is a separate,
// physically qualified choice; emergency Seek Dock enters Passive itself.
inline bool stopRoombaForPowerHold(roocore::Roomba &robot, roocore::Transport &link,
                                  bool passive) {
  if (robot.enterSafe() != roocore::Result::Ok || robot.stopAll() != roocore::Result::Ok)
    return false;
  if (passive && robot.beginPassive() != roocore::Result::Ok)
    return false;
  return link.finishTransmit(50);
}
// Shared by firmware and host transport tests. Safe takeover stops native docking;
// zero wheel/cleaning commands precede Passive so the original charger can operate.
inline bool stopRoombaForPowerCutoff(roocore::Roomba &robot, roocore::Transport &link) {
  if (robot.enterSafe() != roocore::Result::Ok)
    return false;
  if (robot.releaseControl() != roocore::Result::Ok)
    return false;
  return link.finishTransmit(50);
}
} // namespace roonav
