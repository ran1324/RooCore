#include "DockGuard.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using roonav::DockGuard;
using Pose = DockGuard::Pose;
using Issue = DockGuard::Issue;
static const float pi = 3.14159265358979323846f;

// Every dimension/time/speed in this file is a synthetic test fixture, not
// Roomba/Home Base measurements, installation settings or commissioning limits.
static DockGuard::Config fixture() {
  DockGuard::Config c;
  c.configured = c.verified = c.simulationOnly = true;
  c.anchorRevision = 7;
  c.depth = .25f;
  c.halfWidth = .15f;
  c.bodyRadius = .17f;
  c.margin = .02f;
  c.sideX = .12f;
  c.sideY = -.13f;
  c.sideRadius = .10f;
  c.corridorMinX = .08f;
  c.corridorMaxX = 1.2f;
  c.corridorHalfWidth = .12f;
  c.headingTolerance = .2f;
  c.maxLinearSpeed = .6f;
  c.maxAngularSpeed = 4.f;
  c.maxHorizonMs = 5000;
  c.maxAccessMs = 30000;
  c.poseMaxAgeMs = c.sensorsMaxAgeMs = 300;
  c.irMaxAgeMs = 500;
  c.stepMeters = .01f;
  c.stepRadians = .04f;
  c.map.minX = c.map.minY = -3.f;
  c.map.maxX = c.map.maxY = 3.f;
  c.map.valid = true;
  return c;
}
static DockGuard makeGuard() {
  DockGuard g;
  assert(g.configure(fixture(), true));
  return g;
}
static DockGuard::Observation observation(Pose p = Pose(.8f, 0, pi), uint32_t now = 1000) {
  DockGuard::Observation o;
  o.now = o.poseMs = o.sensorsMs = o.irMs = now;
  o.pose = p;
  o.localized = o.sensorsValid = o.irValid = true;
  o.irRaw = 173;
  return o;
}
static void configuration() {
  DockGuard g;
  auto c = fixture();
  assert(!g.ready());
  assert(g.blocksOrdinaryPose(Pose(2,2,0)));
  assert(!g.permitsMotion(observation(), .1f, 0, 100, false));
  assert(!g.beginApproach(observation()));
  assert(!g.configure(c)); // synthetic data cannot activate physical defaults
  assert(g.configure(c, true));
  c.verified = false;
  assert(!g.configure(c, true));
  c = fixture(); c.bodyRadius = 0;
  assert(!g.configure(c, true));
  c = fixture(); c.margin = -1;
  assert(!g.configure(c, true));
  c = fixture(); c.frontX = std::numeric_limits<float>::quiet_NaN();
  assert(!g.configure(c, true));
  c = fixture(); c.headingTolerance = pi;
  assert(!g.configure(c, true));
  c = fixture(); c.poseMaxAgeMs = 0x80000000u;
  assert(!g.configure(c, true));
  c = fixture(); c.frontX = 3.1f;
  assert(!g.configure(c, true)); // physical base itself outside the map
  c = fixture(); c.map.valid = false;
  assert(!g.configure(c, true));
  c = fixture(); c.anchorRevision = 0;
  assert(!g.configure(c, true));
  c = fixture(); c.simulationOnly = false;
  assert(g.configure(c)); // explicit verified physical config, still only test data here
}
static void ordinaryGeometry() {
  auto g = makeGuard();
  assert(g.blocksOrdinaryPose(Pose(-.10f,0,0)));
  assert(g.blocksOrdinaryPose(Pose(-.1f,.32f,pi)));
  assert(!g.blocksOrdinaryPose(Pose(-.1f,.60f,pi)));
  // Planner cells account for future turns, while the final gate can use a
  // specific clear orientation. This centre is safe now but unsafe to rotate.
  assert(!g.blocksOrdinaryPose(Pose(-.10f,.38f,pi/2)));
  assert(g.blocksOrdinaryCenter(-.10f,.38f));
  assert(!g.blocksOrdinaryCenter(-.10f,.60f));
  assert(!g.blocksOrdinaryCenter(-.10f,.46f));
  assert(g.blocksOrdinaryCell(-.10f,.46f,.05f));
  assert(!g.blocksOrdinaryCell(-.10f,.60f,.05f));
  assert(g.blocksOrdinaryCell(1,1,-.1f));
  // Dock protection does not remove intentional side-bristle wall contact.
  assert(!g.blocksOrdinaryCenter(2.78f,1.f));
  assert(g.permitsMotion(observation(Pose(2.78f,1.f,0)),0,0,100,true));
  auto o = observation(Pose(-.1f,.70f,-pi/2));
  assert(!g.permitsMotion(o,.4f,0,1500,true)); // side crossing
  assert(g.issue() == Issue::BaseIntersection);
  o.pose.yaw = pi/2;
  assert(!g.permitsMotion(o,-.4f,0,1500,false)); // reverse side crossing
  o.pose = Pose(-1,0,0);
  assert(!g.permitsMotion(o,.5f,0,4000,false)); // both endpoints clear: swept path crosses base
  o.pose = Pose(-.7f,.7f,-pi/4);
  assert(!g.permitsMotion(o,.5f,0,2500,true)); // diagonal crossing
  o.pose = Pose(.7f,.7f,0);
  assert(g.permitsMotion(o,.1f,0,1000,true)); // no gratuitous wide force-field exclusion
  assert(g.permitsMotion(o,-.1f,0,1000,false));
  // The body clears the base; an offset brush sweeps into it during a full turn.
  o.pose = Pose(-.10f,.38f,pi/2);
  assert(!g.blocksOrdinaryPose(o.pose));
  assert(!g.permitsMotion(o,0,pi,2000,true));
  assert(!g.permitsMotion(o,0,pi,2000,false)); // stopped bristles still can snag
  auto c = fixture(); c.sideRadius = 0;
  assert(g.configure(c,true));
  assert(g.permitsMotion(o,0,pi,2000,true)); // circular body itself remains clear
  c.margin = .08f;
  assert(g.configure(c,true));
  assert(g.blocksOrdinaryPose(o.pose)); // uncertainty expands the protected footprint
  // A coarse subdivision cannot tunnel: each entire interval is swept-bounded.
  c = fixture(); c.stepMeters = 100; c.stepRadians = 100;
  assert(g.configure(c,true));
  o.pose = Pose(-1,0,0);
  assert(!g.permitsMotion(o,.5f,0,4000,false));
  c = fixture(); c.stepMeters = .00001f;
  assert(g.configure(c,true));
  o.pose = Pose(1,1,0);
  assert(!g.permitsMotion(o,.1f,0,1000,false));
  assert(g.issue() == Issue::SweepBudget);
}
static void rotatedRectangle() {
  auto c = fixture();
  c.frontX = 1; c.frontY = -1; c.yaw = pi/2;
  DockGuard g;
  assert(g.configure(c,true));
  // Local (-.1,.4) transforms to global (.6,-1.1), safely beside base.
  assert(!g.blocksOrdinaryPose(Pose(.6f,-1.1f,0)));
  assert(g.blocksOrdinaryPose(Pose(1,-1.1f,0)));
  auto o = observation(Pose(1,-.2f,-pi/2));
  assert(g.beginApproach(o));
  assert(g.permitsMotion(o,.1f,0,1000,false));
}
static void approachAndDeparture() {
  auto g = makeGuard();
  auto o = observation();
  assert(!g.permitsNativeDock(o));
  assert(g.beginApproach(o));
  assert(g.permitsNativeDock(o));
  assert(g.permitsMotion(o,.2f,0,2000,false));
  assert(!g.permitsMotion(o,-.1f,0,100,false));
  assert(g.issue() == Issue::WrongDirection);
  assert(!g.permitsMotion(o,.1f,0,100,true));
  assert(g.issue() == Issue::BrushesActive);
  assert(!g.permitsMotion(o,0,1,1000,false)); // in-place turn cannot widen approach
  o.pose = Pose(.15f,0,pi);
  assert(g.blocksOrdinaryPose(o.pose)); // planner does not inherit docking exemption
  assert(g.permitsNativeDock(o));
  auto side = o; side.pose.y = .3f;
  assert(!g.permitsNativeDock(side));
  side = o; side.pose.x = -.1f;
  assert(!g.permitsNativeDock(side));
  side = o; side.pose.yaw = pi/2;
  assert(!g.permitsNativeDock(side));
  side = o; side.irRaw = 0;
  assert(g.permitsNativeDock(side)); // beam loss is not map evidence
  side.irMs = side.now - 501;
  assert(!g.permitsNativeDock(side));
  o.dockContact = true;
  g.observe(o);
  assert(g.mode() == DockGuard::Mode::Docked);
  assert(!g.permitsNativeDock(o));
  assert(!g.permitsMotion(o,0,0,100,false));
  g.endAccess();
  assert(g.mode() == DockGuard::Mode::Docked);
  assert(g.beginDeparture(o));
  assert(g.permitsMotion(o,-.1f,0,1000,false));
  assert(!g.permitsMotion(o,.1f,0,100,false));
  assert(g.issue() == Issue::WrongDirection);
  o.pose.x = .7f; o.dockContact = false;
  g.endAccess();
  assert(g.mode() == DockGuard::Mode::Ordinary);
  assert(g.permitsMotion(o,-.1f,0,100,false));
  // A pose overlapping the protected area cannot open an approach exception.
  g = makeGuard(); o = observation(Pose(.15f,0,pi));
  assert(!g.beginApproach(o));
  // Qualified explicit anchoring can recognize an already-docked stationary robot.
  o.dockContact = true;
  assert(g.confirmDocked(o));
  assert(g.beginDeparture(o));
}
static void freshnessAndInvalidation() {
  auto g = makeGuard(); auto o = observation();
  auto stale = o; stale.poseMs = o.now - 301;
  assert(!g.permitsMotion(stale,0,0,100,false));
  assert(g.issue() == Issue::StalePose);
  stale = o; stale.sensorsMs = o.now - 301;
  assert(!g.permitsMotion(stale,.1f,0,100,false));
  assert(g.issue() == Issue::StaleSensors);
  stale = o; stale.localized = false;
  assert(!g.beginApproach(stale));
  stale = o; stale.pose.yaw = std::numeric_limits<float>::infinity();
  assert(!g.permitsMotion(stale,.1f,0,100,false));
  stale = o; stale.irMs = o.now - 501;
  assert(!g.beginApproach(stale));
  stale = o; stale.irRaw = 0;
  assert(!g.beginApproach(stale));
  stale = o; stale.irRaw = 162;
  assert(!g.beginApproach(stale));
  // Stale contact is not promoted to a current moved-base observation.
  stale = o; stale.dockContact = true; stale.sensorsMs = o.now - 301;
  g.observe(stale); assert(g.anchorValid());
  o.dockContact = true;
  g.observe(o); assert(!g.anchorValid());
  assert(g.issue() == Issue::UnexpectedContact);
  assert(g.footprintIntersectsBase(Pose(0,0,0)));
  assert(g.blocksOrdinaryPose(Pose(2,2,0)));
  assert(!g.beginDeparture(o));
  assert(!g.permitsMotion(observation(),.1f,0,100,false));
  g = makeGuard(); o = observation(); o.anchorMismatch = true;
  g.observe(o); assert(!g.anchorValid());
  g = makeGuard(); o = observation(); assert(g.beginApproach(o));
  o.dockContact = true; // contact far from the expected contact face is inconsistent
  g.observe(o); assert(!g.anchorValid());
  g = makeGuard(); o = observation(Pose(.15f,0,pi)); o.bumper = true;
  g.observe(o); assert(g.issue() == Issue::BumperNearBase && !g.ready());
  g = makeGuard(); o = observation(Pose(2,2,0)); o.bumper = true;
  g.observe(o); assert(g.ready()); // other safety layer owns a distant bumper
  g.invalidateAt(Pose(1,1,0));
  assert(g.hasSuspectedAnchor());
  assert(g.config().frontX == 0 && g.suspectedAnchor().x == 1);
  assert(g.footprintIntersectsBase(Pose(0,0,0)));
}
static void timeBoundsAndMap() {
  auto g = makeGuard(); auto o = observation();
  assert(g.beginApproach(o));
  o = observation(o.pose,30900);
  assert(g.beginApproach(o)); // repeated request does not reset start=1000
  assert(!g.permitsMotion(o,.1f,0,200,false));
  assert(g.issue() == Issue::AccessExpired);
  o = observation(o.pose,31000);
  assert(!g.permitsNativeDock(o));
  // A contact after timeout is not accepted as a successful approach.
  g = makeGuard(); o = observation(); assert(g.beginApproach(o));
  o = observation(Pose(.15f,0,pi),32000); o.dockContact = true;
  g.observe(o); assert(!g.ready() && g.issue() == Issue::AccessExpired);
  // Millisecond rollover preserves a valid short permission/freshness interval.
  g = makeGuard(); o = observation(Pose(.8f,0,pi),0xfffffff0u);
  assert(g.beginApproach(o));
  o = observation(o.pose,40);
  assert(g.permitsNativeDock(o));
  assert(g.permitsMotion(o,.1f,0,100,false));
  // Outside-map protection is applied to swept body, including in a permit.
  g = makeGuard(); o = observation(Pose(2.75f,0,0));
  assert(!g.permitsMotion(o,.2f,0,1000,false));
  assert(g.issue() == Issue::OutsideMap);
  o = observation();
  assert(!g.permitsMotion(o,std::numeric_limits<float>::quiet_NaN(),0,100,false));
  assert(!g.permitsMotion(o,0,0,0,false));
  assert(!g.permitsMotion(o,.7f,0,100,false));
  auto c = fixture(); c.map.maxX = .9f;
  assert(g.configure(c,true));
  o = observation(Pose(.15f,0,pi)); o.dockContact = true;
  assert(g.confirmDocked(o)); assert(g.beginDeparture(o));
  o.pose.x = .6f; o.dockContact = false;
  assert(!g.permitsMotion(o,-.2f,0,1000,false));
  assert(g.issue() == Issue::OutsideMap);
}
static void exactIR() {
  for (unsigned raw = 0; raw <= 255; ++raw) {
    auto ir = DockGuard::classifyIR(uint8_t(raw));
    const bool expected = raw == 161 || raw == 164 || raw == 165 || raw == 168 ||
                          raw == 169 || raw == 172 || raw == 173;
    assert(ir.dock == expected);
    assert(ir.virtualWall == (raw == 162));
    assert(ir.forceField == (raw == 161 || raw == 165 || raw == 169 || raw == 173));
    assert(ir.green == (raw == 164 || raw == 165 || raw == 172 || raw == 173));
    assert(ir.red == (raw == 168 || raw == 169 || raw == 172 || raw == 173));
  }
  auto g = makeGuard(); auto o = observation(Pose(1,1,0));
  assert(g.permitsMotion(o,.1f,0,100,false));
  o.irRaw = 161;
  assert(g.permitsMotion(o,.1f,0,100,false)); // force field supplies no metric radius
  o.irRaw = 0; o.pose = Pose(0,0,0);
  assert(!g.permitsMotion(o,.1f,0,100,false)); // no beacon never removes physical base
}
int main() {
  configuration(); ordinaryGeometry(); rotatedRectangle(); approachAndDeparture();
  freshnessAndInvalidation(); timeBoundsAndMap(); exactIR();
  std::puts("dock guard: synthetic geometry, swept turns/reverse, corridor, freshness and exact IR passed");
}
