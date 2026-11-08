#pragma once
#include <RoombaOI.h>

namespace roonav {
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
