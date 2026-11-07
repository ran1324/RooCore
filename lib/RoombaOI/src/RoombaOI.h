#pragma once
#include <stddef.h>
#include <stdint.h>

namespace roocore {

enum class Result { Ok, InvalidArgument, WrongMode, Unsupported, Busy,
                    TransportFault, Timeout, Malformed, ResyncRequired };
enum class Mode : uint8_t { Off = 0, Passive = 1, Safe = 2, Full = 3 };
enum class Sensor : uint8_t {
  Basic=0, Safety=1, Motion=2, Battery=3, Signals=4, Status=5, AllBase=6,
  BumpsAndDrops=7, Wall=8, CliffLeft=9, CliffFrontLeft=10,
  CliffFrontRight=11, CliffRight=12, VirtualWall=13, Overcurrents=14,
  DirtDetect=15, Unused16=16, InfraredOmni=17, Buttons=18,
  DistanceMm=19, AngleDegrees=20, ChargingState=21, VoltageMv=22,
  CurrentMa=23, TemperatureC=24, ChargeMah=25, CapacityMah=26,
  WallSignal=27, CliffLeftSignal=28, CliffFrontLeftSignal=29,
  CliffFrontRightSignal=30, CliffRightSignal=31, Unused32=32, Unused33=33,
  ChargingSources=34, OiMode=35, SongNumber=36, SongPlaying=37,
  StreamCount=38, RequestedVelocity=39, RequestedRadius=40,
  RequestedRightVelocity=41, RequestedLeftVelocity=42,
  LeftEncoder=43, RightEncoder=44, LightBumper=45, LightLeft=46,
  LightFrontLeft=47, LightCenterLeft=48, LightCenterRight=49,
  LightFrontRight=50, LightRight=51, InfraredLeft=52, InfraredRight=53,
  LeftMotorCurrent=54, RightMotorCurrent=55, MainBrushCurrent=56,
  SideBrushCurrent=57, Stasis=58,
  AllExtended=100, Extended=101, LightSignals=106, MotorCurrentsAndStasis=107
};
enum class Button : uint8_t { Clean=1, Spot=2, Dock=4, Minute=8,
                             Hour=16, Day=32, Schedule=64, Clock=128 };
enum class Day : uint8_t { Sunday, Monday, Tuesday, Wednesday, Thursday, Friday, Saturday };
enum class Charging : uint8_t { NotCharging, Reconditioning, FullCharging,
                                Trickle, Waiting, Fault };
enum class Turn { Clockwise, CounterClockwise };
struct Capabilities {
  bool extendedSensors = false; // Opt in after probing the actual robot.
  bool schedulingAndDisplay = false;
  bool resetAndStop = false;
};
struct Sample {
  int32_t value = 0;
  uint32_t timestampMs = 0;
  uint32_t generation = 0;
  bool valid = false;
  bool fresh(uint32_t now, uint32_t maxAge) const {
    return valid && uint32_t(now - timestampMs) <= maxAge;
  }
};
struct BatteryState {
  bool valid = false, percentageValid = false;
  uint16_t millivolts = 0, chargeMah = 0, capacityMah = 0;
  int16_t milliamps = 0;
  int8_t temperatureC = 0;
  Charging charging = Charging::NotCharging;
  float percentage = 0;
};
struct SafetyState {
  bool valid=false, bumpLeft=false, bumpRight=false;
  bool wheelDropLeft=false, wheelDropRight=false;
  bool cliffLeft=false, cliffFrontLeft=false, cliffFrontRight=false, cliffRight=false;
  bool virtualWall=false;
};
struct Note { uint8_t midi, ticks64; }; // midi=0 is a rest; 1 tick = 1/64 s.
struct ScheduleEntry { bool enabled; uint8_t hour, minute; };
struct Leds { bool debris, spot, dock, checkRobot; uint8_t color, intensity; };
struct Motors { bool mainBrush, sideBrush, vacuum, reverseMain, reverseSide; };

// One owner/task only. No concurrent reads/writes on the underlying UART.
class Transport {
 public:
  virtual ~Transport() {}
  virtual size_t write(const uint8_t* bytes, size_t count) = 0;
  virtual int read() = 0; // -1 when no byte is currently available; never block.
  // Optional platform operations. Existing Stream-only transports stay valid.
  virtual bool hasLinkControl() const { return false; }
  virtual bool hasBrc() const { return false; }
  virtual bool finishTransmit(uint32_t) { return false; } // bounded TX drain
  virtual bool setBaud(uint32_t) { return false; }
  virtual bool brcLow(bool) { return false; } // true pulls ROBOT BRC low
  virtual uint32_t nowMs() const { return 0; }
  virtual void waitMs(uint32_t) {}
};

class Roomba {
 public:
  explicit Roomba(Transport& transport, Capabilities capabilities = Capabilities(),
                  uint32_t baud = 115200);
  Result beginPassive();
  Result enterSafe();
  Result enterFullDisablingSafety();
  Result clean(); // Toggle-like native command, not an idempotent "ensure cleaning".
  Result spot();
  Result maxClean();
  Result seekDock(); // Starts native docking behavior, not navigation to a known pose.
  Result powerDownRobot(); // Does NOT disconnect a battery-fed XIAO.
  Result stopInterface();
  Result resetRobot();
  // Blocking maintenance operations: no active control, query or stream.
  // Ok requires a fresh Passive-mode response, not just a GPIO/UART action.
  Result verifyConnection();
  Result changeBaud(uint32_t baud);
  Result wakeViaBrc();
  Result forceBaud19200ViaBrc(); // robot must already be powered on
  uint32_t baudRate() const { return baud_; } // locally configured, not an ACK
  bool connectionVerified() const { return connectionVerified_; }
  Result driveStraight(int mmPerSecond, uint32_t now, uint32_t leaseMs=300);
  Result driveArc(int mmPerSecond, int radiusMm, uint32_t now, uint32_t leaseMs=300);
  Result turnInPlace(int mmPerSecond, Turn direction, uint32_t now, uint32_t leaseMs=300);
  Result driveWheels(int leftMmPerSecond, int rightMmPerSecond,
                     uint32_t now, uint32_t leaseMs=300);
  Result driveWheelPwm(int left, int right, uint32_t now, uint32_t leaseMs=300);
  Result stopWheels();
  Result stopAll();
  Result releaseControl(); // Stop actuators then return to Passive for charging/sleep.
  Result setMotors(Motors motors);
  Result setMotorPwm(int mainBrush, int sideBrush, int vacuum);
  Result setLeds(Leds leds);
  Result defineSong(uint8_t slot, const Note* notes, size_t count);
  Result playSong(uint8_t slot);
  Result pressButton(Button button);
  Result setClock(Day day, uint8_t hour, uint8_t minute);
  Result setSchedule(const ScheduleEntry (&week)[7]);
  Result setSchedulingLeds(uint8_t weekdays, uint8_t indicators);
  Result displayText(const char (&text)[5]);
  Result displaySegments(const uint8_t (&segments)[4]); // Known firmware issue; see docs.

  Result request(Sensor sensor, uint32_t now, uint32_t timeoutMs=200);
  Result request(const Sensor* sensors, size_t count, uint32_t now, uint32_t timeoutMs=200);
  Result requestBattery(uint32_t now) { return request(Sensor::Battery, now); }
  Result startStream(const Sensor* sensors, size_t count);
  Result pauseStream();
  Result resumeStream();
  // Ends a stream or recovers from a timed-out unframed reply. poll() must observe
  // 100 ms of quiet input before another request. Not recovery from a partial TX.
  Result resynchronizeInput(uint32_t now);
  void poll(uint32_t now); // Call continuously: RX, deadlines, and wheel lease.
  Sample sensor(Sensor id) const;
  BatteryState battery(uint32_t now, uint32_t maxAgeMs=1000) const;
  SafetyState safety(uint32_t now, uint32_t maxAgeMs=100) const;
  Mode requestedMode() const { return mode_; } // Commanded/inferred, NOT an ACK.
  Result lastEvent() const { return event_; }
  bool queryPending() const { return pending_; }
  bool inputReady() const { return !pending_ && !stream_ && !draining_ && !desync_ && !linkBusy_; }
  uint32_t framesReceived() const { return frames_; }
  uint32_t rejectedFrames() const { return rejected_; }
  static size_t packetSize(Sensor id);
  static int32_t encoderDelta(uint16_t current, uint16_t previous);

 private:
  Transport& io_;
  Capabilities caps_;
  uint32_t baud_;
  Mode mode_ = Mode::Off;
  Result event_ = Result::Ok;
  bool txFault_=false, pending_=false, stream_=false, paused_=false;
  bool desync_=false, draining_=false, queried_=false, lease_=false;
  bool linkBusy_=false, probing_=false, verificationRequired_=false, connectionVerified_=false;
  uint32_t deadline_=0, lastQuery_=0, lastRx_=0, leaseStart_=0, leaseMs_=0;
  uint32_t generation_=0, frames_=0, rejected_=0;
  Sensor selection_[32]{};
  size_t selected_=0, expected_=0, used_=0;
  uint8_t rx_[258]{};
  Sample samples_[59]{};
  Result send(const uint8_t* bytes, size_t count);
  Result command(uint8_t opcode, bool actuator=false);
  Result changeMode(uint8_t opcode, Mode target);
  Result actuate(const uint8_t* bytes, size_t count);
  Result wheels(uint8_t opcode, int first, int second, uint32_t now, uint32_t leaseMs);
  Result validate(const Sensor* ids, size_t count, size_t& bytes, bool stream) const;
  bool supported(Sensor id) const;
  bool decode(uint8_t id, const uint8_t* data, size_t count, uint32_t now, bool commit);
  void finishQuery(uint32_t now);
  void streamByte(uint8_t byte, uint32_t now);
  void invalidateSamples();
  Result linkPreflight(bool needBrc) const;
  Result probeLink();
  Result linkFailure(Result error);
  bool brcPulse();
  bool finishTx();
};
} // namespace roocore
