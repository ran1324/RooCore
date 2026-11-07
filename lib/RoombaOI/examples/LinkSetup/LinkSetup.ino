#include <RoombaOIArduino.h>

// Plain XIAO ESP32S3. BRC is driven through the planned NPN, NOT directly.
HardwareSerial robotUart(1);
roocore::Esp32Transport transport(robotUart, UART_NUM_1, D9);
roocore::Roomba robot(transport);
roocore::Result setupResult=roocore::Result::Unsupported;
roocore::BatteryState latestBattery;
uint32_t lastRequest=0;
bool linkReady=false;

// Set true only when deliberately recovering an unknown robot baud rate.
// The robot must already be powered on. This does NOT turn its power on.
constexpr bool FORCE_19200=false;

void setup() {
  robotUart.begin(115200,SERIAL_8N1,D1,D0);
  transport.beginBrc(); // GPIO LOW releases the NPN collector/BRC line.
  setupResult=robot.wakeViaBrc(); // one pulse; waits and verifies a Passive reply
  if (FORCE_19200) setupResult=robot.forceBaud19200ViaBrc();
  if (setupResult==roocore::Result::Ok)
    setupResult=robot.changeBaud(57600); // both endpoints + fresh response check
  linkReady=setupResult==roocore::Result::Ok;
  // Deliberately no motors, automatic retries, Wi-Fi credentials or USB logging.
}

void loop() {
  const uint32_t now=millis();
  robot.poll(now);
  if (robot.lastEvent()==roocore::Result::Timeout) linkReady=false;
  if (linkReady && robot.inputReady() && uint32_t(now-lastRequest)>=1000) {
    setupResult=robot.requestBattery(now);
    lastRequest=now;
  }
  latestBattery=robot.battery(now);
  delay(1);
}
