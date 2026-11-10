#include "RooNavigation.h"
#include <cassert>
#include <memory>
#include <cstdio>
using namespace roonav;
static MapZone box(MapZone::Kind kind, float x1, float y1, float x2, float y2) {
  MapZone z; z.kind = kind; z.x1=x1; z.y1=y1; z.x2=x2; z.y2=y2; return z;
}
int main() {
  Config cfg; cfg.cols=40; cfg.rows=40; cfg.originX=cfg.originY=0;
  auto n = std::unique_ptr<Navigator>(new Navigator(cfg));
  MapZone z[3] = {box(MapZone::NoGo, 2, 1, 2.4f, 2),
                 box(MapZone::Wall, 1, 3, 3, 3),
                 box(MapZone::Carpet, .4f, .4f, 1.7f, 1.7f)};
  z[2].extraPasses=2;
  assert(n->setZones(z, 3));
  assert(n->zones.blocked(1.9f, 1.5f, .2f));
  assert(n->zones.blocked(2, 2.9f, .2f));
  assert(!n->zones.blocked(1.5f, 2.5f, .2f));
  Pose p; p.x=1.75f; p.y=1.5f;
  assert(!n->zoneMotionAllowed(p, .2f, 0));
  p.yaw=Pi;
  assert(!n->zoneMotionAllowed(p, -.2f, 0));
  assert(n->zoneMotionAllowed(p, .2f, 0));
  assert(n->zones.boostAt(1,1));
  assert(n->zones.extraAt(1,1)==2);
  p.x=1.025f; p.y=1.025f; p.yaw=0;
  n->start(false, p, 0);
  assert(n->totalPasses()==3);
  assert(!n->setZones(z, 3)); // active policy cannot be replaced
  Input in; in.pose=p; in.localized=in.scanValid=in.sensorsValid=true;
  in.battery=90;
  for (float &r : in.ranges) r=1.5f;
  for (unsigned t=200; t<2000; t+=200) {
    in.now=in.poseMs=in.scanMs=in.sensorsMs=t;
    n->updateMap(in);
    assert(!n->safe[n->cell(2.05f,1.45f)]); // rays never erase policy
  }
  for (int c=0; c<n->count(); ++c) n->cells[c]=-1;
  in.cleaningActive=true;
  // Extra credit is separate and only recorded during the requested extra pass.
  n->pass=1;
  n->recordSweep(in);
  bool extra=false;
  for (int c=0;c<n->count();++c) {
    extra |= n->zoneCovered[c] != 0;
    assert(n->covered[c]==0);
  }
  assert(extra);
  in.cliff=true;
  assert(n->tick(in).action==Action::Stop && n->reason==Reason::Cliff);
  n->state=State::Idle; z[2].present=false;
  assert(n->setZones(z,3));
  assert(n->totalPasses()==1 && !n->zones.boostAt(1,1));
  z[2].present=true;
  z[0]=box(MapZone::NoGo,.5f,.5f,1.5f,1.5f);
  assert(n->setZones(z,3));
  assert(!n->zones.boostAt(1,1) && n->zones.extraAt(1,1)==0);
  z[0].x1=NAN;
  assert(!n->setZones(z,3)); // rejected transaction keeps previous barriers
  assert(n->zones.blocked(1,1,0));
  puts("PASS: persistent exclusion, footprint/forward/reverse guards, carpet overlap/presence, independent passes and cliff priority");
}
