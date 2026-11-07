#include "RoombaOI.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>
using namespace roocore;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
struct Fake : Transport {
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  size_t chunk=1000, failAfter=1000000;
  size_t write(const uint8_t* p,size_t n) override {
    if (tx.size()>=failAfter) return 0;
    n=std::min(n,std::min(chunk,failAfter-tx.size()));
    tx.insert(tx.end(),p,p+n); return n;
  }
  int read() override { if(rx.empty())return -1; int b=rx.front(); rx.pop_front(); return b; }
  void feed(const std::vector<uint8_t>& p) { rx.insert(rx.end(),p.begin(),p.end()); }
  void expect(std::initializer_list<uint8_t> p) { CHECK(tx==std::vector<uint8_t>(p)); tx.clear(); }
};
static std::vector<uint8_t> frame(std::vector<uint8_t> payload) {
  std::vector<uint8_t> result={19,uint8_t(payload.size())};
  result.insert(result.end(),payload.begin(),payload.end()); uint8_t sum=0;
  for(auto b:result)sum=uint8_t(sum+b);
  result.push_back(uint8_t(0-sum)); return result;
}
static const std::vector<uint8_t> batteryBytes={2,0x38,0x40,0xfe,0x0c,0xfb,0x05,0xdc,0x0b,0xb8};
static void commands() {
  Fake f; f.chunk=1; Capabilities caps; caps.schedulingAndDisplay=true; caps.resetAndStop=true;
  Roomba r(f,caps);
  CHECK(r.driveStraight(100,0)==Result::WrongMode); CHECK(f.tx.empty());
  CHECK(r.beginPassive()==Result::Ok); f.expect({128});
  CHECK(r.enterSafe()==Result::Ok); f.expect({131});
  CHECK(r.driveWheels(-200,500,0)==Result::Ok); f.expect({145,1,244,255,56});
  CHECK(r.driveArc(-200,500,1)==Result::Ok); f.expect({137,255,56,1,244});
  CHECK(r.driveStraight(200,2)==Result::Ok); f.expect({137,0,200,127,255});
  CHECK(r.turnInPlace(100,Turn::Clockwise,3)==Result::Ok); f.expect({137,0,100,255,255});
  CHECK(r.turnInPlace(100,Turn::CounterClockwise,4)==Result::Ok); f.expect({137,0,100,0,1});
  CHECK(r.driveWheelPwm(-255,255,5)==Result::Ok); f.expect({146,0,255,255,1});
  CHECK(r.stopWheels()==Result::Ok); f.expect({145,0,0,0,0});
  CHECK(r.setMotors({true,true,false,false,true})==Result::Ok); f.expect({138,13});
  CHECK(r.setMotorPwm(-127,127,64)==Result::Ok); f.expect({144,129,127,64});
  CHECK(r.setLeds({false,false,true,false,0,128})==Result::Ok); f.expect({139,4,0,128});
  Note notes[]={{60,32},{0,16}};
  CHECK(r.defineSong(0,notes,2)==Result::Ok); f.expect({140,0,2,60,32,0,16});
  CHECK(r.playSong(0)==Result::Ok); f.expect({141,0});
  CHECK(r.setClock(Day::Friday,10,36)==Result::Ok); f.expect({168,5,10,36});
  ScheduleEntry week[7]{}; week[3]={true,15,0}; week[5]={true,10,36};
  CHECK(r.setSchedule(week)==Result::Ok); f.expect({167,40,0,0,0,0,0,0,15,0,0,0,10,36,0,0});
  CHECK(r.displayText("ABCD")==Result::Ok); f.expect({164,65,66,67,68});
  const uint8_t digits[]={1,2,4,8};
  CHECK(r.displaySegments(digits)==Result::Ok); f.expect({163,1,2,4,8});
  CHECK(r.setSchedulingLeds(65,31)==Result::Ok); f.expect({162,65,31});
  CHECK(r.driveWheels(501,0,0)==Result::InvalidArgument);
  CHECK(r.driveArc(1,0,0)==Result::InvalidArgument);
  CHECK(r.driveArc(1,1,0)==Result::InvalidArgument);
  CHECK(r.driveWheelPwm(256,0,0)==Result::InvalidArgument);
  CHECK(r.driveStraight(1,0,0)==Result::InvalidArgument);
  CHECK(r.driveStraight(1,0,2001)==Result::InvalidArgument);
  CHECK(r.setMotorPwm(0,0,-1)==Result::InvalidArgument);
  CHECK(r.defineSong(4,notes,2)==Result::InvalidArgument);
  CHECK(r.defineSong(0,nullptr,2)==Result::InvalidArgument);
  CHECK(r.setClock(Day::Friday,24,0)==Result::InvalidArgument); CHECK(f.tx.empty());
  CHECK(r.pressButton(Button::Clean)==Result::Ok); f.expect({165,1});
  CHECK(r.requestedMode()==Mode::Passive);
  CHECK(r.clean()==Result::Ok); f.expect({135});
  CHECK(r.spot()==Result::Ok); f.expect({134});
  CHECK(r.maxClean()==Result::Ok); f.expect({136});
  CHECK(r.seekDock()==Result::Ok); f.expect({143});
  CHECK(r.powerDownRobot()==Result::Ok); f.expect({133});
  CHECK(r.enterFullDisablingSafety()==Result::Ok); f.expect({132});
  CHECK(r.releaseControl()==Result::Ok); f.expect({145,0,0,0,0,138,0,128});
  CHECK(r.stopInterface()==Result::Ok); f.expect({173});
  CHECK(r.resetRobot()==Result::Ok); f.expect({7});
  CHECK(r.requestBattery(0)==Result::WrongMode);
}
static void queries() {
  Fake f; Roomba r(f); r.beginPassive(); f.tx.clear();
  CHECK(r.requestBattery(0)==Result::Ok); f.expect({142,3});
  CHECK(r.requestBattery(1)==Result::Busy);
  for (size_t i=0;i<batteryBytes.size()-1;++i) { f.feed({batteryBytes[i]}); r.poll(uint32_t(i)); CHECK(!r.battery(9).valid); }
  f.feed({batteryBytes.back()}); r.poll(10);
  auto b=r.battery(10); CHECK(b.valid); CHECK(b.millivolts==14400); CHECK(b.milliamps==-500);
  CHECK(b.temperatureC==-5); CHECK(b.chargeMah==1500); CHECK(b.capacityMah==3000);
  CHECK(b.percentageValid && b.percentage==50); CHECK(b.charging==Charging::FullCharging);
  CHECK(!r.battery(1011).valid); CHECK(r.requestBattery(14)==Result::Busy);
  CHECK(r.requestBattery(15)==Result::Ok);
  auto empty=batteryBytes; empty[8]=empty[9]=0; f.feed(empty); r.poll(16);
  CHECK(r.battery(16).valid && !r.battery(16).percentageValid);
  Sensor ids[]={Sensor::CurrentMa,Sensor::TemperatureC,Sensor::DistanceMm};
  CHECK(r.request(ids,3,30)==Result::Ok); f.tx.clear();
  f.feed({0x80,0,0x80,0xff,0xff}); r.poll(31);
  CHECK(r.sensor(Sensor::CurrentMa).value==-32768);
  CHECK(r.sensor(Sensor::TemperatureC).value==-128);
  CHECK(r.sensor(Sensor::DistanceMm).value==-1);
  CHECK(!r.battery(31).valid); // No mixed-generation battery snapshots.
  CHECK(r.request(Sensor::Battery,50,0)==Result::InvalidArgument);
}
static void limitsAndTimeouts() {
  Fake f; Roomba r(f); r.beginPassive(); f.tx.clear();
  CHECK(r.stopInterface()==Result::Unsupported);
  CHECK(r.request(Sensor::LeftEncoder,0)==Result::Unsupported);
  CHECK(r.request(Sensor(102),0)==Result::InvalidArgument);
  Sensor duplicate[]={Sensor::Battery,Sensor::VoltageMv};
  CHECK(r.request(duplicate,2,0)==Result::InvalidArgument);
  CHECK(r.request(nullptr,1,0)==Result::InvalidArgument);
  CHECK(f.tx.empty());
  CHECK(r.request(Sensor::VoltageMv,0xfffffff0u,30)==Result::Ok);
  f.feed({0x38}); r.poll(0xfffffff1u);
  r.poll(14); CHECK(r.lastEvent()==Result::Timeout); CHECK(!r.sensor(Sensor::VoltageMv).valid);
  CHECK(r.requestBattery(15)==Result::ResyncRequired);
  CHECK(r.resynchronizeInput(15)==Result::Ok);
  CHECK(r.resynchronizeInput(16)==Result::Busy);
  f.feed({0x40}); r.poll(90); r.poll(189); CHECK(!r.inputReady());
  r.poll(190); CHECK(r.inputReady());
  CHECK(r.requestBattery(190)==Result::Ok); f.feed(batteryBytes); r.poll(191); CHECK(r.battery(191).valid);
  f.feed({99}); r.poll(192); CHECK(r.lastEvent()==Result::ResyncRequired); CHECK(!r.battery(192).valid);
}
static void streams() {
  const Sensor ids[]={Sensor::CliffFrontLeftSignal,Sensor::VirtualWall};
  // The PDF's decimal bytes 2,25 represent 537, despite its prose saying 549.
  const std::vector<uint8_t> documented={19,5,29,2,25,13,0,163};
  for (size_t split=0;split<=documented.size();++split) {
    Fake f; Roomba r(f); r.beginPassive(); f.tx.clear();
    CHECK(r.startStream(ids,2)==Result::Ok); f.expect({148,2,29,13});
    f.feed(std::vector<uint8_t>(documented.begin(),documented.begin()+split)); r.poll(0);
    f.feed(std::vector<uint8_t>(documented.begin()+split,documented.end())); r.poll(1);
    CHECK(r.framesReceived()==1); CHECK(r.sensor(Sensor::CliffFrontLeftSignal).value==537);
    CHECK(r.requestBattery(20)==Result::Busy);
    CHECK(r.pauseStream()==Result::Ok); f.expect({150,0});
    CHECK(r.requestBattery(20)==Result::Busy);
    CHECK(r.resumeStream()==Result::Ok); f.expect({150,1});
    auto bad=documented; bad[3]^=1; f.feed(bad); f.feed(documented); r.poll(30);
    CHECK(r.framesReceived()==2); CHECK(r.rejectedFrames()>0);
    CHECK(r.resynchronizeInput(31)==Result::Ok); r.poll(130); CHECK(!r.inputReady());
    r.poll(131); CHECK(r.inputReady());
  }
  Fake f; Roomba r(f); r.beginPassive(); Sensor battery[]={Sensor::Battery};
  CHECK(r.startStream(battery,1)==Result::Ok);
  auto payload=batteryBytes; payload.insert(payload.begin(),3); f.feed(frame(payload)); r.poll(1);
  CHECK(r.battery(1).valid);
  payload[1]=9; payload[2]=0; // Invalid charging enum, valid checksum: reject atomically.
  f.feed(frame(payload)); r.poll(2); CHECK(r.framesReceived()==1); CHECK(r.battery(2).millivolts==14400);
  uint32_t noise=17;
  for (unsigned i=0;i<10000;++i) { noise=noise*1664525u+1013904223u; f.feed({uint8_t(noise>>24)}); r.poll(3); }
  payload=batteryBytes; payload.insert(payload.begin(),3);
  f.feed(frame(payload)); r.poll(4); CHECK(r.framesReceived()==2);
  // Baud budget: group 6 cannot fit in one 15 ms frame at 19200 baud.
  Fake slow; Roomba low(slow,Capabilities(),19200); low.beginPassive();
  Sensor all[]={Sensor::AllBase}; CHECK(low.startStream(all,1)==Result::InvalidArgument);
}
static void groupsAndSafety() {
  const Sensor ids[]={Sensor::Basic,Sensor::Safety,Sensor::Motion,Sensor::Battery,
    Sensor::Signals,Sensor::Status,Sensor::AllBase,Sensor::AllExtended,
    Sensor::Extended,Sensor::LightSignals,Sensor::MotorCurrentsAndStasis};
  const size_t sizes[]={26,10,6,10,14,12,52,80,28,12,9};
  for (unsigned i=0;i<11;++i) CHECK(Roomba::packetSize(ids[i])==sizes[i]);
  CHECK(Roomba::encoderDelta(2,65534)==4); CHECK(Roomba::encoderDelta(65534,2)==-4);
  Fake f; Capabilities caps; caps.extendedSensors=true; Roomba r(f,caps); r.beginPassive();
  CHECK(r.request(Sensor::AllExtended,0)==Result::Ok);
  std::vector<uint8_t> all(80,0); all[0]=10; all[2]=1; all[6]=1;
  // All extended group offset 52 is left encoder; keep modulo bits unsigned.
  all[52]=0xff; all[53]=0xfe; f.feed(all); r.poll(1);
  CHECK(r.sensor(Sensor::LeftEncoder).value==65534);
  const auto s=r.safety(1); CHECK(s.valid && s.bumpLeft && s.wheelDropLeft && s.cliffLeft && s.virtualWall);
  CHECK(!s.bumpRight && !s.wheelDropRight); CHECK(!r.safety(102).valid);
}
static void motionLeaseAndFaults() {
  Fake f; Roomba r(f); r.beginPassive(); r.enterSafe(); f.tx.clear();
  CHECK(r.driveWheels(100,100,0xffffff00u,300)==Result::Ok); f.tx.clear();
  r.poll(43); CHECK(f.tx.empty()); r.poll(44); f.expect({145,0,0,0,0,138,0,128});
  r.poll(100); CHECK(f.tx.empty());
  CHECK(r.requestedMode()==Mode::Passive);
  CHECK(r.enterSafe()==Result::Ok); f.tx.clear();
  CHECK(r.driveWheels(100,100,200)==Result::Ok); f.tx.clear();
  CHECK(r.enterFullDisablingSafety()==Result::Ok); f.expect({145,0,0,0,0,138,0,132});
  CHECK(r.request(Sensor::OiMode,220)==Result::Ok); f.feed({1}); r.poll(221);
  CHECK(r.driveWheels(100,100,222)==Result::WrongMode);
  // A delayed Passive report must not remove a live wheel deadline.
  CHECK(r.enterSafe()==Result::Ok);
  CHECK(r.driveWheels(100,100,300)==Result::Ok);
  CHECK(r.request(Sensor::OiMode,320)==Result::Ok); f.feed({1}); r.poll(321); f.tx.clear();
  r.poll(600); f.expect({145,0,0,0,0,138,0,128});
  Fake broken; Roomba bad(broken); bad.beginPassive(); bad.enterSafe(); broken.tx.clear();
  broken.failAfter=2;
  CHECK(bad.driveWheels(100,100,0)==Result::TransportFault);
  CHECK(broken.tx.size()==2); CHECK(bad.stopWheels()==Result::TransportFault);
  CHECK(broken.tx.size()==2); CHECK(bad.resynchronizeInput(0)==Result::TransportFault);
}
struct LinkFake : Fake {
  struct Edge { uint32_t time; bool low; };
  std::vector<Edge> edges;
  uint32_t clock=0, hostBaud=115200, robotBaud=115200;
  uint32_t scheduledBaud=0, baudSwitchTime=0, drainedAt=0, pulseStretch=0;
  unsigned pulseCount=0, flushes=0, failFlush=0;
  bool controls=true, brc=true, answer=true, baudOk=true, gpioOk=true, low=false;
  uint8_t answerMode=1;
  std::vector<uint8_t> commandBuffer;
  bool hasLinkControl() const override { return controls; }
  bool hasBrc() const override { return brc; }
  uint32_t nowMs() const override { return clock; }
  void waitMs(uint32_t ms) override { clock+=ms+(low?pulseStretch:0); }
  bool finishTransmit(uint32_t timeout) override {
    CHECK(timeout==1000); ++flushes;
    if (flushes==failFlush) return false;
    clock+=7; drainedAt=clock;
    if (scheduledBaud) { robotBaud=scheduledBaud; scheduledBaud=0; }
    return true;
  }
  bool setBaud(uint32_t rate) override {
    baudSwitchTime=clock;
    if (!baudOk) return false;
    hostBaud=rate; return true;
  }
  bool brcLow(bool value) override {
    edges.push_back({clock,value});
    if (!gpioOk) return false;
    if (value && !low) ++pulseCount;
    low=value;
    if (!low && pulseCount==3) robotBaud=19200;
    return true;
  }
  size_t write(const uint8_t* bytes,size_t n) override {
    const size_t written=Fake::write(bytes,n);
    for (size_t i=0;i<written;++i) {
      if (hostBaud!=robotBaud) continue;
      commandBuffer.push_back(bytes[i]);
      const uint8_t op=commandBuffer[0];
      if (op==128) { commandBuffer.clear(); continue; }
      if ((op==129 || op==142) && commandBuffer.size()==2) {
        if (op==129) {
          const uint32_t rates[]={300,600,1200,2400,4800,9600,14400,19200,28800,38400,57600,115200};
          scheduledBaud=rates[commandBuffer[1]];
        } else if (commandBuffer[1]==35 && answer) rx.push_back(answerMode);
        commandBuffer.clear();
      }
    }
    return written;
  }
};
static void linkMaintenance() {
  const uint32_t rates[]={300,600,1200,2400,4800,9600,14400,19200,28800,38400,57600,115200};
  for (uint8_t code=0;code<12;++code) {
    LinkFake f; Roomba r(f); r.beginPassive(); f.tx.clear();
    CHECK(r.changeBaud(rates[code])==Result::Ok);
    CHECK(f.tx==std::vector<uint8_t>({129,code,128,142,35}));
    CHECK(r.baudRate()==rates[code] && f.hostBaud==rates[code] && f.robotBaud==rates[code]);
    CHECK(r.connectionVerified());
    // Two TX drains consume 14ms, then the full 100ms guard before host switch.
    CHECK(f.baudSwitchTime==114);
  }
  {
    LinkFake f; Roomba r(f); r.beginPassive(); f.tx.clear();
    CHECK(r.changeBaud(230400)==Result::InvalidArgument); CHECK(f.tx.empty());
    r.enterSafe(); f.tx.clear();
    CHECK(r.changeBaud(19200)==Result::WrongMode);
    CHECK(r.wakeViaBrc()==Result::WrongMode); CHECK(f.tx.empty() && f.edges.empty());
  }
  {
    LinkFake f; Roomba r(f); r.beginPassive(); r.requestBattery(0); f.tx.clear();
    CHECK(r.changeBaud(19200)==Result::Busy); CHECK(r.wakeViaBrc()==Result::Busy);
    CHECK(f.tx.empty());
  }
  {
    LinkFake f; Roomba r(f); r.beginPassive(); Sensor ids[]={Sensor::Battery};
    r.startStream(ids,1); r.pauseStream(); f.tx.clear();
    CHECK(r.changeBaud(19200)==Result::Busy); CHECK(r.forceBaud19200ViaBrc()==Result::Busy);
    CHECK(f.tx.empty());
  }
  {
    LinkFake f; f.clock=0xfffffff0u; Roomba r(f);
    CHECK(r.wakeViaBrc()==Result::Ok); CHECK(r.connectionVerified());
    CHECK(f.edges.size()==2 && f.edges[0].low && !f.edges[1].low);
    CHECK(uint32_t(f.edges[1].time-f.edges[0].time)==100);
    CHECK(r.baudRate()==115200); CHECK(f.tx==std::vector<uint8_t>({128,142,35}));
    CHECK(uint32_t(f.clock-0xfffffff0u)>=2200);
  }
  {
    LinkFake f; f.robotBaud=57600; Roomba r(f);
    CHECK(r.forceBaud19200ViaBrc()==Result::Ok);
    CHECK(f.edges.size()==7 && !f.edges[0].low);
    CHECK(f.edges[1].time-f.edges[0].time==2000);
    for(unsigned i=1;i<7;i+=2) {
      CHECK(f.edges[i].low && !f.edges[i+1].low);
      CHECK(f.edges[i+1].time-f.edges[i].time==100);
    }
    CHECK(f.edges[3].time-f.edges[2].time==100);
    CHECK(f.edges[5].time-f.edges[4].time==100);
    CHECK(r.baudRate()==19200 && r.connectionVerified());
  }
  {
    LinkFake f; f.answer=false; Roomba r(f);
    CHECK(r.wakeViaBrc()==Result::Timeout); CHECK(!r.connectionVerified());
    CHECK(r.enterSafe()==Result::ResyncRequired);
    CHECK(r.defineSong(0,nullptr,0)==Result::InvalidArgument);
    Note note={60,10}; CHECK(r.defineSong(0,&note,1)==Result::ResyncRequired);
    CHECK(f.edges.size()==2 && !f.low); // No hidden pulse retry train.
    f.answer=true; CHECK(r.verifyConnection()==Result::Ok); CHECK(r.connectionVerified());
  }
  {
    LinkFake f; f.answerMode=2; Roomba r(f);
    CHECK(r.verifyConnection()==Result::Malformed); CHECK(!r.connectionVerified());
    f.answerMode=1; CHECK(r.verifyConnection()==Result::Ok);
  }
  {
    LinkFake f; f.answer=false; f.rx.push_back(1); Roomba r(f);
    CHECK(r.wakeViaBrc()==Result::Timeout); // Old buffered mode is not a fresh ACK.
    CHECK(!r.connectionVerified());
  }
  {
    LinkFake f; f.baudOk=false; Roomba r(f); r.beginPassive(); f.tx.clear();
    CHECK(r.changeBaud(19200)==Result::TransportFault);
    CHECK(!r.connectionVerified()); CHECK(f.robotBaud==19200 && f.hostBaud==115200);
    CHECK(r.enterSafe()==Result::ResyncRequired); CHECK(f.tx==std::vector<uint8_t>({129,7}));
    f.baudOk=true; CHECK(r.forceBaud19200ViaBrc()==Result::Ok);
  }
  for(unsigned fail=1;fail<=2;++fail) {
    LinkFake f; f.failFlush=fail; Roomba r(f); r.beginPassive(); f.tx.clear();
    CHECK(r.changeBaud(19200)==Result::TransportFault);
    CHECK(r.verifyConnection()==Result::TransportFault); CHECK(f.baudSwitchTime==0);
  }
  {
    LinkFake f; f.pulseStretch=501; Roomba r(f);
    CHECK(r.forceBaud19200ViaBrc()==Result::TransportFault);
    CHECK(!f.low && f.pulseCount==1); CHECK(f.hostBaud==115200 && f.tx.empty());
  }
  {
    LinkFake f; f.brc=false; Roomba r(f);
    CHECK(r.wakeViaBrc()==Result::Unsupported); CHECK(f.edges.empty() && f.tx.empty());
    f.controls=false; CHECK(r.verifyConnection()==Result::Unsupported);
  }
  {
    LinkFake f; Roomba r(f); r.beginPassive(); f.failAfter=f.tx.size()+1;
    CHECK(r.changeBaud(19200)==Result::TransportFault); CHECK(f.baudSwitchTime==0);
  }
}
static void allSensorWidths() {
  // Independent packet-width/signedness reference, from the iRobot tables.
  const uint8_t sizes[]={1,1,1,1,1,1,1,1,1,1,1,1,2,2,1,2,2,1,2,2,
    2,2,2,2,2,1,2,1,1,1,1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1,1,2,2,2,2,1};
  for (uint8_t id=7;id<=58;++id) {
    Fake f; Capabilities caps; caps.extendedSensors=true; Roomba r(f,caps); r.beginPassive();
    CHECK(Roomba::packetSize(Sensor(id))==sizes[id-7]);
    CHECK(r.request(Sensor(id),0)==Result::Ok);
    std::vector<uint8_t> bytes(sizes[id-7],255);
    if (id==21)bytes[0]=5; if (id==35)bytes[0]=1;
    f.feed(bytes); r.poll(1); CHECK(r.sensor(Sensor(id)).valid);
    const bool isSigned=id==19 || id==20 || id==23 || id==24 || (id>=39 && id<=42) || (id>=54 && id<=57);
    const int32_t expected=id==21?5:id==35?1:isSigned?-1:sizes[id-7]==1?255:65535;
    CHECK(r.sensor(Sensor(id)).value==expected);
  }
}
int main() {
  commands(); queries(); limitsAndTimeouts(); streams(); groupsAndSafety(); motionLeaseAndFaults();
  linkMaintenance(); allSensorWidths();
  std::printf("PASS: %u checks (wire fixtures, parsing, errors, leases, fuzz, capability gates)\n",checks);
}
