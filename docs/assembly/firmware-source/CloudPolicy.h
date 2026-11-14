#pragma once
#include "MapZones.h"
#include <stddef.h>
#include <string.h>

namespace roonav {
// TLS authenticates this envelope. The checksum detects storage/copy corruption only.
struct CloudPolicy {
  static constexpr unsigned Header = 152, ZoneBytes = 20, Maximum = Header + MapZones::Capacity * ZoneBytes;
  uint32_t generation = 0, revision = 0, checksum = 0;
  uint16_t cols = 0, rows = 0, count = 0;
  float resolution = 0, originX = 0, originY = 0;
  char cloudMap[33]{}, sourceMap[80]{};
  MapZone zones[MapZones::Capacity];
  static uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
  static uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | (uint32_t(u16(p+2)) << 16); }
  static float f32(const uint8_t *p) { uint32_t b=u32(p); float f; memcpy(&f,&b,4); return f; }
  static uint32_t hash(const uint8_t *p, size_t n) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<n;++i) h=(h ^ (i>=148 && i<152 ? 0 : p[i]))*16777619u;
    return h;
  }
  static bool identity(const char *p, size_t capacity) {
    size_t n=0;
    for(;n<capacity && p[n];++n)
      if(!((p[n]>='a'&&p[n]<='z')||(p[n]>='A'&&p[n]<='Z')||
           (p[n]>='0'&&p[n]<='9')||p[n]=='-'||p[n]=='_'||p[n]=='.')) return false;
    return n>0 && n<capacity;
  }
  bool decode(const uint8_t *p,size_t size) {
    *this=CloudPolicy{};
    if(size<Header||memcmp(p,"RCZ1",4)||u16(p+4)!=size||u16(p+6)>MapZones::Capacity||
       size!=Header+u16(p+6)*ZoneBytes||p[145]||p[146]||p[147]||u32(p+148)!=hash(p,size)) return false;
    generation=u32(p+8);revision=u32(p+12);count=u16(p+6);cols=u16(p+16);rows=u16(p+18);
    resolution=f32(p+20);originX=f32(p+24);originY=f32(p+28);checksum=u32(p+148);
    memcpy(cloudMap,p+32,33);memcpy(sourceMap,p+65,80);
    if(!generation||!revision||!identity(cloudMap,33)||strlen(cloudMap)!=32||!identity(sourceMap,80)||
       cols<4||rows<4||unsigned(cols)*rows>16384||!isfinite(resolution)||resolution<.1f||resolution>1||
       !isfinite(originX)||!isfinite(originY)) return false;
    for(unsigned i=0;i<count;++i) {
      const uint8_t *v=p+Header+i*ZoneBytes;
      MapZone &z=zones[i];
      if(v[0]>3||(v[1]&~7)||v[2]>2||v[3]) return false;
      z.kind=MapZone::Kind(v[0]);z.enabled=v[1]&1;z.present=v[1]&2;z.boost=v[1]&4;z.extraPasses=v[2];
      z.x1=f32(v+4);z.y1=f32(v+8);z.x2=f32(v+12);z.y2=f32(v+16);
      if(!z.valid()||z.x1<originX||z.x2<originX||z.y1<originY||z.y2<originY||
         z.x1>originX+cols*resolution||z.x2>originX+cols*resolution||
         z.y1>originY+rows*resolution||z.y2>originY+rows*resolution) return false;
    }
    return true;
  }
  bool matches(const char *map,int c,int r,float scale,float ox,float oy) const {
    return generation && !strcmp(sourceMap,map)&&cols==c&&rows==r&&
           fabsf(resolution-scale)<.00001f&&fabsf(originX-ox)<.00001f&&fabsf(originY-oy)<.00001f;
  }
  unsigned maxExtra() const {
    unsigned result=0;
    for(unsigned i=0;i<count && i<MapZones::Capacity;++i)
      if(zones[i].kind==MapZone::Carpet && zones[i].enabled && zones[i].present && zones[i].extraPasses>result) result=zones[i].extraPasses;
    return result;
  }
  bool committed(uint32_t expectedGeneration,uint32_t expectedChecksum) const {
    return generation && generation==expectedGeneration && checksum==expectedChecksum;
  }
  bool newerThan(const CloudPolicy &old) const { return generation>old.generation; }
};
// Network cadence/backoff never owns the controller, never queues old snapshots.
class CloudCadence {
public:
  uint32_t next=0, delay=1000, lastMap=0;
  bool due(uint32_t now) const { return int32_t(now-next)>=0; }
  void result(uint32_t now,bool ok,uint32_t interval) {
    delay=ok?interval:(delay<30000?delay*2:60000);
    if(delay>60000)delay=60000;
    next=now+delay;
  }
};
} // namespace roonav
