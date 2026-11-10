#pragma once
#include <math.h>
#include <stdint.h>

namespace roonav {
// Map-frame geometry. Names/IDs live in the host document, never affect safety.
struct MapZone {
  enum Kind : uint8_t { Room, NoGo, Wall, Carpet };
  Kind kind = Room;
  float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  bool enabled = true, present = true, boost = true;
  uint8_t extraPasses = 0;
  bool valid() const {
    return kind <= Carpet && isfinite(x1) && isfinite(y1) && isfinite(x2) &&
           isfinite(y2) && extraPasses <= 2 &&
           (kind == Wall ? hypotf(x2 - x1, y2 - y1) > .001f : x2 > x1 && y2 > y1);
  }
  float distanceTo(float x, float y) const {
    if (kind != Wall)
      return hypotf(fmaxf(x1 - x, fmaxf(0, x - x2)),
                    fmaxf(y1 - y, fmaxf(0, y - y2)));
    float dx = x2 - x1, dy = y2 - y1;
    float t = fmaxf(0, fminf(1, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)));
    return hypotf(x - x1 - t * dx, y - y1 - t * dy);
  }
};

class MapZones {
public:
  static constexpr unsigned Capacity = 32;
  MapZone items[Capacity];
  unsigned count = 0;
  bool hasBarriers() const {
    for (unsigned i = 0; i < count; ++i)
      if (items[i].enabled && (items[i].kind == MapZone::NoGo || items[i].kind == MapZone::Wall))
        return true;
    return false;
  }
  bool blocked(float x, float y, float clearance) const {
    if (!isfinite(x) || !isfinite(y) || !isfinite(clearance) || clearance < 0)
      return true;
    for (unsigned i = 0; i < count; ++i) {
      const MapZone &z = items[i];
      if (z.enabled && (z.kind == MapZone::NoGo || z.kind == MapZone::Wall) &&
          z.distanceTo(x, y) <= clearance)
        return true;
    }
    return false;
  }
  bool boostAt(float x, float y) const {
    if (blocked(x, y, 0))
      return false;
    for (unsigned i = 0; i < count; ++i) {
      const MapZone &z = items[i];
      if (z.enabled && z.kind == MapZone::Carpet && z.present && z.boost &&
          z.distanceTo(x, y) == 0)
        return true;
    }
    return false;
  }
  uint8_t extraAt(float x, float y) const {
    if (blocked(x, y, 0))
      return 0;
    uint8_t result = 0;
    for (unsigned i = 0; i < count; ++i) {
      const MapZone &z = items[i];
      if (z.enabled && z.kind == MapZone::Carpet && z.present &&
          z.distanceTo(x, y) == 0 && z.extraPasses > result)
        result = z.extraPasses; // overlaps take the strongest policy, never add
    }
    return result;
  }
  uint8_t maxExtra() const {
    uint8_t result = 0;
    for (unsigned i = 0; i < count; ++i)
      if (items[i].enabled && items[i].kind == MapZone::Carpet && items[i].present &&
          items[i].extraPasses > result)
        result = items[i].extraPasses;
    return result;
  }
};
} // namespace roonav
