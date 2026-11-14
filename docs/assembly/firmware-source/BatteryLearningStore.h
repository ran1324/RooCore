#pragma once
#include "BatteryLearning.h"
#include <stddef.h>
#include <type_traits>

namespace roonav {
// Two alternating local snapshots. JSON exports are the portable representation.
class BatteryLearningStore {
public:
  struct Snapshot {
    uint32_t magic = 0x42415431, version = 1, size = sizeof(BatteryLearning::State);
    uint32_t sequence = 0, checksum = 0;
    BatteryLearning::State state;
  };
  struct Port {
    virtual ~Port() {}
    virtual bool read(unsigned slot, void *out, size_t size) = 0;
    virtual bool write(unsigned slot, const void *data, size_t size) = 0;
    virtual bool exists(unsigned slot) = 0;
    virtual bool remove(unsigned slot) = 0;
  };
  uint32_t sequence = 0;
  bool corrupt = false, mismatch = false;
  static uint32_t crc(const Snapshot &s) {
    const auto *p = reinterpret_cast<const uint8_t *>(&s);
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < sizeof s; ++i) {
      if (i >= offsetof(Snapshot, checksum) && i < offsetof(Snapshot, checksum)+4) continue;
      c ^= p[i];
      for (unsigned b = 0; b < 8; ++b) c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
    }
    return ~c;
  }
  static bool valid(const Snapshot &s) {
    return s.magic == 0x42415431 && s.version == 1 && s.size == sizeof(BatteryLearning::State) &&
        s.sequence && s.checksum == crc(s) && BatteryLearning::validState(s.state);
  }
  bool load(BatteryLearning &learner, Port &port, Snapshot &scratch, uint32_t boot) {
    bool loaded = false; corrupt = mismatch = false; sequence = 0;
    for (unsigned slot = 0; slot < 2; ++slot) {
      if (!port.exists(slot)) continue;
      if (!port.read(slot, &scratch, sizeof scratch) || !valid(scratch)) { corrupt = true; continue; }
      if (scratch.state.packKey != learner.cfg.packKey ||
          scratch.state.calibrationKey != learner.cfg.calibrationKey) { mismatch = true; continue; }
      if (!loaded || int32_t(scratch.sequence-sequence) > 0) {
        learner.data = scratch.state; sequence = scratch.sequence; loaded = true;
      }
    }
    // Existing unreadable/profile-mismatched data are never silently overwritten.
    learner.begin(boot);
    if (!loaded && (corrupt || mismatch)) learner.profileMismatch = true;
    return loaded;
  }
  bool save(BatteryLearning &learner, Port &port, Snapshot &scratch) {
    if (learner.profileMismatch || !learner.cfg.packKey || !learner.cfg.calibrationKey ||
        !BatteryLearning::validState(learner.data) || sequence == UINT32_MAX) return false;
    scratch = Snapshot{}; scratch.sequence = sequence + 1; scratch.state = learner.data;
    scratch.checksum = crc(scratch);
    const uint32_t expectedCrc = scratch.checksum, expectedSeq = scratch.sequence;
    const uint64_t revision = learner.data.revision;
    const unsigned slot = scratch.sequence & 1;
    if (!port.write(slot, &scratch, sizeof scratch) || !port.read(slot, &scratch, sizeof scratch) ||
        !valid(scratch) || scratch.sequence != expectedSeq || scratch.checksum != expectedCrc) return false;
    sequence = expectedSeq; learner.savedRevision = revision;
    return true;
  }
  bool reset(BatteryLearning &learner, Port &port, uint32_t boot) {
    for (unsigned slot = 0; slot < 2; ++slot)
      if (port.exists(slot) && !port.remove(slot)) { learner.profileMismatch = true; return false; }
    sequence = 0; corrupt = mismatch = false; learner.reset(boot);
    return true;
  }
};
static_assert(std::is_trivially_copyable<BatteryLearning::State>::value, "Local snapshot must be copyable");
} // namespace roonav
