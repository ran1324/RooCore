#include <Arduino.h>
#include <Matter.h>
#include <Preferences.h>
#include <bootloader_random.h>
#include <esp_random.h>
#include <setup_payload/SetupPayload.h>
#include <atomic>
#include "MatterVacuum.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !ARDUINO_USB_CDC_ON_BOOT
#error "RooCoreMatterSim requires ESP32-S3 and USB CDC on boot. UART logging is not allowed."
#endif

// No robot, LiDAR, external power, GPIO, UART-control or MQTT drivers are used.
// SDK/ROM boot logs can still use UART0: future tests require a disconnected board.
// All Matter model access is confined to the CHIP task (events and scheduled work).
roocore_sim::MatterVacuum vacuum;
static bool started = false;
static std::atomic<bool> tickPending{false};

static void halt(const char *message) {
  Serial.println(message);
  while (true) delay(1000);
}

static bool setupIdentity() {
  Preferences preferences;
  if (!preferences.begin("roo-matter-sim", false)) return false;
  uint32_t pin = preferences.getUInt("pin", 0);
  uint16_t discriminator = preferences.getUShort("disc", 0xFFFF);
  if (!chip::PayloadContents::IsValidSetupPIN(pin) || discriminator > 4095) {
    // Enable the documented internal entropy source before Matter starts Wi-Fi/BLE.
    bootloader_random_enable();
    do { pin = esp_random() & 0x07FFFFFF; } while (!chip::PayloadContents::IsValidSetupPIN(pin));
    discriminator = esp_random() & 0x0FFF;
    bootloader_random_disable();
    if (preferences.putUInt("pin", pin) != sizeof(pin) ||
        preferences.putUShort("disc", discriminator) != sizeof(discriminator)) return false;
  }
  preferences.end();
  char serial[24];
  snprintf(serial, sizeof(serial), "ROOSIM-%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
  return Matter.setVendorName("RooCore") && Matter.setProductName("RooCore Simulator") &&
         Matter.setDeviceName("RooCore Simulator") && Matter.setSerialNumber(serial) && Matter.setSetupPasscode(pin) &&
         Matter.setSetupDiscriminator(discriminator);
}

void setup() {
  Serial.begin(115200);  // USB CDC only; enforced above. No physical UART is started.
  if (!setupIdentity()) halt("Simulator stopped: cannot prepare pairing identity.");
  if (!Matter.selectNetwork(MATTER_NETWORK_WIFI)) halt("Simulator stopped: Matter Wi-Fi unavailable.");
  Matter.onEvent([](matterEvent_t type, const chip::DeviceLayer::ChipDeviceEvent *event) {
    if (type == MATTER_SERVER_READY) vacuum.ready();
    if (type == MATTER_WIFI_CONNECTIVITY_CHANGE) {
      vacuum.network(event->WiFiConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established);
    }
  });
  if (!vacuum.begin()) halt("Simulator stopped: cannot create vacuum endpoint.");
  Matter.begin();
  if (!roocore_sim::MatterLifecycle::isStackStarted()) halt("Simulator stopped: Matter startup failed.");
  started = true;
  Serial.println("RooCore Simulator. No robot control. Type p for the local pairing code.");
}

void loop() {
  if (!started) return;
  // Only local USB input exposes the code. No web UI, public API, or remote reset command.
  if (Serial.available() && Serial.read() == 'p') {
    if (Matter.isDeviceCommissioned()) Serial.println("Already paired. Manage sharing in Apple Home.");
    else Serial.println(Matter.getManualPairingCode());
  }
  // Bound queued work when the CHIP task is busy. All state access stays on that task.
  if (!tickPending.exchange(true)) {
    if (chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
          vacuum.tick();
          tickPending.store(false);
        }, 0) != CHIP_NO_ERROR) tickPending.store(false);
  }
  delay(100);
}
