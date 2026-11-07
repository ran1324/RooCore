#pragma once
#include "RooNavigation.h"
#include <stdint.h>
namespace roonav {
// One bounded assistance session. Server replies never turn off local safety.
class NavigationHelp {
public:
  enum class Status : uint8_t { None, LocalOnly, Held, Checking, Resolved };
  Status status = Status::None;
  uint32_t id = 0, frame = 0, started = 0, deadline = 0;
  Reason reason = Reason::None;
  unsigned attempts = 0, confirmations = 0;
  bool operatorHeld = false;
  void operatorPause() {
    operatorHeld = true;
    hold();
  }
  void operatorResume() { operatorHeld = false; }
  Pose proposed;
  bool pending() const {
    return status == Status::LocalOnly || status == Status::Held || status == Status::Checking;
  }
  bool begin(Reason why, uint32_t now, bool localSafe) {
    if (operatorHeld || pending() || attempts >= 2)
      return false;
    ++attempts;
    ++id;
    ++frame;
    started = now;
    deadline = now + 7200000;
    reason = why;
    status = localSafe ? Status::LocalOnly : Status::Held;
    confirmations = 0;
    return true;
  }
  bool validReply(uint32_t requested, uint32_t requestedFrame, uint32_t expires,
                  uint32_t now) const {
    return pending() && requested == id && requestedFrame == frame && int32_t(expires - now) > 0 &&
           int32_t(expires - now) <= 1500 && int32_t(deadline - now) > 0;
  }
  void hold() {
    if (pending())
      status = Status::Held;
  }
  bool allowLocal(const Input &in) {
    if (operatorHeld || status != Status::LocalOnly)
      return false;
    if (int32_t(deadline - in.now) <= 0 || !in.localized || !in.scanValid || !in.sensorsValid ||
        in.cliff || in.wheelDrop || in.bumper || in.overcurrent || in.battery <= 6 ||
        uint32_t(in.now - in.poseMs) > 300 || uint32_t(in.now - in.scanMs) > 400 ||
        uint32_t(in.now - in.sensorsMs) > 200) {
      status = Status::Held;
      return false;
    }
    return true;
  }
  bool candidate(Pose pose) {
    if (operatorHeld || !pending() || !isfinite(pose.x) || !isfinite(pose.y) || !isfinite(pose.yaw))
      return false;
    proposed = pose;
    confirmations = 0;
    status = Status::Checking;
    return true;
  }
  bool confirm(bool consistent) {
    if (status != Status::Checking)
      return false;
    if (!consistent) {
      confirmations = 0;
      status = Status::Held;
      return false;
    }
    if (++confirmations < 3)
      return false;
    status = Status::Resolved;
    return true;
  }
};
} // namespace roonav
