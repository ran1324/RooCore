// Host adapter executes the same C++ planner as XIAO; physics stays in Python.
#include "RooNavigation.h"
#include <new>
using namespace roonav;
extern "C" {
void *roo_create() {
  Config c;
  c.stationaryDockSimulation = true;
  c.cols = 48;
  c.rows = 36;
  c.originX = c.originY = 0;
  return new (std::nothrow) Navigator(c);
}
int roo_set_zones(void *p, const float *values, int count, int validateOnly, float x, float y) {
  if (count < 0 || count > int(MapZones::Capacity)) return 0;
  Navigator &n = *static_cast<Navigator *>(p);
  MapZone items[MapZones::Capacity];
  MapZones trial;
  trial.count = unsigned(count);
  for (int i = 0; i < count; ++i) {
    const float *v = values + i * 9;
    MapZone &z = items[i];
    z.kind = MapZone::Kind(int(v[0]));
    z.x1 = v[1]; z.y1 = v[2]; z.x2 = v[3]; z.y2 = v[4];
    z.enabled = v[5] != 0; z.present = v[6] != 0; z.boost = v[7] != 0;
    z.extraPasses = uint8_t(v[8]);
    if (!z.valid()) return 0;
    trial.items[i] = z;
  }
  float clearance = n.cfg.radius + n.cfg.margin + n.cfg.resolution * .707107f;
  if (trial.blocked(x, y, clearance) || trial.blocked(n.dock.x, n.dock.y, clearance)) return 0;
  return validateOnly ? 1 : n.setZones(items, unsigned(count));
}
void roo_zone_status(void *p, int *v, int *map) {
  Navigator &n = *static_cast<Navigator *>(p);
  v[0] = n.totalPasses();
  for (int c = 0; c < n.count(); ++c) {
    map[c * 3] = n.zoneBlocked[c];
    map[c * 3 + 1] = n.zoneExtra[c];
    map[c * 3 + 2] = n.zoneCovered[c];
  }
}
void roo_destroy(void *p) { delete static_cast<Navigator *>(p); }
void roo_command(void *p, int command, int deep, unsigned now, float x, float y, float yaw) {
  Navigator &n = *static_cast<Navigator *>(p);
  Pose pose;
  pose.x = x;
  pose.y = y;
  pose.yaw = yaw;
  if (command == 0)
    n.start(deep, pose, now);
  if (command == 1)
    n.pause(now);
  if (command == 2) {
    Input i;
    i.pose = pose;
    n.goHome(i);
  }
}
void roo_step(void *p, const float *v, unsigned now, unsigned flags, float *out, int resume) {
  Navigator &n = *static_cast<Navigator *>(p);
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
  i.retreatVerified = flags & 256;
  i.cleaningActive = flags & 512;
  i.sideBrushActive = flags & 1024;
  if (resume) {
    out[0] = n.resume(i);
    return;
  }
  Output o = n.tick(i);
  out[0] = o.speed;
  out[1] = o.turn;
  out[2] = o.pwm;
  out[3] = int(o.action);
  out[4] = o.sidePwm == 255 ? o.pwm : o.sidePwm;
}
int roo_path(void *p, int *out, int maximum) {
  Navigator &n = *static_cast<Navigator *>(p);
  int count = int(n.pathLength());
  if (count > maximum)
    count = maximum;
  for (int i = 0; i < count; ++i)
    out[i] = n.pathCell(count - i - 1);
  return count;
}
void roo_status(void *p, int *v, float *f, int *map) {
  Navigator &n = *static_cast<Navigator *>(p);
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
  Pose target = n.targetPose();
  f[0] = target.x;
  f[1] = target.y;
  f[2] = n.checkpoint.x;
  f[3] = n.checkpoint.y;
  for (int c = 0; c < n.count(); ++c) {
    map[c * 8] = n.cells[c];
    map[c * 8 + 1] = n.covered[c];
    map[c * 8 + 2] = n.sideCovered[c];
    map[c * 8 + 3] = n.safe[c];
    map[c * 8 + 4] = n.roomMask[c];
    map[c * 8 + 5] = n.portal[c];
    map[c * 8 + 6] = n.excluded[c];
    map[c * 8 + 7] = n.contacts[c];
  }
}
}
