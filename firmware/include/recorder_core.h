#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace roocore {
constexpr uint8_t TX_PIN = 1, RX_PIN = 3, BRC_PIN = 2;
constexpr uint32_t BRC_PERIOD_MS = 60000, BRC_LOW_MS = 1000;
constexpr uint32_t LEASE_MS = 10000;
constexpr uint8_t BASIC[] = {7,8,9,10,11,12,13,14,15,17,18,21,22,23,24,25,26,35};
constexpr uint8_t WHEELS[] = {43,44};
inline bool elapsed(uint32_t now, uint32_t then, uint32_t period) {
  return uint32_t(now-then) >= period;
}
// One short pulse per minute, independent of networking.
struct BrcClock {
  bool low = false;
  uint32_t cycle = 0, edge = 0;
  bool update(uint32_t now) {
    if (low && elapsed(now,edge,BRC_LOW_MS)) { low=false; return true; }
    if (!low && elapsed(now,cycle,BRC_PERIOD_MS)) {
      cycle=now; edge=now; low=true; return true;
    }
    return false;
  }
};
// Only these commands exist; there are no drive or cleaning commands.
enum class Command { Invalid, Ready, StartBasic, StartWheels, Stop, Ping };
inline Command parseCommand(const char *line) {
  if (!strcmp(line,"READY")) return Command::Ready;
  if (!strcmp(line,"START basic")) return Command::StartBasic;
  if (!strcmp(line,"START wheels")) return Command::StartWheels;
  if (!strcmp(line,"STOP")) return Command::Stop;
  if (!strcmp(line,"PING")) return Command::Ping;
  return Command::Invalid;
}
inline size_t streamCommand(bool wheels, uint8_t *out) {
  size_t n=0; out[n++]=148;
  out[n++]=sizeof(BASIC)+(wheels?sizeof(WHEELS):0);
  for (auto id:BASIC) out[n++]=id;
  if (wheels) for (auto id:WHEELS) out[n++]=id;
  out[n++]=150; out[n++]=1;
  return n;
}
}
