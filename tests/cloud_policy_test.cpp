#include "CloudPolicy.h"
#include "RooNavigation.h"
#include "NativeCapture.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <fstream>
roonav::Pose pose(float x,float y) {roonav::Pose p;p.x=x;p.y=y;return p;}
int main(int argc,char **argv) {
  assert(argc==2);
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> wire((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  roonav::CloudPolicy policy;
  assert(policy.decode(wire.data(),wire.size()));
  assert(policy.generation==7 && policy.revision==3 && policy.count==2);
  assert(policy.matches("test-map",128,128,.1f,-6.4f,-6.4f));
  assert(!policy.matches("other-map",128,128,.1f,-6.4f,-6.4f));
  auto *nav=new roonav::Navigator;
  assert(nav->setZones(policy.zones,policy.count));
  assert(nav->zones.hasBarriers());
  // Removing the network has no path to zone mutation or local planner permission.
  roonav::CloudCadence network;
  for(unsigned i=0;i<100;++i)network.result(i*1000,false,1000);
  assert(network.delay==60000);
  assert(!nav->zoneMotionAllowed(pose(1.85f,2.5f),.1f,0));
  assert(nav->zoneMotionAllowed(pose(-3,-3),.1f,0));
  nav->start(false,pose(-3,-3),1000,true);
  assert(nav->zones.hasBarriers());
  assert(!nav->setZones(nullptr,0)); // active cleaning cannot discard barriers
  assert(!nav->zoneMotionAllowed(pose(1.85f,2.5f),.1f,0));
  assert(policy.committed(7,policy.checksum));
  assert(!policy.committed(8,policy.checksum)); // torn new write cannot restore an older restriction set
  assert(!policy.committed(7,policy.checksum+1));
  roonav::CloudPolicy empty;assert(!empty.committed(0,0));
  roonav::UpdateHold quiet;
  quiet.observe(100,true,10,20);assert(!quiet.ready);
  quiet.observe(2099,true,10,20);assert(!quiet.ready);
  quiet.observe(2100,true,10,20);assert(quiet.ready);
  quiet.observe(2101,true,11,20);assert(!quiet.ready);
  quiet.observe(4200,false,11,20);assert(!quiet.ready);
  auto old=policy;old.generation=8;assert(!policy.newerThan(old));
  for(size_t i=0;i<wire.size();++i) {
    wire[i]^=1;assert(!policy.decode(wire.data(),wire.size()));wire[i]^=1;
  }
  for(size_t i=0;i<wire.size();++i)assert(!policy.decode(wire.data(),i));
  delete nav;
  puts("cloud policy cross-language, truncation/corruption, offline barrier tests passed");
}
