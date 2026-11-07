#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace roonav {
// T-mini Plus model 151: triangle transport, 8-bit intensity, distance /4 mm,
// no triangulation angle correction. NOT the 152 SH variant. See SOURCES.md.
class TminiPlus {
public:
  float ranges[180]{}; // nearest hit in each 2-degree bin, clockwise raw frame
  uint32_t scanMs = 0, scans = 0, rejected = 0;
  bool valid = false;
  void reset() {
    used_ = 0;
    collecting_ = false;
    valid = false;
    memset(pending_, 0, sizeof pending_);
  }
  void feed(uint8_t b, uint32_t now) {
    if (used_ && uint32_t(now - byteMs_) > 50) {
      used_ = 0;
      collecting_ = false;
    }
    byteMs_ = now;
    if (used_ == 0 && b != 0xaa)
      return;
    if (used_ == 1 && b != 0x55) {
      used_ = (b == 0xaa ? 1 : 0);
      return;
    }
    bytes_[used_++] = b;
    if (used_ == 4 && (bytes_[3] == 0 || bytes_[3] > 80)) {
      reject();
      return;
    }
    if (used_ < 10 || used_ < size_t(10 + 3 * bytes_[3]))
      return;
    const uint8_t count = bytes_[3];
    uint16_t checksum = word(0) ^ word(2) ^ word(4) ^ word(6);
    for (unsigned i = 0; i < count; ++i)
      checksum ^= uint16_t(bytes_[10 + 3 * i]) ^ word(11 + 3 * i);
    const uint16_t first = word(4), last = word(6);
    if (checksum != word(8) || !(first & 1) || !(last & 1) || (first >> 1) >= 23040 ||
        (last >> 1) >= 23040) {
      reject();
      return;
    }
    float start = (first >> 1) / 64.0f, end = (last >> 1) / 64.0f;
    if (end < start)
      end += 360;
    if (count > 1 && end - start > 90) {
      reject();
      return;
    }
    if (bytes_[2] & 1) {
      unsigned bins = 0, quadrant[4]{};
      for (unsigned i = 0; i < 180; ++i)
        if (pending_[i] > 0) {
          ++bins;
          ++quadrant[i / 45];
        }
      if (collecting_ && bins >= 90 && quadrant[0] >= 15 && quadrant[1] >= 15 &&
          quadrant[2] >= 15 && quadrant[3] >= 15 && uint32_t(now - startMs_) >= 40 &&
          uint32_t(now - startMs_) <= 400) {
        memcpy(ranges, pending_, sizeof ranges);
        scanMs = now;
        ++scans;
        valid = true;
      } else if (collecting_) {
        valid = false;
        ++rejected;
      }
      memset(pending_, 0, sizeof pending_);
      collecting_ = true;
      startMs_ = now;
    }
    if (collecting_)
      for (unsigned i = 0; i < count; ++i) {
        // Low two distance bits carry optical flags, not millimetres.
        const uint16_t raw = word(11 + 3 * i);
        const float distance = (raw & 0xfffc) / 4000.0f;
        float angle = start + (count > 1 ? (end - start) * i / (count - 1) : 0);
        if (angle >= 360)
          angle -= 360;
        const unsigned bin = unsigned(angle / 2) % 180;
        if (distance >= .05f && distance <= 12 && (pending_[bin] == 0 || distance < pending_[bin]))
          pending_[bin] = distance;
      }
    used_ = 0;
  }

private:
  uint16_t word(unsigned i) const { return uint16_t(bytes_[i]) | uint16_t(bytes_[i + 1]) << 8; }
  void reject() {
    ++rejected;
    used_ = 0;
    collecting_ = false;
    valid = false;
  }
  uint8_t bytes_[250]{};
  size_t used_ = 0;
  float pending_[180]{};
  bool collecting_ = false;
  uint32_t byteMs_ = 0, startMs_ = 0;
};
} // namespace roonav
