#pragma once
#include <stdint.h>
#include <stddef.h>

namespace roonav {
// Validated OI frames only. Null/missing values must never become measured zeroes.
struct NativeFrame {
  uint32_t sequence = 0, ms = 0, generation = 0, rejected = 0, sourceLost = 0;
  uint64_t present = 0;
  int32_t values[52]{}; // packet IDs 7..58, including raw reserved fields
};
template <size_t Capacity> class NativeCapture {
 public:
  bool recording = false, extended = false;
  uint32_t session = 0, latest = 0;
  void start(bool all) {
    ++session; latest = 0; size_ = 0; extended = all; recording = true;
  }
  void stop() { recording = false; }
  void append(NativeFrame frame) {
    if (!recording || latest == UINT32_MAX) { recording = false; return; }
    frame.sequence = ++latest;
    rows_[(latest - 1) % Capacity] = frame;
    if (size_ < Capacity) ++size_;
  }
  uint32_t oldest() const { return latest - uint32_t(size_) + 1; }
  size_t copyAfter(uint32_t after, NativeFrame *out, size_t limit) const {
    if (after >= latest) return 0;
    uint32_t first = after + 1;
    if (first < oldest()) first = oldest();
    size_t n = 0;
    for (uint32_t seq = first; n < limit && seq <= latest; ++seq)
      out[n++] = rows_[(seq - 1) % Capacity];
    return n;
  }
 private:
  NativeFrame rows_[Capacity];
  size_t size_ = 0;
};

// Software admission gate, not a hardware standstill guarantee. Threshold is
// measured/configured by the owner; the hold time is a software observation window.
class UpdateHold {
 public:
  bool ready = false;
  void reset() { ready = tracking_ = false; }
  void observe(uint32_t now, bool freshSafe, uint16_t left, uint16_t right) {
    if (!freshSafe) { reset(); return; }
    if (!tracking_ || left != left_ || right != right_) {
      left_ = left; right_ = right; since_ = now; tracking_ = true; ready = false;
    } else ready = uint32_t(now - since_) >= 2000;
  }
 private:
  bool tracking_ = false;
  uint16_t left_ = 0, right_ = 0;
  uint32_t since_ = 0;
};
}
