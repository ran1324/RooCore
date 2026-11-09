#include <RooCoreLidarPower.h>

RooCoreLidarPower lidar;
// This sketch does NOT turn on the motor, send scan commands, or operate Roomba.
// Full application integration and hardware qualification are still required.
void setup() {
  lidar.begin(); // D4 LOW, D5 input pulldown, then D3 LOW. No USB console.
}

void loop() {
  // The eventual single owning task calls:
  // lidar.requestOn(millis(), qualifiedRiseDelayMs); // request power, don't block
  // lidar.poll(millis());                // opens UART only after qualified rise
  // lidar.write(command, length);        // application protocol, only while ready
  // lidar.powerOff();                    // wires stay connected
  delay(100);
}

