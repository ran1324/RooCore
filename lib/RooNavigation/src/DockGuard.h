#pragma once
#include <cmath>
#include <cstdint>

namespace roonav {

// Geometric motion authorization, not a replacement for cliff/bumper/power guards.
// All lengths are metres, angles radians, speeds m/s and rad/s, and times ms.
// No dimension or physical safety limit is supplied by default. See docs/DOCK_GUARD.md.
class DockGuard {
public:
  struct Pose {
    float x = 0, y = 0, yaw = 0;
    Pose() = default;
    Pose(float px, float py, float angle) : x(px), y(py), yaw(angle) {}
  };
  struct MapBounds {
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool valid = false;
  };
  struct Config {
    bool configured = false, verified = false, simulationOnly = false;
    uint32_t anchorRevision = 0;
    // Front contact-face centre and outward normal. The measured rectangle,
    // including exposed cables/adapter, is local [-depth,0] x [-halfWidth,halfWidth].
    float frontX = 0, frontY = 0, yaw = 0, depth = 0, halfWidth = 0;
    // bodyRadius bounds body AND fixed attachments. margin includes measured
    // geometry/pose error, stop latency/braking and planner discretization.
    float margin = 0, bodyRadius = 0;
    // Side brush is a disk offset from the robot centre in its own frame.
    // Its bristles can snag even stopped, so ordinary exclusion always includes it.
    float sideX = 0, sideY = 0, sideRadius = 0;
    // Approved ROBOT-CENTRE corridor, already reduced by physical clearance.
    // minX >= 0 prohibits access behind the contact face. Heading points inward.
    float corridorMinX = 0, corridorMaxX = 0, corridorHalfWidth = 0;
    float headingTolerance = 0, maxLinearSpeed = 0, maxAngularSpeed = 0;
    uint32_t maxHorizonMs = 0, maxAccessMs = 0;
    uint32_t poseMaxAgeMs = 0, sensorsMaxAgeMs = 0, irMaxAgeMs = 0;
    // Numerical subdivision limits, not calibrated hardware quantities. Every
    // interval is additionally inflated by its entire arc-length bound.
    float stepMeters = 0, stepRadians = 0;
    MapBounds map;
  };
  struct Observation {
    uint32_t now = 0, poseMs = 0, sensorsMs = 0, irMs = 0;
    Pose pose;
    bool localized = false, sensorsValid = false;
    bool dockContact = false, bumper = false;
    bool irValid = false;
    uint8_t irRaw = 0;
    bool anchorMismatch = false;
  };
  enum class Mode : uint8_t { Ordinary, Approach, Docked, Departure };
  enum class Issue : uint8_t {
    None, Unconfigured, InvalidConfig, AnchorMoved, UnexpectedContact, BumperNearBase,
    StalePose, StaleSensors, InvalidPose, OutsideMap, BaseIntersection,
    OutsideCorridor, WrongDirection, BrushesActive, NoDockEvidence, NoContact,
    AccessExpired, AccessRequired, DockedHold, InvalidMotion, SweepBudget
  };
  struct IR {
    bool dock = false, forceField = false, red = false, green = false;
    bool virtualWall = false;
  };
  // iRobot 600 OI reference, 2021-04-13, pp.25-26. These are exact received
  // characters, not distance or a cross-generation bitmask. 620 support needs
  // actual qualification. No IR code ever expands the geometric corridor.
  static IR classifyIR(uint8_t raw) {
    IR r;
    switch (raw) {
    case 161: r.dock = r.forceField = true; break;
    case 164: r.dock = r.green = true; break;
    case 165: r.dock = r.green = r.forceField = true; break;
    case 168: r.dock = r.red = true; break;
    case 169: r.dock = r.red = r.forceField = true; break;
    case 172: r.dock = r.red = r.green = true; break;
    case 173: r.dock = r.red = r.green = r.forceField = true; break;
    case 162: r.virtualWall = true; break;
    default: break;
    }
    return r;
  }
  bool configure(const Config &c, bool allowSimulation = false) {
    cfg_ = c;
    mode_ = Mode::Ordinary;
    accessStart_ = 0;
    suspectedValid_ = false;
    configValid_ = c.configured && c.verified && (!c.simulationOnly || allowSimulation) && validConfig(c);
    anchorValid_ = configValid_;
    issue_ = configValid_ ? Issue::None : (c.configured ? Issue::InvalidConfig : Issue::Unconfigured);
    return configValid_;
  }
  const Config &config() const { return cfg_; }
  bool configured() const { return configValid_; }
  bool ready() const { return configValid_ && anchorValid_; }
  bool anchorValid() const { return anchorValid_; }
  uint32_t anchorRevision() const { return cfg_.anchorRevision; }
  Mode mode() const { return mode_; }
  Issue issue() const { return issue_; }
  bool hasSuspectedAnchor() const { return suspectedValid_; }
  const Pose &suspectedAnchor() const { return suspected_; }
  void invalidate(Issue reason = Issue::AnchorMoved) {
    anchorValid_ = false;
    mode_ = Mode::Ordinary;
    issue_ = reason;
  }
  // Only an independently localized suspected front face may be passed here;
  // raw IR alone cannot supply one. The original cfg_ rectangle is retained.
  void invalidateAt(const Pose &suspectedFront) {
    suspectedValid_ = finitePose(suspectedFront);
    if (suspectedValid_) suspected_ = suspectedFront;
    invalidate();
  }
  void endAccess() {
    // Expected powered contact remains known while stationary on the base.
    if (mode_ != Mode::Docked) mode_ = Mode::Ordinary;
  }
  bool freshDockIR(const Observation &o) const {
    return o.irValid && fresh(o.now, o.irMs, cfg_.irMaxAgeMs) && classifyIR(o.irRaw).dock;
  }
  void observe(const Observation &o) {
    if (!ready()) return;
    if (o.anchorMismatch) { invalidate(); return; }
    if (!o.sensorsValid || !fresh(o.now, o.sensorsMs, cfg_.sensorsMaxAgeMs)) return;
    if (o.dockContact && mode_ == Mode::Ordinary) {
      invalidate(Issue::UnexpectedContact);
      return;
    }
    if (o.bumper && o.localized && fresh(o.now, o.poseMs, cfg_.poseMaxAgeMs) &&
        finitePose(o.pose) && intersectsBase(o.pose, true, 0, 0)) {
      invalidate(Issue::BumperNearBase);
      return;
    }
    if (o.dockContact && mode_ == Mode::Approach) {
      if (uint32_t(o.now - accessStart_) >= cfg_.maxAccessMs) {
        invalidate(Issue::AccessExpired);
        return;
      }
      if (!poseChecks(o) || !contactPose(o.pose)) { invalidate(); return; }
      // This only recognizes expected geometry/contact, not completed charging.
      mode_ = Mode::Docked;
    }
  }
  bool beginApproach(const Observation &o) {
    if (mode_ == Mode::Approach) return permitsNativeDock(o); // do not renew expiry
    if (!checks(o)) return false;
    if (mode_ != Mode::Ordinary) return deny(Issue::AccessRequired);
    if (!freshDockIR(o)) return deny(Issue::NoDockEvidence);
    if (o.dockContact) { invalidate(Issue::UnexpectedContact); return false; }
    if (!corridor(o.pose, 0, 0)) return deny(Issue::OutsideCorridor);
    // Enter the exception from the clear front staging area, never from an
    // already overlapping side/back pose or a freshly restored on-base pose.
    if (intersectsBase(o.pose, false, 0, 0)) return deny(Issue::BaseIntersection);
    mode_ = Mode::Approach;
    accessStart_ = o.now;
    return allow();
  }
  // Explicit qualified local anchoring action, or the end of an Approach.
  // Do not call automatically merely because a charging-source bit is present.
  bool confirmDocked(const Observation &o) {
    if (!checksWithoutObservation(o)) return false;
    if (o.anchorMismatch) { invalidate(); return false; }
    if (mode_ != Mode::Ordinary && mode_ != Mode::Approach && mode_ != Mode::Docked)
      return deny(Issue::AccessRequired);
    if (!o.dockContact) return deny(Issue::NoContact);
    if (!contactPose(o.pose)) return deny(Issue::OutsideCorridor);
    mode_ = Mode::Docked;
    return allow();
  }
  bool beginDeparture(const Observation &o) {
    if (mode_ == Mode::Departure) return checks(o) && accessFresh(o.now);
    if (!checks(o)) return false;
    if (mode_ != Mode::Docked) return deny(Issue::AccessRequired);
    if (!o.dockContact) return deny(Issue::NoContact);
    if (!corridor(o.pose, 0, 0)) return deny(Issue::OutsideCorridor);
    mode_ = Mode::Departure;
    accessStart_ = o.now;
    return allow();
  }
  bool permitsNativeDock(const Observation &o) {
    if (!checks(o)) return false;
    if (mode_ == Mode::Docked) return deny(Issue::DockedHold);
    if (mode_ != Mode::Approach) return deny(Issue::AccessRequired);
    if (!accessFresh(o.now)) return false;
    if (!corridor(o.pose, 0, 0)) return deny(Issue::OutsideCorridor);
    // Absence is not evidence of an absent base; continue geometric protection.
    // The live supervisor requires fresh IR telemetry, but a temporary code 0
    // does not relocate the mapped base or widen the admitted corridor.
    if (!o.irValid || !fresh(o.now, o.irMs, cfg_.irMaxAgeMs)) return deny(Issue::NoDockEvidence);
    return allow();
  }
  // Planner callback. Always ordinary exclusion; routing cannot grant an access
  // permit. Invalid anchors/configurations fail closed for planning/motion only.
  bool blocksOrdinaryPose(const Pose &p, bool brushesActive = true) const {
    return !ready() || !finitePose(p) || !insideMap(p, brushesActive, 0, 0) ||
           intersectsBase(p, brushesActive, 0, 0) || intersectsSuspected(p, brushesActive);
  }
  // A grid planner does not yet know the eventual turn angle. Inflate by the
  // full orientation-independent envelope; the final gate uses the tighter
  // oriented sweep. Stopped side-brush bristles are included here too.
  bool blocksOrdinaryCenter(float x, float y) const {
    const Pose p(x, y, 0);
    if (!ready() || !finitePose(p)) return true;
    const float brushReach = cfg_.sideRadius > 0 ? std::hypot(cfg_.sideX, cfg_.sideY) + cfg_.sideRadius : 0;
    const float radius = std::fmax(cfg_.bodyRadius, brushReach) + cfg_.margin;
    if (!mapCircle(p, cfg_.bodyRadius + cfg_.margin)) return true;
    const Pose q = local(p);
    return circleBase(q.x, q.y, radius);
  }
  // Covers every possible centre in a square map cell. Checking just its four
  // corners is insufficient near a rounded inflated-obstacle boundary.
  bool blocksOrdinaryCell(float x, float y, float halfSize) const {
    const Pose p(x, y, 0);
    if (!ready() || !finitePose(p) || !std::isfinite(halfSize) || halfSize < 0) return true;
    if (!mapCircle(p, cfg_.bodyRadius + cfg_.margin + halfSize)) return true;
    const float brushReach = cfg_.sideRadius > 0 ? std::hypot(cfg_.sideX, cfg_.sideY) + cfg_.sideRadius : 0;
    const float radius = std::fmax(cfg_.bodyRadius, brushReach) + cfg_.margin + std::sqrt(2.f) * halfSize;
    const Pose q = local(p);
    return circleBase(q.x, q.y, radius);
  }
  // Useful for map display even while an invalid anchor inhibits ALL motion.
  bool footprintIntersectsBase(const Pose &p, bool brushesActive = true) const {
    return configValid_ && finitePose(p) && intersectsBase(p, brushesActive, 0, 0);
  }
  bool permitsMotion(const Observation &o, float linear, float angular,
                     uint32_t horizonMs, bool brushesActive) {
    if (!checks(o)) return false;
    if (!std::isfinite(linear) || !std::isfinite(angular) || horizonMs == 0 ||
        horizonMs > cfg_.maxHorizonMs || std::fabs(linear) > cfg_.maxLinearSpeed ||
        std::fabs(angular) > cfg_.maxAngularSpeed)
      return deny(Issue::InvalidMotion);
    if (mode_ == Mode::Docked) return deny(Issue::DockedHold);
    const bool access = mode_ == Mode::Approach || mode_ == Mode::Departure;
    if (access) {
      if (!accessFresh(o.now) || horizonMs > cfg_.maxAccessMs - uint32_t(o.now - accessStart_))
        return deny(Issue::AccessExpired);
      if (brushesActive) return deny(Issue::BrushesActive);
      if ((mode_ == Mode::Approach && linear < 0) || (mode_ == Mode::Departure && linear > 0))
        return deny(Issue::WrongDirection);
    }
    const float duration = float(horizonMs) * .001f;
    const float travel = std::fabs(linear) * duration;
    const float rotation = std::fabs(angular) * duration;
    const float intervals = std::fmax(1.f, std::fmax(std::ceil(travel / cfg_.stepMeters),
                                                  std::ceil(rotation / cfg_.stepRadians)));
    if (!std::isfinite(intervals) || intervals > float(MaxSegments)) return deny(Issue::SweepBudget);
    const unsigned n = unsigned(intervals);
    const float dt = duration / float(n);
    // The disk about an interval's start contains every centre position on that
    // interval (arc length bounds chord/displacement). Brush offset rotation is
    // bounded by |offset|*|delta yaw|. No gap between samples is untested.
    const float bodySweep = std::fabs(linear) * dt;
    const float angleSweep = std::fabs(angular) * dt;
    const float sideSweep = bodySweep + std::hypot(cfg_.sideX, cfg_.sideY) * angleSweep;
    for (unsigned j = 0; j <= n; ++j) {
      const Pose p = advance(o.pose, linear, angular, dt * float(j));
      const float bs = j == n ? 0 : bodySweep, ss = j == n ? 0 : sideSweep;
      if (!insideMap(p, brushesActive, bs, ss)) return deny(Issue::OutsideMap);
      if (access) {
        // Conservatively bound x/y and heading over the complete interval too.
        if (!corridor(p, bs, j == n ? 0 : angleSweep)) return deny(Issue::OutsideCorridor);
      } else if (intersectsBase(p, brushesActive, bs, ss)) return deny(Issue::BaseIntersection);
    }
    return allow();
  }

private:
  enum { MaxSegments = 256 };
  static constexpr float Pi = 3.14159265358979323846f;
  Config cfg_;
  bool configValid_ = false, anchorValid_ = false, suspectedValid_ = false;
  Pose suspected_;
  Mode mode_ = Mode::Ordinary;
  Issue issue_ = Issue::Unconfigured;
  uint32_t accessStart_ = 0;
  bool allow() { issue_ = Issue::None; return true; }
  bool deny(Issue why) { issue_ = why; return false; }
  static bool fresh(uint32_t now, uint32_t sample, uint32_t maxAge) {
    return maxAge > 0 && uint32_t(now - sample) <= maxAge;
  }
  static bool finitePose(const Pose &p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.yaw);
  }
  static bool validTime(uint32_t t) { return t > 0 && t < 0x80000000u; }
  static float wrap(float a) { return std::remainder(a, 2.f * Pi); }
  static bool validConfig(const Config &c) {
    const float numbers[] = {c.frontX,c.frontY,c.yaw,c.depth,c.halfWidth,c.margin,c.bodyRadius,
      c.sideX,c.sideY,c.sideRadius,c.corridorMinX,c.corridorMaxX,c.corridorHalfWidth,
      c.headingTolerance,c.maxLinearSpeed,c.maxAngularSpeed,c.stepMeters,c.stepRadians,
      c.map.minX,c.map.minY,c.map.maxX,c.map.maxY};
    for (float v : numbers) if (!std::isfinite(v)) return false;
    if (c.anchorRevision == 0 || c.depth <= 0 || c.halfWidth <= 0 || c.margin < 0 ||
        c.bodyRadius <= 0 || c.sideRadius < 0 || c.corridorMinX < 0 ||
        c.corridorMaxX <= c.corridorMinX || c.corridorHalfWidth <= 0 ||
        c.headingTolerance <= 0 || c.headingTolerance >= Pi / 2 ||
        c.maxLinearSpeed <= 0 || c.maxAngularSpeed <= 0 || c.stepMeters <= 0 ||
        c.stepRadians <= 0 || !c.map.valid || c.map.minX >= c.map.maxX || c.map.minY >= c.map.maxY ||
        !validTime(c.maxHorizonMs) || !validTime(c.maxAccessMs) ||
        !validTime(c.poseMaxAgeMs) || !validTime(c.sensorsMaxAgeMs) || !validTime(c.irMaxAgeMs))
      return false;
    // Keep the complete configured physical footprint in this map. A corridor
    // also cannot grant passage beyond the map: the swept body is checked.
    for (int k = 0; k < 4; ++k) {
      const float x = k & 1 ? -c.depth : 0, y = k & 2 ? -c.halfWidth : c.halfWidth;
      const float gx = c.frontX + x * std::cos(c.yaw) - y * std::sin(c.yaw);
      const float gy = c.frontY + x * std::sin(c.yaw) + y * std::cos(c.yaw);
      if (gx < c.map.minX || gx > c.map.maxX || gy < c.map.minY || gy > c.map.maxY) return false;
    }
    return true;
  }
  bool poseChecks(const Observation &o) {
    if (!finitePose(o.pose)) return deny(Issue::InvalidPose);
    if (!o.localized || !fresh(o.now, o.poseMs, cfg_.poseMaxAgeMs)) return deny(Issue::StalePose);
    if (!insideMap(o.pose, false, 0, 0)) return deny(Issue::OutsideMap);
    return true;
  }
  bool checksWithoutObservation(const Observation &o) {
    if (!ready()) return false; // preserve the latched invalidation diagnosis
    if (!poseChecks(o)) return false;
    if (!o.sensorsValid || !fresh(o.now, o.sensorsMs, cfg_.sensorsMaxAgeMs)) return deny(Issue::StaleSensors);
    return true;
  }
  bool checks(const Observation &o) {
    observe(o);
    return checksWithoutObservation(o);
  }
  bool accessFresh(uint32_t now) {
    if (uint32_t(now - accessStart_) >= cfg_.maxAccessMs) {
      mode_ = Mode::Ordinary;
      return deny(Issue::AccessExpired);
    }
    return true;
  }
  Pose local(const Pose &p) const {
    const float x = p.x - cfg_.frontX, y = p.y - cfg_.frontY;
    return Pose(x * std::cos(cfg_.yaw) + y * std::sin(cfg_.yaw),
                -x * std::sin(cfg_.yaw) + y * std::cos(cfg_.yaw), wrap(p.yaw - cfg_.yaw));
  }
  bool corridor(const Pose &p, float positionSweep, float angleSweep) const {
    const Pose q = local(p);
    return q.x - positionSweep >= cfg_.corridorMinX && q.x + positionSweep <= cfg_.corridorMaxX &&
           std::fabs(q.y) + positionSweep <= cfg_.corridorHalfWidth &&
           std::fabs(wrap(q.yaw - Pi)) + angleSweep <= cfg_.headingTolerance;
  }
  bool contactPose(const Pose &p) const {
    const Pose q = local(p);
    // A source bit at the far staging end is not contact with this anchor.
    // A side-brush intersection alone is not evidence of charging contacts.
    return corridor(p, 0, 0) && circleBase(q.x, q.y, cfg_.bodyRadius + cfg_.margin);
  }
  bool circleBase(float x, float y, float radius) const {
    const float dx = x < -cfg_.depth ? -cfg_.depth - x : (x > 0 ? x : 0);
    const float dy = std::fmax(std::fabs(y) - cfg_.halfWidth, 0.f);
    // hypot avoids overflow for finite but implausible incoming coordinates.
    return std::hypot(dx, dy) <= radius;
  }
  Pose brush(const Pose &p) const {
    return Pose(p.x + cfg_.sideX * std::cos(p.yaw) - cfg_.sideY * std::sin(p.yaw),
                p.y + cfg_.sideX * std::sin(p.yaw) + cfg_.sideY * std::cos(p.yaw), p.yaw);
  }
  bool intersectsBase(const Pose &p, bool /*brushes*/, float bodySweep, float sideSweep) const {
    const Pose q = local(p);
    if (circleBase(q.x, q.y, cfg_.bodyRadius + cfg_.margin + bodySweep)) return true;
    if (cfg_.sideRadius > 0) {
      const Pose b = local(brush(p));
      if (circleBase(b.x, b.y, cfg_.sideRadius + cfg_.margin + sideSweep)) return true;
    }
    return false;
  }
  bool intersectsSuspected(const Pose &p, bool /*brushes*/) const {
    if (!suspectedValid_) return false;
    // Currently anchor invalidation inhibits all motion, but retain this second
    // footprint predicate explicitly so a future recovery cannot lose it.
    const float dx = p.x - suspected_.x, dy = p.y - suspected_.y;
    const Pose relative(dx * std::cos(suspected_.yaw) + dy * std::sin(suspected_.yaw),
                        -dx * std::sin(suspected_.yaw) + dy * std::cos(suspected_.yaw),
                        wrap(p.yaw - suspected_.yaw));
    if (circleBase(relative.x, relative.y, cfg_.bodyRadius + cfg_.margin)) return true;
    if (cfg_.sideRadius > 0) {
      const Pose b = brush(relative);
      return circleBase(b.x, b.y, cfg_.sideRadius + cfg_.margin);
    }
    return false;
  }
  bool mapCircle(const Pose &p, float radius) const {
    return finitePose(p) && p.x - radius >= cfg_.map.minX && p.x + radius <= cfg_.map.maxX &&
           p.y - radius >= cfg_.map.minY && p.y + radius <= cfg_.map.maxY;
  }
  bool insideMap(const Pose &p, bool /*brushes*/, float bodySweep, float /*sideSweep*/) const {
    // Navigation owns deliberate side-bristle contact along walls. Map extent
    // bounds the swept body; the full brush disk remains protected against the
    // base/cables independently. Otherwise wall edging would become impossible.
    return mapCircle(p, cfg_.bodyRadius + cfg_.margin + bodySweep);
  }
  static Pose advance(const Pose &p, float v, float w, float seconds) {
    const float theta = w * seconds;
    // sinc midpoint form is stable for straight motion and tiny angular speed.
    const float half = theta * .5f;
    const float sinc = std::fabs(half) < .0001f ? 1.f - half * half / 6.f : std::sin(half) / half;
    const float d = v * seconds * sinc;
    return Pose(p.x + d * std::cos(p.yaw + half), p.y + d * std::sin(p.yaw + half), wrap(p.yaw + theta));
  }
};
} // namespace roonav
