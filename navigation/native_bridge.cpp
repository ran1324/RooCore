// Host adapter executes the same C++ planner as XIAO; physics stays in Python.
#include "RooNavigation.h"
#include "DockGuard.h"
#include <new>
using namespace roonav;
struct HostController {
  Navigator nav;
  DockGuard guard;
  bool departing = false;
  static Config navigationConfig() { Config c; c.cols=48; c.rows=36; c.originX=c.originY=0; return c; }
  static bool ordinaryAllowed(void *ctx, float x, float y) {
    return !static_cast<DockGuard *>(ctx)->blocksOrdinaryCell(x,y,.05f);
  }
  HostController() : nav(navigationConfig()) {
    // Synthetic dock fixture: these numbers must NEVER be copied to hardware calibration.
    DockGuard::Config c;
    c.configured=c.verified=c.simulationOnly=true; c.anchorRevision=1;
    c.frontX=.25f; c.frontY=.675f; c.yaw=0; c.depth=.20f; c.halfWidth=.16f;
    c.margin=.03f; c.bodyRadius=.17f; c.sideX=.10f; c.sideY=-.13f; c.sideRadius=.10f;
    c.corridorMinX=.16f; c.corridorMaxX=.80f; c.corridorHalfWidth=.12f;
    c.headingTolerance=.12f; c.maxLinearSpeed=.3f; c.maxAngularSpeed=1.5f;
    c.maxHorizonMs=500; c.maxAccessMs=30000;
    c.poseMaxAgeMs=300; c.sensorsMaxAgeMs=200; c.irMaxAgeMs=200;
    c.stepMeters=.005f; c.stepRadians=.025f;
    c.map.valid=true; c.map.minX=c.map.minY=0; c.map.maxX=4.8f; c.map.maxY=3.6f;
    guard.configure(c,true);
    nav.ordinaryCenterAllowed=ordinaryAllowed; nav.ordinaryGuardContext=&guard;
  }
  Output tick(const Input &i, bool moved) {
    DockGuard::Observation obs;
    obs.now=i.now; obs.poseMs=i.poseMs; obs.sensorsMs=i.sensorsMs; obs.irMs=i.now;
    obs.pose=DockGuard::Pose(i.pose.x,i.pose.y,i.pose.yaw);
    obs.localized=i.localized; obs.sensorsValid=i.sensorsValid;
    obs.dockContact=i.docked; obs.bumper=i.bumper; obs.anchorMismatch=moved;
    obs.irValid=true; obs.irRaw=distance(i.pose.x,i.pose.y,.25f,.675f)<.85f ? 173 : 0;
    guard.observe(obs);
    Output out;
    if (nav.state==State::Paused || nav.state==State::Fault || nav.state==State::Complete || nav.state==State::Idle) {
      departing=false; guard.endAccess(); return out;
    }
    if (!guard.ready()) { nav.fault(Reason::NoRoute); return out; }
    if (departing) {
      // Explicit bounded synthetic reverse exit, independent of navigation path following.
      if (!i.localized || !i.scanValid || i.cliff || i.wheelDrop || i.bumper) {
        nav.fault(Reason::Telemetry); departing=false; return out;
      }
      if (i.pose.x >= .675f) { departing=false; guard.endAccess(); }
      else { out.action=Action::Drive; out.speed=-.04f; }
    }
    if (!departing) {
      const State before=nav.state;
      out=nav.tick(i);
      if (before==State::Charging && nav.state==State::Resuming) {
        if (!guard.beginDeparture(obs)) { nav.fault(Reason::NoRoute); return Output(); }
        departing=true;
      }
    }
    if (out.action==Action::SeekDock) {
      if (guard.mode()==DockGuard::Mode::Ordinary) {
        const float error=wrap(Pi-i.pose.yaw);
        if (fabsf(error)>.04f) { out.action=Action::Drive; out.turn=error>0 ? .4f : -.4f; }
        else if (!guard.beginApproach(obs)) { nav.fault(Reason::NoRoute); return Output(); }
      }
      if (out.action==Action::SeekDock) {
        if (!guard.permitsNativeDock(obs)) { nav.fault(Reason::NoRoute); return Output(); }
        // Unlike the physical OI routine, host physics explicitly integrates this synthetic approach.
        out.speed=.04f;
      }
    }
    if ((out.action==Action::Drive || out.action==Action::SeekDock) &&
        !guard.permitsMotion(obs,out.speed,out.turn,200,out.pwm || (out.sidePwm!=255 && out.sidePwm))) {
      nav.fault(Reason::NoRoute); return Output();
    }
    return out;
  }
};
extern "C" {
void *roo_create() {
  return new (std::nothrow) HostController();
}
void roo_destroy(void *p) { delete static_cast<HostController *>(p); }
void roo_command(void *p, int command, int deep, unsigned now, float x, float y, float yaw) {
  HostController &host = *static_cast<HostController *>(p);
  Navigator &n = host.nav;
  Pose pose;
  pose.x = x;
  pose.y = y;
  pose.yaw = yaw;
  if (command == 0)
    n.start(deep, pose, now, true);
  if (command == 1) {
    host.departing = false;
    host.guard.endAccess();
    n.pause(now);
  }
  if (command == 2) {
    Input i;
    i.pose = pose;
    i.now = now;
    n.goHome(i);
  }
}
void roo_step(void *p, const float *v, unsigned now, unsigned flags, float *out, int resume) {
  HostController &host = *static_cast<HostController *>(p);
  Navigator &n = host.nav;
  Input i;
  i.now = i.poseMs = i.scanMs = i.sensorsMs = now;
  i.pose.x = v[0];
  i.pose.y = v[1];
  i.pose.yaw = v[2];
  i.battery = v[3];
  i.dirt = uint8_t(v[184]);
  memcpy(i.ranges, v + 4, sizeof i.ranges);
  i.localized = flags & 1;
  i.scanValid = flags & 2;
  i.sensorsValid = flags & 4;
  i.cliff = flags & 8;
  i.wheelDrop = flags & 16;
  i.bumper = flags & 32;
  i.docked = flags & 64;
  i.charging = flags & 128;
  i.batteryValid = v[188] != 0;
  i.chargerValid = flags & 2048;
  i.chargerState = uint8_t(v[185]);
  i.chargerSources = uint8_t(v[186]);
  i.chargerMs = unsigned(v[187] * 1000.0f);
  i.chargeContactConfirmed = v[189] != 0;
  i.chargeReady = v[190] != 0;
  i.retreatVerified = flags & 256;
  i.cleaningActive = flags & 512;
  i.sideBrushActive = flags & 1024;
  if (resume) {
    out[0] = n.resume(i);
    return;
  }
  Output o = host.tick(i, flags & 4096);
  out[0] = o.speed;
  out[1] = o.turn;
  out[2] = o.pwm;
  out[3] = int(o.action);
  out[4] = o.sidePwm == 255 ? o.pwm : o.sidePwm;
}
int roo_path(void *p, int *out, int maximum) {
  HostController &host = *static_cast<HostController *>(p);
  Navigator &n = host.nav;
  int count = int(n.pathLength());
  if (count > maximum)
    count = maximum;
  for (int i = 0; i < count; ++i)
    out[i] = n.pathCell(count - i - 1);
  return count;
}
void roo_status(void *p, int *v, float *f, int *map) {
  HostController &host = *static_cast<HostController *>(p);
  Navigator &n = host.nav;
  v[0] = int(n.state);
  v[1] = int(n.reason);
  v[2] = n.pass;
  v[3] = n.passes;
  v[4] = n.skipped;
  v[5] = n.roomsCompleted;
  v[6] = n.edgeSegments;
  v[7] = n.edgeIncomplete;
  v[8] = n.target();
  v[9] = int(n.pathLength());
  v[10] = n.completedCells;
  v[11] = n.clearedObstacles;
  v[12] = n.obstacleWaits;
  v[13] = int(n.activeMs);
  v[14] = int(n.returnCause);
  v[15] = int(n.returnAt);
  Pose target = n.targetPose();
  f[0] = target.x;
  f[1] = target.y;
  f[2] = n.checkpoint.x;
  f[3] = n.checkpoint.y;
  f[4] = int(host.guard.issue());
  f[5] = int(host.guard.mode());
  f[6] = host.guard.anchorValid();
  for (int c = 0; c < n.count(); ++c) {
    map[c * 8] = n.cells[c];
    map[c * 8 + 1] = n.covered[c];
    map[c * 8 + 2] = n.sideCovered[c];
    map[c * 8 + 3] = n.safe[c];
    map[c * 8 + 4] = n.roomMask[c];
    map[c * 8 + 5] = n.portal[c];
    map[c * 8 + 6] = n.excluded[c] || host.guard.footprintIntersectsBase(DockGuard::Pose(n.cfg.originX + (c % n.cfg.cols + .5f) * n.cfg.resolution, n.cfg.originY + (c / n.cfg.cols + .5f) * n.cfg.resolution, 0));
    map[c * 8 + 7] = n.contacts[c];
  }
}
}
