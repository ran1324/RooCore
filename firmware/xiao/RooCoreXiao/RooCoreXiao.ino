#include <Arduino.h>
#include <BatteryLearning.h>
#include <BatteryLearningStore.h>
#include <ArduinoOTA.h>
#include <atomic>
#include <NativeCapture.h>
#include <CarpetBoost.h>
#include <ChargeSupervisor.h>
#include <ControlArbiter.h>
#include <CloudPolicy.h>
#include <CloudTrust.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <time.h>
#include <DirtMonitor.h>
#include <DockGuard.h>
#include <LittleFS.h>
#include <NavigationHelp.h>
#include <PowerManagement.h>
#include <PowerShutdownOI.h>
#include <RooCoreLidarPower.h>
#include <RooNavigation.h>
#include <RoombaOIArduino.h>
#include <TminiPlus.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WifiProvisioning.h>
#include <esp_heap_caps.h>
#include <new>
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif
#ifndef ROO_SETUP_AP_PASSWORD
#define ROO_SETUP_AP_PASSWORD ""
#endif
#ifndef ROO_CLOUD_ENABLED
#define ROO_CLOUD_ENABLED false
#define ROO_CLOUD_DEVICE_TOKEN ""
#define ROO_CLOUD_INTERVAL_MS 1000
#define ROO_CLOUD_MAP_INTERVAL_MS 10000
#define ROO_CLOUD_REQUIRE_POLICY true
#endif
static_assert(!ROO_CLOUD_ENABLED || ROO_CLOUD_REQUIRE_POLICY, "Cloud integration requires persistent local zone policy");
static_assert(ROO_CLOUD_INTERVAL_MS >= 1000 && ROO_CLOUD_INTERVAL_MS <= 60000, "Cloud cadence outside software budget");
static_assert(ROO_CLOUD_MAP_INTERVAL_MS >= ROO_CLOUD_INTERVAL_MS && ROO_CLOUD_MAP_INTERVAL_MS <= 60000, "Cloud map cadence outside software budget");
#ifndef ROO_CHARGE_DEPARTURE_VERIFIED
#define ROO_CHARGE_DEPARTURE_VERIFIED false
#define ROO_CHARGE_MISSION_READY false
#define ROO_CHARGE_RESUME_PERCENT 80.0f
#define ROO_CHARGE_NATIVE_MIN_PERCENT 100.0f
#define ROO_CHARGE_MIN_C 0
#define ROO_CHARGE_MAX_C 0
#endif
#ifndef ROO_PROGRESS_INTERVAL_MS
#define ROO_PROGRESS_INTERVAL_MS 1000
#endif
static_assert(ROO_PROGRESS_INTERVAL_MS >= 500 && ROO_PROGRESS_INTERVAL_MS <= 60000,
              "Progress cadence must be bounded");
#ifndef ROO_DOCK_GEOMETRY_VERIFIED
#define ROO_DOCK_GEOMETRY_VERIFIED false
#define ROO_DOCK_FRONT_X_M 0.0f
#define ROO_DOCK_FRONT_Y_M 0.0f
#define ROO_DOCK_OUTWARD_YAW_RAD 0.0f
#define ROO_DOCK_DEPTH_M 0.0f
#define ROO_DOCK_HALF_WIDTH_M 0.0f
#define ROO_DOCK_MARGIN_M 0.0f
#define ROO_DOCK_BODY_RADIUS_M 0.0f
#define ROO_DOCK_CORRIDOR_MIN_M 0.0f
#define ROO_DOCK_CORRIDOR_MAX_M 0.0f
#define ROO_DOCK_CORRIDOR_HALF_WIDTH_M 0.0f
#define ROO_DOCK_HEADING_TOLERANCE_RAD 0.0f
#define ROO_DOCK_MAX_LINEAR_MPS 0.0f
#define ROO_DOCK_MAX_ANGULAR_RPS 0.0f
#endif
#ifndef ROO_LIDAR_POWER_VERIFIED
#define ROO_LIDAR_POWER_VERIFIED false
#endif
#ifndef ROO_LIDAR_RISE_MS
#define ROO_LIDAR_RISE_MS 0
#endif
static_assert(ROO_LIDAR_RISE_MS >= 0 && ROO_LIDAR_RISE_MS <= INT32_MAX,
              "LiDAR rise time must fit a wrap-safe millis interval");
constexpr bool LidarPowerConfigured = ROO_LIDAR_POWER_VERIFIED && ROO_LIDAR_RISE_MS > 0;

#ifndef ROO_RECORD_EXTENDED_ENABLED
#define ROO_RECORD_EXTENDED_ENABLED false
#endif
#ifndef ROO_OTA_ENABLED
#define ROO_OTA_ENABLED false
#define ROO_OTA_PASSWORD ""
#define ROO_OTA_POWER_VERIFIED false
#define ROO_OTA_MIN_MV 0
#endif
#ifndef ROO_FIRMWARE_VERSION
#define ROO_FIRMWARE_VERSION "native-recorder-v1"
#endif

// Older local configurations keep the optional feature disabled.
#ifndef ROO_CARPET_CURRENT_VERIFIED
#define ROO_CARPET_CURRENT_VERIFIED false
#endif
#ifndef ROO_CARPET_BOOST_ENABLED
#define ROO_CARPET_BOOST_ENABLED false
#define ROO_CARPET_MIN_MV 0
#define ROO_CARPET_MAX_MV 0
#define ROO_CARPET_85_MIN_MA 0
#define ROO_CARPET_85_EXIT_MA 0
#define ROO_CARPET_85_ENTER_MA 0
#define ROO_CARPET_85_MAX_MA 0
#define ROO_CARPET_127_MIN_MA 0
#define ROO_CARPET_127_EXIT_MA 0
#define ROO_CARPET_127_ENTER_MA 0
#define ROO_CARPET_127_MAX_MA 0
#endif

#ifndef ROO_DIRT_THRESHOLD
#define ROO_DIRT_THRESHOLD 0
#define ROO_DIRT_CONSECUTIVE 5
#define ROO_DIRT_MIN_HIGH_MS 300
#define ROO_DIRT_MAX_GAP_MS 200
#define ROO_DIRT_REGION_RADIUS_M .30f
#define ROO_DIRT_REVISIT_BUDGET_MS 20000
#endif
#ifndef ROO_STASIS_VERIFIED
#define ROO_STASIS_VERIFIED false
#endif
#ifndef ROO_POWER_POLICY_VERIFIED
#define ROO_POWER_POLICY_VERIFIED false
#endif
#ifndef ROO_POWER_HW_OFF_MAX_MV
#define ROO_POWER_HW_OFF_MAX_MV 0
#endif
#ifndef ROO_POWER_RECOVER_MV
#define ROO_POWER_RECOVER_MV 0
#endif
#ifndef ROO_POWER_SAVE_MIN_MV
#define ROO_POWER_SAVE_MIN_MV 0
#endif
#ifndef ROO_POWER_PASSIVE_STOP_VERIFIED
#define ROO_POWER_PASSIVE_STOP_VERIFIED false
#endif
#ifndef ROO_NATIVE_FALLBACK_VERIFIED
#define ROO_NATIVE_FALLBACK_VERIFIED false
#endif
#ifndef ROO_FALLBACK_CLEARANCE_VERIFIED
#define ROO_FALLBACK_CLEARANCE_VERIFIED false
#endif
#ifndef ROO_BATTERY_PACK_ID
#define ROO_BATTERY_PACK_ID ""
#endif
#ifndef ROO_BATTERY_CALIBRATION_ID
#define ROO_BATTERY_CALIBRATION_ID ""
#endif
#ifndef ROO_BATTERY_CURRENT_VERIFIED
#define ROO_BATTERY_CURRENT_VERIFIED false
#endif
#ifndef ROO_BATTERY_GAUGE_VERIFIED
#define ROO_BATTERY_GAUGE_VERIFIED false
#endif
#ifndef ROO_BATTERY_FULL_REFERENCE_VERIFIED
#define ROO_BATTERY_FULL_REFERENCE_VERIFIED false
#endif
#ifndef ROO_BATTERY_CURRENT_ERROR_MA
#define ROO_BATTERY_CURRENT_ERROR_MA 0.0f
#endif
#ifndef ROO_BATTERY_GAUGE_ERROR_MAH
#define ROO_BATTERY_GAUGE_ERROR_MAH 0.0f
#endif
#ifndef ROO_BATTERY_MIN_C
#define ROO_BATTERY_MIN_C 0
#endif
#ifndef ROO_BATTERY_MAX_C
#define ROO_BATTERY_MAX_C 0
#endif
#ifndef ROO_BATTERY_RESERVE_MARGIN_MAH
#define ROO_BATTERY_RESERVE_MARGIN_MAH 0.0f
#endif
#ifndef ROO_BATTERY_ADAPTIVE_RETURN_ENABLED
#define ROO_BATTERY_ADAPTIVE_RETURN_ENABLED false
#endif
#ifndef ROO_BATTERY_STORAGE_VERIFIED
#define ROO_BATTERY_STORAGE_VERIFIED false
#endif
static_assert(ROO_DIRT_THRESHOLD >= 0 && ROO_DIRT_THRESHOLD <= 255, "Dirt threshold range");

// Real hardware UARTs. USB CDC is diagnostic only, never the Roomba UART.
HardwareSerial roombaSerial(1);
RooCoreLidarPower lidarPort; // owns UART2, GPIO5 TX, GPIO6 RX and GPIO4 ON
roocore::Esp32Transport robotLink(roombaSerial, UART_NUM_1, 8);
roocore::Capabilities capabilities;
roocore::Roomba *robot;
roonav::Navigator *navigator = nullptr; // large maps belong in external PSRAM
roonav::Navigator *primaryNavigator = nullptr, *provisionalNavigator = nullptr;
roonav::NavigationHelp assistance;
roonav::CarpetBoost carpet;
roonav::Pose floorMotionPose;
uint32_t floorMotionAt = 0;
float floorSpeed = 0, floorTurn = 0;
bool floorMotionReady = false;
roonav::Odometry odometry;
roonav::TminiPlus lidar;
roonav::ControlArbiter arbiter;
roonav::PowerManagement powerManagement;
roonav::BatteryLearning batteryLearning;
roonav::BatteryLearningStore batteryLearningStore;
roonav::BatteryLearningStore::Snapshot *batteryScratch = nullptr;
SemaphoreHandle_t batteryMutex = nullptr;
roonav::BatteryLearning::Reserve batteryReserve;
uint32_t batteryReturnSequence = 0, batteryRouteAt = 0, batteryStoreAt = 0;
float batteryRouteM = NAN;
bool batteryStoreFailed = false;

roonav::ChargeSupervisor chargeSupervisor;
roonav::DockGuard dockGuard;
uint32_t dockCalibrationKey = 0;
uint32_t chargeWheelAt = 0, chargeWheelGeneration = 0;
uint16_t chargeLeft = 0, chargeRight = 0;
bool chargeWheelsKnown = false;
bool powerStreamResync = false;
uint32_t powerStreamResyncAt = 0;
constexpr char PowerIntentPath[] = "/power-cutoff";
WebServer web(80);
RooWifiProvisioning wifiSetup(web);
std::atomic<bool> wifiSetupRequested{false}, wifiSetupHold{false}, wifiSetupRelease{false}, wifiSetupPermitted{false};
std::atomic<uint32_t> wifiSetupCheckedAt{0};
roonav::UpdateHold wifiSetupStopped;
bool wifiPortalStarted=false, wifiButtonConsumed=false;
uint32_t wifiButtonAt=0;
void serviceWifiSetup(uint32_t);
bool wifiSetupAdmitted();

QueueHandle_t requests;
SemaphoreHandle_t statusMutex;
String publishedStatus, publishedHelpMap;
char bootId[17]{}, missionId[40]{}, mapId[40]{}, missionStartBoot[17]{}, returnBoot[17]{};
uint32_t missionCounter = 0, missionStartedAt = 0, mapRevision = 0, dockAnchorRevision = 0;
uint32_t observedReturnSequence = 0, progressSequence = 0, lastProgress = 0;
struct ProgressSnapshot {
  bool valid = false, localized = false, anchored = false;
  uint32_t at = 0, sequence = 0, revision = 0, anchorRevision = 0;
  roonav::Config config;
  roonav::Pose pose, staging;
  roonav::DockGuard::Config dockConfig;
  char boot[17]{}, mission[40]{}, map[80]{}, status[16000]{};
  roonav::CloudPolicy policy;
  float ranges[180]{};
  uint32_t scanAt = 0;
  roocore::Sample sensors[52];
  int8_t cells[roonav::Navigator::MaxCells];
  uint8_t covered[roonav::Navigator::MaxCells], side[roonav::Navigator::MaxCells];
  uint8_t excluded[roonav::Navigator::MaxCells];
};
ProgressSnapshot *progressPublished = nullptr, *progressServing = nullptr, *progressCloud = nullptr;
roonav::CloudPolicy cloudPolicy;
roonav::UpdateHold cloudPolicyStopped;
QueueHandle_t cloudPolicies = nullptr;
bool cloudPolicyRequired = ROO_CLOUD_ENABLED && ROO_CLOUD_REQUIRE_POLICY, cloudPolicyFault = false;
uint32_t cloudExpectedGeneration = 0, cloudExpectedChecksum = 0;
constexpr char CloudPolicyIntent[] = "/cloud-policy-required";
bool cloudPolicyReady();
void serviceCloudPolicy();
void cloudTask(void *);
String encodeCloudSnapshot(const ProgressSnapshot &, bool);

SemaphoreHandle_t progressMutex;

using NativeLog = roonav::NativeCapture<256>;
NativeLog *nativeLog = nullptr;
roonav::NativeFrame nativeApiRows[16]; // HTTP task only
SemaphoreHandle_t nativeMutex;
bool nativeObservation = false, nativeProfileChanging = false, nativeStartPending = false;
bool nativeExtended = false, serviceMode = false;
uint32_t nativeProfileAt = 0, nativeGeneration = 0;
std::atomic<uint32_t> nativeSourceLost{0}, otaCheckedAt{0};
roonav::UpdateHold updateHold;
std::atomic<bool> otaReady{false}, otaUploading{false}, otaReboot{false};
bool otaStarted = false;
bool otaAdmitted() { return otaReady.load() && uint32_t(millis() - otaCheckedAt.load()) <= 200; }

struct Request {
  char action[24];
  uint32_t epoch, sequence, expires;
  int left, right, pwm;
  roonav::Pose candidate, reference;
  char boot[17], mission[40];
};
struct SavedMission {
  uint32_t magic = 0x524f4f31, version = 6, sequence = 0, crc = 0;
  roonav::Config config;
  roonav::CloudPolicy cloudPolicy;
  roonav::Pose dock, checkpoint, pose;
  uint8_t state, pass, passes, recoveries;
  uint32_t skipped, completed;
  bool finished;
  int8_t cells[roonav::Navigator::MaxCells];
  uint8_t covered[roonav::Navigator::MaxCells], excluded[roonav::Navigator::MaxCells];
  uint8_t sideCovered[roonav::Navigator::MaxCells], edgeSeen[roonav::Navigator::MaxCells];
  uint8_t zoneCovered[roonav::Navigator::MaxCells];
  uint8_t contacts[roonav::Navigator::MaxCells], retries[roonav::Navigator::MaxCells];
  uint32_t edgeSegments, edgeIncomplete;
  uint32_t activeMs = 0, mapRevision = 0, dockAnchorRevision = 0;
  uint32_t missionStartedAt = 0, returnAt = 0, returnSequence = 0;
  uint8_t returnCause = 0, cleaningPhase = 0;
  uint32_t dockCalibration = 0;
  char missionId[40]{}, mapId[40]{}, missionStartBoot[17]{}, returnBoot[17]{};
};
SavedMission *saved = nullptr; // checkpoint workspace belongs in PSRAM, leaving internal heap for TLS/control stacks
// Explicit declarations keep Arduino's generated prototypes after these types
// in both the primary sketch and the staged qualification build.
uint32_t checksum(const SavedMission &value);
bool readSlot(const char *path, SavedMission &out);
void request(const Request &r, uint32_t now);
void serveNativeRecording();
bool undocking = false, startAfterUndock = false, deepAfterUndock = false;
uint32_t undockAt = 0, undockProgressAt = 0;
float undockBest = 0;
bool storageReady = false, anchored = false, nativeDock = false, linkReady = false;
uint32_t lastScan = 0, lastGeneration = 0, sequence = 0, armingSince = 0;
uint32_t lastControl = 0, lastPublish = 0, lastSaved = 0, anchorAt = 0;
float alignmentScore = 0;
uint8_t appliedPwm = 0, appliedSidePwm = 0, appliedVacuumPwm = 0;
bool ownsRobot = false;
String lastError = "not commissioned";
roonav::Pose trail[16];
uint32_t trailMs[16]{};
uint32_t wheelDropSince = 0;
unsigned trailAt = 0;
roonav::Input latest;
roonav::DirtMonitor *dirtMonitor = nullptr;
roonav::DirtMonitor::Record *dirtApiRows = nullptr;
SemaphoreHandle_t dirtMutex;
uint32_t dirtBoot = 0, dirtFrame = 1, dirtSession = 0;
struct PendingDirt {
  roocore::Sample sample;
  uint32_t frame, rejected;
  bool managed, globalPose, cleaning, safe, link;
  float battery;
  uint8_t brush, vacuum;
};
PendingDirt dirtQueue[64];
unsigned dirtHead = 0, dirtSize = 0, dirtQueueLost = 0;
uint32_t dirtRejected = 0;
bool stasisMaintenance = false;
uint32_t stasisMaintenanceAt = 0, lastStasisGeneration = 0;

uint32_t batteryKey(const char *value) {
  if (!value[0]) return 0;
  uint32_t h = 2166136261u;
  for (const char *p = value; *p; ++p) h = (h ^ uint8_t(*p)) * 16777619u;
  return h ? h : 1;
}
void configureBatteryLearning() {
  auto &c = batteryLearning.cfg;
  c.packKey = batteryKey(ROO_BATTERY_PACK_ID);
  c.calibrationKey = batteryKey(ROO_BATTERY_CALIBRATION_ID);
  c.currentVerified = ROO_BATTERY_CURRENT_VERIFIED; c.gaugeVerified = ROO_BATTERY_GAUGE_VERIFIED;
  c.fullReferenceVerified = ROO_BATTERY_FULL_REFERENCE_VERIFIED;
  c.currentErrorMa = ROO_BATTERY_CURRENT_ERROR_MA; c.gaugeErrorMah = ROO_BATTERY_GAUGE_ERROR_MAH;
  c.minimumC = ROO_BATTERY_MIN_C; c.maximumC = ROO_BATTERY_MAX_C;
  c.reserveMarginMah = ROO_BATTERY_RESERVE_MARGIN_MAH;
  c.adaptiveReturn = ROO_BATTERY_ADAPTIVE_RETURN_ENABLED;
  if (c.calibrationKey) {
    uint32_t errorI = 0, errorQ = 0;
    memcpy(&errorI,&c.currentErrorMa,sizeof errorI); memcpy(&errorQ,&c.gaugeErrorMah,sizeof errorQ);
    const uint32_t words[] = {c.currentVerified,c.gaugeVerified,c.fullReferenceVerified,errorI,errorQ,
        uint32_t(int32_t(c.minimumC)),uint32_t(int32_t(c.maximumC))};
    for (uint32_t v : words) for (unsigned b = 0; b < 4; ++b)
      c.calibrationKey = (c.calibrationKey ^ uint8_t(v >> (8*b))) * 16777619u;
    if (!c.calibrationKey) c.calibrationKey = 1;
  }
}
struct FirmwareBatteryStore : roonav::BatteryLearningStore::Port {
  const char *path(unsigned slot) const { return slot ? "/battery-b" : "/battery-a"; }
  bool exists(unsigned slot) override { return LittleFS.exists(path(slot)); }
  bool read(unsigned slot, void *out, size_t size) override {
    File f = LittleFS.open(path(slot),"r");
    const bool ok = f && f.size() == size && f.read(static_cast<uint8_t *>(out),size) == size;
    f.close(); return ok;
  }
  bool write(unsigned slot, const void *data, size_t size) override {
    File f = LittleFS.open(path(slot),"w");
    const bool ok = f && f.write(static_cast<const uint8_t *>(data),size) == size;
    if (f) f.flush(); f.close(); return ok;
  }
  bool remove(unsigned slot) override { return LittleFS.remove(path(slot)); }
} batteryFiles;
roonav::BatteryLearning::Sample batteryLearningSample(uint32_t now) {
  using Sensor = roocore::Sensor;
  roonav::BatteryLearning::Sample s;
  const auto mv = robot->sensor(Sensor::VoltageMv), ma = robot->sensor(Sensor::CurrentMa),
      temperature = robot->sensor(Sensor::TemperatureC), charge = robot->sensor(Sensor::ChargeMah),
      capacity = robot->sensor(Sensor::CapacityMah), state = robot->sensor(Sensor::ChargingState),
      sources = robot->sensor(Sensor::ChargingSources), mode = robot->sensor(Sensor::OiMode);
  s.now = now; s.stamp = mv.timestampMs; s.generation = mv.generation; s.boot = dirtBoot;
  s.frame = navigator == primaryNavigator && anchored && latest.localized && dockGuard.ready() &&
      latest.sensorsValid && uint32_t(now-latest.sensorsMs) <= 200 &&
      uint32_t(now-latest.poseMs) <= 300 && !latest.cliff && !latest.wheelDrop &&
      !latest.bumper && !latest.overcurrent ? dockCalibrationKey : 0;
  s.valid = linkReady && !powerStreamResync && !nativeProfileChanging && mv.fresh(now,200) &&
      ma.fresh(now,200) && temperature.fresh(now,200) && state.fresh(now,200) &&
      sources.fresh(now,200) && mode.fresh(now,200) && ma.generation == mv.generation &&
      temperature.generation == mv.generation && state.generation == mv.generation &&
      sources.generation == mv.generation && mode.generation == mv.generation;
  s.gaugeValid = charge.fresh(now,200) && capacity.fresh(now,200) &&
      charge.generation == mv.generation && capacity.generation == mv.generation &&
      capacity.value > 0 && charge.value >= 0 && charge.value <= capacity.value;
  s.mv = uint16_t(mv.value); s.ma = int16_t(ma.value); s.temperatureC = int16_t(temperature.value);
  s.chargeMah = uint16_t(charge.value); s.capacityMah = uint16_t(capacity.value);
  s.charging = uint8_t(state.value); s.sources = uint8_t(sources.value); s.mode = uint8_t(mode.value);
  s.stationary = chargeWheelsKnown && uint32_t(now-chargeWheelAt) >= 1000;
  s.dockConfirmed = latest.chargeContactConfirmed && latest.chargerValid;
  s.critical = s.mv <= powerManagement.cfg.criticalMv;
  return s;
}
bool batteryStorageAllowed(const roonav::BatteryLearning::Sample &s) {
  return storageReady && batteryScratch && ROO_BATTERY_STORAGE_VERIFIED &&
      powerManagement.cfg.valid() && batteryLearning.cfg.packKey && batteryLearning.cfg.calibrationKey &&
      s.valid && s.stationary && s.mode == 1 && !serviceMode && !nativeProfileChanging &&
      !movingSession() && !nativeDock && s.charging != 5 &&
      s.mv > powerManagement.cfg.warningMv && s.mv >= powerManagement.cfg.saveMinMv;
}

bool ordinaryCenterAllowed(void *context, float x, float y) {
  const auto *guard = static_cast<roonav::DockGuard *>(context);
  return !guard->blocksOrdinaryCell(x, y, primaryNavigator->cfg.resolution * .5f);
}
void configureDockGuard(uint32_t revision) {
  roonav::DockGuard::Config cfg;
  cfg.configured = cfg.verified = ROO_DOCK_GEOMETRY_VERIFIED && ROO_BRUSH_GEOMETRY_CALIBRATED;
  cfg.anchorRevision = revision;
  cfg.frontX = ROO_DOCK_FRONT_X_M; cfg.frontY = ROO_DOCK_FRONT_Y_M;
  cfg.yaw = ROO_DOCK_OUTWARD_YAW_RAD;
  cfg.depth = ROO_DOCK_DEPTH_M; cfg.halfWidth = ROO_DOCK_HALF_WIDTH_M;
  cfg.margin = ROO_DOCK_MARGIN_M; cfg.bodyRadius = ROO_DOCK_BODY_RADIUS_M;
  cfg.sideX = ROO_SIDE_BRUSH_X_METERS; cfg.sideY = ROO_SIDE_BRUSH_Y_METERS;
  cfg.sideRadius = ROO_SIDE_BRUSH_RADIUS_METERS;
  cfg.corridorMinX = ROO_DOCK_CORRIDOR_MIN_M; cfg.corridorMaxX = ROO_DOCK_CORRIDOR_MAX_M;
  cfg.corridorHalfWidth = ROO_DOCK_CORRIDOR_HALF_WIDTH_M;
  cfg.headingTolerance = ROO_DOCK_HEADING_TOLERANCE_RAD;
  cfg.maxLinearSpeed = ROO_DOCK_MAX_LINEAR_MPS; cfg.maxAngularSpeed = ROO_DOCK_MAX_ANGULAR_RPS;
  cfg.maxHorizonMs = 500; cfg.maxAccessMs = 60000;
  cfg.poseMaxAgeMs = 300; cfg.sensorsMaxAgeMs = cfg.irMaxAgeMs = 200;
  cfg.stepMeters = .01f; cfg.stepRadians = .03f;
  cfg.map.minX = primaryNavigator->cfg.originX; cfg.map.minY = primaryNavigator->cfg.originY;
  cfg.map.maxX = cfg.map.minX + primaryNavigator->cfg.cols * primaryNavigator->cfg.resolution;
  cfg.map.maxY = cfg.map.minY + primaryNavigator->cfg.rows * primaryNavigator->cfg.resolution;
  cfg.map.valid = true;
  const float calibration[] = {cfg.frontX, cfg.frontY, cfg.yaw, cfg.depth, cfg.halfWidth,
      cfg.margin, cfg.bodyRadius, cfg.sideX, cfg.sideY, cfg.sideRadius, cfg.corridorMinX,
      cfg.corridorMaxX, cfg.corridorHalfWidth, cfg.headingTolerance, cfg.maxLinearSpeed,
      cfg.maxAngularSpeed, ROO_DOCK_EXIT_METERS, float(cfg.verified)};
  dockCalibrationKey = 2166136261u;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(calibration);
  for (size_t i = 0; i < sizeof calibration; ++i)
    dockCalibrationKey = (dockCalibrationKey ^ bytes[i]) * 16777619u;
  dockGuard.configure(cfg); // simulationOnly is false; no synthetic dimensions in firmware
  if (dockGuard.configured() &&
      fabsf(roonav::wrap(primaryNavigator->dock.yaw - (cfg.yaw + roonav::Pi))) >
          fminf(.08f, cfg.headingTolerance))
    dockGuard.invalidate(); // staging heading and measured contact face must agree
}
roonav::DockGuard::Observation dockObservation(uint32_t now) {
  roonav::DockGuard::Observation o;
  o.now = now; o.poseMs = latest.poseMs; o.sensorsMs = latest.sensorsMs;
  o.pose = roonav::DockGuard::Pose(latest.pose.x, latest.pose.y, latest.pose.yaw);
  o.localized = latest.localized && navigator == primaryNavigator;
  o.sensorsValid = latest.sensorsValid && latest.chargerValid;
  o.dockContact = latest.docked; o.bumper = latest.bumper;
  const auto ir = robot->sensor(roocore::Sensor::InfraredOmni);
  o.irMs = ir.timestampMs; o.irValid = ir.fresh(now, 200); o.irRaw = ir.value;
  return o;
}
bool terminal(roonav::State s) {
  return s == roonav::State::Idle || s == roonav::State::Fault || s == roonav::State::Paused ||
         s == roonav::State::Complete;
}
bool active() { return !terminal(navigator->state); }
bool movingSession() { return active() || undocking; }
bool lidarPower(bool on) {
  if (!on) {
    // Every deliberate power-off, including battery cutoff, uses this path.
    // end UART -> TX LOW / RX input pulldown -> ON LOW. No OE pin or unplugging.
    lidarPort.powerOff();
    lidar.reset();
    lastScan = 0;
    latest.scanValid = false;
    return true;
  }
  if (!LidarPowerConfigured) {
    lastError = "LiDAR power sequence requires hardware qualification and rise time";
    return false;
  }
  const bool wasPowered = lidarPort.powered();
  if (!lidarPort.requestOn(millis(), ROO_LIDAR_RISE_MS)) {
    lastError = "LiDAR UART fault; request off before retrying";
    return false;
  }
  if (!wasPowered) {
    lidar.reset();
    lastScan = 0;
    latest.scanValid = false;
  }
  return true;
}

void serviceLidar(uint32_t now) {
  const auto before = lidarPort.state();
  if (lidarPort.poll(now)) {
    const uint8_t start[] = {0xa5, 0x60};
    if (lidarPort.write(start, sizeof start) != sizeof start) {
      lidarPort.abort(); // latched fault, no automatic on/off retry loop
      lastError = "LiDAR scan command failed; power off";
    }
  } else if (before == RooCoreLidarPower::State::Warming &&
             lidarPort.state() == RooCoreLidarPower::State::Fault) {
    lastError = "LiDAR UART initialization failed; power off";
  }
  if (!lidarPort.ready()) {
    lidar.reset();
    lastScan = 0;
    latest.scanValid = false;
    return;
  }
  for (unsigned i = 0; i < 2048 && lidarPort.available(); ++i)
    lidar.feed(uint8_t(lidarPort.read()), now);
}
// Invoked by Roomba::poll after the whole validated frame is committed. No I/O/locks here.
void observeDirt(void *, roocore::Sensor id, const roocore::Sample &reading) {
  if (nativeObservation && !nativeProfileChanging && id == roocore::Sensor::BumpsAndDrops &&
      reading.generation != nativeGeneration) {
    nativeGeneration = reading.generation;
    if (xSemaphoreTake(nativeMutex, 0) == pdTRUE) {
      if (nativeLog->recording) {
        roonav::NativeFrame row;
        row.ms = reading.timestampMs; row.generation = reading.generation;
        row.rejected = robot->rejectedFrames(); row.sourceLost = nativeSourceLost.load();
        for (unsigned packet = 7; packet <= 58; ++packet) {
          const auto value = robot->sensor(static_cast<roocore::Sensor>(packet));
          if (value.valid && value.generation == reading.generation) {
            row.present |= uint64_t(1) << (packet - 7);
            row.values[packet - 7] = value.value;
          }
        }
        nativeLog->append(row);
      }
      xSemaphoreGive(nativeMutex);
    } else ++nativeSourceLost;
  }
  if (id != roocore::Sensor::DirtDetect)
    return;
  if (dirtSize == 64) {
    ++dirtQueueLost;
    return;
  }
  PendingDirt &p = dirtQueue[(dirtHead + dirtSize++) % 64];
  p.sample = reading;
  p.frame = dirtFrame;
  p.rejected = robot->rejectedFrames();
  const auto safety = robot->safety(reading.timestampMs, 200);
  const auto battery = robot->battery(reading.timestampMs, 1000);
  const auto mode = robot->sensor(roocore::Sensor::OiMode);
  const auto current = robot->sensor(roocore::Sensor::Overcurrents);
  p.managed = ownsRobot && !nativeDock && mode.fresh(reading.timestampMs, 200) && mode.value == 2;
  p.globalPose = navigator == primaryNavigator && anchored && !assistance.pending();
  p.cleaning = p.managed && appliedPwm > 0;
  p.link = safety.valid && battery.valid && battery.percentageValid &&
           current.fresh(reading.timestampMs, 200);
  p.safe = p.link && !safety.cliffLeft && !safety.cliffFrontLeft && !safety.cliffFrontRight &&
           !safety.cliffRight && !safety.virtualWall && !safety.wheelDropLeft &&
           !safety.wheelDropRight && !safety.bumpLeft && !safety.bumpRight && current.value == 0;
  p.battery = battery.percentage;
  p.brush = appliedPwm;
  p.vacuum = appliedVacuumPwm;
}
roonav::DirtMonitor::Context dirtContext(uint32_t now) {
  roonav::DirtMonitor::Context c;
  c.now = now;
  c.poseMs = latest.poseMs;
  c.frame = dirtFrame;
  c.pose = latest.pose;
  c.managed = ownsRobot && !nativeDock;
  c.poseValid = navigator == primaryNavigator && anchored && !assistance.pending() &&
                latest.localized && uint32_t(now - latest.poseMs) <= 100 && latest.scanValid &&
                uint32_t(now - latest.scanMs) <= 400;
  c.cleaning = latest.cleaningActive;
  c.battery = latest.battery;
  c.link = latest.sensorsValid && uint32_t(now - latest.sensorsMs) <= 200 &&
           arbiter.owner != roonav::Owner::Stopping && arbiter.owner != roonav::Owner::Held;
  c.safe = c.link && !latest.cliff && !latest.wheelDrop && !latest.bumper && !latest.overcurrent;
  c.brush = appliedPwm;
  c.vacuum = appliedVacuumPwm;
  c.action = !c.managed   ? roonav::DirtMonitor::Action::Unmanaged
             : c.cleaning ? roonav::DirtMonitor::Action::RoutineCleaning
                          : roonav::DirtMonitor::Action::RoutineTravel;
  return c;
}
void collectDirt(uint32_t now) {
  // Logging cannot hold the control task waiting for an HTTP client.
  if (!dirtMonitor || xSemaphoreTake(dirtMutex, 0) != pdTRUE)
    return;
  auto c = dirtContext(now);
  while (dirtSize) {
    PendingDirt p = dirtQueue[dirtHead];
    dirtHead = (dirtHead + 1) % 64;
    --dirtSize;
    if (p.rejected != dirtRejected) {
      dirtMonitor->gap(c);
      dirtRejected = p.rejected;
    }
    auto sampleContext = c;
    sampleContext.managed = p.managed;
    sampleContext.cleaning = p.cleaning;
    sampleContext.safe = c.safe && p.safe;
    sampleContext.link = c.link && p.link;
    sampleContext.battery = fminf(c.battery, p.battery);
    sampleContext.poseValid = c.poseValid && p.globalPose && p.frame == dirtFrame;
    sampleContext.brush = p.brush;
    sampleContext.vacuum = p.vacuum;
    sampleContext.action = !p.managed   ? roonav::DirtMonitor::Action::Unmanaged
                           : p.cleaning ? roonav::DirtMonitor::Action::RoutineCleaning
                                        : roonav::DirtMonitor::Action::RoutineTravel;
    dirtMonitor->observe(uint8_t(p.sample.value), p.sample.timestampMs, p.sample.generation,
                         sampleContext);
  }
  if (dirtQueueLost || robot->rejectedFrames() != dirtRejected) {
    dirtMonitor->gap(c, dirtQueueLost);
    dirtQueueLost = 0;
    dirtRejected = robot->rejectedFrames();
  }
  dirtMonitor->tick(c);
  const auto stasis = robot->stasis(now);
  if (stasis.sample.fresh(now, 200) && stasis.sample.generation != lastStasisGeneration) {
    lastStasisGeneration = stasis.sample.generation;
    if (stasis.maintenanceSuggested && !stasisMaintenance) {
      stasisMaintenance = true;
      stasisMaintenanceAt = now;
      dirtMonitor->maintenance(2, stasis.sample.timestampMs, c);
    } else if (stasis.usableForProgress)
      stasisMaintenance = false;
  }
  xSemaphoreGive(dirtMutex);
}
void stopMotion() {
  appliedPwm = appliedSidePwm = appliedVacuumPwm = 0;
  carpet.invalidate();
  floorMotionReady = false;
  // Passive Drive packets are not an emergency stop for native docking.
  // Safe stops native cleaning/docking without disabling cliff protection.
  if (nativeDock || (ownsRobot && robot->requestedMode() != roocore::Mode::Safe)) {
    robot->enterSafe();
    nativeDock = false;
  }
  if (robot->requestedMode() == roocore::Mode::Safe)
    robot->releaseControl();
  armingSince = 0;
  ownsRobot = false;
}
uint32_t checksum(const SavedMission &value) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof value; ++i) {
    if (i >= offsetof(SavedMission, crc) && i < offsetof(SavedMission, crc) + 4)
      continue;
    h = (h ^ bytes[i]) * 16777619u;
  }
  return h;
}
bool saveMission() {
  if (!storageReady)
    return false;
  stopMotion();
  if (!robotLink.finishTransmit(50))
    return false; // never write flash while motors are commanded
  saved->cloudPolicy = cloudPolicy;
  saved->config = navigator->cfg;
  saved->dock = navigator->dock;
  saved->checkpoint = navigator->checkpoint;
  saved->pose = odometry.pose;
  saved->state = uint8_t(navigator->savedState());
  saved->pass = navigator->pass;
  saved->passes = navigator->passes;
  saved->recoveries = navigator->recoveries;
  saved->skipped = navigator->skipped;
  saved->completed = navigator->completedCells;
  saved->finished = navigator->finished;
  saved->edgeSegments = navigator->edgeSegments;
  saved->edgeIncomplete = navigator->edgeIncomplete;
  saved->activeMs = navigator->activeMs;
  saved->mapRevision = mapRevision;
  saved->dockAnchorRevision = dockAnchorRevision;
  saved->missionStartedAt = missionStartedAt;
  saved->returnAt = navigator->returnAt;
  saved->returnSequence = navigator->returnSequence;
  saved->returnCause = uint8_t(navigator->returnCause);
  saved->cleaningPhase = uint8_t(navigator->savedCleaningPhase());
  saved->dockCalibration = dockCalibrationKey;
  memcpy(saved->missionId, missionId, sizeof missionId);
  memcpy(saved->mapId, mapId, sizeof mapId);
  memcpy(saved->missionStartBoot, missionStartBoot, sizeof missionStartBoot);
  memcpy(saved->returnBoot, returnBoot, sizeof returnBoot);
  memcpy(saved->contacts, navigator->contacts, sizeof(saved->contacts));
  memcpy(saved->retries, navigator->retries, sizeof(saved->retries));
  memcpy(saved->sideCovered, navigator->sideCovered, sizeof(saved->sideCovered));
  memcpy(saved->edgeSeen, navigator->edgeSeen, sizeof(saved->edgeSeen));
  memcpy(saved->cells, navigator->cells, sizeof(saved->cells));
  memcpy(saved->covered, navigator->covered, sizeof(saved->covered));
  memcpy(saved->zoneCovered, navigator->zoneCovered, sizeof(saved->zoneCovered));
  memcpy(saved->excluded, navigator->excluded, sizeof(saved->excluded));
  ++saved->sequence;
  saved->crc = checksum(*saved);
  // Alternating complete snapshots: a torn write leaves the other slot intact.
  const bool provisional = navigator != primaryNavigator;
  const char *path = provisional ? (saved->sequence & 1 ? "/local-a" : "/local-b")
                                 : (saved->sequence & 1 ? "/mission-a" : "/mission-b");
  File file = LittleFS.open(path, "w");
  if (!file)
    return false;
  bool ok = file.write(reinterpret_cast<uint8_t *>(saved), sizeof(*saved)) == sizeof(*saved);
  file.flush();
  file.close();
  lastSaved = millis();
  return ok;
}
bool readSlot(const char *path, SavedMission &out) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != sizeof out)
    return false;
  bool ok = file.read(reinterpret_cast<uint8_t *>(&out), sizeof out) == sizeof out;
  file.close();
  return ok && out.magic == 0x524f4f31 && out.version == 6 && out.crc == checksum(out) &&
         out.returnCause <= uint8_t(roonav::ReturnCause::Completed) &&
         (out.cleaningPhase == uint8_t(roonav::State::Cleaning) ||
          out.cleaningPhase == uint8_t(roonav::State::Edging) ||
          out.cleaningPhase == uint8_t(roonav::State::Touchup)) &&
         out.missionId[sizeof out.missionId - 1] == 0 &&
         out.mapId[sizeof out.mapId - 1] == 0 &&
         out.missionStartBoot[sizeof out.missionStartBoot - 1] == 0 &&
         out.returnBoot[sizeof out.returnBoot - 1] == 0 &&
         out.cloudPolicy.count <= roonav::MapZones::Capacity &&
         out.pass < out.passes + out.cloudPolicy.maxExtra() && out.passes >= 1 && out.passes <= 2 &&
         out.state <= uint8_t(roonav::State::Yielding);
}
void loadMission() {
  if (!storageReady)
    return;
  auto *other = static_cast<SavedMission *>(
      heap_caps_malloc(sizeof(SavedMission), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!other)
    return;
  bool a = readSlot("/mission-a", *saved), b = readSlot("/mission-b", *other);
  if (b && (!a || int32_t(other->sequence - saved->sequence) > 0))
    *saved = *other;
  heap_caps_free(other);
  if (!a && !b)
    return;
  if (!isfinite(saved->dock.x) || !isfinite(saved->dock.y) || !isfinite(saved->dock.yaw) ||
      !isfinite(saved->checkpoint.x) || !isfinite(saved->checkpoint.y) ||
      !isfinite(saved->checkpoint.yaw))
    return;
  // Reject checkpoint after geometry/calibration changes.
  if (saved->config.cols != navigator->cfg.cols || saved->config.rows != navigator->cfg.rows ||
      saved->dockCalibration != dockCalibrationKey ||
      saved->config.resolution != navigator->cfg.resolution ||
      saved->config.metersPerTick != navigator->cfg.metersPerTick ||
      saved->config.wheelSeparation != navigator->cfg.wheelSeparation ||
      saved->config.brushWidth != navigator->cfg.brushWidth ||
      saved->config.brushDepth != navigator->cfg.brushDepth ||
      saved->config.brushOffset != navigator->cfg.brushOffset ||
      saved->config.sideX != navigator->cfg.sideX || saved->config.sideY != navigator->cfg.sideY ||
      saved->config.sideRadius != navigator->cfg.sideRadius ||
      saved->config.originX != navigator->cfg.originX ||
      saved->config.originY != navigator->cfg.originY ||
      saved->config.radius != navigator->cfg.radius || saved->config.margin != navigator->cfg.margin)
    return;
  for (int i = 0; i < navigator->count(); ++i)
    if (saved->cells[i] < -1 || saved->cells[i] > 1)
      return;
  if (saved->cloudPolicy.generation) {
    if (!roonav::CloudPolicy::identity(saved->cloudPolicy.sourceMap,80) ||
        !roonav::CloudPolicy::identity(saved->cloudPolicy.cloudMap,33) ||
        !saved->cloudPolicy.matches(saved->mapId, saved->config.cols, saved->config.rows,
        saved->config.resolution, saved->config.originX, saved->config.originY) ||
        !navigator->setZones(saved->cloudPolicy.zones, saved->cloudPolicy.count)) {
      cloudPolicyFault = true;
      return;
    }
    cloudPolicy = saved->cloudPolicy;
    cloudPolicyRequired = true;
  }
  navigator->edgeSegments = saved->edgeSegments;
  navigator->edgeIncomplete = saved->edgeIncomplete;
  memcpy(navigator->contacts, saved->contacts, sizeof(saved->contacts));
  memcpy(navigator->retries, saved->retries, sizeof(saved->retries));
  memcpy(navigator->sideCovered, saved->sideCovered, sizeof(saved->sideCovered));
  memcpy(navigator->edgeSeen, saved->edgeSeen, sizeof(saved->edgeSeen));
  navigator->dock = saved->dock;
  navigator->checkpoint = saved->checkpoint;
  navigator->pass = saved->pass;
  navigator->passes = saved->passes;
  navigator->recoveries = saved->recoveries;
  navigator->skipped = saved->skipped;
  navigator->completedCells = saved->completed;
  navigator->finished = saved->finished;
  memcpy(navigator->cells, saved->cells, sizeof(saved->cells));
  memcpy(navigator->covered, saved->covered, sizeof(saved->covered));
  memcpy(navigator->zoneCovered, saved->zoneCovered, sizeof(saved->zoneCovered));
  memcpy(navigator->excluded, saved->excluded, sizeof(saved->excluded));
  navigator->restorePaused(roonav::State(saved->state), millis(), saved->activeMs,
                           roonav::State(saved->cleaningPhase));
  navigator->restoredMapConfiguration();
  navigator->returnCause = roonav::ReturnCause(saved->returnCause);
  navigator->returnAt = saved->returnAt;
  navigator->returnSequence = observedReturnSequence = saved->returnSequence;
  mapRevision = saved->mapRevision;
  dockAnchorRevision = saved->dockAnchorRevision;
  missionStartedAt = saved->missionStartedAt;
  memcpy(missionId, saved->missionId, sizeof missionId);
  memcpy(mapId, saved->mapId, sizeof mapId);
  memcpy(missionStartBoot, saved->missionStartBoot, sizeof missionStartBoot);
  memcpy(returnBoot, saved->returnBoot, sizeof returnBoot);
  anchored = false;
  arbiter.hold();
  lastError = "checkpoint restored; physical dock reanchor required";
}
// Only the control task owns these operations. HTTP handlers only enqueue commands.
struct FirmwarePowerPort : roonav::PowerManagement::Port {
  bool stop(bool passive) override {
    dockGuard.endAccess();
    arbiter.hold();
    assistance.operatorPause();
    navigator->pause(millis());
    undocking = startAfterUndock = false;
    xQueueReset(requests);
    appliedPwm = appliedSidePwm = appliedVacuumPwm = 0;
    carpet.invalidate();
    floorMotionReady = false;
    const bool stopped = roonav::stopRoombaForPowerHold(*robot, robotLink, passive);
    nativeDock = false;
    ownsRobot = false;
    armingSince = 0;
    return stopped; // UART completion only; policy waits for fresh stationary encoders
  }
  bool persistIntent() override {
    if (!storageReady || !robotLink.finishTransmit(50)) return false;
    const char marker[] = "RooCore power-managed mission v2\n";
    File file = LittleFS.open(PowerIntentPath, "w");
    if (!file) return false;
    bool ok = file.write(reinterpret_cast<const uint8_t *>(marker), sizeof marker) == sizeof marker;
    file.flush(); file.close();
    file = LittleFS.open(PowerIntentPath, "r");
    char checked[sizeof marker]{};
    ok = ok && file && file.size() == sizeof marker &&
         file.read(reinterpret_cast<uint8_t *>(checked), sizeof checked) == sizeof checked &&
         !memcmp(marker, checked, sizeof marker);
    file.close();
    // Reject pre-flash UART samples. No movement can start until the fresh stream returns.
    if (ok && robot->resynchronizeInput(millis()) == roocore::Result::Ok) {
      powerStreamResync = true;
      powerStreamResyncAt = millis();
      return true;
    }
    return false;
  }
  bool clearIntent() override {
    return storageReady && (!LittleFS.exists(PowerIntentPath) || LittleFS.remove(PowerIntentPath));
  }
  bool returnHome() override {
    if(wifiSetupHold.load())return false;
    if (navigator != primaryNavigator || !anchored || !dockGuard.ready() || !lidarPort.ready())
      return false;
    // Persisted mission intent already exists before motion; no flash write at low voltage.
    if (robot->enterSafe() != roocore::Result::Ok || robot->stopAll() != roocore::Result::Ok ||
        !robotLink.finishTransmit(50)) return false;
    appliedPwm = appliedSidePwm = appliedVacuumPwm = 0;
    carpet.invalidate();
    assistance.operatorPause();
    undocking = startAfterUndock = false;
    nativeDock = false; ownsRobot = false;
    navigator->goHome(latest, false, roonav::ReturnCause::Battery);
    arbiter.local(millis()); // invalidates any old remote velocity lease
    return true;
  }
  void lidarOff() override { lidarPower(false); }
  bool fallbackSafe(bool starting) override {
    const uint32_t now = millis();
    // No height sensors are integrated. Only a physically surveyed, anchored dock
    // corridor including the complete add-on height/envelope can qualify this path.
    if (wifiSetupHold.load() || !cloudPolicyReady() || navigator->zones.hasBarriers() || !ROO_NATIVE_FALLBACK_VERIFIED || !ROO_FALLBACK_CLEARANCE_VERIFIED ||
        !ROO_COMMISSIONED || !ROO_ENCODERS_VERIFIED || !linkReady || powerStreamResync ||
        navigator != primaryNavigator || !anchored || !dockGuard.ready() ||
        !latest.localized || !latest.sensorsValid || !latest.chargerValid ||
        uint32_t(now - latest.sensorsMs) > 200 || uint32_t(now - latest.poseMs) > 300 ||
        latest.cliff || latest.wheelDrop || latest.bumper || latest.overcurrent ||
        latest.chargerState == 5 || latest.chargerSources || navigator->nearbyContact(latest.pose))
      return false;
    return starting ? dockGuard.beginApproach(dockObservation(now))
                    : dockGuard.permitsNativeDock(dockObservation(now));
  }
  bool seekDock() override {
    if(wifiSetupHold.load())return false;
    // LiDAR has been shut down in order; never retry a partial native command.
    if (robot->seekDock() != roocore::Result::Ok || !robotLink.finishTransmit(50)) return false;
    nativeDock = true;
    ownsRobot = false; // the native controller now owns motion; no lease renewals
    return true;
  }
} powerPort;

void serviceBatteryLearning(uint32_t now) {
  if (!batteryMutex || xSemaphoreTake(batteryMutex,0) != pdTRUE) return;
  const auto sample = batteryLearningSample(now);
  batteryLearning.observe(sample);
  const bool localized = sample.frame != 0;
  if (navigator->returnSequence != batteryReturnSequence) {
    batteryReturnSequence = navigator->returnSequence;
    if (navigator->returnCause != roonav::ReturnCause::None && localized && !nativeObservation)
      batteryLearning.beginReturn(now,dockCalibrationKey,primaryNavigator->routeToDockMeters(latest.pose));
  }
  if (uint32_t(now-batteryRouteAt) >= 1000) {
    batteryRouteAt = now;
    batteryRouteM = localized ? primaryNavigator->routeToDockMeters(latest.pose) : NAN;
  }
  batteryReserve = localized && uint32_t(now-batteryRouteAt) <= 1000
      ? batteryLearning.reserve(now,dockCalibrationKey,batteryRouteM) : roonav::BatteryLearning::Reserve{};
  const bool early = batteryReserve.earlyReturn &&
      !nativeObservation && !serviceMode && !nativeProfileChanging &&
      (navigator->state == roonav::State::Cleaning || navigator->state == roonav::State::Edging ||
       navigator->state == roonav::State::Touchup);
  const bool retryReady = !batteryStoreFailed || uint32_t(now-batteryStoreAt) >= 300000;
  if (batteryLearning.dirty() && retryReady && batteryStorageAllowed(sample) &&
      uint32_t(now-batteryStoreAt) >= 300000) {
    batteryStoreAt = now;
    batteryStoreFailed = !batteryLearningStore.save(batteryLearning,batteryFiles,*batteryScratch);
    // Flash latency must not masquerade as a measured continuous interval.
    batteryLearning.breakContinuity();
  }
  xSemaphoreGive(batteryMutex);
  if (early) powerManagement.requestReserveReturn(millis(),powerPort);
}
String batterySummaryJson(uint32_t now) {
  if (!batteryMutex || xSemaphoreTake(batteryMutex,pdMS_TO_TICKS(5)) != pdTRUE) return "null";
  const auto model = batteryLearning.model(); const auto e = batteryLearning.estimate(now);
  const auto reserve = batteryReserve; const auto cfg = batteryLearning.cfg;
  const uint32_t lineage = batteryLearning.data.lineage, cycles = batteryLearning.data.completedCycles,
      gaps = batteryLearning.data.gaps, invalid = batteryLearning.data.invalidSamples,
      sequence = batteryLearningStore.sequence;
  const uint64_t covered = batteryLearning.data.coveredMs, missing = batteryLearning.data.missingMs;
  const double draw = batteryLearning.data.drawMah, charge = batteryLearning.data.chargeMah,
      drawMwh = batteryLearning.data.drawMwh, chargeMwh = batteryLearning.data.chargeMwh;
  const bool dirty = batteryLearning.dirty(), mismatch = batteryLearning.profileMismatch,
      failed = batteryStoreFailed, corrupt = batteryLearningStore.corrupt,
      fresh = batteryLearning.fresh(now), memory = batteryScratch != nullptr;
  xSemaphoreGive(batteryMutex);
  String j = "{\"schema\":1,\"pack_key\":" + String(cfg.packKey) +
      ",\"calibration_key\":" + String(cfg.calibrationKey) + ",\"lineage\":" + String(lineage);
  j += ",\"reference\":\"oi_soc_and_integrated_current\",\"independent_capacity_measurement\":false";
  j += ",\"fresh\":" + String(fresh ? "true" : "false") +
      ",\"qualified\":" + String(cfg.qualified() ? "true" : "false") +
      ",\"adaptive_enabled\":" + String(cfg.adaptiveReturn ? "true" : "false");
  j += ",\"cycles\":" + String(cycles) + ",\"gaps\":" + String(gaps) + ",\"invalid_samples\":" + String(invalid);
  j += ",\"covered_ms\":" + String((unsigned long long)covered) +
      ",\"missing_ms\":" + String((unsigned long long)missing);
  j += ",\"draw_mah\":" + String(draw,3) + ",\"charge_mah\":" + String(charge,3) +
      ",\"draw_mwh\":" + String(drawMwh,3) + ",\"charge_mwh\":" + String(chargeMwh,3);
  j += ",\"advisory_cycles\":" + String(model.advisoryCycles) +
      ",\"qualified_cycles\":" + String(model.qualifiedCycles) + ",\"return_cycles\":" + String(model.returnCycles);
  j += ",\"capacity_mah\":" + (model.advisoryCycles ? String(model.capacityMah,2) : String("null")) +
      ",\"capacity_min_mah\":" + String(model.capacityMinimumMah,2) +
      ",\"capacity_max_mah\":" + String(model.capacityMaximumMah,2) +
      ",\"capacity_lower_mah\":" + (model.qualifiedCycles >= cfg.minimumCycles ? String(model.capacityLowerMah,2) : String("null"));
  j += ",\"remaining_mah\":" + (e.gaugeValid || e.anchored ? String(e.remainingMah,2) : String("null")) +
      ",\"remaining_source\":\"" + String(e.anchored ? "full_anchor_integrated" : e.gaugeValid ? "oi_gauge" : "unavailable") + "\"";
  j += ",\"remaining_lower_mah\":" + (e.learnedCapacity ? String(e.remainingLowerMah,2) : String("null")) +
      ",\"reserve_available\":" + String(reserve.available && fresh ? "true" : "false") +
      ",\"reserve_required_mah\":" + (reserve.available && fresh ? String(reserve.requiredMah,2) : String("null"));
  j += ",\"storage_enabled\":" + String(ROO_BATTERY_STORAGE_VERIFIED ? "true" : "false") +
      ",\"storage_memory\":" + String(memory ? "true" : "false") + ",\"snapshot_sequence\":" + String(sequence) +
      ",\"unsaved\":" + String(dirty ? "true" : "false") + ",\"write_failed\":" + String(failed ? "true" : "false") +
      ",\"profile_blocked\":" + String(mismatch ? "true" : "false") + ",\"corrupt_slot\":" + String(corrupt ? "true" : "false") + "}";
  return j;
}
String returnEventJson() {
  if (navigator->returnCause == roonav::ReturnCause::None || !missionId[0] || !returnBoot[0])
    return "null";
  const char *names[] = {"none", "manual", "battery", "completed"};
  return "{\"cause\":\"" + String(names[uint8_t(navigator->returnCause)]) +
         "\",\"provenance\":\"firmware_mission_event\",\"confirmed\":true,\"mission_id\":\"" +
         String(missionId) + "\",\"boot_id\":\"" + returnBoot +
         "\",\"at_ms\":" + String(navigator->returnAt) + "}";
}
void recordReturnEvent() {
  if (navigator->returnSequence != observedReturnSequence) {
    observedReturnSequence = navigator->returnSequence;
    memcpy(returnBoot, bootId, sizeof returnBoot);
  }
}
void beginMissionIdentity(uint32_t now) {
  snprintf(missionId, sizeof missionId, "%s-%08lx", bootId,
           static_cast<unsigned long>(++missionCounter));
  memcpy(missionStartBoot, bootId, sizeof missionStartBoot);
  missionStartedAt = now;
  returnBoot[0] = 0;
  chargeSupervisor.reset();
}
void captureProgress(uint32_t now, const String &status) {
  if (uint32_t(now - lastProgress) < ROO_PROGRESS_INTERVAL_MS ||
      xSemaphoreTake(progressMutex, 0) != pdTRUE)
    return;
  // Bounded raw copy only. HTTP task serializes arrays outside the control loop/lock.
  auto &p = *progressPublished;
  p.valid = status.length() < sizeof p.status;
  if (p.valid) {
    p.at = now; p.sequence = ++progressSequence; p.revision = mapRevision;
    p.policy = cloudPolicyReady() ? cloudPolicy : roonav::CloudPolicy{};
    p.scanAt = latest.scanMs;
    memcpy(p.ranges, latest.ranges, sizeof p.ranges);
    for(unsigned i=0;i<52;++i) p.sensors[i]=robot->sensor(static_cast<roocore::Sensor>(i+7));
    p.anchorRevision = dockAnchorRevision;
    p.config = navigator->cfg; p.pose = latest.pose;
    p.dockConfig = dockGuard.config(); p.staging = navigator->dock;
    p.localized = latest.localized && navigator == primaryNavigator;
    p.anchored = anchored && dockGuard.ready() && navigator == primaryNavigator;
    memcpy(p.boot, bootId, sizeof bootId);
    memcpy(p.mission, missionId, sizeof missionId);
    if (navigator == primaryNavigator)
      snprintf(p.map, sizeof p.map, "%s", mapId);
    else
      snprintf(p.map, sizeof p.map, "%s-local-%lu", mapId, static_cast<unsigned long>(dirtFrame));
    status.toCharArray(p.status, sizeof p.status);
    const size_t count = navigator->count();
    memcpy(p.cells, navigator->cells, count);
    memcpy(p.covered, navigator->covered, count);
    memcpy(p.side, navigator->sideCovered, count);
    memcpy(p.excluded, navigator->excluded, count);
    lastProgress = now;
  }
  xSemaphoreGive(progressMutex);
}
void publish(uint32_t now) {
  String s = "{\"now\":" + String(now) + ",\"epoch\":" + String(arbiter.epoch) +
             ",\"owner\":" + String(uint8_t(arbiter.owner));
  s += ",\"boot_id\":\"" + String(bootId) + "\",\"mission_id\":\"" + missionId + "\"";
  s += ",\"firmware_version\":\"" ROO_FIRMWARE_VERSION "\",\"firmware_build\":\"" __DATE__ " " __TIME__ "\"";
  s += ",\"native_observation\":" + String(nativeObservation ? "true" : "false");
  s += ",\"service_mode\":" + String(serviceMode ? "true" : "false") +
       ",\"ota_ready\":" + String(otaReady.load() ? "true" : "false") +
       ",\"ota_enabled\":" + String(ROO_OTA_ENABLED ? "true" : "false");
  s += ",\"profile\":\"" + String(navigator->passes == 2 ? "deep" : "daily") + "\"";
  s += ",\"mission_active\":" + String(active() ? "true" : "false");
  s += ",\"active_ms\":" + String(navigator->activeMs) +
       ",\"mission_started_at_ms\":" + String(missionStartedAt) +
       ",\"mission_start_boot_id\":\"" + missionStartBoot + "\"";
  s += ",\"return_cause\":" + returnEventJson();
  s += ",\"pass_index\":" + String(navigator->pass);
  const auto battery = robot->battery(now, 200);
  const auto batterySample = robot->sensor(roocore::Sensor::ChargeMah);
  s += ",\"sensors_valid\":" + String(latest.sensorsValid ? "true" : "false");
  s += ",\"battery_valid\":" + String(latest.batteryValid ? "true" : "false");
  s += ",\"battery_sample_ms\":" + String(batterySample.timestampMs) +
       ",\"battery_ms\":" + String(batterySample.timestampMs);
  s += ",\"charge_mah\":" + (battery.valid ? String(battery.chargeMah) : String("null")) +
       ",\"capacity_mah\":" + (battery.valid ? String(battery.capacityMah) : String("null"));
  s += ",\"current_ma\":" + (battery.valid ? String(battery.milliamps) : String("null")) +
       ",\"temperature_c\":" + (battery.valid ? String(battery.temperatureC) : String("null"));
  s += ",\"charger_valid\":" + String(latest.chargerValid ? "true" : "false") +
       ",\"charging_state\":" + (latest.chargerValid ? String(latest.chargerState) : String("null")) +
       ",\"charging_sources\":" + (latest.chargerValid ? String(latest.chargerSources) : String("null"));
  const char *chargePhases[] = {"unknown", "contact", "waiting", "bulk", "reconditioning",
                               "maintenance", "preparing", "held"};
  const char *chargeReasons[] = {"none", "telemetry_invalid", "no_source", "charger_fault",
                                "not_passive_or_moving", "departure_unqualified", "waiting_timeout",
                                "preparation_timeout"};
  s += ",\"charge_phase\":\"" + String(chargePhases[uint8_t(chargeSupervisor.phase)]) + "\"";
  s += ",\"charge_reason\":\"" + String(chargeReasons[uint8_t(chargeSupervisor.why)]) + "\"";
  s += ",\"charge_policy\":\"" + String(ROO_CHARGE_MISSION_READY ? "mission_energy_ready" : "native_cycle_complete") + "\"";
  s += ",\"automatic_brc_wake\":false,\"link_recovery\":\"" +
       String(latest.chargerValid ? "not_required" : "held_manual_verification_required") + "\"";
  s += ",\"dock_guard_ready\":" + String(dockGuard.ready() ? "true" : "false") +
       ",\"dock_guard_issue\":" + String(uint8_t(dockGuard.issue())) +
       ",\"dock_guard_mode\":" + String(uint8_t(dockGuard.mode()));
  s += ",\"help_id\":" + String(assistance.id) + ",\"frame\":" + String(assistance.frame);
  s += ",\"help_status\":" + String(uint8_t(assistance.status));
  s += ",\"help_reason\":" + String(uint8_t(assistance.reason));
  s += ",\"help_pending\":" + String(assistance.pending() ? "true" : "false");
  s += ",\"global_localized\":" +
       String(anchored && navigator == primaryNavigator ? "true" : "false");
  s += ",\"provisional\":" + String(navigator != primaryNavigator ? "true" : "false");
  const auto ir = robot->sensor(roocore::Sensor::InfraredOmni);
  s += ",\"dock_ir_raw\":" + String(ir.fresh(now, 200) ? ir.value : -1);
  s += ",\"state\":" + String(uint8_t(navigator->state)) +
       ",\"reason\":" + String(uint8_t(navigator->reason));
  s += ",\"undocking\":" + String(undocking ? "true" : "false");
  s += ",\"commissioned\":" +
       String(ROO_COMMISSIONED && ROO_ENCODERS_VERIFIED && powerManagement.cfg.valid() &&
                      LidarPowerConfigured && dockGuard.ready() ? "true"
                                                                                     : "false");
  s += ",\"localized\":" + String(latest.localized ? "true" : "false") +
       ",\"x\":" + String(odometry.pose.x, 4) + ",\"y\":" + String(odometry.pose.y, 4) +
       ",\"yaw\":" + String(odometry.pose.yaw, 5);
  s += ",\"battery\":" + (latest.batteryValid ? String(latest.battery, 1) : String("null")) + ",\"pose_ms\":" + String(latest.poseMs) +
       ",\"scan_ms\":" + String(latest.scanMs) + ",\"sensors_ms\":" + String(latest.sensorsMs);
  s += ",\"scan_valid\":" + String(latest.scanValid ? "true" : "false") +
       ",\"docked\":" + String(latest.docked ? "true" : "false") +
       ",\"charging\":" + String(latest.charging ? "true" : "false");
  s += ",\"cliff\":" + String(latest.cliff ? "true" : "false") +
       ",\"wheel_drop\":" + String(latest.wheelDrop ? "true" : "false") +
       ",\"alignment\":" + String(alignmentScore, 3);
  unsigned known = 0, main = 0, side = 0, pending = 0;
  for (int c = 0; c < navigator->count(); ++c)
    if (navigator->cells[c] == -1) {
      for (int sub = 0; sub < 4; ++sub) {
        ++known;
        bool central = navigator->covered[c] & (1u << (sub + navigator->pass * 4));
        main += central;
        side += !central && (navigator->sideCovered[c] & (1u << sub));
        pending += navigator->safe[c] && !central;
      }
    }
  s += ",\"known_floor_samples\":" + String(known);
  s += ",\"main_cleaned_samples\":" + String(main);
  s += ",\"side_only_samples\":" + String(side);
  s += ",\"pending_safe_samples\":" + String(pending);
  s += ",\"cleared_obstacles\":" + String(navigator->clearedObstacles);
  s += ",\"obstacle_waits\":" + String(navigator->obstacleWaits);
  s += ",\"contact_points\":[";
  unsigned contactCount = 0;
  for (int c = 0; c < navigator->count(); ++c) {
    if (!navigator->contacts[c])
      continue;
    if (contactCount < 32) {
      if (contactCount)
        s += ',';
      auto p = navigator->center(c);
      s += "[" + String(p.x, 3) + "," + String(p.y, 3) + "]";
    }
    ++contactCount;
  }
  s += "],\"contact_count\":" + String(contactCount);
  s += ",\"frontier_visits\":" + String(navigator->frontierVisits);
  s += ",\"brush_width_m\":" + String(navigator->cfg.brushWidth, 3);
  s += ",\"brush_depth_m\":" + String(navigator->cfg.brushDepth, 3);
  s += ",\"brush_offset_m\":" + String(navigator->cfg.brushOffset, 3);
  s += ",\"side_brush_x_m\":" + String(navigator->cfg.sideX, 3);
  s += ",\"side_brush_y_m\":" + String(navigator->cfg.sideY, 3);
  s += ",\"side_brush_radius_m\":" + String(navigator->cfg.sideRadius, 3);
  s += ",\"geometry_calibrated\":" + String(ROO_BRUSH_GEOMETRY_CALIBRATED ? "true" : "false");
  s += ",\"pass_axis\":\"" + String(navigator->pass == 0 ? "horizontal" : "vertical") + "\"";
  const char *floorNames[] = {"unknown", "hard_estimate", "carpet_estimate"};
  s += ",\"floor_estimate\":\"" + String(floorNames[unsigned(carpet.surface)]) + "\"";
  s += ",\"carpet_boost_configured\":" + String(carpet.configured() ? "true" : "false");
  s +=
      ",\"carpet_entries\":" + String(carpet.entries) + ",\"carpet_exits\":" + String(carpet.exits);
  s += ",\"carpet_entered_at_ms\":" + String(carpet.enteredAt);
  s += ",\"carpet_exited_at_ms\":" + String(carpet.exitedAt);
  const auto brushCurrent = robot->sensor(roocore::Sensor::MainBrushCurrent);
  s += ",\"main_brush_current_ma\":" +
       (brushCurrent.fresh(now, 200) ? String(brushCurrent.value) : String("null"));
  s += ",\"brush_pwm\":" + String(appliedPwm) + ",\"vacuum_pwm\":" + String(appliedVacuumPwm);
  const auto floorVoltage = robot->sensor(roocore::Sensor::VoltageMv);
  s += ",\"battery_mv\":" +
       (floorVoltage.fresh(now, 1000) ? String(floorVoltage.value) : String("null"));
  const char *powerStates[] = {"unconfigured", "boot_guard", "running", "warning", "returning",
      "urgent_return", "critical_hold", "handoff_hold", "native_fallback", "docked_hold", "held"};
  const char *powerReasons[] = {"calibration_required", "boot_stabilizing", "none", "low_voltage",
      "telemetry_invalid", "charger_present", "stop_failed", "persistence_failed", "restart_guard",
      "approval_required", "safety_blocked", "native_command_failed", "approval_revoked", "operator_hold", "learned_reserve"};
  s += ",\"power_state\":\"" + String(powerStates[unsigned(powerManagement.state)]) + "\"";
  s += ",\"power_reason\":\"" + String(powerReasons[unsigned(powerManagement.reason)]) + "\"";
  s += ",\"power_configured\":" + String(powerManagement.cfg.valid() ? "true" : "false");
  s += ",\"power_warning_mv\":" + String(powerManagement.cfg.warningMv);
  s += ",\"power_return_mv\":" + String(powerManagement.cfg.returnMv);
  s += ",\"power_return_ms\":" + String(powerManagement.cfg.returnMs);
  s += ",\"power_urgent_mv\":" + String(powerManagement.cfg.urgentMv);
  s += ",\"power_critical_mv\":" + String(powerManagement.cfg.criticalMv);
  s += ",\"power_hw_cutoff_nominal_mv\":12110,\"power_d10_assertions\":0";
  s += ",\"fallback_offered\":" + String(powerManagement.fallbackOffered() ? "true" : "false");
  s += ",\"fallback_approved\":" + String(powerManagement.approved ? "true" : "false");
  s += ",\"fallback_qualified\":" + String(ROO_NATIVE_FALLBACK_VERIFIED && ROO_FALLBACK_CLEARANCE_VERIFIED ? "true" : "false");
  s += ",\"fallback_approval_revision\":" + String(powerManagement.approvalRevision);
  s += ",\"fallback_scope\":" + String(powerManagement.scope);
  s += ",\"fallback_attempts\":" + String(powerManagement.fallbackAttempts);
  s += ",\"power_stop_confirmed\":" + String(powerManagement.stopConfirmed ? "true" : "false");
  const char *lidarStates[] = {"off", "warming", "ready", "fault"};
  s += ",\"lidar_power_state\":\"" + String(lidarStates[unsigned(lidarPort.state())]) + "\"";
  s += ",\"lidar_power_configured\":" + String(LidarPowerConfigured ? "true" : "false");
  s += ",\"lidar_uart_ready\":" + String(lidarPort.ready() ? "true" : "false");
  s += ",\"lidar_rise_ms\":" + String(ROO_LIDAR_RISE_MS);
  s += ",\"power_intent_pending\":" + String(powerManagement.intentPending ? "true" : "false");
  s += ",\"power_hw_off_max_verified_mv\":" + String(powerManagement.cfg.hardwareOffMaxMv);
  s += ",\"power_recover_mv\":" + String(powerManagement.cfg.recoverMv);
  s += ",\"power_save_min_mv\":" + String(powerManagement.cfg.saveMinMv);

  s += ",\"floor_forward_mps\":" + String(floorSpeed, 3);
  s += ",\"floor_yaw_rate\":" + String(floorTurn, 3);
  const auto stasis = robot->stasis(now);
  s += ",\"stasis_value\":" +
       (stasis.sample.fresh(now, 200) ? String(stasis.sample.value) : String("null"));
  s += ",\"stasis_usable_for_progress\":" + String(stasis.usableForProgress ? "true" : "false");
  s += ",\"caster_maintenance_suggested\":" + String(stasisMaintenance ? "true" : "false");
  s += ",\"caster_maintenance_at_ms\":" + String(stasisMaintenanceAt);
  s += ",\"caster_maintenance_message\":";
  s += stasisMaintenance
           ? "\"Check and clean the caster wheel, then dry it before refitting. This may also "
             "prompt a general maintenance check; it does not prove the whole robot is dirty.\""
           : "null";
  s += ",\"dirt_automatic_cleaning\":false";
  s += ",\"wall_ir_valid\":" + String(latest.wallIRValid ? "true" : "false");
  s += ",\"wall_signal\":" + String(latest.wallSignal);
  s += ",\"rooms_completed\":" + String(navigator->roomsCompleted);
  s += ",\"edge_incomplete\":" + String(navigator->edgeIncomplete);
  s += ",\"passes\":" + String(navigator->passes);
  s += ",\"pass\":" + String(navigator->pass + 1) + ",\"skipped\":" + String(navigator->skipped) +
       ",\"error\":\"" + lastError + "\",\"ranges\":[";
  for (unsigned i = 0; i < 180; ++i) {
    if (i)
      s += ',';
    s += String(latest.ranges[i], 3);
  }
  s += "]";
  int target = navigator->target();
  if (target >= 0) {
    auto p = navigator->targetPose();
    s += ",\"target\":[" + String(p.x, 4) + "," + String(p.y, 4) + "]";
  } else
    s += ",\"target\":null";
  s += "}";
  s.remove(s.length()-1);
  s += ",\"battery_learning\":" + batterySummaryJson(now) + "}";
  captureProgress(now, s);
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  publishedStatus = s;
  xSemaphoreGive(statusMutex);
}
void sample(uint32_t now) {
  const auto safety = robot->safety(now, 200);
  const auto battery = robot->battery(now, 200);
  auto left = robot->sensor(roocore::Sensor::LeftEncoder),
       right = robot->sensor(roocore::Sensor::RightEncoder);
  if (left.generation != lastGeneration && left.fresh(now, 200) && right.fresh(now, 200) &&
      left.generation == right.generation) {
    lastGeneration = left.generation;
    if (odometry.valid &&
        !odometry.update(left.value, right.value, left.timestampMs, navigator->cfg))
      anchored = false;
  }
  latest.cleaningActive = appliedPwm > 0 &&
                          robot->sensor(roocore::Sensor::OiMode).fresh(now, 200) &&
                          robot->sensor(roocore::Sensor::OiMode).value == 2;
  const auto wallSignal = robot->sensor(roocore::Sensor::WallSignal);
  const auto wall = robot->sensor(roocore::Sensor::Wall);
  latest.wallIRValid = wall.fresh(now, 200) && wallSignal.fresh(now, 200);
  latest.wallDetected = latest.wallIRValid && wall.value != 0;
  latest.wallSignal = latest.wallIRValid ? wallSignal.value : 0;
  latest.sideBrushActive = appliedSidePwm > 0 &&
                           robot->sensor(roocore::Sensor::OiMode).fresh(now, 200) &&
                           robot->sensor(roocore::Sensor::OiMode).value == 2;
  latest.now = now;
  latest.pose = odometry.pose;
  latest.poseMs = odometry.stamp;
  latest.sensorsMs = robot->sensor(roocore::Sensor::BumpsAndDrops).timestampMs;
  latest.sensorsValid = safety.valid;
  latest.batteryValid = battery.valid && battery.percentageValid;
  latest.cliff = safety.cliffLeft || safety.cliffFrontLeft || safety.cliffFrontRight ||
                 safety.cliffRight || safety.virtualWall;
  latest.wheelDrop = safety.wheelDropLeft || safety.wheelDropRight;
  latest.bumper = safety.bumpLeft || safety.bumpRight;
  if (latest.wheelDrop) {
    if (!wheelDropSince)
      wheelDropSince = now;
    if (uint32_t(now - wheelDropSince) >= 500)
      memset(trailMs, 0, sizeof trailMs);
  } else
    wheelDropSince = 0;
  const auto overcurrent = robot->sensor(roocore::Sensor::Overcurrents);
  latest.overcurrent = !overcurrent.fresh(now, 200) || overcurrent.value != 0;
  latest.battery = battery.percentage;
  const auto charger = robot->sensor(roocore::Sensor::ChargingSources);
  const auto chargeState = robot->sensor(roocore::Sensor::ChargingState);
  const auto oiMode = robot->sensor(roocore::Sensor::OiMode);
  latest.chargerValid = battery.valid && charger.fresh(now, 200) && oiMode.fresh(now, 200) &&
                        charger.generation == chargeState.generation &&
                        oiMode.generation == chargeState.generation;
  latest.chargerMs = chargeState.timestampMs;
  latest.chargerSources = charger.value;
  latest.chargerState = uint8_t(battery.charging);
  latest.docked = charger.fresh(now, 200) && (charger.value & 2);
  latest.charging = latest.chargerValid && latest.docked &&
                    (battery.charging == roocore::Charging::FullCharging ||
                     battery.charging == roocore::Charging::Trickle ||
                     battery.charging == roocore::Charging::Reconditioning);
  if (!left.fresh(now, 200) || !right.fresh(now, 200) || left.generation != right.generation) {
    chargeWheelsKnown = false;
    chargeWheelAt = now;
  } else if (!chargeWheelsKnown || left.generation != chargeWheelGeneration) {
    if (!chargeWheelsKnown || chargeLeft != left.value || chargeRight != right.value)
      chargeWheelAt = now;
    chargeWheelsKnown = true;
    chargeWheelGeneration = left.generation;
    chargeLeft = left.value; chargeRight = right.value;
  }
  roonav::ChargeSupervisor::Sample charge;
  charge.now = now; charge.stamp = chargeState.timestampMs; charge.generation = chargeState.generation;
  charge.valid = latest.chargerValid;
  charge.percentageValid = latest.batteryValid;
  charge.percentage = battery.percentage;
  charge.state = latest.chargerState; charge.sources = latest.chargerSources; charge.mode = oiMode.value;
  charge.stopped = chargeWheelsKnown && uint32_t(now - chargeWheelAt) >= 1000;
  charge.currentMa = battery.milliamps; charge.temperatureC = battery.temperatureC;
  chargeSupervisor.update(charge, navigator->state == roonav::State::Charging && !navigator->finished);
  latest.chargeContactConfirmed = chargeSupervisor.contactConfirmed;
  const auto departureVoltage = robot->sensor(roocore::Sensor::VoltageMv);
  latest.chargeReady = chargeSupervisor.ready && departureVoltage.fresh(now, 200) &&
                      departureVoltage.value > int32_t(powerManagement.cfg.warningMv);
  latest.dirt = uint8_t(robot->sensor(roocore::Sensor::DirtDetect).value);
  if (lidar.scans != lastScan && lidar.valid) {
    ++mapRevision;
    lastScan = lidar.scans;
    memset(latest.ranges, 0, sizeof latest.ranges);
    for (int i = 0; i < 180; ++i) {
      float r = lidar.ranges[i];
      if (r <= 0)
        continue;
      float a = (ROO_LIDAR_CLOCKWISE ? -i : i) * roonav::Pi / 90 +
                ROO_LIDAR_YAW_DEGREES * roonav::Pi / 180;
      float x = r * cosf(a) + ROO_LIDAR_X_METERS, y = r * sinf(a) + ROO_LIDAR_Y_METERS;
      float angle = atan2f(y, x);
      if (angle < 0)
        angle += 2 * roonav::Pi;
      unsigned j = unsigned(angle * 90 / roonav::Pi) % 180;
      float range = hypotf(x, y);
      if (latest.ranges[j] == 0 || range < latest.ranges[j])
        latest.ranges[j] = range;
    }
    if (!powerManagement.blocksMotion() &&
        assistance.status == roonav::NavigationHelp::Status::Checking) {
      roonav::Pose candidate = assistance.proposed;
      float score = 0;
      bool ok = roonav::alignToMap(candidate, latest.ranges, *primaryNavigator, score) &&
                score >= .85f &&
                roonav::distance(candidate.x, candidate.y, assistance.proposed.x,
                                 assistance.proposed.y) <= .04f &&
                fabsf(roonav::wrap(candidate.yaw - assistance.proposed.yaw)) <= .03f &&
                latest.sensorsValid && !latest.cliff && !latest.wheelDrop && !latest.bumper &&
                !latest.overcurrent;
      const bool accepted = assistance.confirm(ok);
      if (!ok && navigator == provisionalNavigator && !assistance.operatorHeld &&
          latest.sensorsValid && !latest.cliff && !latest.wheelDrop && !latest.bumper &&
          !latest.overcurrent) {
        // A rejected global hypothesis does not invalidate an otherwise healthy local map.
        latest.scanValid = true;
        latest.scanMs = now;
        latest.localized = anchored && odometry.valid;
        if (navigator->resume(latest)) {
          assistance.status = roonav::NavigationHelp::Status::LocalOnly;
          arbiter.owner = roonav::Owner::Local;
          lastError = "global candidate rejected; continuing isolated local cleaning";
        }
      }
      if (accepted) {
        stopMotion();
        navigator = primaryNavigator;
        ++dirtFrame;
        odometry.reset(assistance.proposed, left.value, right.value, now);
        anchored = true;
        latest.pose = odometry.pose;
        latest.poseMs = now;
        latest.localized = true;
        latest.scanValid = true;
        latest.scanMs = now;
        navigator->resume(latest);
        arbiter.owner = roonav::Owner::Local;
        lastError = "relocalized; provisional coverage retained separately";
        saveMission();
      }
    }
    if (anchored && odometry.valid &&
        assistance.status != roonav::NavigationHelp::Status::Checking &&
        !roonav::alignToMap(odometry.pose, latest.ranges, *navigator, alignmentScore))
      anchored = false;
    latest.pose = odometry.pose;
  }
  latest.scanMs = lidar.scanMs;
  latest.scanValid = lidarPort.ready() && lidar.valid && uint32_t(now - lidar.scanMs) <= 400;
  latest.localized = anchored && odometry.valid;
  latest.retreatVerified = false;
  for (unsigned j = 0; j < 16; ++j)
    if (uint32_t(now - trailMs[j]) < 5000 && trailMs[j] &&
        roonav::distance(trail[j].x, trail[j].y, latest.pose.x - .06f * cosf(latest.pose.yaw),
                         latest.pose.y - .06f * sinf(latest.pose.yaw)) < .03f)
      latest.retreatVerified = true;
  if (latest.localized && latest.sensorsValid && !latest.cliff && !latest.wheelDrop &&
      !latest.bumper && !latest.overcurrent && uint32_t(now - trailMs[(trailAt + 15) % 16]) > 200) {
    trail[trailAt] = latest.pose;
    trailMs[trailAt] = now;
    trailAt = (trailAt + 1) % 16;
  }
}
bool healthy() {
  const uint32_t now = millis();
  return !wifiSetupHold.load() && cloudPolicyReady() && !powerManagement.blocksMotion() && uint32_t(now - latest.sensorsMs) <= 200 &&
         uint32_t(now - latest.poseMs) <= 300 && uint32_t(now - latest.scanMs) <= 400 &&
         latest.sensorsValid && latest.batteryValid && latest.localized && latest.scanValid && !latest.cliff &&
         !latest.wheelDrop && (!latest.bumper || navigator->wallRetreat()) && !latest.overcurrent &&
         latest.batteryValid;
}
void updateFloorEstimate(uint32_t now) {
  const bool safe = healthy() && active() && !nativeDock && !undocking && appliedPwm > 0;
  if (!safe) {
    floorMotionReady = false;
    floorSpeed = floorTurn = 0;
  } else if (!floorMotionReady) {
    floorMotionReady = true;
    floorMotionPose = latest.pose;
    floorMotionAt = now;
    floorSpeed = floorTurn = 0;
  } else if (uint32_t(now - floorMotionAt) >= 200) {
    float dt = uint32_t(now - floorMotionAt) / 1000.f;
    float dx = latest.pose.x - floorMotionPose.x, dy = latest.pose.y - floorMotionPose.y;
    floorSpeed = (dx * cosf(floorMotionPose.yaw) + dy * sinf(floorMotionPose.yaw)) / dt;
    floorTurn = roonav::wrap(latest.pose.yaw - floorMotionPose.yaw) / dt;
    floorMotionPose = latest.pose;
    floorMotionAt = now;
  }
  const auto current = robot->sensor(roocore::Sensor::MainBrushCurrent);
  const auto voltage = robot->sensor(roocore::Sensor::VoltageMv);
  roonav::CarpetBoost::Sample s;
  s.now = now;
  s.stamp = current.timestampMs;
  s.currentMa = current.value;
  s.batteryMv = voltage.value;
  s.brushPwm = appliedPwm;
  s.valid = ROO_CARPET_CURRENT_VERIFIED && current.fresh(now, 200) && voltage.fresh(now, 1000);
  s.safe = safe;
  s.movingStraight = floorMotionReady && floorSpeed >= .03f && floorSpeed <= .25f &&
                     fabsf(floorTurn) <= .25f && uint32_t(now - floorMotionAt) <= 300;
  carpet.update(s);
}
bool nativePowerAllowed() {
  return powerManagement.state == roonav::PowerManagement::State::Running ||
      (powerManagement.state == roonav::PowerManagement::State::Unconfigured &&
       !powerManagement.intentPending);
}
void request(const Request &r, uint32_t now) {
  if(wifiSetupHold.load()) { stopMotion();lastError="Wi-Fi setup hold; retry command after setup";return; }
  if (!strcmp(r.action, "record-start")) {
    const auto mode = robot->sensor(roocore::Sensor::OiMode);
    if (cloudPolicyRequired || navigator->zones.hasBarriers() || serviceMode || nativeProfileChanging || movingSession() || nativeDock || ownsRobot ||
        !nativePowerAllowed() || !linkReady || !mode.fresh(now, 200) || mode.value != 1 ||
        (r.pwm && !ROO_RECORD_EXTENDED_ENABLED)) {
      lastError = "recording requires idle RooCore, fresh Passive OI, no power hold; extended needs opt-in";
      return;
    }
    xSemaphoreTake(nativeMutex, portMAX_DELAY);
    const bool running = nativeLog->recording;
    xSemaphoreGive(nativeMutex);
    if (running) { lastError = "recording already active; stop before starting a new session"; return; }
    nativeObservation = true; nativeExtended = r.pwm != 0;
    arbiter.hold(); assistance.operatorPause(); anchored = false; dockGuard.invalidate();
    lidarPower(false); // log native robot baseline; no add-on scanning/navigation
    nativeStartPending = true; nativeProfileChanging = true; nativeProfileAt = now;
    if (robot->resynchronizeInput(now) != roocore::Result::Ok) {
      nativeStartPending = nativeProfileChanging = false;
      lastError = "record stream resynchronization failed; observation hold retained";
    } else lastError = "preparing passive native recording; start Clean using robot button";
    return;
  }
  if (!strcmp(r.action, "record-stop")) {
    xSemaphoreTake(nativeMutex, portMAX_DELAY); nativeLog->stop(); xSemaphoreGive(nativeMutex);
    nativeStartPending = false;
    lastError = "recording stopped; native cleaning continues; observation hold retained";
    return;
  }
  if (!strcmp(r.action, "ota-arm") || !strcmp(r.action, "record-exit")) {
    if (!ROO_COMMISSIONED || !ROO_ENCODERS_VERIFIED || !linkReady || powerStreamResync ||
        nativeProfileChanging || movingSession() || !nativePowerAllowed() ||
        (strcmp(r.action, "record-exit") == 0 && serviceMode) ||
        (!strcmp(r.action, "ota-arm") && (!ROO_OTA_ENABLED || !ROO_OTA_POWER_VERIFIED ||
         ROO_OTA_MIN_MV <= 0 || strlen(ROO_OTA_PASSWORD) < 24))) {
      lastError = "service/exit requires qualified stop+encoders, idle mission, healthy link and OTA power settings";
      return;
    }
    if (robot->enterSafe() != roocore::Result::Ok || robot->stopAll() != roocore::Result::Ok ||
        !robotLink.finishTransmit(50)) {
      lastError = "native stop failed; observation hold retained"; return;
    }
    xSemaphoreTake(nativeMutex, portMAX_DELAY); nativeLog->stop(); xSemaphoreGive(nativeMutex);
    appliedPwm = appliedSidePwm = appliedVacuumPwm = 0;
    ownsRobot = nativeDock = false; arbiter.hold(); assistance.operatorPause();
    anchored = false; dockGuard.invalidate(); lidarPower(false);
    if (!strcmp(r.action, "ota-arm")) {
      serviceMode = true; nativeObservation = false; updateHold.reset(); otaReady.store(false);
      nativeProfileChanging = true; nativeStartPending = false; nativeProfileAt = now;
      if (robot->resynchronizeInput(now) != roocore::Result::Ok) {
        nativeProfileChanging = false; linkReady = false;
      }
      lastError = "service hold: waiting for fresh Safe mode and stationary wheels at qualified voltage";
    } else {
      if (robot->beginPassive() != roocore::Result::Ok ||
          robot->resynchronizeInput(now) != roocore::Result::Ok) {
        nativeObservation = true; linkReady = false;
        lastError = "native exit failed; observation hold retained"; return;
      }
      nativeObservation = false;
      nativeProfileChanging = true; nativeStartPending = false; nativeProfileAt = now;
      lastError = "native cleaning stopped; restoring navigation stream; physical reanchor required";
    }
    return;
  }
  if (!strcmp(r.action, "ota-exit")) {
    if (serviceMode && !otaUploading.load()) {
      otaReady.store(false); updateHold.reset();
      if (robot->beginPassive() != roocore::Result::Ok ||
          robot->resynchronizeInput(now) != roocore::Result::Ok) {
        linkReady = false; lastError = "service exit failed; service hold retained"; return;
      }
      serviceMode = false;
      nativeProfileChanging = true; nativeStartPending = false; nativeProfileAt = now;
      lastError = "service hold released; physical reanchor and explicit mission required";
    }
    return;
  }
  if (nativeObservation || serviceMode || nativeProfileChanging) {
    lastError = "native recording/service holds RooCore control; use record-stop, record-exit or ota-exit";
    return;
  }

  if (!strcmp(r.action, "fallback-revoke")) {
    powerManagement.revoke(powerPort);
    lastError = "native fallback approval revoked";
    return;
  }
  if (!strcmp(r.action, "fallback-approve")) {
    const int32_t remaining = int32_t(r.expires - now);
    if (strcmp(r.boot, bootId) || strcmp(r.mission, missionId) ||
        remaining <= 0 || remaining > 5000 ||
        !powerManagement.approve(missionCounter, r.epoch, now, r.expires))
      lastError = "fallback approval rejected: stale scope, no qualification or no fresh telemetry";
    else
      lastError = "explicit fallback approval recorded for this mission; local safety still required";
    return;
  }
  if (!strcmp(r.action, "power-reset")) {
    if (movingSession() || !powerManagement.resetAfterCharge(now, powerPort))
      lastError = "power reset requires a stopped powered dock, stable qualified charge and fresh data";
    else
      lastError = "power hold cleared; a new explicit mission command is required";
    return;
  }
  if (!strcmp(r.action,"battery-reset")) {
    if (!batteryMutex || strcmp(r.boot,bootId) || int32_t(r.expires-now) <= 0 ||
        uint32_t(r.expires-now) > 5000) { lastError = "stale battery reset"; return; }
    xSemaphoreTake(batteryMutex,portMAX_DELAY);
    if (r.sequence != batteryLearning.data.lineage || !batteryStorageAllowed(batteryLearningSample(now)))
      lastError = "battery reset requires current lineage and qualified stationary Passive power";
    else {
      batteryStoreFailed = !batteryLearningStore.reset(batteryLearning,batteryFiles,dirtBoot ^ esp_random());
      batteryReserve = {}; batteryRouteM = NAN; batteryStoreAt = now;
      lastError = batteryStoreFailed ? "battery reset failed; inspect storage" : "battery history reset; no motion started";
    }
    xSemaphoreGive(batteryMutex); return;
  }
  if (!strcmp(r.action, "pause")) {
    powerManagement.operatorPause(powerPort);
    if (powerManagement.blocksMotion()) return;
    assistance.operatorPause();
    undocking = startAfterUndock = false;
    dockGuard.endAccess();
    stopMotion();
    navigator->pause(now);
    arbiter.hold();
    saveMission();
    return;
  }
  if (powerManagement.blocksMotion() && strcmp(r.action, "init-storage")) {
    lastError = "power guard holds motion; inspect power_state/power_reason";
    return;
  }
  if ((powerManagement.returning() || powerManagement.state == roonav::PowerManagement::State::Warning) &&
      strcmp(r.action, "home")) {
    lastError = "voltage policy blocks new missions, remote takeover and cleaning resume";
    return;
  }
  if (!strcmp(r.action, "init-storage")) {
    if (cloudPolicyRequired || movingSession() || !powerManagement.canInitializeStorage()) {
      lastError = "stop before formatting; power intent must not be erased";
      return;
    }
    storageReady = LittleFS.format() && LittleFS.begin(false);
    powerManagement.begin(millis(), storageReady && LittleFS.exists(PowerIntentPath));
    return;
  }
  if (!strcmp(r.action, "map-clear")) {
    if (cloudPolicyRequired || movingSession() || !storageReady || powerManagement.intentPending) {
      lastError = "map-clear requires stopped mission, storage and no power-cutoff intent";
      return;
    }
    stopMotion();
    navigator = primaryNavigator;
    navigator->clearMap(now);
    snprintf(mapId, sizeof mapId, "%s-map-%08lx", bootId,
             static_cast<unsigned long>(++mapRevision));
    beginMissionIdentity(now);
    anchored = false;
    arbiter.hold();
    lastError = saveMission() ? "map cleared; physical dock reanchor required" : "map clear save failed";
    return;
  }
  if (!ROO_COMMISSIONED || !ROO_ENCODERS_VERIFIED) {
    lastError = "commissioning required; motion disabled";
    return;
  }
  if (!storageReady || !linkReady) {
    lastError = "storage or robot link unavailable";
    return;
  }
  if (!strncmp(r.action, "help-", 5)) {
    if (!assistance.validReply(r.epoch, r.sequence, r.expires, now)) {
      lastError = "stale or wrong-frame assistance reply";
      return;
    }
    if (!strcmp(r.action, "help-hold")) {
      assistance.hold();
      navigator->pause(now);
      stopMotion();
      return;
    }
    if (!strcmp(r.action, "help-continue")) {
      // Acknowledgement only: cannot reset a safety fault, extend a budget or re-arm remote
      // ownership.
      lastError = "server analyzing; local safety and battery limits remain active";
      return;
    }
    if (!strcmp(r.action, "help-relocalize")) {
      if (assistance.operatorHeld) {
        lastError = "operator paused; resume or reanchor explicitly";
        return;
      }
      if (nativeDock || !latest.scanValid || !latest.sensorsValid || latest.cliff ||
          latest.wheelDrop || latest.bumper || latest.overcurrent || latest.battery <= 6 ||
          uint32_t(now - latest.scanMs) > 400 || uint32_t(now - latest.sensorsMs) > 200) {
        lastError = "candidate requires healthy local observations";
        return;
      }
      float yaw = roonav::wrap(r.candidate.yaw - r.reference.yaw);
      roonav::Pose proposal = r.candidate;
      float dx = latest.pose.x - r.reference.x, dy = latest.pose.y - r.reference.y;
      proposal.x += dx * cosf(yaw) - dy * sinf(yaw);
      proposal.y += dx * sinf(yaw) + dy * cosf(yaw);
      proposal.yaw = roonav::wrap(latest.pose.yaw + yaw);
      int c = primaryNavigator->cell(proposal.x, proposal.y);
      if (c < 0 || !primaryNavigator->safe[c] || !assistance.candidate(proposal)) {
        lastError = "candidate outside known safe map";
        return;
      }
      stopMotion();
      navigator->pause(now);
      if (!lidarPower(true))
        return;
      lastError = "checking candidate against three fresh scans";
      return;
    }
    lastError = "unknown assistance action";
    return;
  }
  if (!strcmp(r.action, "resume") && assistance.operatorHeld && assistance.pending()) {
    if (navigator == provisionalNavigator && healthy() && navigator->resume(latest)) {
      assistance.operatorHeld = false;
      assistance.status = roonav::NavigationHelp::Status::LocalOnly;
      arbiter.owner = roonav::Owner::Local;
    } else
      lastError = "local assistance resume requires healthy observations";
    return;
  }
  if (!strcmp(r.action, "relocate") && !assistance.pending() && navigator == primaryNavigator &&
      navigator->state != roonav::State::Idle && navigator->state != roonav::State::Complete) {
    assistance.operatorResume();
    stopMotion();
    navigator->fault(roonav::Reason::Localization);
    anchored = false;
    return;
  }
  if (assistance.pending() && strcmp(r.action, "anchor")) {
    lastError = "assistance active; use help response or pause";
    return;
  }
  if (!strcmp(r.action, "anchor")) {
    if (movingSession() || !latest.docked || !latest.chargerValid ||
        latest.chargerState == 5 || !latest.sensorsValid) {
      lastError = "anchor requires stopped robot on powered dock";
      return;
    }
    const auto l = robot->sensor(roocore::Sensor::LeftEncoder),
               rr = robot->sensor(roocore::Sensor::RightEncoder);
    if (!l.fresh(now, 200) || !rr.fresh(now, 200)) {
      lastError = "encoders missing";
      return;
    }
    if (!lidarPower(true))
      return;
    if (assistance.pending()) {
      navigator = primaryNavigator;
      assistance.operatorHeld = false;
      assistance.status = roonav::NavigationHelp::Status::Resolved;
    }
    roonav::Pose p = navigator->dock;
    p.x += ROO_DOCK_EXIT_METERS * cosf(p.yaw);
    p.y += ROO_DOCK_EXIT_METERS * sinf(p.yaw);
    ++dirtFrame;
    odometry.reset(p, l.value, rr.value, now);
    latest.pose = p; latest.poseMs = now; latest.localized = true;
    configureDockGuard(++dockAnchorRevision);
    if (!dockGuard.confirmDocked(dockObservation(now))) {
      anchored = false;
      latest.localized = false;
      lastError = "dock geometry/anchor is not qualified";
      return;
    }
    anchored = true;
    assistance.operatorResume();
    anchorAt = now;
    lastError = "";
    return;
  }
  if (!strcmp(r.action, "velocity")) {
    if (!healthy() || !arbiter.command(r.epoch, r.sequence, r.expires, r.left, r.right, r.pwm, now))
      lastError = "remote command rejected";
    return;
  }
  if (!strcmp(r.action, "resume")) {
    if (!lidarPower(true))
      return;
    anchorAt = now;
  }
  if (!strcmp(r.action, "daily") || !strcmp(r.action, "deep")) {
    if (movingSession()) {
      lastError = "pause before starting a new mission";
      return;
    }
    if (!anchored || !dockGuard.ready() || !latest.sensorsValid || !latest.batteryValid || latest.cliff ||
        latest.wheelDrop || latest.bumper || latest.overcurrent ||
        uint32_t(now - latest.sensorsMs) > 200 || (!latest.docked && !healthy())) {
      lastError = "fresh sensors and physical map anchor required";
      return;
    }
    if (latest.docked && (!latest.chargerValid || latest.chargerState == 5 ||
                          !ROO_DOCK_EXIT_VERIFIED || !chargeSupervisor.cfg.valid())) {
      lastError = "dock departure and battery policy require qualification";
      return;
    }
    if (!powerManagement.canStartMission() || powerManagement.scope) {
      lastError = "new mission requires normal voltage and explicit power reset after prior mission";
      return;
    }
    beginMissionIdentity(now);
    if (!powerManagement.armMission(missionCounter, now, powerPort)) {
      lastError = "cannot persist the power-managed mission before motion";
      return;
    }
    navigator->start(!strcmp(r.action, "deep"), navigator->dock, now, true);
    if (latest.docked)
      navigator->waitOnDock(now);
    xSemaphoreTake(dirtMutex, portMAX_DELAY);
    dirtMonitor->beginSession(++dirtSession, dirtContext(now));
    xSemaphoreGive(dirtMutex);
    assistance.attempts = 0;
    assistance.operatorResume();
    arbiter.owner = roonav::Owner::Local;
    lastError = "";
    if (!saveMission()) {
      navigator->fault(roonav::Reason::Telemetry);
      lastError = "mission checkpoint save failed";
    }
    return;
  }
  if (!healthy()) {
    lastError = "fresh healthy sensors and dock anchor required";
    return;
  }
  if ((!strcmp(r.action, "home") || !strcmp(r.action, "resume")) && !powerManagement.scope) {
    beginMissionIdentity(now);
    if (!powerManagement.armMission(missionCounter, now, powerPort)) {
      lastError = "cannot arm power protection for home/resume";
      return;
    }
    // Arming invalidates old UART frames; repeat the explicit home/resume request
    // after the fresh-data boot guard clears. No movement is scheduled here.
    lastError = "power mission armed; wait for fresh telemetry and repeat home/resume";
    return;
  }
  if (!strcmp(r.action, "home")) {
    if (latest.docked && latest.chargerValid) {
      stopMotion();
      lastError = "already on powered base";
      return;
    }
    navigator->goHome(latest);
    recordReturnEvent();
    arbiter.local(now);
    if (!saveMission()) {
      navigator->fault(roonav::Reason::Telemetry);
      lastError = "return checkpoint save failed";
    }
  } else if (!strcmp(r.action, "resume")) {
    if (navigator->resume(latest)) {
      chargeSupervisor.reset();
      assistance.operatorResume();
      if (navigator->state == roonav::State::Charging)
        arbiter.owner = roonav::Owner::Local; // stationary local charging needs no remote handoff
      else
        arbiter.local(now);
    } else
      lastError = "resume rejected";
  } else if (!strcmp(r.action, "remote")) {
    if (!arbiter.takeover(now, healthy(), active()))
      lastError = "takeover rejected";
  } else if (!strcmp(r.action, "local")) {
    arbiter.local(now);
  } else
    lastError = "unknown action";
}
void apply(roonav::Output out, uint32_t now) {
  if (powerManagement.blocksMotion())
    return;
  now = millis();
  if (powerManagement.blocksCleaning()) { out.pwm = 0; out.sidePwm = 0; }
  if (wifiSetupHold.load() || !cloudPolicyReady()) { stopMotion(); return; }
  if (out.action == roonav::Action::SeekDock && navigator->zones.hasBarriers()) {
    stopMotion(); navigator->fault(roonav::Reason::NoRoute);
    lastError = "native dock handoff cannot enforce map barriers";
    return;
  }
  if (out.action == roonav::Action::SeekDock) {
    // The planner cannot bypass approval by requesting a native final approach.
    // Every Seek Dock handoff goes through the same stopped, scoped safety gate.
    if (latest.docked && latest.chargerValid) { stopMotion(); return; }
    if (!powerManagement.offerHandoff(now, powerPort)) {
      stopMotion();
      navigator->fault(roonav::Reason::Telemetry);
      lastError = "native docking handoff could not be prepared";
    } else
      lastError = "stopped at dock approach; explicit native handoff approval required";
    return;
  }
  if (out.action != roonav::Action::Drive) {
    stopMotion();
    if (terminal(navigator->state))
      dockGuard.endAccess();
    return;
  }
  if (!healthy()) {
    stopMotion();
    navigator->fault(roonav::Reason::Telemetry);
    return;
  }
  int l = 0, r = 0;
  if (!roonav::quantizeMotion(out, navigator->cfg.wheelSeparation, l, r)) {
    stopMotion();
    navigator->fault(roonav::Reason::Telemetry);
    return;
  }
  if (navigator != primaryNavigator ||
      !dockGuard.permitsMotion(dockObservation(now), out.speed, out.turn, 300,
                               out.pwm > 0 || (out.sidePwm != 255 && out.sidePwm > 0)) ||
      !navigator->allowsMotion(latest.pose, out.speed, out.turn, undocking)) {
    stopMotion();
    navigator->fault(roonav::Reason::NoRoute);
    return;
  }
  if (fabsf(out.speed) > .001f) {
    const float direction = out.speed > 0 ? 0 : roonav::Pi;
    bool clear = out.speed >= 0 || latest.retreatVerified || (undocking && ROO_DOCK_EXIT_VERIFIED);
    for (unsigned i = 0; i < 180; ++i)
      if (fabsf(roonav::wrap(i * roonav::Pi / 90 - direction)) < .5f) {
        float range = latest.ranges[i];
        if (!isfinite(range) || range < (navigator->wallApproach() ? navigator->cfg.radius - .015f
                                         : navigator->state == roonav::State::Edging
                                             ? navigator->cfg.radius + navigator->cfg.margin + .025f
                                             : navigator->cfg.radius + navigator->cfg.margin +
                                                   .02f + fabsf(out.speed) * .2f))
          clear = false;
      }
    if (!clear) {
      stopMotion();
      if (out.speed > 0 && !undocking && navigator->state == roonav::State::Cleaning)
        navigator->yieldForObstacle(latest);
      else
        navigator->fault(roonav::Reason::NoRoute);
      return;
    }
  }
  const auto mode = robot->sensor(roocore::Sensor::OiMode);
  if (robot->requestedMode() != roocore::Mode::Safe) {
    if (!armingSince) {
      robot->enterSafe();
      armingSince = now;
    }
    return;
  }
  if (!mode.fresh(now, 200) || mode.value != 2) {
    if (!armingSince)
      armingSince = now;
    if (uint32_t(now - armingSince) > 1500) {
      stopMotion();
      navigator->fault(roonav::Reason::Telemetry);
    }
    return;
  }
  armingSince = 0;
  appliedPwm = out.pwm;
  appliedSidePwm = out.sidePwm == 255 ? out.pwm : out.sidePwm;
  appliedVacuumPwm =
      out.speed >= .03f && fabsf(out.turn) <= .25f ? carpet.vacuum(out.pwm, out.pwm, now) : out.pwm;
  ownsRobot = true;
  if (robot->driveWheels(l, r, now, 300) != roocore::Result::Ok ||
      robot->setMotorPwm(out.pwm, appliedSidePwm, appliedVacuumPwm) != roocore::Result::Ok) {
    stopMotion();
    navigator->fault(roonav::Reason::Telemetry);
  }
}
void tryAssistance(uint32_t now) {
  if (powerManagement.returning() || powerManagement.blocksMotion() ||
      !ROO_ASSISTANCE_ENABLED || assistance.operatorHeld || assistance.pending() ||
      navigator != primaryNavigator || navigator->state != roonav::State::Fault ||
      (navigator->reason != roonav::Reason::Localization &&
       navigator->reason != roonav::Reason::NoRoute))
    return;
  stopMotion();
  const auto left = robot->sensor(roocore::Sensor::LeftEncoder),
             right = robot->sensor(roocore::Sensor::RightEncoder);
  bool localSafe = latest.sensorsValid && latest.scanValid && latest.battery > 6 && !latest.cliff &&
                   !latest.wheelDrop && !latest.bumper && !latest.overcurrent &&
                   uint32_t(now - latest.sensorsMs) <= 200 &&
                   uint32_t(now - latest.scanMs) <= 400 && left.fresh(now, 200) &&
                   right.fresh(now, 200);
  // A fresh map must not erase a nearby bumper hazard that may be below the laser.
  // Keep the help request, but require verified relocalization/operator intervention.
  localSafe = localSafe && !navigator->nearbyContact(latest.pose);
  // A provisional coordinate frame cannot safely erase the global base exclusion.
  // Keep server-assisted relocalization, but hold local driving until that succeeds.
  localSafe = false;
  if (!saveMission()) {
    navigator->fault(roonav::Reason::Telemetry);
    lastError = "cannot preserve global mission";
    return;
  }
  if (!assistance.begin(navigator->reason, now, localSafe))
    return;
  assistance.operatorHeld = false;
  arbiter.hold();
  // Freeze the global mission. The provisional map/coverage uses separate files and frame.
  String map = "{\"id\":" + String(assistance.id) + ",\"frame\":" + String(assistance.frame) +
               ",\"cols\":" + String(navigator->cfg.cols) +
               ",\"rows\":" + String(navigator->cfg.rows) +
               ",\"resolution\":" + String(navigator->cfg.resolution, 3) +
               ",\"origin_x\":" + String(navigator->cfg.originX, 3) +
               ",\"origin_y\":" + String(navigator->cfg.originY, 3) + ",\"cells\":\"";
  map.reserve(2 * navigator->count() + 512);
  for (int c = 0; c < navigator->count(); ++c)
    map += navigator->cells[c] == 1 ? 'o' : navigator->cells[c] == -1 ? 'f' : 'u';
  map += "\",\"safe\":\"";
  for (int c = 0; c < navigator->count(); ++c)
    map += navigator->safe[c] ? '1' : '0';
  map += "\"}";
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  publishedHelpMap = map;
  xSemaphoreGive(statusMutex);
  if (!lidarPower(true)) {
    assistance.hold();
    return;
  }
  anchorAt = now;
  if (!localSafe) {
    lastError = "help requested; waiting for healthy sensors";
    return;
  }
  if (!provisionalNavigator) {
    void *memory = heap_caps_malloc(sizeof(roonav::Navigator), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
      assistance.hold();
      lastError = "no PSRAM for provisional map";
      return;
    }
    provisionalNavigator = new (memory) roonav::Navigator();
  }
  provisionalNavigator->cfg = primaryNavigator->cfg;
  provisionalNavigator->cfg.homeKnown = false;
  provisionalNavigator->cfg.perimeter = false;
  provisionalNavigator->cfg.wallTouch = false;
  navigator = provisionalNavigator;
  roonav::Pose origin;
  ++dirtFrame;
  odometry.reset(origin, left.value, right.value, now);
  anchored = true;
  navigator->start(false, origin, now);
  latest.pose = origin;
  latest.poseMs = now;
  latest.localized = true;
  arbiter.owner = roonav::Owner::Local;
  lastError = "global pose unavailable; cleaning in separate local frame; help requested";
}
bool startRobotStream() {
  roocore::Sensor ids[10] = {roocore::Sensor::Safety,          roocore::Sensor::Battery,
                             roocore::Sensor::ChargingSources, roocore::Sensor::OiMode,
                             roocore::Sensor::WallSignal,      roocore::Sensor::InfraredOmni};
  unsigned count = 6;
  if (ROO_ENCODERS_VERIFIED) {
    ids[count++] = roocore::Sensor::LeftEncoder;
    ids[count++] = roocore::Sensor::RightEncoder;
  }
  if (ROO_CARPET_CURRENT_VERIFIED)
    ids[count++] = roocore::Sensor::MainBrushCurrent;
  if (ROO_STASIS_VERIFIED)
    ids[count++] = roocore::Sensor::Stasis;
  return robot->startStream(ids, count) == roocore::Result::Ok;
}

roonav::PowerManagement::Input powerInput(uint32_t now) {
  roonav::PowerManagement::Input in;
  in.now = now;
  const auto voltage = robot->sensor(roocore::Sensor::VoltageMv);
  const auto charge = robot->sensor(roocore::Sensor::ChargingState);
  const auto source = robot->sensor(roocore::Sensor::ChargingSources);
  const auto mode = robot->sensor(roocore::Sensor::OiMode);
  const auto left = robot->sensor(roocore::Sensor::LeftEncoder);
  const auto right = robot->sensor(roocore::Sensor::RightEncoder);
  in.valid = linkReady && !powerStreamResync && voltage.fresh(now, 200) && charge.fresh(now, 200) &&
             source.fresh(now, 200) && mode.fresh(now, 200) && left.fresh(now, 200) &&
             right.fresh(now, 200) && charge.generation == voltage.generation &&
             source.generation == voltage.generation && mode.generation == voltage.generation &&
             left.generation == voltage.generation && right.generation == voltage.generation;
  in.stamp = voltage.timestampMs;
  in.generation = voltage.generation;
  in.millivolts = voltage.value;
  in.left = left.value;
  in.right = right.value;
  in.mode = mode.value;
  in.charging = charge.value;
  in.sources = source.value;
  return in;
}

void serviceNativeModes(uint32_t now) {
  if (nativeProfileChanging) {
    if (robot->inputReady()) {
      bool ok;
      if (nativeObservation) {
        const auto id = nativeExtended ? roocore::Sensor::AllExtended : roocore::Sensor::AllBase;
        ok = robot->startStream(&id, 1) == roocore::Result::Ok;
      } else ok = startRobotStream();
      nativeProfileChanging = false;
      if (ok && nativeStartPending) {
        nativeSourceLost = 0; nativeGeneration = 0;
        xSemaphoreTake(nativeMutex, portMAX_DELAY);
        nativeLog->start(nativeExtended); xSemaphoreGive(nativeMutex);
        lastError = "recording native OI; Clean button controls native cleaning";
      } else if (!ok) {
        linkReady = false; lastError = "native stream setup failed; no automatic retry";
      }
      nativeStartPending = false;
    } else if (uint32_t(now - nativeProfileAt) > 2000) {
      nativeProfileChanging = nativeStartPending = false; linkReady = false;
      lastError = "native stream drain timed out; no automatic retry";
    }
  }
  if (serviceMode) {
    const auto mode = robot->sensor(roocore::Sensor::OiMode);
    const auto mv = robot->sensor(roocore::Sensor::VoltageMv);
    const auto left = robot->sensor(roocore::Sensor::LeftEncoder);
    const auto right = robot->sensor(roocore::Sensor::RightEncoder);
    const bool safe = linkReady && mode.fresh(now, 200) && mode.value == 2 &&
        mv.fresh(now, 200) && mv.value >= ROO_OTA_MIN_MV && left.fresh(now, 200) &&
        right.fresh(now, 200) && latest.sensorsValid && !latest.cliff && !latest.wheelDrop &&
        !latest.overcurrent && left.generation == right.generation &&
        left.generation == mv.generation && mode.generation == mv.generation;
    updateHold.observe(now, safe, uint16_t(left.value), uint16_t(right.value));
    otaCheckedAt.store(now);
    otaReady.store(updateHold.ready);
  }
}

void controlTask(void *) {
  robot->beginPassive();
  if (ROO_COMMISSIONED) {
    robot->enterSafe();
    robot->stopAll();
    robot->beginPassive();
  }
  linkReady = robot->verifyConnection() == roocore::Result::Ok;
  if (linkReady)
    linkReady = startRobotStream();
  storageReady = LittleFS.begin(false);
  cloudPolicyRequired = cloudPolicyRequired || (storageReady && LittleFS.exists(CloudPolicyIntent));
  if (cloudPolicyRequired && storageReady && LittleFS.exists(CloudPolicyIntent)) {
    File marker=LittleFS.open(CloudPolicyIntent,"r");
    uint32_t expected[2]{};
    if(!marker || marker.size()!=sizeof expected || marker.read(reinterpret_cast<uint8_t *>(expected),sizeof expected)!=sizeof expected || !expected[0]) cloudPolicyFault=true;
    else {cloudExpectedGeneration=expected[0];cloudExpectedChecksum=expected[1];}
    if(marker)marker.close();
  }
  loadMission();
  if (cloudPolicyRequired && cloudPolicy.generation &&
      (cloudPolicy.generation!=cloudExpectedGeneration || cloudPolicy.checksum!=cloudExpectedChecksum)) cloudPolicyFault=true;
  if (batteryMutex) {
    xSemaphoreTake(batteryMutex,portMAX_DELAY);
    if (storageReady && batteryScratch)
      batteryLearningStore.load(batteryLearning,batteryFiles,*batteryScratch,dirtBoot);
    else batteryLearning.begin(dirtBoot);
    batteryReturnSequence = navigator->returnSequence;
    batteryStoreAt = millis();
    xSemaphoreGive(batteryMutex);
  }
  powerManagement.begin(millis(), storageReady && LittleFS.exists(PowerIntentPath));
  for (;;) {
    const uint32_t now = millis();
    robot->poll(now);
    if (powerStreamResync && robot->inputReady()) {
      linkReady = startRobotStream();
      powerStreamResync = false;
    }
    if (powerStreamResync && uint32_t(now - powerStreamResyncAt) > 2000) {
      powerStreamResync = false;
      linkReady = false; // bounded failure, no stream restart loop
    }
    serviceCloudPolicy();
    serviceNativeModes(now);
    serviceLidar(now);
    if (uint32_t(now - lastControl) >= 50) {
      lastControl = now;
      sample(now);
      serviceWifiSetup(now);
      if (!nativeObservation && !serviceMode && !nativeProfileChanging)
        powerManagement.tick(powerInput(now), powerPort);
      if (anchored && !nativeObservation && !serviceMode && !nativeProfileChanging) {
        dockGuard.observe(dockObservation(now));
        if (!dockGuard.ready() && active()) {
          stopMotion();
          undocking = false;
          navigator->fault(roonav::Reason::NoRoute);
          lastError = "dock anchor invalid; motion held";
        }
      }
      serviceBatteryLearning(now);
      collectDirt(now);
      updateFloorEstimate(now);
      Request req;
      for (unsigned i = 0; i < 4 && xQueueReceive(requests, &req, 0) == pdTRUE; ++i)
        request(req, now);
      if (!nativeObservation && !serviceMode && !nativeProfileChanging && !powerManagement.blocksMotion()) {
        const auto before = navigator->state;
        arbiter.poll(now, healthy(), active());
        if (!powerManagement.returning() && assistance.pending() && !assistance.allowLocal(latest)) {
          navigator->pause(now);
          stopMotion();
        }
        roonav::Output out;
        if (undocking && terminal(navigator->state)) {
          undocking = startAfterUndock = false;
          stopMotion();
          dockGuard.endAccess();
        }
        if (undocking) {
          const float remaining =
              roonav::distance(latest.pose.x, latest.pose.y, navigator->dock.x, navigator->dock.y);
          if (remaining < undockBest - .01f) {
            undockBest = remaining;
            undockProgressAt = now;
          }
          if (!healthy() || !ROO_DOCK_EXIT_VERIFIED || uint32_t(now - undockAt) > 15000 ||
              uint32_t(now - undockProgressAt) > 3000 ||
              fabsf(roonav::wrap(latest.pose.yaw - navigator->dock.yaw)) > .25f) {
            undocking = false;
            navigator->fault(roonav::Reason::NoProgress);
          } else if (remaining < .04f) {
            undocking = false;
            dockGuard.endAccess();
            if (startAfterUndock) {
              navigator->start(deepAfterUndock, navigator->dock, now, true);
              startAfterUndock = false;
            }
          } else {
            out.action = roonav::Action::Drive;
            out.speed = -.06f;
          }
        } else if (active()) {
          // Always maintain a local map/mission during remote control for fallback.
          out = navigator->tick(latest, arbiter.owner == roonav::Owner::Remote);
          recordReturnEvent();
          if (navigator->state != roonav::State::Cleaning && arbiter.owner == roonav::Owner::Remote)
            arbiter.local(now);
          if (arbiter.owner == roonav::Owner::Remote && healthy() &&
              navigator->state == roonav::State::Cleaning) {
            out.action = roonav::Action::Drive;
            out.speed = (arbiter.left + arbiter.right) / 2000.f;
            out.turn = (arbiter.right - arbiter.left) / (1000 * navigator->cfg.wheelSeparation);
            out.pwm = arbiter.pwm;
          }
          if (arbiter.owner == roonav::Owner::Stopping || arbiter.owner == roonav::Owner::Held)
            out = roonav::Output();
        }
        arbiter.poll(millis(), healthy(), movingSession());
        if (arbiter.owner == roonav::Owner::Stopping || arbiter.owner == roonav::Owner::Held)
          out = roonav::Output();
        if (ROO_COMMISSIONED && ROO_ENCODERS_VERIFIED)
          apply(out, now);
        else
          stopMotion();
        if (navigator->state == roonav::State::Fault)
          undocking = false;
        if (before == roonav::State::Charging && navigator->state == roonav::State::Resuming) {
          if (ROO_DOCK_EXIT_VERIFIED && dockGuard.beginDeparture(dockObservation(now))) {
            undocking = true;
            startAfterUndock = false;
            undockAt = undockProgressAt = now;
            undockBest = 1e9f;
          } else
            navigator->fault(roonav::Reason::NoRoute);
        }
        if (before != navigator->state) {
          if (!saveMission()) {
            stopMotion();
            navigator->fault(roonav::Reason::Telemetry);
            lastError = "checkpoint write failed";
          }
        }
        // Do not interrupt and restart the robot's native docking routine for a
        // periodic save. Entering Docking was already checkpointed.
        if (active() && !undocking && navigator->state != roonav::State::Docking &&
            uint32_t(now - lastSaved) >= 30000 && !saveMission()) {
          stopMotion();
          navigator->fault(roonav::Reason::Telemetry);
          lastError = "checkpoint write failed";
        }
        if (navigator->state == roonav::State::Charging)
          lidarPower(chargeSupervisor.preparation && !chargeSupervisor.held());
        if (!powerManagement.returning()) tryAssistance(now);
        if (!assistance.pending() && !undocking && terminal(navigator->state) &&
            uint32_t(now - anchorAt) > 10000)
          lidarPower(false);
      }
    }
    if (uint32_t(now - lastPublish) >= 100) {
      lastPublish = now;
      publish(now);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
bool authenticated() {
  return strlen(ROO_API_TOKEN) >= 24 &&
         web.header("Authorization") == String("Bearer ") + ROO_API_TOKEN;
}
bool number(const char *name, int64_t &out, int64_t minimum, int64_t maximum) {
  if (!web.hasArg(name))
    return false;
  String value = web.arg(name);
  char *end = nullptr;
  out = strtoll(value.c_str(), &end, 10);
  return end != value.c_str() && *end == 0 && out >= minimum && out <= maximum;
}
void serveProgress() {
  if (!authenticated()) { web.send(401); return; }
  if (xSemaphoreTake(progressMutex, pdMS_TO_TICKS(50)) != pdTRUE) { web.send(503); return; }
  memcpy(progressServing, progressPublished, sizeof(ProgressSnapshot));
  xSemaphoreGive(progressMutex);
  const auto &p = *progressServing;
  if (!p.valid) { web.send(503, "application/json", "{\"error\":\"progress_not_ready\"}"); return; }
  String s;
  if (!s.reserve(400000)) { web.send(503, "application/json", "{\"error\":\"progress_memory\"}"); return; }
  s = "{\"schema\":1,\"source\":\"robot\",\"boot_id\":\"" + String(p.boot) +
      "\",\"mission_id\":\"" + p.mission + "\",\"map_id\":\"" + p.map + "\"";
  s += ",\"map_revision\":" + String(p.revision) + ",\"sequence\":" + String(p.sequence) +
       ",\"at_ms\":" + String(p.at) + ",\"status\":" + p.status;
  s += ",\"pose\":{\"x\":" + String(p.pose.x, 4) + ",\"y\":" + String(p.pose.y, 4) +
       ",\"yaw\":" + String(p.pose.yaw, 5) + ",\"valid\":" + (p.localized ? "true" : "false") + "}";
  s += ",\"dock\":{\"anchor_revision\":" + String(p.anchorRevision) +
       ",\"valid\":" + (p.anchored ? "true" : "false") +
       ",\"pose_kind\":\"front_contact_face\",\"x\":" + String(p.dockConfig.frontX, 4) +
       ",\"y\":" + String(p.dockConfig.frontY, 4) + ",\"yaw\":" + String(p.dockConfig.yaw, 5) +
       ",\"depth\":" + String(p.dockConfig.depth, 4) +
       ",\"half_width\":" + String(p.dockConfig.halfWidth, 4) +
       ",\"geometry_verified\":" + (p.dockConfig.verified ? "true" : "false") +
       ",\"staging\":{\"x\":" + String(p.staging.x, 4) + ",\"y\":" + String(p.staging.y, 4) +
       ",\"yaw\":" + String(p.staging.yaw, 5) + "}}";
  s += ",\"map\":{\"cols\":" + String(p.config.cols) + ",\"rows\":" + String(p.config.rows) +
       ",\"resolution\":" + String(p.config.resolution, 4) +
       ",\"origin_x\":" + String(p.config.originX, 4) +
       ",\"origin_y\":" + String(p.config.originY, 4) + ",\"cells\":[";
  const unsigned count = p.config.cols * p.config.rows;
  for (unsigned i = 0; i < count; ++i) { if (i) s += ','; s += String(int(p.cells[i])); }
  s += "]},\"coverage\":{\"main\":[";
  for (unsigned i = 0; i < count; ++i) { if (i) s += ','; s += String(p.covered[i]); }
  s += "],\"side\":[";
  for (unsigned i = 0; i < count; ++i) { if (i) s += ','; s += String(p.side[i]); }
  s += "],\"excluded\":[";
  for (unsigned i = 0; i < count; ++i) { if (i) s += ','; s += String(p.excluded[i]); }
  s += "]}}";
  web.send(200, "application/json", s);
}
String dirtNumber(uint64_t n) {
  char text[24];
  snprintf(text, sizeof text, "%llu", static_cast<unsigned long long>(n));
  return String(text);
}
void serveNativeRecording() {
  if (!authenticated()) { web.send(401); return; }
  int64_t cursor = 0, expectedSession = 0;
  if ((web.hasArg("after") && !number("after", cursor, 0, UINT32_MAX)) ||
      (web.hasArg("session") && !number("session", expectedSession, 0, UINT32_MAX))) {
    web.send(400); return;
  }
  if (cursor && (!web.hasArg("session") || !web.hasArg("boot_id"))) {
    web.send(400, "text/plain", "cursor requires session and boot_id"); return;
  }
  if (xSemaphoreTake(nativeMutex, pdMS_TO_TICKS(50)) != pdTRUE) { web.send(503); return; }
  const uint32_t session = nativeLog->session, oldest = nativeLog->oldest(), latestSeq = nativeLog->latest;
  const bool recording = nativeLog->recording, extended = nativeLog->extended;
  const unsigned count = nativeLog->copyAfter(uint32_t(cursor), nativeApiRows, 16);
  xSemaphoreGive(nativeMutex);
  if ((web.hasArg("session") && uint32_t(expectedSession) != session) ||
      (web.hasArg("boot_id") && web.arg("boot_id") != bootId) || uint32_t(cursor) > latestSeq) {
    web.send(409, "text/plain", "recording identity/cursor changed; start a separate capture"); return;
  }
  const uint32_t next = count ? nativeApiRows[count - 1].sequence : uint32_t(cursor);
  String s;
  s.reserve(24000);
  s = "{\"schema\":1,\"boot_id\":\"" + String(bootId) + "\",\"session\":" + String(session) +
      ",\"recording\":" + String(recording ? "true" : "false") +
      ",\"profile\":\"" + String(extended ? "extended" : "base") + "\"";
  s += ",\"firmware_version\":\"" ROO_FIRMWARE_VERSION "\",\"firmware_build\":\"" __DATE__ " " __TIME__ "\"";
  s += ",\"hardware_verified\":false,\"oldest\":" + String(oldest) + ",\"latest\":" + String(latestSeq) +
       ",\"next\":" + String(next) + ",\"has_more\":" + String(next < latestSeq ? "true" : "false") +
       ",\"lost_records\":" + String(uint32_t(cursor) + 1 < oldest ? oldest - uint32_t(cursor) - 1 : 0) +
       ",\"records\":[";
  for (unsigned i = 0; i < count; ++i) {
    const auto &row = nativeApiRows[i];
    if (i) s += ',';
    s += "{\"sequence\":" + String(row.sequence) + ",\"ms\":" + String(row.ms) +
         ",\"generation\":" + String(row.generation) + ",\"rejected_frames\":" + String(row.rejected) +
         ",\"source_lost\":" + String(row.sourceLost) + ",\"sensors\":{";
    bool first = true;
    for (unsigned id = 7; id <= 58; ++id) {
      if (!(row.present & (uint64_t(1) << (id - 7)))) continue;
      if (!first) s += ',';
      first = false;
      s += "\"" + String(id) + "\":" + String(row.values[id - 7]);
    }
    s += "}}";
  }
  s += "]}";
  web.send(200, "application/json", s);
}

void serveBatteryHistory(bool traces) {
  if (!authenticated()) { web.send(401); return; }
  int64_t after = 0, lineage = 0;
  if ((web.hasArg("after") && !number("after",after,0,4294967295LL)) ||
      (web.hasArg("lineage") && !number("lineage",lineage,0,4294967295LL)) ||
      (after && !web.hasArg("lineage"))) { web.send(400); return; }
  static roonav::BatteryLearning::Cycle rows[8];
  static roonav::BatteryLearning::Trace samples[16];
  if (!batteryMutex || xSemaphoreTake(batteryMutex,pdMS_TO_TICKS(5)) != pdTRUE) { web.send(503); return; }
  const uint32_t current = batteryLearning.data.lineage, pack = batteryLearning.data.packKey,
      calibration = batteryLearning.data.calibrationKey,
      oldest = traces ? batteryLearning.oldestTrace() : batteryLearning.oldestCycle(),
      latestId = traces ? batteryLearning.data.latestTrace : batteryLearning.data.latestCycle;
  if ((web.hasArg("lineage") && uint32_t(lineage) != current) || uint32_t(after) > latestId) {
    xSemaphoreGive(batteryMutex); web.send(409,"application/json","{\"error\":\"history_scope_changed\"}"); return;
  }
  unsigned count = 0;
  uint32_t id = uint32_t(after) < oldest ? oldest : uint32_t(after)+1;
  for (; id && id <= latestId && count < (traces ? 16u : 8u); ++id) {
    if (traces) samples[count] = *batteryLearning.trace(id); else rows[count] = *batteryLearning.cycle(id);
    ++count;
  }
  xSemaphoreGive(batteryMutex);
  const uint32_t next = count ? (traces ? samples[count-1].id : rows[count-1].id) : uint32_t(after);
  String j = "{\"schema\":1,\"pack_key\":" + String(pack) + ",\"calibration_key\":" + String(calibration) +
      ",\"lineage\":" + String(current) + ",\"oldest\":" + String(oldest) + ",\"latest\":" + String(latestId) +
      ",\"next\":" + String(next) + ",\"has_more\":" + String(next < latestId ? "true" : "false") +
      ",\"lost_records\":" + String(uint32_t(after) < oldest-1 ? oldest-1-uint32_t(after) : 0) + ",\"records\":[";
  j.reserve(10000);
  for (unsigned n = 0; n < count; ++n) {
    if (n) j += ',';
    if (traces) {
      const auto &r = samples[n];
      j += "{\"id\":" + String(r.id) + ",\"cycle\":" + String(r.cycle) + ",\"boot\":" + String(r.boot) +
          ",\"ms\":" + String(r.ms) + ",\"mv\":" + String(r.mv) + ",\"ma\":" + String(r.ma) +
          ",\"temperature_c\":" + String(r.temperatureC) + ",\"oi_charge_mah\":" + String(r.chargeMah) +
          ",\"oi_capacity_mah\":" + String(r.capacityMah) + ",\"gauge_valid\":" + String(r.gaugeValid ? "true" : "false") +
          ",\"charging\":" + String(r.charging) + ",\"sources\":" + String(r.sources) +
          ",\"mode\":" + String(r.mode) + ",\"used_mah\":" + String(r.usedMah,3) + ",\"quality\":" + String(r.quality) + "}";
    } else {
      const auto &r = rows[n];
      j += "{\"id\":" + String(r.id) + ",\"boot\":" + String(r.boot) + ",\"start_ms\":" + String(r.startMs) +
          ",\"end_ms\":" + String(r.endMs) + ",\"quality\":" + String(r.quality) + ",\"gaps\":" + String(r.gaps) +
          ",\"covered_ms\":" + String((unsigned long long)r.coveredMs) + ",\"missing_ms\":" + String((unsigned long long)r.missingMs) +
          ",\"draw_mah\":" + String(r.drawMah,3) + ",\"charge_mah\":" + String(r.chargeMah,3) +
          ",\"draw_mwh\":" + String(r.drawMwh,3) + ",\"min_mv\":" + String(r.minimumMv) + ",\"max_mv\":" + String(r.maximumMv) +
          ",\"min_c\":" + String(r.minimumC) + ",\"max_c\":" + String(r.maximumC) +
          ",\"start_gauge_mah\":" + String(r.startGaugeMah) + ",\"end_gauge_mah\":" + String(r.endGaugeMah) +
          ",\"gauge_capacity_mah\":" + String(r.gaugeCapacityMah) +
          ",\"capacity_candidate_mah\":" + String(r.capacityCandidateMah,3) + ",\"capacity_lower_mah\":" + String(r.capacityLowerMah,3) +
          ",\"capacity_accepted\":" + String(r.capacityAccepted ? "true" : "false") +
          ",\"capacity_qualified\":" + String(r.capacityQualified ? "true" : "false") +
          ",\"return_accepted\":" + String(r.returnAccepted ? "true" : "false") + ",\"return_mah\":" + String(r.returnMah,3) +
          ",\"return_error_mah\":" + String(r.returnErrorMah,3) + ",\"return_route_m\":" + String(r.returnRouteM,3) +
          ",\"return_frame\":" + String(r.returnFrame) + ",\"threshold_mask\":" + String(r.thresholdMask) + ",\"thresholds\":[";
      for (unsigned k=0;k<4;++k) {
        if (k) j += ',';
        j += (r.thresholdMask & (1u<<k)) ? "{\"oi_charge_mah\":" + String(r.thresholdGaugeMah[k]) +
            ",\"used_mah\":" + String(r.thresholdUsedMah[k],3) + "}" : String("null");
      }
      j += "]}";
    }
  }
  j += "]}"; web.send(200,"application/json",j);
}
void serveDirt() {
  if (!authenticated()) {
    web.send(401);
    return;
  }
  uint64_t cursor = 0;
  if (web.hasArg("after")) {
    String text = web.arg("after");
    if (!text.length() || text.length() > 19) {
      web.send(400);
      return;
    }
    for (unsigned i = 0; i < text.length(); ++i)
      if (text[i] < '0' || text[i] > '9') {
        web.send(400);
        return;
      }
    cursor = strtoull(text.c_str(), nullptr, 10);
  }
  xSemaphoreTake(dirtMutex, portMAX_DELAY);
  unsigned count = dirtMonitor->copyAfter(cursor, dirtApiRows, 32);
  uint64_t oldest = dirtMonitor->oldestSequence(), latestSeq = dirtMonitor->latestSequence;
  auto cfg = dirtMonitor->cfg;
  uint32_t lost = dirtMonitor->lostSourceReadings;
  xSemaphoreGive(dirtMutex); // never serialize JSON or send HTTP under the control lock
  if (cursor > latestSeq) {
    web.send(409, "application/json",
             "{\"error\":\"cursor_ahead\",\"boot_id\":" + String(dirtBoot) + "}");
    return;
  }
  String s = "{\"schema\":1,\"boot_id\":" + String(dirtBoot) +
             ",\"mode\":\"observe_only\",\"automatic_cleaning\":false";
  s.reserve(16000);
  s += ",\"coordinate_system\":\"roocore_odom\",\"pose_source\":\"anchored_odometry_with_local_"
       "scan_alignment\"";
  s += ",\"oldest\":" + dirtNumber(oldest) + ",\"latest\":" + dirtNumber(latestSeq);
  uint64_t next = count ? dirtApiRows[count - 1].sequence : cursor;
  s += ",\"next\":" + dirtNumber(next) +
       ",\"has_more\":" + String(next < latestSeq ? "true" : "false");
  s += ",\"lost_records\":" + dirtNumber(cursor + 1 < oldest ? oldest - cursor - 1 : 0);
  s += ",\"lost_source_readings\":" + String(lost);
  s += ",\"config\":{\"threshold\":" + String(cfg.threshold) +
       ",\"consecutive\":" + String(cfg.consecutive);
  s += ",\"minimum_high_ms\":" + String(cfg.minimumHighMs) +
       ",\"maximum_gap_ms\":" + String(cfg.maximumGapMs);
  s += ",\"radius_m\":" + String(cfg.radius, 3) +
       ",\"max_extra_passes\":1,\"revisit_budget_ms\":" + String(cfg.revisitBudgetMs) +
       "},\"records\":[";
  const char *kinds[] = {"reading",
                         "region_detected",
                         "natural_revisit_started",
                         "natural_revisit_result",
                         "interrupted",
                         "source_gap",
                         "maintenance"};
  const char *actions[] = {"unmanaged", "stopped", "routine_travel", "routine_cleaning"};
  const char *reasons[] = {"none",
                           "awaiting_physical_calibration",
                           "safety",
                           "low_battery",
                           "communication_lost",
                           "pose_unreliable",
                           "time_limit",
                           "insufficient_comparable_data",
                           "frame_changed",
                           "region_limit",
                           "data_loss",
                           "caster_needs_cleaning"};
  for (unsigned i = 0; i < count; ++i) {
    const auto &r = dirtApiRows[i];
    if (i)
      s += ',';
    s += "{\"seq\":" + dirtNumber(r.sequence) + ",\"kind\":\"" + String(kinds[unsigned(r.kind)]) +
         "\",\"received_ms\":" + String(r.stamp);
    s += ",\"generation\":" + String(r.generation) + ",\"session\":" + String(r.session) +
         ",\"region\":" + String(r.region);
    s += ",\"value\":" + String(r.value) +
         ",\"frame\":" + (r.located ? String(r.frame) : String("null")) + ",\"pose\":";
    if (r.located)
      s += "{\"x\":" + String(r.pose.x, 4) + ",\"y\":" + String(r.pose.y, 4) +
           ",\"yaw\":" + String(r.pose.yaw, 4) + ",\"estimated\":true}";
    else
      s += "null";
    s += ",\"action\":\"" + String(actions[unsigned(r.action)]) + "\",\"reason\":\"" +
         String(reasons[unsigned(r.why)]) + "\"";
    s += ",\"brush_pwm\":" + String(r.brush) + ",\"vacuum_pwm\":" + String(r.vacuum) +
         ",\"sample_count\":" + String(r.count);
    s += ",\"before_mean\":" + (r.region ? String(r.before, 3) : String("null"));
    s += ",\"after_mean\":" + (r.kind == roonav::DirtMonitor::Kind::RevisitResult && r.count
                                   ? String(r.after, 3)
                                   : String("null"));
    if (r.kind == roonav::DirtMonitor::Kind::Region)
      s += ",\"proposed_action\":\"one_extra_pass\",\"execution\":\"deferred_calibration\"";
    s += ",\"comparison_valid\":" + String(r.comparable ? "true" : "false") +
         ",\"readings_decreased\":" +
         (r.comparable ? String(r.decreased ? "true" : "false") : String("null")) + "}";
  }
  s += "]}";
  web.send(200, "application/json", s);
}
void setup() {
  lidarPort.begin(); // before allocations/network/tasks: UART detached, signals LOW, then ON LOW
  Serial.begin(115200);
  // D10/GPIO9 stays unconnected: revised EN protection is entirely hardware-owned.
  roombaSerial.setRxBufferSize(4096);
  roombaSerial.begin(115200, SERIAL_8N1, 2, 1);
  robotLink.beginBrc();
  capabilities.extendedSensors =
      ROO_RECORD_EXTENDED_ENABLED || ROO_ENCODERS_VERIFIED || ROO_CARPET_CURRENT_VERIFIED || ROO_STASIS_VERIFIED;
  carpet.cfg.enabled = ROO_CARPET_BOOST_ENABLED && ROO_CARPET_CURRENT_VERIFIED;
  carpet.cfg.minimumMv = ROO_CARPET_MIN_MV;
  carpet.cfg.maximumMv = ROO_CARPET_MAX_MV;
  carpet.cfg.daily.minimumMa = ROO_CARPET_85_MIN_MA;
  carpet.cfg.daily.exitMa = ROO_CARPET_85_EXIT_MA;
  carpet.cfg.daily.enterMa = ROO_CARPET_85_ENTER_MA;
  carpet.cfg.daily.maximumMa = ROO_CARPET_85_MAX_MA;
  carpet.cfg.deep.minimumMa = ROO_CARPET_127_MIN_MA;
  carpet.cfg.deep.exitMa = ROO_CARPET_127_EXIT_MA;
  carpet.cfg.deep.enterMa = ROO_CARPET_127_ENTER_MA;
  carpet.cfg.deep.maximumMa = ROO_CARPET_127_MAX_MA;
  robot = new roocore::Roomba(robotLink, capabilities);
  void *navMemory =
      heap_caps_malloc(sizeof(roonav::Navigator), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!navMemory) {
    Serial.println("PSRAM allocation failed; motors disabled");
    return;
  }
  navigator = new (navMemory) roonav::Navigator();
  primaryNavigator = navigator;
  navigator->externalVoltagePolicy = true;
  void *dirtMemory =
      heap_caps_malloc(sizeof(roonav::DirtMonitor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  dirtApiRows = static_cast<roonav::DirtMonitor::Record *>(heap_caps_malloc(
      32 * sizeof(roonav::DirtMonitor::Record), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  dirtMutex = xSemaphoreCreateMutex();
  if (!dirtMemory || !dirtApiRows || !dirtMutex) {
    Serial.println("Dirt recorder allocation failed; motion disabled");
    return;
  }
  dirtMonitor = new (dirtMemory) roonav::DirtMonitor();
  dirtMonitor->cfg.threshold = ROO_DIRT_THRESHOLD;
  dirtMonitor->cfg.consecutive = ROO_DIRT_CONSECUTIVE;
  dirtMonitor->cfg.minimumHighMs = ROO_DIRT_MIN_HIGH_MS;
  dirtMonitor->cfg.maximumGapMs = ROO_DIRT_MAX_GAP_MS;
  dirtMonitor->cfg.radius = ROO_DIRT_REGION_RADIUS_M;
  dirtMonitor->cfg.revisitBudgetMs = ROO_DIRT_REVISIT_BUDGET_MS;
  if (!dirtMonitor->cfg.valid()) {
    Serial.println("Invalid dirt analysis settings; motion disabled");
    return;
  }
  dirtBoot = esp_random();
  snprintf(bootId, sizeof bootId, "%08lx%08lx", static_cast<unsigned long>(dirtBoot),
           static_cast<unsigned long>(esp_random()));
  configureBatteryLearning();
  batteryMutex = xSemaphoreCreateMutex();
  void *batteryMemory = heap_caps_malloc(sizeof(roonav::BatteryLearningStore::Snapshot),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (batteryMemory) batteryScratch = new (batteryMemory) roonav::BatteryLearningStore::Snapshot();
  snprintf(mapId, sizeof mapId, "%s-map", bootId);
  snprintf(missionId, sizeof missionId, "%s-idle", bootId);
  memcpy(missionStartBoot, bootId, sizeof missionStartBoot);
  chargeSupervisor.cfg.departureVerified = ROO_CHARGE_DEPARTURE_VERIFIED;
  chargeSupervisor.cfg.policy = ROO_CHARGE_MISSION_READY
      ? roonav::ChargeSupervisor::Policy::MissionEnergyReady
      : roonav::ChargeSupervisor::Policy::NativeCycleComplete;
  chargeSupervisor.cfg.resumePercent = ROO_CHARGE_RESUME_PERCENT;
  chargeSupervisor.cfg.nativeMinimumPercent = ROO_CHARGE_NATIVE_MIN_PERCENT;
  chargeSupervisor.cfg.minimumC = ROO_CHARGE_MIN_C;
  chargeSupervisor.cfg.maximumC = ROO_CHARGE_MAX_C;
  robot->setSampleObserver(observeDirt);
  powerManagement.cfg.verified = ROO_POWER_POLICY_VERIFIED && ROO_COMMISSIONED &&
      ROO_ENCODERS_VERIFIED && LidarPowerConfigured && ROO_POWER_PASSIVE_STOP_VERIFIED;
  powerManagement.cfg.passiveStopVerified = ROO_POWER_PASSIVE_STOP_VERIFIED;
  powerManagement.cfg.fallbackVerified = ROO_NATIVE_FALLBACK_VERIFIED;
  powerManagement.cfg.clearanceVerified = ROO_FALLBACK_CLEARANCE_VERIFIED;
  powerManagement.cfg.hardwareOffMaxMv = ROO_POWER_HW_OFF_MAX_MV;
  powerManagement.cfg.recoverMv = ROO_POWER_RECOVER_MV;
  powerManagement.cfg.saveMinMv = ROO_POWER_SAVE_MIN_MV;
  assistance.id = esp_random();
  navigator->cfg.brushWidth = ROO_BRUSH_WIDTH_METERS;
  navigator->cfg.brushDepth = ROO_BRUSH_DEPTH_METERS;
  navigator->cfg.brushOffset = ROO_BRUSH_OFFSET_METERS;
  navigator->cfg.sideX = ROO_SIDE_BRUSH_X_METERS;
  navigator->cfg.sideY = ROO_SIDE_BRUSH_Y_METERS;
  navigator->cfg.sideRadius = ROO_SIDE_BRUSH_RADIUS_METERS;
  navigator->cfg.perimeter = ROO_PERIMETER_ENABLED;
  navigator->cfg.wallTouch = ROO_WALL_TOUCH_VERIFIED;
  navigator->cfg.wallIRTarget = ROO_WALL_IR_TARGET;
  navigator->dock.x = -ROO_DOCK_EXIT_METERS;
  navigator->cfg.metersPerTick = ROO_METERS_PER_TICK;
  navigator->cfg.wheelSeparation = ROO_WHEEL_SEPARATION;
  configureDockGuard(1);
  navigator->ordinaryCenterAllowed = ordinaryCenterAllowed;
  navigator->ordinaryGuardContext = &dockGuard;
  void *checkpointMemory=heap_caps_malloc(sizeof(SavedMission),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!checkpointMemory){Serial.println("Checkpoint PSRAM allocation failed; motion disabled");return;}
  saved=new (checkpointMemory) SavedMission();
  progressPublished = static_cast<ProgressSnapshot *>(heap_caps_malloc(
      sizeof(ProgressSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  progressServing = static_cast<ProgressSnapshot *>(heap_caps_malloc(
      sizeof(ProgressSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  progressMutex = xSemaphoreCreateMutex();
  if (!progressPublished || !progressServing || !progressMutex) {
    Serial.println("Progress PSRAM allocation failed; motion disabled");
    return;
  }
  new (progressPublished) ProgressSnapshot();
  new (progressServing) ProgressSnapshot();
  void *nativeMemory = heap_caps_malloc(sizeof(NativeLog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  nativeMutex = xSemaphoreCreateMutex();
  if (!nativeMemory || !nativeMutex) { Serial.println("Native recorder allocation failed"); return; }
  nativeLog = new (nativeMemory) NativeLog();
  requests = xQueueCreate(8, sizeof(Request));
  statusMutex = xSemaphoreCreateMutex();
  if (!requests || !statusMutex) {
    Serial.println("allocation failure");
    return;
  }
  if (ROO_CLOUD_ENABLED && strlen(ROO_CLOUD_DEVICE_TOKEN) >= 32) {
    progressCloud = static_cast<ProgressSnapshot *>(heap_caps_malloc(sizeof(ProgressSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    cloudPolicies = xQueueCreate(1, sizeof(roonav::CloudPolicy));
    if(progressCloud) new (progressCloud) ProgressSnapshot();
    if(!progressCloud || !cloudPolicies) Serial.println("Cloud allocation failed; local policy gate remains enforced");
  }
  xTaskCreatePinnedToCore(controlTask, "robot-control", 16384, nullptr, 3, nullptr, 1);
  // Network selection never changes commissioning, cloud identity, mission or map policy.
  pinMode(0,INPUT_PULLUP); // XIAO ESP32S3 BOOT; D0 is GPIO1 and remains the OI UART.
  const bool networkKnown=wifiSetup.begin(ROO_WIFI_SSID,ROO_WIFI_PASSWORD);
  if(!networkKnown && strlen(ROO_SETUP_AP_PASSWORD)>=12)wifiSetupRequested.store(true);
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  const char *headers[] = {"Authorization","Origin","Content-Length","Content-Type","Transfer-Encoding"};
  web.collectHeaders(headers, 5);
  wifiSetup.routes();
  web.on("/battery", HTTP_GET, [] {
    if (!authenticated()) { web.send(401); return; }
    const String json = batterySummaryJson(millis());
    web.send(json == "null" ? 503 : 200,"application/json",json);
  });
  web.on("/battery/history", HTTP_GET, [] { serveBatteryHistory(false); });
  web.on("/battery/samples", HTTP_GET, [] { serveBatteryHistory(true); });
  web.on("/recording", HTTP_GET, serveNativeRecording);
  web.on("/dirt", HTTP_GET, serveDirt);
  web.on("/progress", HTTP_GET, serveProgress);
  web.on("/status", HTTP_GET, [] {
    if (!authenticated()) {
      web.send(401);
      return;
    }
    xSemaphoreTake(statusMutex, portMAX_DELAY);
    String s = publishedStatus;
    xSemaphoreGive(statusMutex);
    web.send(200, "application/json", s);
  });
  web.on("/help/map", HTTP_GET, [] {
    if (!authenticated()) {
      web.send(401);
      return;
    }
    xSemaphoreTake(statusMutex, portMAX_DELAY);
    String data = publishedHelpMap;
    xSemaphoreGive(statusMutex);
    if (!data.length()) {
      web.send(409);
      return;
    }
    web.send(200, "application/json", data);
  });
  web.on("/command", HTTP_POST, [] {
    if (!authenticated()) {
      web.send(401);
      return;
    }
    Request req{};
    String action = web.arg("action");
    if (action.length() == 0 || action.length() >= sizeof req.action) {
      web.send(400);
      return;
    }
    action.toCharArray(req.action, sizeof req.action);
    if (action == "record-start") {
      const String profile = web.hasArg("profile") ? web.arg("profile") : "base";
      if (profile != "base" && profile != "extended") { web.send(400); return; }
      req.pwm = profile == "extended";
    }
    if ((action == "ota-arm" || action == "record-exit") &&
        web.arg("confirm") != "stop-native-and-hold") {
      web.send(400, "text/plain", "confirm=stop-native-and-hold required"); return;
    }
    if (action == "fallback-approve") {
      int64_t revision, expires;
      const String boot = web.arg("boot_id"), mission = web.arg("mission_id");
      if (web.arg("confirm") != "native-clearance-risk-accepted" ||
          boot.length() != 16 || mission.length() == 0 || mission.length() >= sizeof req.mission ||
          !number("revision", revision, 0, 4294967295LL) ||
          !number("expires", expires, 0, 4294967295LL)) {
        web.send(400, "text/plain", "explicit confirmation, boot, mission, revision and expiry required");
        return;
      }
      boot.toCharArray(req.boot, sizeof req.boot);
      mission.toCharArray(req.mission, sizeof req.mission);
      req.epoch = revision; req.expires = expires;
    }
    if (action == "battery-reset") {
      int64_t lineage, expires; const String boot = web.arg("boot_id");
      if (web.arg("confirm") != "reset-battery-learning" || boot.length() != 16 ||
          !number("lineage",lineage,0,4294967295LL) || !number("expires",expires,0,4294967295LL)) {
        web.send(400,"text/plain","explicit reset confirmation, boot, lineage and expiry required"); return;
      }
      boot.toCharArray(req.boot,sizeof req.boot); req.sequence = lineage; req.expires = expires;
    }
    if (action == "velocity") {
      int64_t epoch, seq, expires, left, right, pwm;
      if (!number("epoch", epoch, 0, 4294967295LL) || !number("sequence", seq, 0, 4294967295LL) ||
          !number("expires", expires, 0, 4294967295LL) || !number("left", left, -250, 250) ||
          !number("right", right, -250, 250) || !number("pwm", pwm, 0, 127)) {
        web.send(400);
        return;
      }
      req.epoch = epoch;
      req.sequence = seq;
      req.expires = expires;
      req.left = left;
      req.right = right;
      req.pwm = pwm;
    }
    if (action.startsWith("help-")) {
      int64_t id, frame, expires;
      if (!number("id", id, 0, 4294967295LL) || !number("frame", frame, 0, 4294967295LL) ||
          !number("expires", expires, 0, 4294967295LL)) {
        web.send(400);
        return;
      }
      req.epoch = id;
      req.sequence = frame;
      req.expires = expires;
      if (action == "help-relocalize") {
        float *values[] = {&req.candidate.x, &req.candidate.y, &req.candidate.yaw,
                           &req.reference.x, &req.reference.y, &req.reference.yaw};
        const char *names[] = {"x", "y", "yaw", "ref_x", "ref_y", "ref_yaw"};
        for (int j = 0; j < 6; ++j) {
          String v = web.arg(names[j]);
          char *end = nullptr;
          *values[j] = strtof(v.c_str(), &end);
          if (!v.length() || end == v.c_str() || *end || !isfinite(*values[j]) ||
              fabsf(*values[j]) > 50) {
            web.send(400);
            return;
          }
        }
      }
    }
    if (xQueueSend(requests, &req, 0) != pdTRUE) {
      web.send(503, "text/plain", "queue full");
      return;
    }
    web.send(202, "text/plain", "queued; inspect status for result");
  });
  ArduinoOTA.setHostname("roocore-xiao").setPassword(ROO_OTA_PASSWORD)
      .setRebootOnSuccess(false).setMdnsEnabled(false);
  ArduinoOTA.onStart([] {
    otaUploading.store(true);
    if (!otaAdmitted() || ArduinoOTA.getCommand() != U_FLASH) Update.abort();
  });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) {
    if (!otaAdmitted()) Update.abort();
  });
  ArduinoOTA.onEnd([] { otaUploading.store(false); otaReboot.store(true); });
  ArduinoOTA.onError([](ota_error_t) { otaUploading.store(false); });
  web.begin();
  if (progressCloud && cloudPolicies)
    xTaskCreatePinnedToCore(cloudTask, "cloud-uplink", 16384, nullptr, 1, nullptr, 0);
}
void loop() {
  if (!navigator) {
    delay(100);
    return;
  }
  const uint32_t wifiNow=millis();
  if(digitalRead(0)==LOW) {
    if(!wifiButtonAt)wifiButtonAt=wifiNow;
    if(!wifiButtonConsumed && uint32_t(wifiNow-wifiButtonAt)>=3000){
      wifiButtonConsumed=true;wifiSetupRequested.store(true);
    }
  }else {wifiButtonAt=0;wifiButtonConsumed=false;}
  if(wifiSetupHold.load() && !wifiPortalStarted){
    wifiPortalStarted=true;
    if(!wifiSetupAdmitted() || !wifiSetup.open(ROO_SETUP_AP_PASSWORD,wifiNow))wifiSetupRelease.store(true);
  }
  if(wifiSetup.tick(wifiNow,wifiSetupAdmitted()))wifiSetupRelease.store(true);
  if(!wifiSetupHold.load())wifiPortalStarted=false;
  web.handleClient();
  if (ROO_OTA_ENABLED && strlen(ROO_OTA_PASSWORD) >= 24 && WiFi.status() == WL_CONNECTED) {
    if (otaAdmitted()) {
      if (!otaStarted) { ArduinoOTA.begin(); otaStarted = true; }
      ArduinoOTA.handle();
    } else if (otaStarted && !otaUploading.load()) { ArduinoOTA.end(); otaStarted = false; }
  }
  if (otaReboot.load()) { delay(100); ESP.restart(); }
  delay(1);
}

bool cloudPolicyReady() {
  if (cloudPolicyFault) return false;
  if (!cloudPolicyRequired) return true;
  return storageReady && cloudPolicy.committed(cloudExpectedGeneration,cloudExpectedChecksum) && navigator == primaryNavigator &&
      cloudPolicy.matches(mapId, primaryNavigator->cfg.cols, primaryNavigator->cfg.rows,
                          primaryNavigator->cfg.resolution, primaryNavigator->cfg.originX,
                          primaryNavigator->cfg.originY);
}
void serviceCloudPolicy() {
  const uint32_t now=millis();
  const auto left=robot->sensor(roocore::Sensor::LeftEncoder), right=robot->sensor(roocore::Sensor::RightEncoder);
  const auto voltage=robot->sensor(roocore::Sensor::VoltageMv);
  const bool stoppedState=!movingSession() && navigator==primaryNavigator && !nativeObservation && !serviceMode && !nativeDock &&
      (navigator->state==roonav::State::Idle || navigator->state==roonav::State::Paused || navigator->state==roonav::State::Complete);
  const bool safe=stoppedState && ROO_ENCODERS_VERIFIED && linkReady && !powerManagement.blocksMotion() &&
      powerManagement.cfg.valid() && voltage.fresh(now,200) && voltage.value>=int32_t(powerManagement.cfg.saveMinMv) &&
      left.fresh(now,200) && right.fresh(now,200) && left.generation==right.generation &&
      latest.sensorsValid && !latest.cliff && !latest.wheelDrop && !latest.bumper && !latest.overcurrent &&
      !appliedPwm && !appliedSidePwm && !appliedVacuumPwm;
  cloudPolicyStopped.observe(now,safe,uint16_t(left.value),uint16_t(right.value));
  if(wifiSetupHold.load() || !cloudPolicies || !stoppedState || !cloudPolicyStopped.ready)return;
  roonav::CloudPolicy candidate;
  if (xQueueReceive(cloudPolicies, &candidate, 0) != pdTRUE || !candidate.newerThan(cloudPolicy)) return;
  const auto &cfg=navigator->cfg;
  if (!candidate.matches(mapId,cfg.cols,cfg.rows,cfg.resolution,cfg.originX,cfg.originY)) {
    lastError="cloud policy rejected: different map frame"; return;
  }
  roonav::MapZones check;
  check.count=candidate.count;
  for(unsigned i=0;i<candidate.count;++i)check.items[i]=candidate.zones[i];
  const float clearance=cfg.radius+cfg.margin+cfg.resolution*.707107f;
  if ((latest.localized && check.blocked(latest.pose.x,latest.pose.y,clearance)) ||
      (anchored && check.blocked(navigator->dock.x,navigator->dock.y,clearance))) {
    lastError="cloud policy rejected: exclusion overlaps current robot or dock";return;
  }
  stopMotion();
  if (!storageReady || !robotLink.finishTransmit(50)) { cloudPolicyFault=true; return; }
  // Sticky intent is written first. A crash must never turn missing restrictions into permission.
  File intent=LittleFS.open(CloudPolicyIntent,"w");
  const uint32_t marker[]={candidate.generation,candidate.checksum};
  bool ok=intent && intent.write(reinterpret_cast<const uint8_t *>(marker),sizeof(marker))==sizeof(marker);
  cloudExpectedGeneration=candidate.generation;cloudExpectedChecksum=candidate.checksum;
  if(intent){intent.flush();intent.close();}
  cloudPolicyRequired=true;
  if(!ok || !navigator->setZones(candidate.zones,candidate.count)) {
    cloudPolicyFault=true;lastError="policy persistence/validation failed; motion held";return;
  }
  cloudPolicy=candidate;
  if(!saveMission() || !saveMission()) {cloudPolicyFault=true;lastError="policy checkpoint failed; motion held";return;}
  auto *verify=static_cast<SavedMission *>(heap_caps_malloc(sizeof(SavedMission),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  ok=verify && readSlot(saved->sequence&1?"/mission-a":"/mission-b",*verify) &&
      verify->cloudPolicy.generation==candidate.generation && verify->cloudPolicy.checksum==candidate.checksum;
  if(verify)heap_caps_free(verify);
  cloudPolicyFault=!ok;
  arbiter.hold(); // policy changes never start/resume a mission
  lastError=ok?"cloud zones persisted locally; explicit resume required":"policy readback failed; motion held";
}
String encodeCloudSnapshot(const ProgressSnapshot &p,bool full) {
  String s;
  if(!s.reserve(full?300000:36000))return String();
  const uint64_t utc=uint64_t(time(nullptr))*1000;
  s="{\"protocol\":1,\"boot_id\":\""+String(p.boot)+"\",\"sequence\":"+String(p.sequence)+
    ",\"at_ms\":"+String(p.at)+",\"sent_unix_ms\":"+dirtNumber(utc)+",\"age_ms\":"+String(uint32_t(millis()-p.at))+
    ",\"map_id\":\""+p.map+"\",\"map_revision\":"+String(p.revision)+",\"status\":"+p.status;
  s+=",\"geometry\":{\"cols\":"+String(p.config.cols)+",\"rows\":"+String(p.config.rows)+
    ",\"resolution\":"+String(p.config.resolution,6)+",\"origin_x\":"+String(p.config.originX,6)+
    ",\"origin_y\":"+String(p.config.originY,6)+"},\"pose\":{\"x\":"+String(p.pose.x,5)+
    ",\"y\":"+String(p.pose.y,5)+",\"yaw\":"+String(p.pose.yaw,6)+"},\"scan_at_ms\":"+String(p.scanAt)+",\"ranges_m\":[";
  for(unsigned i=0;i<180;++i){if(i)s+=',';s+=isfinite(p.ranges[i])&&p.ranges[i]>0&&p.ranges[i]<=100?String(p.ranges[i],3):String("null");}
  s+="],\"sensors\":[";
  unsigned sent=0;
  for(unsigned i=0;i<52;++i){const auto &v=p.sensors[i];if(!v.valid)continue;if(sent++)s+=',';
    s+="{\"id\":"+String(i+7)+",\"value\":"+String(v.value)+",\"at_ms\":"+String(v.timestampMs)+",\"generation\":"+String(v.generation)+"}";
  }
  s+="],\"policy_ack\":{\"generation\":"+String(p.policy.generation)+",\"revision\":"+String(p.policy.revision)+
    ",\"checksum\":"+String(p.policy.checksum)+",\"map_id\":\""+p.map+"\"},\"map\":";
  if(full){
    const unsigned count=p.config.cols*p.config.rows;
    s+="{\"cells\":[";
    for(unsigned i=0;i<count;++i){if(i)s+=',';s+=String(int(p.cells[i]));}
    s+="],\"main\":[";
    for(unsigned i=0;i<count;++i){if(i)s+=',';s+=String(p.covered[i]);}
    s+="],\"side\":[";
    for(unsigned i=0;i<count;++i){if(i)s+=',';s+=String(p.side[i]);}
    s+="],\"excluded\":[";
    for(unsigned i=0;i<count;++i){if(i)s+=',';s+=String(p.excluded[i]);}
    s+="]}";
  }else s+="null";
  s+='}';
  return s;
}
void cloudTask(void *) {
  NetworkClientSecure tls;
  tls.setCACert(RooCloudRootCA);
  tls.setHandshakeTimeout(5);
  roonav::CloudCadence cadence;
  uint32_t lastSequence=0;
  char lastMap[80]{};
  bool mapSent=false;
  for(;;) {
    uint32_t now=millis();
    if(otaUploading.load() || wifiSetupHold.load()){tls.stop();vTaskDelay(pdMS_TO_TICKS(100));continue;}
    if(!cadence.due(now)){vTaskDelay(pdMS_TO_TICKS(50));continue;}
    if(WiFi.status()!=WL_CONNECTED||time(nullptr)<1704067200){
      if(WiFi.status()!=WL_CONNECTED)WiFi.reconnect();
      cadence.result(now,false,ROO_CLOUD_INTERVAL_MS);continue;
    }
    bool copied=false;
    if(xSemaphoreTake(progressMutex,pdMS_TO_TICKS(5))==pdTRUE){
      if(progressPublished->valid){memcpy(progressCloud,progressPublished,sizeof(ProgressSnapshot));copied=true;}
      xSemaphoreGive(progressMutex);
    }
    if(!copied||progressCloud->sequence==lastSequence||uint32_t(millis()-progressCloud->at)>5000){
      cadence.result(now,false,ROO_CLOUD_INTERVAL_MS);continue;
    }
    const bool full=!mapSent||strcmp(lastMap,progressCloud->map)||uint32_t(now-cadence.lastMap)>=ROO_CLOUD_MAP_INTERVAL_MS;
    String body=encodeCloudSnapshot(*progressCloud,full);
    HTTPClient http;
    http.setConnectTimeout(2000);http.setTimeout(2500);http.setReuse(true);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    bool ok=false;
    // Fixed host/path: credentials can never follow a redirect or caller-supplied URL.
    if(body.length() && body.length()<=300000 && http.begin(tls,"https://roocore.saymour.co.il/api/device/v1/snapshot")) {
      http.addHeader("Content-Type","application/json");
      http.addHeader("Authorization",String("Bearer ")+ROO_CLOUD_DEVICE_TOKEN);
      const int code=http.POST(body);
      ok=code==204||code==200;
      if(code==200){
        uint8_t wire[roonav::CloudPolicy::Maximum];
        const int size=http.getSize();
        if(size>=int(roonav::CloudPolicy::Header)&&size<=int(sizeof wire)&&
           http.getStreamPtr()->readBytes(wire,size)==size){
          roonav::CloudPolicy candidate;
          if(candidate.decode(wire,size))xQueueOverwrite(cloudPolicies,&candidate);
          else ok=false;
        }else ok=false;
      }
      http.end();
    }
    if(ok){lastSequence=progressCloud->sequence;if(full){mapSent=true;cadence.lastMap=now;memcpy(lastMap,progressCloud->map,sizeof lastMap);}}
    else tls.stop();
    cadence.result(millis(),ok,ROO_CLOUD_INTERVAL_MS);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void serviceWifiSetup(uint32_t now) {
  const bool stopped=!movingSession() && navigator==primaryNavigator && !nativeObservation && !nativeProfileChanging &&
      !serviceMode && !nativeDock && !otaUploading.load() &&
      (navigator->state==roonav::State::Idle || navigator->state==roonav::State::Paused || navigator->state==roonav::State::Complete);
  const auto left=robot->sensor(roocore::Sensor::LeftEncoder),right=robot->sensor(roocore::Sensor::RightEncoder);
  const auto voltage=robot->sensor(roocore::Sensor::VoltageMv);
  const auto mode=robot->sensor(roocore::Sensor::OiMode);
  const bool safe=stopped && mode.fresh(now,200) && mode.value==1 && ROO_ENCODERS_VERIFIED && linkReady && !powerManagement.blocksMotion() &&
      powerManagement.cfg.valid() && voltage.fresh(now,200) && voltage.value>=int32_t(powerManagement.cfg.saveMinMv) &&
      left.fresh(now,200) && right.fresh(now,200) && left.generation==right.generation &&
      latest.sensorsValid && !latest.cliff && !latest.wheelDrop && !latest.bumper && !latest.overcurrent &&
      !appliedPwm && !appliedSidePwm && !appliedVacuumPwm;
  wifiSetupStopped.observe(now,safe,uint16_t(left.value),uint16_t(right.value));
  // First USB-only preparation is allowed with motion commissioning disabled.
  // This software check cannot establish that the physical harness was disconnected.
  const bool permit=stopped && ((!ROO_COMMISSIONED && !ROO_ENCODERS_VERIFIED && !linkReady && !ownsRobot) || wifiSetupStopped.ready);
  if(wifiSetupRelease.exchange(false)){
    stopMotion();arbiter.hold();if(active())navigator->pause(now);
    wifiSetupHold.store(false);wifiSetupPermitted.store(false);
  }
  const bool requested=wifiSetupRequested.exchange(false);
  if(requested && stopped && !permit)wifiSetupRequested.store(true); // await stop qualification, never defer a moving request
  if(requested && permit && strlen(ROO_SETUP_AP_PASSWORD)>=12){
    stopMotion();arbiter.hold();wifiSetupHold.store(true);lastError="Wi-Fi setup; motion held";
  }
  wifiSetupCheckedAt.store(now);
  wifiSetupPermitted.store(permit);
  if(wifiSetupHold.load())stopMotion();
}

bool wifiSetupAdmitted() { return wifiSetupHold.load() && wifiSetupPermitted.load() &&
  uint32_t(millis()-wifiSetupCheckedAt.load())<=200; }
