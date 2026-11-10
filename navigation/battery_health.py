"""Read-only battery runtime analysis. Never sends commands or changes power limits.

Input is versioned JSONL telemetry with explicit robot/battery/mission/workload
identity. See docs/BATTERY_HEALTH.md; raw /status alone is not this contract.
All alert thresholds are supplied by the caller, not inferred battery limits.
"""

import argparse
from dataclasses import asdict, dataclass, field
import json
import math
from pathlib import Path
from statistics import median


@dataclass(frozen=True)
class Policy:
    max_gap_ms: int
    max_sensor_age_ms: int
    charge_tolerance_pct: float
    baseline_missions: int
    recent_missions: int
    decline_ratio: float
    abrupt_ratio: float
    repeated_recharges: int
    max_step_pct: float

    def __post_init__(self):
        for name in ("max_gap_ms", "max_sensor_age_ms", "baseline_missions",
                     "recent_missions", "repeated_recharges"):
            value = getattr(self, name)
            if type(value) is not int or value <= 0:
                raise ValueError(name + " must be a positive integer")
        if max(self.max_gap_ms, self.max_sensor_age_ms) >= 2**31:
            raise ValueError("time windows must be less than half the MCU clock range")
        if min(self.baseline_missions, self.recent_missions, self.repeated_recharges) < 2:
            raise ValueError("trend/repetition evidence requires at least two observations")
        for name in ("charge_tolerance_pct", "decline_ratio", "abrupt_ratio", "max_step_pct"):
            if not _number(getattr(self, name)):
                raise ValueError(name + " must be finite")
        if not 0 <= self.charge_tolerance_pct < 100 or not 0 < self.max_step_pct <= 100:
            raise ValueError("invalid percentage policy")
        if not 0 < self.abrupt_ratio <= self.decline_ratio < 1:
            raise ValueError("require 0 < abrupt_ratio <= decline_ratio < 1")


def _number(value):
    return type(value) in (int, float) and math.isfinite(value)


def _text(row, name):
    value = row.get(name)
    if not isinstance(value, str) or not value.strip() or len(value) > 200:
        raise ValueError(name + " must be a nonempty bounded string")
    return value


def _sample(row):
    if not isinstance(row, dict) or type(row.get("schema")) is not int or row["schema"] != 1:
        raise ValueError("expected battery telemetry schema 1")
    for name in ("robot_id", "battery_id", "mission_id", "boot_id", "workload_id", "profile"):
        _text(row, name)
    if row.get("source") not in ("robot", "simulation"):
        raise ValueError("source must be robot or simulation")
    if type(row.get("battery_valid")) is not bool:
        raise ValueError("battery_valid must explicitly describe verified fresh battery data")
    s = row.get("status")
    if not isinstance(s, dict):
        raise ValueError("status must be an object")
    for name in ("now", "sensors_ms"):
        if type(s.get(name)) is not int or not 0 <= s[name] < 2**32:
            raise ValueError(name + " must be an unsigned MCU millisecond timestamp")
    for name, high in (("state", 13), ("reason", 16), ("brush_pwm", 127), ("vacuum_pwm", 127)):
        if type(s.get(name)) is not int or not 0 <= s[name] <= high:
            raise ValueError("invalid " + name)
    for name in ("docked", "charging", "localized", "scan_valid", "cliff", "wheel_drop"):
        if type(s.get(name)) is not bool:
            raise ValueError(name + " must be boolean")
    if "undocking" in s and type(s["undocking"]) is not bool:
        raise ValueError("undocking must be boolean")
    if row["battery_valid"] and (not _number(s.get("battery")) or not 0 <= s["battery"] <= 100):
        raise ValueError("valid battery must be a percentage in 0..100")
    if s.get("battery_mv") is not None and (not _number(s["battery_mv"]) or s["battery_mv"] <= 0):
        raise ValueError("battery_mv must be positive or null")
    event = row.get("return_cause")
    if event is not None:
        if not isinstance(event, dict) or event.get("cause") not in ("battery", "manual", "completed", "unknown"):
            raise ValueError("return_cause must describe battery/manual/completed/unknown")
        if event.get("provenance") not in ("firmware_mission_event", "confirmed_collector_event", "unknown"):
            raise ValueError("return cause requires explicit event provenance, not status.reason")
        if type(event.get("confirmed")) is not bool:
            raise ValueError("return_cause.confirmed must be boolean")
        for name in ("mission_id", "boot_id"):
            _text(event, name)
        if type(event.get("at_ms")) is not int or not 0 <= event["at_ms"] < 2**32:
            raise ValueError("return_cause.at_ms must be an unsigned MCU millisecond timestamp")
    return row


def _working(s):
    return (s["state"] in (1, 5, 11, 12) or s.get("undocking", False)) and not s["charging"]


@dataclass
class Leg:
    robot_id: str
    battery_id: str
    source: str
    mission_id: str
    workload_id: str
    profile: str
    boot_id: str
    start_charge: float
    start_ms: int
    end_charge: float = None
    runtime_s: float = 0
    return_reason: str = "open"
    return_evidence: dict = None
    recharge_observed: bool = False
    exclusions: list = field(default_factory=list)
    pwm: set = field(default_factory=set)

    def exclude(self, reason):
        if reason not in self.exclusions:
            self.exclusions.append(reason)

    @property
    def comparable(self):
        return (not self.exclusions and self.source == "robot"
                and self.return_reason == "battery" and self.runtime_s > 0
                and self.end_charge is not None and self.start_charge > self.end_charge
                and len(self.pwm) == 1)

    def record(self):
        result = asdict(self)
        result["pwm"] = [list(p) for p in sorted(self.pwm)]
        result["comparable"] = self.comparable
        return result


class BatteryHealth:
    """Incremental analyzer; replay a persisted log to recover the same history.

    A discharge leg starts after an observed charging state. Only continuous,
    comparable battery-driven returns contribute to the relative runtime trend.
    A return is counted as a recharge only after subsequent dock + charging data.
    """

    def __init__(self, policy):
        self.policy = policy
        self.legs = []
        self.streams = {}
        self.anomalies = []
        self.duplicates = 0
        self.out_of_order = 0

    def _close(self, stream, reason, sample=None):
        leg = stream["leg"]
        if not leg:
            return
        leg.return_reason = reason
        if sample and sample.get("return_cause") is not None:
            leg.return_evidence = dict(sample["return_cause"])
        if sample and sample["battery_valid"]:
            leg.end_charge = sample["status"]["battery"]
        if reason != "battery":
            leg.exclude(reason)
        if not leg.pwm:
            leg.exclude("no_observed_cleaning_load")
        self.legs.append(leg)
        stream["pending"] = leg if reason == "battery" else None
        stream["leg"] = None

    def feed(self, row):
        row = _sample(row)
        key = (row["source"], row["robot_id"], row["battery_id"])
        stream = self.streams.setdefault(key, {
            "previous": None, "leg": None, "pending": None,
            "charged": False, "old_boots": set(),
        })
        s, previous = row["status"], stream["previous"]
        valid = row["battery_valid"] and (s["now"] - s["sensors_ms"]) % 2**32 <= self.policy.max_sensor_age_ms
        dt = None
        if previous:
            p = previous["status"]
            if row["boot_id"] != previous["boot_id"]:
                if row["boot_id"] in stream["old_boots"]:
                    self.out_of_order += 1
                    return
                stream["old_boots"].add(previous["boot_id"])
                self._close(stream, "controller_reboot")
                stream["pending"], stream["charged"] = None, False
                previous = None
            else:
                delta = (s["now"] - p["now"]) % 2**32
                if delta == 0:
                    self.duplicates += 1
                    # A collector can invalidate a cached snapshot after loss
                    # of transport; identical MCU time must not hide that gap.
                    if not valid:
                        self._close(stream, "invalid_or_stale_battery")
                        stream["pending"], stream["charged"] = None, False
                        stream["previous"] = row
                    return
                if delta >= 2**31:
                    self.out_of_order += 1
                    if stream["leg"]:
                        stream["leg"].exclude("out_of_order_or_clock_reset")
                    return
                if delta > self.policy.max_gap_ms:
                    self._close(stream, "telemetry_gap")
                    stream["pending"], stream["charged"] = None, False
                    previous = None
                else:
                    dt = delta / 1000
        if previous and row["mission_id"] != previous["mission_id"]:
            self._close(stream, "mission_changed", row)
            stream["pending"] = None
            # A fresh charging observation may precede the new mission ID.
            stream["charged"] = previous["status"]["docked"] and previous["status"]["charging"]
            dt = None
        if not valid:
            self._close(stream, "invalid_or_stale_battery")
            stream["pending"], stream["charged"] = None, False
            stream["previous"] = row
            return
        if stream["leg"] and previous and dt is not None:
            # Includes undocking/resuming/travel; charging time never contributes.
            if _working(previous["status"]):
                stream["leg"].runtime_s += dt
        if s["docked"] and s["charging"]:
            if stream["leg"]:
                self._close(stream, "charge_without_battery_return", row)
            pending = stream["pending"]
            if pending and pending.mission_id == row["mission_id"]:
                pending.recharge_observed = True
                stream["pending"] = None
            stream["charged"] = True
        elif _working(s) and not stream["leg"]:
            leg = Leg(**{name: row[name] for name in (
                "robot_id", "battery_id", "source", "mission_id", "workload_id", "profile", "boot_id"
            )}, start_charge=s["battery"], start_ms=s["now"])
            if not stream["charged"]:
                leg.exclude("departure_not_observed_after_charge")
            if row["source"] != "robot":
                leg.exclude("simulation")
            stream["leg"], stream["pending"], stream["charged"] = leg, None, False
        leg = stream["leg"]
        if leg:
            if row["profile"] != leg.profile or row["workload_id"] != leg.workload_id:
                leg.exclude("workload_changed")
            if _working(s) and s["brush_pwm"] > 0:
                leg.pwm.add((s["brush_pwm"], s["vacuum_pwm"]))
                if len(leg.pwm) > 1:
                    leg.exclude("cleaning_power_changed")
            if not s["localized"] or not s["scan_valid"]:
                leg.exclude("navigation_not_valid")
            if s["cliff"] or s["wheel_drop"] or s["reason"] in (3, 4, 5, 6):
                leg.exclude("safety_interruption")
            if previous and dt is not None and previous["battery_valid"] and not previous["status"]["charging"]:
                change = s["battery"] - previous["status"]["battery"]
                if abs(change) > self.policy.max_step_pct:
                    leg.exclude("charge_reading_jump")
                    self.anomalies.append({
                        "robot_id": leg.robot_id, "battery_id": leg.battery_id,
                        "mission_id": leg.mission_id, "source": leg.source,
                        "kind": "charge_reading_jump", "change_pct": change,
                        "interval_s": dt, "classification": "needs_measurement",
                    })
            if s["state"] == 2:
                self._close(stream, self._return_reason(row, leg), row)
            elif s["state"] in (0, 6, 7, 8, 9, 10, 13) and not s.get("undocking", False):
                self._close(stream, "mission_finished" if s["state"] == 10 else "interrupted", row)
        stream["previous"] = row

    def _return_reason(self, row, leg):
        # Navigator::goHome currently preserves `reason`, and both the battery
        # path and manual Home use it. A reason code is NOT return provenance.
        event = row.get("return_cause")
        if not event or not event["confirmed"] or event["provenance"] == "unknown":
            return "unknown_return"
        now = row["status"]["now"]
        event_age = (now - event["at_ms"]) % 2**32
        leg_age = (now - leg.start_ms) % 2**32
        if (event["mission_id"] != leg.mission_id or event["boot_id"] != leg.boot_id
                or event_age > self.policy.max_gap_ms or event_age > leg_age):
            return "unknown_return"
        return {"battery": "battery", "manual": "manual_return", "completed": "completed_return",
                "unknown": "unknown_return"}[event["cause"]]

    def _matches(self, a, b):
        return (a.robot_id == b.robot_id and a.battery_id == b.battery_id
                and a.workload_id == b.workload_id and a.profile == b.profile and a.pwm == b.pwm
                and abs(a.start_charge - b.start_charge) <= self.policy.charge_tolerance_pct
                and abs(a.end_charge - b.end_charge) <= self.policy.charge_tolerance_pct)

    def report(self):
        alerts, comparisons = [], []
        qualified = [leg for leg in self.legs if leg.comparable]
        # One representative leg per mission: recharge loops cannot manufacture
        # independent baseline/trend missions. Never mix replacement batteries.
        groups = {}
        for leg in self.legs:
            key = (leg.source, leg.robot_id, leg.battery_id, leg.mission_id)
            group = groups.setdefault(key, {"source": leg.source, "robot_id": leg.robot_id,
                "battery_id": leg.battery_id, "mission_id": leg.mission_id,
                "battery_returns": 0, "observed_recharges": 0, "comparable_legs": 0})
            group["battery_returns"] += leg.return_reason == "battery"
            group["observed_recharges"] += leg.recharge_observed
            group["comparable_legs"] += leg.comparable
        for group in groups.values():
            if group["source"] == "robot" and group["observed_recharges"] >= self.policy.repeated_recharges:
                alerts.append({**group, "kind": "repeated_recharges", "classification": "needs_measurement",
                    "reason": "Repeated charging is observed; mission size and workload can also explain it."})
        latest_by_group = {}
        for leg in qualified:
            key = (leg.robot_id, leg.battery_id, leg.workload_id, leg.profile, tuple(sorted(leg.pwm)))
            latest_by_group[key] = leg
        for latest in latest_by_group.values():
            matched, seen = [], set()
            for leg in qualified:
                if self._matches(leg, latest) and leg.mission_id not in seen:
                    matched.append(leg)
                    seen.add(leg.mission_id)
            n = self.policy.baseline_missions
            common = {"robot_id": latest.robot_id, "battery_id": latest.battery_id,
                      "workload_id": latest.workload_id, "profile": latest.profile}
            if len(matched) <= n:
                comparisons.append({**common, "classification": "insufficient_data",
                    "comparable_missions": len(matched), "baseline_missions_required": n})
                continue
            baseline = matched[:n]
            reference = median(leg.runtime_s for leg in baseline)
            later = matched[n:]
            recent = later[-self.policy.recent_missions:]
            newest = later[-1]
            ratio = newest.runtime_s / reference
            sustained = (len(recent) == self.policy.recent_missions
                         and all(leg.runtime_s / reference <= self.policy.decline_ratio for leg in recent))
            abrupt = ratio <= self.policy.abrupt_ratio
            classification = ("suspected_degradation" if sustained else
                              "needs_measurement" if abrupt else "no_decline_detected")
            evidence = {**common, "classification": classification,
                "baseline_runtime_s": reference, "latest_runtime_s": newest.runtime_s,
                "latest_to_baseline_ratio": ratio,
                "baseline_mission_ids": [leg.mission_id for leg in baseline],
                "recent_mission_ids": [leg.mission_id for leg in recent],
                "start_charge_pct": newest.start_charge, "return_charge_pct": newest.end_charge,
                "kind": "sustained_runtime_decline" if sustained else "abrupt_runtime_decline" if abrupt else "comparison"}
            comparisons.append(evidence)
            if sustained or abrupt:
                alerts.append(evidence)
        alerts.extend(a for a in self.anomalies if a["source"] == "robot")
        if any(a["classification"] == "suspected_degradation" for a in alerts):
            classification = "suspected_degradation"
        elif alerts:
            classification = "needs_measurement"
        elif any(c["classification"] == "no_decline_detected" for c in comparisons):
            classification = "no_decline_detected"
        else:
            classification = "insufficient_data"
        return {"schema": 1, "mode": "advisory_only", "classification": classification,
            "policy": asdict(self.policy), "policy_origin": "caller_configured_heuristics",
            "capacity_percent": None, "replacement_required": None,
            "missions": list(groups.values()), "legs": [leg.record() for leg in self.legs],
            "open_legs": [s["leg"].record() for s in self.streams.values() if s["leg"]],
            "comparisons": comparisons, "alerts": alerts,
            "duplicate_samples": self.duplicates, "out_of_order_samples": self.out_of_order,
            "note": "Relative runtime evidence, not a battery capacity test or a confirmed failure."}


def report(path, policy):
    analyzer = BatteryHealth(policy)
    with Path(path).open(encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                analyzer.feed(json.loads(line))
            except (ValueError, TypeError, KeyError) as error:
                raise ValueError("line %d: %s" % (number, error)) from error
    return analyzer.report()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path, help="versioned battery telemetry JSONL")
    parser.add_argument("--policy", required=True, type=Path, help="explicit analysis policy JSON")
    args = parser.parse_args()
    try:
        policy = Policy(**json.loads(args.policy.read_text(encoding="utf-8")))
        result = report(args.recording, policy)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(2, str(error) + "\n")
    print(json.dumps(result, ensure_ascii=False, allow_nan=False, indent=2))


if __name__ == "__main__":
    main()
