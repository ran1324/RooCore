#include <RoombaOIArduino.h>

// Plain XIAO ESP32S3: D1/GPIO2 RX, D0/GPIO1 TX, via a 5V/3.3V level shifter.
// Disconnect the external XIAO 5V feed before attaching USB in the no-diode build.
HardwareSerial robotUart(1);
roocore::ArduinoTransport transport(robotUart);
roocore::Roomba robot(transport);
uint32_t lastRequest=0;
roocore::BatteryState latestBattery;
roocore::Result lastRequestResult=roocore::Result::Ok;

void setup() {
  robotUart.begin(115200, SERIAL_8N1, 2, 1);
  // The robot must already be awake. No automatic wake/reset or motor commands.
  robot.beginPassive();
}

void loop() {
  const uint32_t now=millis();
  robot.poll(now);
  if (robot.lastEvent()==roocore::Result::Timeout)
    robot.resynchronizeInput(now);
  if (uint32_t(now-lastRequest)>=1000 && robot.inputReady()) {
    lastRequestResult=robot.requestBattery(now);
    lastRequest=now;
  }
  latestBattery=robot.battery(now);
  // Use only latestBattery.valid; percentage also needs percentageValid.
  // Publish through your own Wi-Fi task. This example does not implement OTA.
  delay(1);
}
