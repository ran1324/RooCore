#include <NativeCapture.h>
#include <RoombaOI.h>
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace roocore;
struct Wire : Transport {
  std::vector<uint8_t> written, received;
  size_t at = 0;
  size_t write(const uint8_t *p, size_t n) override { written.insert(written.end(),p,p+n); return n; }
  int read() override { return at < received.size() ? received[at++] : -1; }
};
struct Capture {
  Roomba &robot;
  roonav::NativeCapture<3> log;
  static void observe(void *ctx, Sensor id, const Sample &s) {
    if (id != Sensor::BumpsAndDrops) return;
    auto &c = *static_cast<Capture *>(ctx);
    roonav::NativeFrame frame;
    frame.ms=s.timestampMs; frame.generation=s.generation;
    for (unsigned i=7;i<=58;++i) {
      auto v=c.robot.sensor(static_cast<Sensor>(i));
      if (v.valid && v.generation==s.generation) { frame.present|=uint64_t(1)<<(i-7); frame.values[i-7]=v.value; }
    }
    c.log.append(frame);
  }
};
void feed(Wire &wire, unsigned group, bool corrupt=false) {
  std::vector<uint8_t> bytes{19,0,uint8_t(group)};
  for (unsigned id=7;id<=(group==6?42u:58u);++id) {
    const size_t n=Roomba::packetSize(static_cast<Sensor>(id));
    for(size_t i=0;i<n;++i) bytes.push_back(uint8_t(id==35?1:id==21?0:id));
  }
  bytes[1]=uint8_t(bytes.size()-2);
  uint8_t sum=0; for(auto b:bytes) sum+=b;
  bytes.push_back(uint8_t(-sum)+(corrupt?1:0));
  wire.received.insert(wire.received.end(),bytes.begin(),bytes.end());
}
int main() {
  Wire wire; Capabilities caps; caps.extendedSensors=true;
  Roomba robot(wire,caps); Capture capture{robot,{}};
  robot.setSampleObserver(Capture::observe,&capture);
  assert(robot.beginPassive()==Result::Ok);
  Sensor group=Sensor::AllBase;
  assert(robot.startStream(&group,1)==Result::Ok);
  capture.log.start(false);
  feed(wire,6); robot.poll(100);
  roonav::NativeFrame rows[3];
  assert(capture.log.copyAfter(0,rows,3)==1);
  assert(rows[0].present==((uint64_t(1)<<36)-1)); // all base packets, no stale extensions
  assert(rows[0].values[23-7]==0x1717);
  feed(wire,6,true); robot.poll(115);
  assert(capture.log.latest==1); // rejected frames cannot become measurements
  for(unsigned i=0;i<4;++i) { feed(wire,6); robot.poll(130+i*15); }
  assert(capture.log.latest==5 && capture.log.oldest()==3);
  assert(capture.log.copyAfter(0,rows,3)==3 && rows[0].sequence==3);
  capture.log.stop(); feed(wire,6); robot.poll(200); assert(capture.log.latest==5);
  assert(robot.resynchronizeInput(201)==Result::Ok); robot.poll(302);
  group=Sensor::AllExtended; assert(robot.startStream(&group,1)==Result::Ok);
  capture.log.start(true); feed(wire,100); robot.poll(320);
  assert(capture.log.session==2 && capture.log.latest==1);
  assert(capture.log.copyAfter(0,rows,3)==1 && rows[0].present==((uint64_t(1)<<52)-1));
  // This whole recording path sends only Passive, stream setup and stream pause.
  assert(wire.written==std::vector<uint8_t>({128,148,1,6,150,0,148,1,100}));
  roonav::UpdateHold hold;
  hold.observe(100,true,1,2); assert(!hold.ready);
  hold.observe(2099,true,1,2); assert(!hold.ready);
  hold.observe(2100,true,1,2); assert(hold.ready);
  hold.observe(2101,true,2,2); assert(!hold.ready);
  hold.observe(4101,true,2,2); assert(hold.ready);
  hold.observe(4102,false,2,2); assert(!hold.ready);
  hold.observe(0xfffffff0u,true,0,0); hold.observe(1984,true,0,0); assert(hold.ready);
  puts("PASS native capture: atomic base/extended frames, checksum rejection, loss, passive-only writes, OTA hold");
}
