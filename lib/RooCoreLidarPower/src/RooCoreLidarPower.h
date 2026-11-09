#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include "LidarPowerSequence.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This adapter is for XIAO ESP32S3, not another ESP family."
#endif

namespace roocore_lidar_detail {
class ArduinoIO {
 public:
  static constexpr uint8_t kEnable = 4;  // XIAO D3 -> Pololu 2810 ON
  static constexpr uint8_t kTx = 5;      // XIAO D4 -> LiDAR RX
  static constexpr uint8_t kRx = 6;      // XIAO D5 <- LiDAR TX

  void endUart() { port_.end(); }
  void safeSignals() {
    // end() must precede this. pinMode reclaims the pads from the peripheral
    // manager. Preload LOW before enabling the TX output to avoid a HIGH pulse.
    gpio_set_level(GPIO_NUM_5, 0);
    pinMode(kTx, OUTPUT);
    digitalWrite(kTx, LOW);
    gpio_set_pull_mode(GPIO_NUM_5, GPIO_FLOATING);
    pinMode(kRx, INPUT_PULLDOWN); // never drive the LiDAR's TX output
  }
  void setPower(bool on) {
    gpio_set_level(GPIO_NUM_4, on ? 1 : 0);
    pinMode(kEnable, OUTPUT);
    digitalWrite(kEnable, on ? HIGH : LOW);
  }
  bool startUart() {
    // This API is available in the verified Arduino-ESP32 3.3.12 core.
    // Ordinary begin() enables an RX pullup by default; disable it explicitly.
    if (!port_.enableRxInternalPull(false)) return false;
    port_.setTxBufferSize(0);
    if (port_.setRxBufferSize(8192) != 8192) return false;
    port_.begin(230400, SERIAL_8N1, kRx, kTx);
    return bool(port_);
  }
  bool drainTx() {
    // Poll without blocking robot safety polling. If busy, end() cancels the
    // remaining bytes while the sensor is still powered, then GPIO goes LOW.
    return uart_wait_tx_done(UART_NUM_2, 0) == ESP_OK;
  }
  size_t write(const uint8_t *p, size_t n) { return port_.write(p, n); }
  int available() { return port_.available(); }
  int read() { return port_.read(); }

 private:
  HardwareSerial port_{2};  // UART1 belongs to the Roomba; no other Serial2 owner
};
}  // namespace roocore_lidar_detail

class RooCoreLidarPower {
 public:
  using State = LidarPowerSequence<roocore_lidar_detail::ArduinoIO>::State;
  RooCoreLidarPower() : sequence_(io_) {}
  RooCoreLidarPower(const RooCoreLidarPower &) = delete;
  RooCoreLidarPower &operator=(const RooCoreLidarPower &) = delete;
  void begin() { sequence_.begin(); }
  bool requestOn(uint32_t now, uint32_t qualifiedRiseDelayMs) {
    return sequence_.requestOn(now, qualifiedRiseDelayMs);
  }
  bool poll(uint32_t now) { return sequence_.poll(now); }
  void abort() { sequence_.abort(); }
  bool powerOff() { return sequence_.powerOff(); }
  size_t write(const uint8_t *p, size_t n) { return sequence_.write(p, n); }
  int available() { return sequence_.available(); }
  int read() { return sequence_.read(); }
  bool ready() const { return sequence_.ready(); }
  bool powered() const { return sequence_.powered(); }
  State state() const { return sequence_.state(); }

 private:
  roocore_lidar_detail::ArduinoIO io_;
  LidarPowerSequence<roocore_lidar_detail::ArduinoIO> sequence_;
};

