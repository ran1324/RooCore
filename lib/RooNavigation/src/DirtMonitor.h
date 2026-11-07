#pragma once
#include "RooNavigation.h"
#include <stdint.h>

namespace roonav {
// Observation and advisory planning only. This class has NO actuator/route API.
class DirtMonitor {
public:
  static constexpr unsigned Capacity = 256, MaxRegions = 16;
  enum class Kind : uint8_t {
    Reading,
    Region,
    RevisitStart,
    RevisitResult,
    Interrupted,
    Gap,
    Maintenance
  };
  enum class Action : uint8_t { Unmanaged, Stopped, RoutineTravel, RoutineCleaning };
  enum class Why : uint8_t {
    None,
    AwaitingCalibration,
    Safety,
    LowBattery,
    LinkLoss,
    PoseLost,
    TimeLimit,
    InsufficientData,
    FrameChanged,
    RegionLimit,
    DataLoss,
    CasterCleaning
  };
  struct Config {
    uint8_t threshold = 0; // 0: collection only, NO guessed physical threshold
    unsigned consecutive = 5;
    uint32_t minimumHighMs = 300, maximumGapMs = 200;
    float radius = .30f;
    uint32_t revisitBudgetMs = 20000;
    unsigned maxExtraPasses = 1; // advisory only; cannot enable autonomous cleaning
    bool valid() const {
      return consecutive >= 2 && consecutive <= 100 && minimumHighMs >= 100 &&
             minimumHighMs <= 5000 && maximumGapMs >= 20 && maximumGapMs <= 500 && radius >= .15f &&
             radius <= 1.f && revisitBudgetMs >= 1000 && revisitBudgetMs <= 60000 &&
             maxExtraPasses == 1;
    }
  } cfg;
  struct Context {
    uint32_t now = 0, poseMs = 0, frame = 0;
    Pose pose;
    bool managed = false, poseValid = false, cleaning = false, safe = false, link = false;
    float battery = 0;
    uint8_t brush = 0, vacuum = 0;
    Action action = Action::Unmanaged;
  };
  struct Record {
    uint64_t sequence = 0;
    uint32_t stamp = 0, generation = 0, frame = 0, session = 0, region = 0;
    Pose pose;
    float before = 0, after = 0;
    uint16_t count = 0;
    uint8_t value = 0, brush = 0, vacuum = 0;
    Kind kind = Kind::Reading;
    Action action = Action::Unmanaged;
    Why why = Why::None;
    bool located = false, comparable = false, decreased = false;
  };
  uint32_t session = 0;
  uint64_t latestSequence = 0;
  unsigned regionCount = 0;
  uint32_t lostSourceReadings = 0;
  static constexpr bool automaticCleaningEnabled() { return false; }
  void beginSession(uint32_t id, const Context &c) {
    interruptVisits(c, Why::FrameChanged);
    session = id;
    regionCount = 0;
    streak_ = 0;
    budgetLogged_ = false;
    for (auto &r : regions_)
      r = Region();
  }
  void gap(const Context &c, unsigned lost = 0) {
    streak_ = 0;
    lostSourceReadings += lost;
    interruptVisits(c, Why::DataLoss);
    Record r = base(c);
    r.kind = Kind::Gap;
    r.why = Why::DataLoss;
    r.count = uint16_t(lost > 65535 ? 65535 : lost);
    append(r);
  }
  void maintenance(uint8_t value, uint32_t stamp, const Context &c) {
    Record r = base(c);
    r.kind = Kind::Maintenance;
    r.stamp = stamp;
    r.value = value;
    r.why = Why::CasterCleaning;
    append(r);
  }
  void tick(const Context &c) {
    Why why = eligibility(c);
    if (haveReading_ && uint32_t(c.now - lastReading_) > cfg.maximumGapMs)
      why = Why::LinkLoss;
    if (why != Why::None) {
      streak_ = 0;
      interruptVisits(c, why);
    }
    for (unsigned n = 0; n < regionCount; ++n) {
      Region &r = regions_[n];
      if (r.visiting && uint32_t(c.now - r.visitAt) >= cfg.revisitBudgetMs)
        finish(r, c, Why::TimeLimit);
      if (r.frame != c.frame && !r.done) {
        if (r.visiting)
          finish(r, c, Why::FrameChanged);
        r.done = true;
      }
    }
  }
  void observe(uint8_t value, uint32_t stamp, uint32_t generation, const Context &c) {
    if (haveGeneration_ && generation == lastGeneration_)
      return;
    haveGeneration_ = true;
    lastGeneration_ = generation;
    if (haveReading_ && uint32_t(stamp - lastReading_) > cfg.maximumGapMs)
      gap(c);
    haveReading_ = true;
    lastReading_ = stamp;
    Record reading = base(c);
    reading.stamp = stamp;
    reading.generation = generation;
    reading.value = value;
    // Current pose is an approximation near receive time, never invented at (0,0).
    int32_t delta = int32_t(c.poseMs - stamp);
    reading.located =
        reading.located && delta >= -100 && delta <= 100 && uint32_t(c.now - stamp) <= 200;
    if (!reading.located) {
      reading.pose = Pose();
      reading.frame = 0;
    }
    append(reading);
    tick(c);
    if (!cfg.valid() || !cfg.threshold || eligibility(c) != Why::None || !reading.located) {
      streak_ = 0;
      if (!reading.located)
        interruptVisits(c, Why::PoseLost);
      return;
    }
    for (unsigned n = 0; n < regionCount; ++n) {
      Region &r = regions_[n];
      if (r.done || r.frame != c.frame)
        continue;
      float d = distance(c.pose.x, c.pose.y, r.pose.x, r.pose.y);
      if (!r.visiting && d > cfg.radius + .15f)
        r.left = true;
      if (r.left && !r.visiting && d <= cfg.radius) {
        r.visiting = true;
        r.visitAt = stamp;
        r.samples = 0;
        r.sum = 0;
        r.comparable = true;
        Record e = regionRecord(r, c);
        e.kind = Kind::RevisitStart;
        append(e);
      }
      if (r.visiting) {
        if (d > cfg.radius + .15f)
          finish(r, c, Why::None);
        else {
          if (r.samples < 65535) {
            ++r.samples;
            r.sum += value;
          }
          r.comparable &= c.brush == r.brush && c.vacuum == r.vacuum;
        }
      }
    }
    if (value < cfg.threshold) {
      streak_ = 0;
      return;
    }
    bool restart = !streak_ || uint32_t(stamp - lastHigh_) > cfg.maximumGapMs ||
                   streakFrame_ != c.frame ||
                   distance(c.pose.x, c.pose.y, streakPose_.x, streakPose_.y) > cfg.radius ||
                   c.brush != streakBrush_ || c.vacuum != streakVacuum_;
    if (restart) {
      streak_ = 0;
      streakAt_ = stamp;
      streakSum_ = 0;
      streakPose_ = c.pose;
      streakFrame_ = c.frame;
      streakBrush_ = c.brush;
      streakVacuum_ = c.vacuum;
    }
    lastHigh_ = stamp;
    if (streak_ < 65535) {
      ++streak_;
      streakSum_ += value;
    }
    if (streak_ < cfg.consecutive || uint32_t(stamp - streakAt_) < cfg.minimumHighMs)
      return;
    for (unsigned n = 0; n < regionCount; ++n)
      if (regions_[n].frame == c.frame &&
          distance(c.pose.x, c.pose.y, regions_[n].pose.x, regions_[n].pose.y) <= cfg.radius * 2)
        return; // never create repeated jobs for the same patch in this session
    if (regionCount == MaxRegions) {
      if (!budgetLogged_) {
        Record r = base(c);
        r.kind = Kind::Interrupted;
        r.why = Why::RegionLimit;
        append(r);
        budgetLogged_ = true;
      }
      return;
    }
    Region &r = regions_[regionCount++];
    r.id = regionCount;
    r.frame = c.frame;
    r.pose = streakPose_;
    r.before = float(streakSum_) / streak_;
    r.brush = c.brush;
    r.vacuum = c.vacuum;
    Record e = regionRecord(r, c);
    e.kind = Kind::Region;
    e.why = Why::AwaitingCalibration;
    e.count = uint16_t(streak_);
    append(e);
  }
  unsigned copyAfter(uint64_t cursor, Record *out, unsigned maximum) const {
    unsigned copied = 0;
    uint64_t first = latestSequence > Capacity ? latestSequence - Capacity + 1 : 1;
    uint64_t begin = cursor >= latestSequence ? latestSequence + 1 : cursor + 1;
    if (begin < first)
      begin = first;
    for (uint64_t seq = begin; seq <= latestSequence && copied < maximum; ++seq)
      out[copied++] = records_[(seq - 1) % Capacity];
    return copied;
  }
  uint64_t oldestSequence() const {
    return latestSequence > Capacity ? latestSequence - Capacity + 1 : 1;
  }

private:
  struct Region {
    uint32_t id = 0, frame = 0, visitAt = 0, sum = 0;
    unsigned samples = 0;
    Pose pose;
    float before = 0;
    uint8_t brush = 0, vacuum = 0;
    bool left = false, visiting = false, done = false, comparable = true;
  } regions_[MaxRegions];
  Record records_[Capacity];
  uint32_t lastGeneration_ = 0, streakAt_ = 0, lastHigh_ = 0, streakFrame_ = 0, streakSum_ = 0;
  unsigned streak_ = 0;
  uint8_t streakBrush_ = 0, streakVacuum_ = 0;
  Pose streakPose_;
  bool haveGeneration_ = false, budgetLogged_ = false, haveReading_ = false;
  uint32_t lastReading_ = 0;
  static Why eligibility(const Context &c) {
    if (!c.link)
      return Why::LinkLoss;
    if (!c.safe)
      return Why::Safety;
    if (!isfinite(c.battery) || c.battery <= 25)
      return Why::LowBattery;
    if (!c.managed || !c.cleaning)
      return Why::InsufficientData;
    if (!c.poseValid || !isfinite(c.pose.x) || !isfinite(c.pose.y) || !isfinite(c.pose.yaw))
      return Why::PoseLost;
    return Why::None;
  }
  Record base(const Context &c) const {
    Record r;
    r.stamp = c.now;
    r.session = session;
    r.action = c.action;
    r.brush = c.brush;
    r.vacuum = c.vacuum;
    r.located = c.managed && c.poseValid && isfinite(c.pose.x) && isfinite(c.pose.y) &&
                isfinite(c.pose.yaw);
    if (r.located) {
      r.pose = c.pose;
      r.frame = c.frame;
    }
    return r;
  }
  Record regionRecord(const Region &region, const Context &c) const {
    Record r = base(c);
    r.region = region.id;
    r.pose = region.pose;
    r.frame = region.frame;
    r.located = true;
    r.before = region.before;
    return r;
  }
  void append(Record r) {
    r.sequence = ++latestSequence;
    records_[(r.sequence - 1) % Capacity] = r;
  }
  void finish(Region &region, const Context &c, Why why) {
    Record r = regionRecord(region, c);
    r.kind = why == Why::None || why == Why::TimeLimit ? Kind::RevisitResult : Kind::Interrupted;
    r.why = why;
    r.count = uint16_t(region.samples);
    r.after = region.samples ? float(region.sum) / region.samples : 0;
    r.comparable = (why == Why::None || why == Why::TimeLimit) && region.comparable &&
                   region.samples >= cfg.consecutive &&
                   uint32_t(c.now - region.visitAt) >= cfg.minimumHighMs;
    if (!r.comparable && why == Why::None)
      r.why = Why::InsufficientData;
    r.decreased = r.comparable && r.after < r.before;
    append(r);
    region.visiting = false;
    region.done = true;
  }
  void interruptVisits(const Context &c, Why why) {
    for (unsigned n = 0; n < regionCount; ++n)
      if (regions_[n].visiting)
        finish(regions_[n], c, why);
  }
};
} // namespace roonav
