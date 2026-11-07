#pragma once
#include <Arduino.h>
#include "RoombaOI.h"

namespace roocore {
class ArduinoTransport : public Transport {
 public:
  explicit ArduinoTransport(Stream& serial) : serial_(serial) {}
  size_t write(const uint8_t* bytes, size_t n) override { return serial_.write(bytes,n); }
  int read() override { return serial_.available() ? serial_.read() : -1; }
 private:
  Stream& serial_;
};
}

#if defined(ARDUINO_ARCH_ESP32)
#include <driver/uart.h>
#include <driver/gpio.h>
namespace roocore {
// ESP32 HardwareSerial backend. UART number must match the HardwareSerial object.
class Esp32Transport : public Transport {
 public:
  Esp32Transport(HardwareSerial& serial, uart_port_t uart, int brcDrivePin=-1)
    : serial_(serial), uart_(uart), brcPin_(brcDrivePin) {}
  // Our circuit uses an NPN: GPIO HIGH pulls ROBOT BRC low; LOW releases it.
  // Never attach this push-pull pin directly to the robot's 5V BRC line.
  void beginBrc() {
    if (brcPin_<0) return;
    gpio_set_level(static_cast<gpio_num_t>(brcPin_),0);
    pinMode(brcPin_,OUTPUT);
    digitalWrite(brcPin_,LOW);
    brcReady_=true;
  }
  size_t write(const uint8_t* p,size_t n) override { return serial_.write(p,n); }
  int read() override { return serial_.available() ? serial_.read() : -1; }
  bool hasLinkControl() const override { return true; }
  bool hasBrc() const override { return brcReady_; }
  bool finishTransmit(uint32_t timeoutMs) override {
    return uart_wait_tx_done(uart_,pdMS_TO_TICKS(timeoutMs))==ESP_OK;
  }
  bool setBaud(uint32_t baud) override {
    serial_.updateBaudRate(baud);
    const uint32_t actual=serial_.baudRate();
    // Integer UART dividers can round slightly; verify within one percent.
    const uint32_t error=actual>baud ? actual-baud : baud-actual;
    return actual!=0 && uint64_t(error)*100<=baud;
  }
  bool brcLow(bool low) override {
    if (!brcReady_) return false;
    digitalWrite(brcPin_,low ? HIGH : LOW); return true;
  }
  uint32_t nowMs() const override { return millis(); }
  void waitMs(uint32_t duration) override { delay(duration); }
 private:
  HardwareSerial& serial_;
  uart_port_t uart_;
  int brcPin_;
  bool brcReady_=false;
};
}
#endif
