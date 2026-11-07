#include "RoombaOI.h"
#include <string.h>

namespace roocore {
namespace {
bool range(int v, int lo, int hi) { return v >= lo && v <= hi; }
void be16(uint8_t* out, int v) {
  const uint16_t bits = static_cast<uint16_t>(v);
  out[0] = uint8_t(bits >> 8); out[1] = uint8_t(bits);
}
bool group(uint8_t id, uint8_t& first, uint8_t& last) {
  switch (id) {
    case 0: first=7; last=26; break;
    case 1: first=7; last=16; break;
    case 2: first=17; last=20; break;
    case 3: first=21; last=26; break;
    case 4: first=27; last=34; break;
    case 5: first=35; last=42; break;
    case 6: first=7; last=42; break;
    case 100: first=7; last=58; break;
    case 101: first=43; last=58; break;
    case 106: first=46; last=51; break;
    case 107: first=54; last=58; break;
    default: return false;
  }
  return true;
}
size_t width(uint8_t id) {
  if (id < 7 || id > 58) return 0;
  if (id <= 18 || id == 21 || id == 24 || id == 32 ||
      (id >= 34 && id <= 38) || id == 45 || id == 52 || id == 53 || id == 58) return 1;
  return 2;
}
bool signedValue(uint8_t id) {
  // Encoders are deliberately exposed as unsigned modulo-65536 counters.
  return id==19 || id==20 || id==23 || id==24 ||
         (id>=39 && id<=42) || (id>=54 && id<=57);
}
bool active(Mode m) { return m == Mode::Safe || m == Mode::Full; }
}

Roomba::Roomba(Transport& t, Capabilities caps, uint32_t baud)
  : io_(t), caps_(caps), baud_(baud) {}
Result Roomba::send(const uint8_t* bytes, size_t count) {
  if (txFault_) return Result::TransportFault;
  if (linkBusy_) return Result::Busy;
  if (verificationRequired_ && !probing_) return Result::ResyncRequired;
  size_t offset=0;
  while (offset<count) {
    const size_t n=io_.write(bytes+offset, count-offset);
    if (!n || n>count-offset) {
      txFault_=true; event_=Result::TransportFault;
      // A partial command may leave the robot waiting for operands. Do not
      // append another opcode and pretend that it can be interpreted safely.
      return event_;
    }
    offset+=n;
  }
  return Result::Ok;
}
Result Roomba::command(uint8_t opcode, bool actuator) {
  if (mode_ == Mode::Off || (actuator && !active(mode_))) return Result::WrongMode;
  return send(&opcode,1);
}
Result Roomba::changeMode(uint8_t opcode, Mode target) {
  if (opcode != 128 && verificationRequired_) return Result::ResyncRequired;
  if (opcode != 128 && mode_ == Mode::Off) return Result::WrongMode;
  if (lease_) { const Result stopped=stopAll(); if (stopped!=Result::Ok) return stopped; }
  const Result r=send(&opcode,1);
  if (r==Result::Ok) { mode_=target; lease_=false; samples_[35].valid=false; }
  return r;
}
Result Roomba::beginPassive() { return changeMode(128,Mode::Passive); }
Result Roomba::enterSafe() { return changeMode(131,Mode::Safe); }
Result Roomba::enterFullDisablingSafety() { return changeMode(132,Mode::Full); }
Result Roomba::clean() { return changeMode(135,Mode::Passive); }
Result Roomba::spot() { return changeMode(134,Mode::Passive); }
Result Roomba::maxClean() { return changeMode(136,Mode::Passive); }
Result Roomba::seekDock() { return changeMode(143,Mode::Passive); }
Result Roomba::powerDownRobot() { return changeMode(133,Mode::Passive); }
Result Roomba::stopInterface() {
  if (!caps_.resetAndStop) return Result::Unsupported;
  const Result r=changeMode(173,Mode::Off);
  if (r==Result::Ok) { pending_=stream_=paused_=false; connectionVerified_=false; used_=0; invalidateSamples(); }
  return r;
}
Result Roomba::resetRobot() {
  if (!caps_.resetAndStop) return Result::Unsupported;
  const uint8_t opcode=7;
  const Result r=send(&opcode,1);
  if (r==Result::Ok) {
    mode_=Mode::Off; lease_=pending_=stream_=paused_=false;
    desync_=true; connectionVerified_=false; used_=0; invalidateSamples(); // Boot text must be drained.
  }
  return r;
}
Result Roomba::linkPreflight(bool needBrc) const {
  if (txFault_) return Result::TransportFault;
  if (!io_.hasLinkControl() || (needBrc && !io_.hasBrc())) return Result::Unsupported;
  if (linkBusy_ || pending_ || stream_ || draining_) return Result::Busy;
  if (lease_ || (active(mode_) && !verificationRequired_)) return Result::WrongMode;
  return Result::Ok;
}
Result Roomba::linkFailure(Result error) {
  linkBusy_=probing_=false; connectionVerified_=false; verificationRequired_=true;
  desync_=true; pending_=false; used_=0; invalidateSamples(); event_=error;
  // A failed TX drain or baud reconfiguration leaves the serial link uncertain.
  // Do not automatically roll the host back: the robot may have accepted 129.
  return error;
}
bool Roomba::finishTx() {
  if (io_.finishTransmit(1000)) return true;
  txFault_=true; // bytes may still be queued or truncated; require link reset
  return false;
}
Result Roomba::probeLink() {
  verificationRequired_=true; connectionVerified_=false; invalidateSamples();
  linkBusy_=true;
  if (!finishTx()) return linkFailure(Result::TransportFault);
  const uint32_t started=io_.nowMs(); uint32_t quietSince=started;
  while (uint32_t(io_.nowMs()-quietSince)<100) {
    for (size_t i=0;i<1024;++i) {
      if (io_.read()<0) break;
      quietSince=io_.nowMs();
    }
    if (uint32_t(io_.nowMs()-started)>=1000) return linkFailure(Result::Timeout);
    io_.waitMs(1);
  }
  linkBusy_=false; probing_=true; desync_=draining_=queried_=false; used_=0;
  Result r=beginPassive(); if (r!=Result::Ok) return linkFailure(r);
  linkBusy_=true;
  if (!finishTx()) return linkFailure(Result::TransportFault);
  io_.waitMs(20); // conservative OI startup settling; not a model-specific ACK
  linkBusy_=false;
  r=request(Sensor::OiMode,io_.nowMs(),500);
  if (r!=Result::Ok) return linkFailure(r);
  while (pending_) {
    poll(io_.nowMs());
    if (pending_) io_.waitMs(1);
  }
  const Sample mode=sensor(Sensor::OiMode);
  if (event_!=Result::Ok) return linkFailure(event_);
  if (!mode.valid || mode.value!=int32_t(Mode::Passive)) return linkFailure(Result::Malformed);
  probing_=verificationRequired_=false; connectionVerified_=true; event_=Result::Ok;
  return Result::Ok;
}
Result Roomba::verifyConnection() {
  const Result ready=linkPreflight(false);
  return ready==Result::Ok ? probeLink() : ready;
}
Result Roomba::changeBaud(uint32_t baud) {
  const uint32_t rates[]={300,600,1200,2400,4800,9600,14400,19200,28800,38400,57600,115200};
  uint8_t code=0; while (code<12 && rates[code]!=baud) ++code;
  if (code==12) return Result::InvalidArgument;
  const Result ready=linkPreflight(false); if (ready!=Result::Ok) return ready;
  if (mode_!=Mode::Passive) return Result::WrongMode;
  if (verificationRequired_ || desync_) return Result::ResyncRequired;
  linkBusy_=true;
  if (!finishTx()) return linkFailure(Result::TransportFault);
  linkBusy_=false;
  const uint8_t bytes[]={129,code}; const Result sent=send(bytes,2);
  if (sent!=Result::Ok) return linkFailure(sent);
  linkBusy_=true; verificationRequired_=true; connectionVerified_=false;
  if (!finishTx()) return linkFailure(Result::TransportFault);
  // Start the required delay AFTER the last stop bit, not when write() queued it.
  io_.waitMs(100);
  if (!io_.setBaud(baud)) return linkFailure(Result::TransportFault);
  baud_=baud; linkBusy_=false;
  return probeLink();
}
bool Roomba::brcPulse() {
  if (!io_.brcLow(true)) { io_.brcLow(false); return false; }
  const uint32_t started=io_.nowMs(); io_.waitMs(100);
  const uint32_t elapsed=uint32_t(io_.nowMs()-started);
  const bool released=io_.brcLow(false);
  // If the scheduler stalled, release the line and abort the pulse sequence.
  return released && elapsed>=50 && elapsed<=500;
}
Result Roomba::wakeViaBrc() {
  const Result ready=linkPreflight(true); if (ready!=Result::Ok) return ready;
  linkBusy_=true; verificationRequired_=true; connectionVerified_=false;
  if (!finishTx()) return linkFailure(Result::TransportFault);
  if (!brcPulse()) return linkFailure(Result::TransportFault);
  io_.waitMs(2000); // wake/boot settling; one pulse, never an automatic pulse train
  linkBusy_=false;
  return probeLink(); // UART rate is unchanged. A pulse alone is not success.
}
Result Roomba::forceBaud19200ViaBrc() {
  const Result ready=linkPreflight(true); if (ready!=Result::Ok) return ready;
  linkBusy_=true; verificationRequired_=true; connectionVerified_=false;
  if (!finishTx()) return linkFailure(Result::TransportFault);
  if (!io_.brcLow(false)) return linkFailure(Result::TransportFault);
  // Robot must already be powered on. This is not a power-on circuit.
  io_.waitMs(2000);
  for (unsigned i=0;i<3;++i) {
    if (!brcPulse()) return linkFailure(Result::TransportFault);
    io_.waitMs(100);
  }
  if (!io_.setBaud(19200)) return linkFailure(Result::TransportFault);
  baud_=19200; linkBusy_=false;
  return probeLink();
}
Result Roomba::actuate(const uint8_t* bytes, size_t count) {
  if (desync_ || draining_ || verificationRequired_) return Result::ResyncRequired;
  return active(mode_) ? send(bytes,count) : Result::WrongMode;
}
Result Roomba::wheels(uint8_t opcode,int a,int b,uint32_t now,uint32_t leaseMs) {
  if (!leaseMs || leaseMs>2000) return Result::InvalidArgument;
  uint8_t bytes[5]={opcode,0,0,0,0}; be16(bytes+1,a); be16(bytes+3,b);
  const Result r=actuate(bytes,5);
  if (r==Result::Ok) { lease_=true; leaseStart_=now; leaseMs_=leaseMs; }
  return r;
}
Result Roomba::driveStraight(int speed,uint32_t now,uint32_t leaseMs) {
  if (!range(speed,-500,500)) return Result::InvalidArgument;
  return wheels(137,speed,32767,now,leaseMs);
}
Result Roomba::driveArc(int speed,int radius,uint32_t now,uint32_t leaseMs) {
  if (!range(speed,-500,500) || !range(radius,-2000,2000) || radius==0 || radius==1 || radius==-1)
    return Result::InvalidArgument;
  return wheels(137,speed,radius,now,leaseMs);
}
Result Roomba::turnInPlace(int speed,Turn direction,uint32_t now,uint32_t leaseMs) {
  if (!range(speed,0,500) || (direction!=Turn::Clockwise && direction!=Turn::CounterClockwise))
    return Result::InvalidArgument;
  return wheels(137,speed,direction==Turn::Clockwise ? -1 : 1,now,leaseMs);
}
Result Roomba::driveWheels(int left,int right,uint32_t now,uint32_t leaseMs) {
  if (!range(left,-500,500) || !range(right,-500,500)) return Result::InvalidArgument;
  return wheels(145,right,left,now,leaseMs);
}
Result Roomba::driveWheelPwm(int left,int right,uint32_t now,uint32_t leaseMs) {
  if (!range(left,-255,255) || !range(right,-255,255)) return Result::InvalidArgument;
  return wheels(146,right,left,now,leaseMs);
}
Result Roomba::stopWheels() {
  const uint8_t bytes[]={145,0,0,0,0};
  if (mode_==Mode::Off && !lease_) return Result::WrongMode;
  const Result r=send(bytes,sizeof(bytes));
  if (r==Result::Ok) lease_=false;
  return r; // In Passive the robot may ignore Drive Direct; not an E-stop.
}
Result Roomba::stopAll() {
  Result r=stopWheels(); if (r!=Result::Ok) return r;
  const uint8_t bytes[]={138,0}; return send(bytes,2);
}
Result Roomba::releaseControl() {
  if (!active(mode_)) return Result::WrongMode;
  const Result r=stopAll(); return r==Result::Ok ? beginPassive() : r;
}
Result Roomba::setMotors(Motors m) {
  const uint8_t bits=uint8_t((m.sideBrush?1:0)|(m.vacuum?2:0)|(m.mainBrush?4:0)|
                            (m.reverseSide?8:0)|(m.reverseMain?16:0));
  const uint8_t bytes[]={138,bits}; return actuate(bytes,2);
}
Result Roomba::setMotorPwm(int main,int side,int vacuum) {
  if (!range(main,-127,127)||!range(side,-127,127)||!range(vacuum,0,127)) return Result::InvalidArgument;
  const uint8_t bytes[]={144,uint8_t(main),uint8_t(side),uint8_t(vacuum)};
  return actuate(bytes,4);
}
Result Roomba::setLeds(Leds l) {
  const uint8_t bits=uint8_t((l.debris?1:0)|(l.spot?2:0)|(l.dock?4:0)|(l.checkRobot?8:0));
  const uint8_t bytes[]={139,bits,l.color,l.intensity}; return actuate(bytes,4);
}
Result Roomba::defineSong(uint8_t slot,const Note* notes,size_t n) {
  // Four slots; use 0..3 despite inconsistent 0..4 prose in the PDF.
  if (slot>3 || !notes || n<1 || n>16) return Result::InvalidArgument;
  if (mode_==Mode::Off) return Result::WrongMode;
  uint8_t bytes[35]={140,slot,uint8_t(n)};
  for (size_t i=0;i<n;++i) {
    if (notes[i].midi!=0 && !range(notes[i].midi,31,127)) return Result::InvalidArgument;
    bytes[3+2*i]=notes[i].midi; bytes[4+2*i]=notes[i].ticks64;
  }
  return send(bytes,3+2*n);
}
Result Roomba::playSong(uint8_t slot) {
  if (slot>3) return Result::InvalidArgument;
  const uint8_t bytes[]={141,slot}; return actuate(bytes,2);
}
Result Roomba::pressButton(Button button) {
  const uint8_t b=uint8_t(button);
  if (!b || (b & (b-1))) return Result::InvalidArgument;
  if (b>4 && !caps_.schedulingAndDisplay) return Result::Unsupported;
  if (mode_==Mode::Off) return Result::WrongMode;
  const uint8_t bytes[]={165,b};
  const Result r=send(bytes,2);
  if (r==Result::Ok && b<=4) { mode_=Mode::Passive; lease_=false; samples_[35].valid=false; }
  return r;
}
Result Roomba::setClock(Day day,uint8_t h,uint8_t m) {
  if (!caps_.schedulingAndDisplay) return Result::Unsupported;
  if (uint8_t(day)>6 || h>23 || m>59) return Result::InvalidArgument;
  if (mode_==Mode::Off) return Result::WrongMode;
  const uint8_t bytes[]={168,uint8_t(day),h,m}; return send(bytes,4);
}
Result Roomba::setSchedule(const ScheduleEntry (&week)[7]) {
  if (!caps_.schedulingAndDisplay) return Result::Unsupported;
  if (mode_==Mode::Off) return Result::WrongMode;
  uint8_t bytes[16]={167,0};
  for (size_t i=0;i<7;++i) {
    if (week[i].hour>23 || week[i].minute>59) return Result::InvalidArgument;
    if (week[i].enabled) {
      bytes[1]|=uint8_t(1u<<i); bytes[2+2*i]=week[i].hour; bytes[3+2*i]=week[i].minute;
    }
  }
  return send(bytes,16);
}
Result Roomba::setSchedulingLeds(uint8_t days,uint8_t indicators) {
  if (!caps_.schedulingAndDisplay) return Result::Unsupported;
  if (days>127 || indicators>31) return Result::InvalidArgument;
  const uint8_t bytes[]={162,days,indicators}; return actuate(bytes,3);
}
Result Roomba::displayText(const char (&text)[5]) {
  if (!caps_.schedulingAndDisplay) return Result::Unsupported;
  uint8_t bytes[5]={164};
  for (size_t i=0;i<4;++i) {
    if (!range(text[i],32,126)) return Result::InvalidArgument;
    bytes[i+1]=uint8_t(text[i]);
  }
  if (text[4]!='\0') return Result::InvalidArgument;
  return actuate(bytes,5);
}
Result Roomba::displaySegments(const uint8_t (&s)[4]) {
  if (!caps_.schedulingAndDisplay) return Result::Unsupported;
  const uint8_t bytes[]={163,s[0],s[1],s[2],s[3]}; return actuate(bytes,5);
}

size_t Roomba::packetSize(Sensor sensor) {
  const uint8_t id=uint8_t(sensor); uint8_t first=0,last=0;
  if (!group(id,first,last)) return width(id);
  size_t total=0; for (uint8_t i=first;i<=last;++i) total+=width(i);
  return total;
}
bool Roomba::supported(Sensor s) const {
  const uint8_t id=uint8_t(s);
  return packetSize(s) && (caps_.extendedSensors || (id<=42));
}
Result Roomba::validate(const Sensor* ids,size_t n,size_t& bytes,bool streaming) const {
  if (!ids || !n || n>32) return Result::InvalidArgument;
  bytes=0; uint64_t seen=0;
  for (size_t i=0;i<n;++i) {
    if (!packetSize(ids[i])) return Result::InvalidArgument;
    if (!supported(ids[i])) return Result::Unsupported;
    uint8_t first=uint8_t(ids[i]),last=first; group(first,first,last);
    for (uint8_t id=first;id<=last;++id) {
      const uint64_t bit=uint64_t(1)<<id;
      if (seen&bit) return Result::InvalidArgument; // Also rejects overlapping groups.
      seen|=bit;
    }
    bytes+=packetSize(ids[i])+(streaming?1:0);
  }
  if (bytes>255 || (streaming && (bytes+3)*10000u>uint64_t(baud_)*15u))
    return Result::InvalidArgument;
  return Result::Ok;
}
Result Roomba::request(Sensor id,uint32_t now,uint32_t timeout) {
  // Same receiver as Query List, but use the dedicated Sensors opcode.
  if (mode_==Mode::Off) return Result::WrongMode;
  if (desync_ || draining_) return Result::ResyncRequired;
  if (pending_ || stream_ || (queried_ && uint32_t(now-lastQuery_)<15)) return Result::Busy;
  if (!timeout || timeout>60000) return Result::InvalidArgument;
  size_t bytes=0; Result r=validate(&id,1,bytes,false); if (r!=Result::Ok) return r;
  const uint8_t cmd[]={142,uint8_t(id)}; r=send(cmd,2);
  if (r==Result::Ok) {
    selection_[0]=id; selected_=1; expected_=bytes; used_=0;
    pending_=queried_=true; deadline_=now+timeout; lastQuery_=now;
    event_=Result::Ok;
  }
  return r;
}
Result Roomba::request(const Sensor* ids,size_t n,uint32_t now,uint32_t timeout) {
  if (mode_==Mode::Off) return Result::WrongMode;
  if (desync_ || draining_) return Result::ResyncRequired;
  if (pending_ || stream_ || (queried_ && uint32_t(now-lastQuery_)<15)) return Result::Busy;
  if (!timeout || timeout>60000) return Result::InvalidArgument;
  size_t bytes=0; Result r=validate(ids,n,bytes,false); if (r!=Result::Ok) return r;
  uint8_t cmd[34]={149,uint8_t(n)};
  for (size_t i=0;i<n;++i) cmd[2+i]=uint8_t(ids[i]);
  r=send(cmd,n+2);
  if (r==Result::Ok) {
    memcpy(selection_,ids,n*sizeof(Sensor)); selected_=n; expected_=bytes;
    used_=0; pending_=queried_=true; deadline_=now+timeout; lastQuery_=now;
    event_=Result::Ok;
  }
  return r;
}
Result Roomba::startStream(const Sensor* ids,size_t n) {
  if (mode_==Mode::Off) return Result::WrongMode;
  if (desync_ || draining_) return Result::ResyncRequired;
  if (pending_ || stream_) return Result::Busy;
  size_t bytes=0; Result r=validate(ids,n,bytes,true); if (r!=Result::Ok) return r;
  uint8_t cmd[34]={148,uint8_t(n)};
  for (size_t i=0;i<n;++i) cmd[i+2]=uint8_t(ids[i]);
  r=send(cmd,n+2);
  if (r==Result::Ok) {
    memcpy(selection_,ids,n*sizeof(Sensor)); selected_=n; expected_=bytes;
    used_=0; stream_=true; paused_=false; event_=Result::Ok;
  }
  return r;
}
Result Roomba::pauseStream() {
  if (!stream_) return Result::WrongMode;
  const uint8_t cmd[]={150,0}; const Result r=send(cmd,2);
  if (r==Result::Ok) paused_=true;
  return r;
}
Result Roomba::resumeStream() {
  if (!stream_) return Result::WrongMode;
  const uint8_t cmd[]={150,1}; const Result r=send(cmd,2);
  if (r==Result::Ok) paused_=false;
  return r;
}
Result Roomba::resynchronizeInput(uint32_t now) {
  if (txFault_) return Result::TransportFault;
  if (pending_ || draining_) return Result::Busy;
  if (stream_) { const Result r=pauseStream(); if (r!=Result::Ok) return r; }
  stream_=paused_=false; draining_=true; desync_=true;
  lastRx_=now; used_=0; event_=Result::ResyncRequired; invalidateSamples(); return Result::Ok;
}
void Roomba::invalidateSamples() { for (auto& s:samples_) s.valid=false; }
bool Roomba::decode(uint8_t id,const uint8_t* bytes,size_t n,uint32_t now,bool commit) {
  if (n!=packetSize(Sensor(id))) return false;
  uint8_t first=id,last=id; group(id,first,last); size_t offset=0;
  for (uint8_t item=first;item<=last;++item) {
    const size_t size=width(item); if (!size || offset+size>n) return false;
    int32_t v=bytes[offset];
    if (size==2) v=(v<<8)|bytes[offset+1];
    if (signedValue(item)) {
      if (size==1 && v>=128) v-=256;
      if (size==2 && v>=32768) v-=65536;
    }
    if (item==35 && v>3) return false;
    if (item==21 && v>5) return false;
    if (commit) {
      Sample& s=samples_[item]; s.value=v; s.timestampMs=now;
      s.generation=generation_; s.valid=true;
      // A queued mode report may predate a newly sent motion command. Never
      // cancel its deadline merely because a received report says Passive.
      if (item==35) mode_=Mode(v);
    }
    offset+=size;
  }
  return offset==n;
}
void Roomba::finishQuery(uint32_t now) {
  size_t offset=0;
  for (size_t i=0;i<selected_;++i) {
    const size_t n=packetSize(selection_[i]);
    if (!decode(uint8_t(selection_[i]),rx_+offset,n,now,false)) {
      event_=Result::Malformed; pending_=false; desync_=true;
      used_=0; invalidateSamples(); return;
    }
    offset+=n;
  }
  ++generation_; offset=0;
  for (size_t i=0;i<selected_;++i) {
    const size_t n=packetSize(selection_[i]);
    decode(uint8_t(selection_[i]),rx_+offset,n,now,true); offset+=n;
  }
  pending_=false; used_=0; event_=Result::Ok; ++frames_;
  notifySamples();
}
void Roomba::streamByte(uint8_t byte,uint32_t now) {
  if (used_==sizeof(rx_)) { memmove(rx_,rx_+1,--used_); ++rejected_; }
  rx_[used_++]=byte;
  while (used_) {
    if (rx_[0]!=19) { memmove(rx_,rx_+1,--used_); continue; }
    if (used_<2) return;
    if (rx_[1]!=expected_) { memmove(rx_,rx_+1,--used_); ++rejected_; continue; }
    const size_t total=size_t(rx_[1])+3;
    if (used_<total) return;
    uint8_t sum=0; for (size_t i=0;i<total;++i) sum=uint8_t(sum+rx_[i]);
    bool valid=sum==0; size_t offset=2;
    for (size_t i=0;valid && i<selected_;++i) {
      const uint8_t id=uint8_t(selection_[i]); const size_t n=packetSize(selection_[i]);
      valid=offset+1+n<=total-1 && rx_[offset]==id && decode(id,rx_+offset+1,n,now,false);
      offset+=1+n;
    }
    valid=valid && offset==total-1;
    if (!valid) {
      ++rejected_; event_=Result::Malformed;
      memmove(rx_,rx_+1,--used_); continue;
    }
    ++generation_; offset=2;
    for (size_t i=0;i<selected_;++i) {
      const size_t n=packetSize(selection_[i]);
      decode(uint8_t(selection_[i]),rx_+offset+1,n,now,true); offset+=1+n;
    }
    ++frames_; event_=Result::Ok;
    notifySamples();
    used_-=total; memmove(rx_,rx_+total,used_);
  }
}
void Roomba::notifySamples() {
  if (!observer_) return;
  for (unsigned id=7; id<=58; ++id)
    if (samples_[id].valid && samples_[id].generation==generation_)
      observer_(observerContext_, Sensor(id), samples_[id]);
}
void Roomba::poll(uint32_t now) {
  if (linkBusy_) return;
  if (lease_ && uint32_t(now-leaseStart_)>=leaseMs_) {
    Result r=stopAll();
    if (r==Result::Ok) r=beginPassive();
    lease_=false; event_=r;
  }
  if (pending_ && int32_t(now-deadline_)>=0) {
    pending_=false; used_=0; desync_=true; invalidateSamples(); event_=Result::Timeout; connectionVerified_=false;
  }
  if (stream_ && used_ && uint32_t(now-lastRx_)>100) { used_=0; ++rejected_; }
  // Bound work per call even if a transport supplies endless input.
  for (size_t budget=0;budget<1024;++budget) {
    const int b=io_.read(); if (b<0) break;
    lastRx_=now;
    if (draining_ || desync_) continue;
    if (pending_) {
      rx_[used_++]=uint8_t(b);
      if (used_==expected_) finishQuery(now);
    } else if (stream_) streamByte(uint8_t(b),now);
    else { desync_=true; event_=Result::ResyncRequired; invalidateSamples(); }
  }
  if (draining_ && uint32_t(now-lastRx_)>=100) {
    draining_=desync_=false; queried_=false; used_=0; event_=Result::Ok;
  }
}
StasisState Roomba::stasis(uint32_t now,uint32_t maxAgeMs) const {
  StasisState result;result.sample=sensor(Sensor::Stasis);
  if(!result.sample.fresh(now,maxAgeMs))return result;
  if(result.sample.value>=0 && result.sample.value<=2)
    result.meaning=StasisMeaning(result.sample.value);
  result.usableForProgress=result.meaning==StasisMeaning::NoForwardProgress || result.meaning==StasisMeaning::ForwardProgress;
  result.maintenanceSuggested=result.meaning==StasisMeaning::CasterNeedsCleaning;
  return result;
}
Sample Roomba::sensor(Sensor id) const {
  const uint8_t i=uint8_t(id); return i>=7 && i<=58 ? samples_[i] : Sample();
}
BatteryState Roomba::battery(uint32_t now,uint32_t maxAge) const {
  BatteryState b; const uint32_t gen=samples_[21].generation;
  for (uint8_t i=21;i<=26;++i)
    if (!samples_[i].fresh(now,maxAge) || samples_[i].generation!=gen) return b;
  b.valid=true; b.charging=Charging(samples_[21].value);
  b.millivolts=uint16_t(samples_[22].value); b.milliamps=int16_t(samples_[23].value);
  b.temperatureC=int8_t(samples_[24].value); b.chargeMah=uint16_t(samples_[25].value);
  b.capacityMah=uint16_t(samples_[26].value);
  b.percentageValid=b.capacityMah>0 && b.chargeMah<=b.capacityMah;
  if (b.percentageValid) b.percentage=100.0f*b.chargeMah/b.capacityMah;
  return b;
}
int32_t Roomba::encoderDelta(uint16_t current,uint16_t previous) {
  const uint16_t diff=uint16_t(current-previous);
  return diff>=32768 ? int32_t(diff)-65536 : diff;
}
SafetyState Roomba::safety(uint32_t now,uint32_t maxAge) const {
  SafetyState s;
  const uint8_t ids[]={7,9,10,11,12,13};
  for (uint8_t id:ids)
    if (!samples_[id].fresh(now,maxAge) || samples_[id].generation!=samples_[7].generation) return s;
  s.valid=true;
  s.bumpLeft=(samples_[7].value & 2)!=0; s.bumpRight=(samples_[7].value & 1)!=0;
  s.wheelDropLeft=(samples_[7].value & 8)!=0; s.wheelDropRight=(samples_[7].value & 4)!=0;
  s.cliffLeft=samples_[9].value!=0; s.cliffFrontLeft=samples_[10].value!=0;
  s.cliffFrontRight=samples_[11].value!=0; s.cliffRight=samples_[12].value!=0;
  s.virtualWall=samples_[13].value!=0;
  return s;
}
} // namespace roocore
