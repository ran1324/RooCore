"""Read-only /progress normalization, recording, replay and battery analysis."""

from copy import deepcopy
import json
import math
import time
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener

from .battery_health import BatteryHealth, Policy
from .history import utc_now


STATES = ("READY", "CLEANING", "RETURNING", "DOCKING", "CHARGING", "RESUMING",
          "SETTLING", "RECOVERING", "PAUSED", "FAULT", "COMPLETE", "EDGING", "TOUCHUP", "YIELDING")


def _integer(value, low=0, high=2**32 - 1):
    return type(value) is int and low <= value <= high


def _finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def validate_snapshot(data):
    """Reject incomplete/corrupted frames rather than rendering invented cells."""
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 1:
        raise ValueError("unsupported progress schema")
    if data.get("source") not in ("robot", "simulation"):
        raise ValueError("missing progress provenance")
    for key in ("boot_id", "mission_id", "map_id"):
        if not isinstance(data.get(key), str) or not 0 < len(data[key]) <= 200:
            raise ValueError("invalid progress identity: " + key)
    for key in ("map_revision", "sequence", "at_ms"):
        if not _integer(data.get(key)):
            raise ValueError("invalid progress counter: " + key)
    m, s, pose, coverage = (data.get(key) for key in ("map", "status", "pose", "coverage"))
    if not all(isinstance(v, dict) for v in (m, s, pose, coverage)):
        raise ValueError("missing progress object")
    if not _integer(m.get("cols"), 1, 200) or not _integer(m.get("rows"), 1, 200):
        raise ValueError("invalid map dimensions")
    n = m["cols"] * m["rows"]
    if n > 16384 or not _finite(m.get("resolution")) or not 0 < m["resolution"] <= 1:
        raise ValueError("invalid map extent")
    for key in ("origin_x", "origin_y"):
        if not _finite(m.get(key)):
            raise ValueError("invalid map origin")
    if not isinstance(m.get("cells"), list) or len(m["cells"]) != n or any(type(v) is not int or v not in (-1, 0, 1) for v in m["cells"]):
        raise ValueError("invalid occupancy cells")
    for key, high in (("main", 255), ("side", 15), ("excluded", 255)):
        values = coverage.get(key)
        if not isinstance(values, list) or len(values) != n or any(not _integer(v, 0, high) for v in values):
            raise ValueError("invalid coverage: " + key)
    for key in ("now", "sensors_ms"):
        if not _integer(s.get(key)):
            raise ValueError("invalid status time")
    if s["now"] != data["at_ms"] or not _integer(s.get("state"), 0, len(STATES) - 1):
        raise ValueError("inconsistent status frame")
    for key in ("mission_id", "boot_id"):
        if key in s and s[key] != data[key]:
            raise ValueError("inconsistent status identity")
    if not _integer(s.get("reason"), 0, 16):
        raise ValueError("invalid status reason")
    for key in ("active_ms", "cleaning_only_ms"):
        if key in s and not _integer(s[key]):
            raise ValueError("invalid runtime counter")
    if not _integer(s.get("pass_index", s.get("pass", 1) - 1), 0, 1):
        raise ValueError("unsupported pass index")
    for key in ("docked", "charging", "localized", "scan_valid", "cliff", "wheel_drop", "battery_valid"):
        if type(s.get(key)) is not bool:
            raise ValueError("missing status validity: " + key)
    for key in ("brush_pwm", "vacuum_pwm"):
        if not _integer(s.get(key), 0, 127):
            raise ValueError("invalid cleaning command")
    if s["battery_valid"] and (not _finite(s.get("battery")) or not 0 <= s["battery"] <= 100):
        raise ValueError("invalid battery percentage")
    if type(pose.get("valid")) is not bool or any(not _finite(pose.get(k)) for k in ("x", "y", "yaw")):
        raise ValueError("invalid pose")
    return data


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError("telemetry redirects are disabled")


class RobotReader:
    """Only GET /progress. URL/token never enter the normalized view or log."""
    def __init__(self, base, token):
        parsed = urlparse(base)
        if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
            raise ValueError("robot URL must be an http(s) origin without credentials")
        if not isinstance(token, str) or len(token) < 24 or "\n" in token or "\r" in token:
            raise ValueError("server-held robot token must contain at least 24 characters")
        self.url, self.token = base.rstrip("/") + "/progress", token
        self.opener = build_opener(NoRedirect())

    def fetch(self):
        request = Request(self.url, headers={"Authorization": "Bearer " + self.token}, method="GET")
        with self.opener.open(request, timeout=2) as response:
            payload = response.read(2_000_001)
        if len(payload) > 2_000_000:
            raise ValueError("oversized progress snapshot")
        snapshot = validate_snapshot(json.loads(payload))
        if snapshot["source"] != "robot":
            raise ValueError("robot endpoint returned non-robot provenance")
        return snapshot


def empty_view():
    return {"scenario": "reported", "profile": "daily", "state": "READY", "reason": "waiting for telemetry",
        "time": 0, "pose": {"x": 0, "y": 0, "yaw": 0, "valid": False}, "battery": None,
        "distance": None, "collisions": None, "pass": 1, "passes": 1, "coverage": None,
        "truth_coverage": None, "truth_combined": None, "fine_covered": [], "side_covered": [],
        "contacts": [], "room": [], "portals": [], "edge_incomplete": None, "pass_axis": "horizontal",
        "pending_main_m2": None, "engine": "reported firmware", "geometry": {"calibrated": False},
        "cells": [0], "cols": 1, "rows": 1, "resolution": .1, "map_origin": {"x": 0, "y": 0},
        "safe": [], "covered": [], "path": [], "checkpoint": None, "dock": {"x": 0, "y": 0, "valid": False},
        "events": [], "trace": [], "trace_segments": [], "obstacles": [], "dirt": None,
        "flags": {"lidar": False, "localization": False, "cliff": False}, "command": None}


class Monitor:
    def __init__(self, store, source, robot_id=None, battery_id=None, workload_id=None,
                 policy=None, stale_after=5, clock=time.monotonic):
        if source not in ("robot", "replay"):
            raise ValueError("monitor source must be robot or replay")
        if not _finite(stale_after) or stale_after <= 0:
            raise ValueError("stale threshold must be positive")
        self.store, self.source = store, source
        self.robot_id, self.battery_id, self.workload_id = robot_id, battery_id, workload_id
        self.storage_id = robot_id or "unassigned-replay"
        self.clock, self.stale_after = clock, stale_after
        self.analyzer = BatteryHealth(policy) if policy and robot_id and battery_id and workload_id else None
        self.policy = policy
        self.recovered_envelope = None
        if self.analyzer:
            for envelope in store.envelopes(source, self.storage_id):
                self.analyzer.feed(envelope)
                self.recovered_envelope = envelope
        self.last, self.received, self.received_at, self.error = None, None, None, None
        self.view, self.segments, self.old_boots = empty_view(), [], set()
        self.gaps, self.break_trace = store.usage(source, self.storage_id)["gaps"], False

    def _envelope(self, data):
        if (not all((self.robot_id, self.battery_id, self.workload_id))
                or data["status"]["state"] == 0):
            return None
        s = deepcopy(data["status"])
        # Battery packet age must be explicit. Never borrow a generic sensor timestamp.
        battery_ms = s.get("battery_sample_ms", s.get("battery_ms"))
        valid = s["battery_valid"] and _integer(battery_ms)
        s["sensors_ms"] = battery_ms if valid else s["sensors_ms"]
        event = s.get("return_cause")
        if event is not None and not isinstance(event, dict):
            raise ValueError("return_cause must be an object or null")
        if event and event.get("cause") == "complete":
            event = dict(event, cause="completed")
        return {"schema": 1, "source": data["source"], "robot_id": self.robot_id, "battery_id": self.battery_id,
            "mission_id": data["mission_id"], "boot_id": data["boot_id"], "workload_id": self.workload_id,
            "profile": s.get("profile", "unknown"), "battery_valid": valid, "status": s, "return_cause": event}

    def failed(self):
        # Never expose urllib error text containing a private URL or credentials.
        self.error = "telemetry unavailable or invalid"
        if not self.break_trace:
            self.gaps += 1
        self.break_trace = True
        if self.last:
            envelope = self._envelope(self.last)
            if envelope:
                envelope["battery_valid"] = False
            if self.analyzer and envelope:
                self.analyzer.feed(envelope)
            summary = self._summary(self.last)
            self.store.record(self.source, self.storage_id, {"gap": True, "snapshot": self.last}, summary, envelope, {"gaps": 1})

    def ingest(self, raw):
        data = deepcopy(validate_snapshot(raw))
        s, m = data["status"], data["map"]
        usage, contiguous, dt = {}, False, 0
        if self.last:
            previous = self.last
            if data["map_id"] == previous["map_id"]:
                if any(m[key] != previous["map"][key] for key in ("cols", "rows", "resolution", "origin_x", "origin_y")):
                    raise ValueError("map geometry changed without a new map identity")
                revision_delta = (data["map_revision"] - previous["map_revision"]) % 2**32
                if revision_delta >= 2**31:
                    raise ValueError("map revision moved backwards")
            if data["boot_id"] != previous["boot_id"]:
                if data["boot_id"] in self.old_boots:
                    return False
                self.old_boots.add(previous["boot_id"])
            else:
                delta = (data["at_ms"] - previous["at_ms"]) % 2**32
                sequence_delta = (data["sequence"] - previous["sequence"]) % 2**32
                if delta == 0:
                    # Re-fetching a cached snapshot is not fresh robot progress.
                    return False
                if delta >= 2**31 or sequence_delta == 0 or sequence_delta >= 2**31:
                    self.failed()
                    return False
                dt = delta / 1000
                contiguous = (not self.break_trace and dt <= self.stale_after
                    and self.received is not None and self.clock() - self.received <= self.stale_after
                    and data["mission_id"] == previous["mission_id"] and data["map_id"] == previous["map_id"])
            if not contiguous and not self.break_trace:
                self.gaps += 1
                usage["gaps"] = 1
        envelope = self._envelope(data)
        if self.analyzer and envelope:
            if not contiguous and self.last:
                invalid = self._envelope(self.last)
                if invalid:
                    invalid["battery_valid"] = False
                    self.analyzer.feed(invalid)
                    self.store.record(self.source, self.storage_id,
                        {"discontinuity": True, "snapshot": self.last, "next_boot": data["boot_id"], "next_ms": data["at_ms"]},
                        self._summary(self.last), invalid)
            elif self.recovered_envelope and not self.last:
                invalid = deepcopy(self.recovered_envelope)
                invalid["battery_valid"] = False
                self.analyzer.feed(invalid)
                self.store.record(self.source, self.storage_id,
                    {"server_restart_gap": True, "boot_id": invalid["boot_id"], "now": invalid["status"]["now"]},
                    {}, invalid, {"gaps": 1})
            self.analyzer.feed(envelope)
        if contiguous:
            p = self.last["status"]
            p_age = (p["now"] - p["sensors_ms"]) % 2**32
            age = (s["now"] - s["sensors_ms"]) % 2**32
            if max(age, p_age) <= self.stale_after * 1000 and not any((p["cliff"], p["wheel_drop"], s["cliff"], s["wheel_drop"])):
                usage.update(observed_s=dt, brush_s=dt if p["brush_pwm"] else 0, vacuum_s=dt if p["vacuum_pwm"] else 0)
        view = empty_view()
        pass_index = s.get("pass_index", max(0, s.get("pass", 1) - 1))
        if not _integer(pass_index, 0, 1):
            raise ValueError("unsupported pass index")
        main = [c * 4 + sub for c, bits in enumerate(data["coverage"]["main"]) for sub in range(4)
                if m["cells"][c] == -1 and bits & (1 << (sub + pass_index * 4))]
        main_set = set(main)
        side = [c * 4 + sub for c, bits in enumerate(data["coverage"]["side"]) for sub in range(4)
                if m["cells"][c] == -1 and bits & (1 << sub) and c * 4 + sub not in main_set]
        known = 4 * sum(v == -1 for v in m["cells"])
        same_frame = self.last and data["map_id"] == self.last["map_id"] and data["boot_id"] == self.last["boot_id"]
        pose_valid = data["pose"]["valid"] and s["localized"] and s["scan_valid"]
        pose = dict(data["pose"], valid=pose_valid)
        if not pose_valid and same_frame:
            pose = dict(self.view["pose"], valid=False)
        if not same_frame:
            self.segments = []
        if pose_valid:
            # Never draw a long straight route through an unobserved pose jump.
            # One cell is a rendering continuity rule, not a motion/safety limit.
            jump = math.hypot(pose["x"] - self.view["pose"]["x"], pose["y"] - self.view["pose"]["y"]) > m["resolution"]
            if not contiguous or not self.segments or not self.view["pose"]["valid"] or jump:
                self.segments.append([])
            self.segments[-1].append([pose["x"], pose["y"]])
            self.segments[-1] = self.segments[-1][-2000:]
            self.segments = self.segments[-20:]
        battery_ms = s.get("battery_sample_ms", s.get("battery_ms"))
        battery_fresh = s["battery_valid"] and _integer(battery_ms) and (s["now"] - battery_ms) % 2**32 <= self.stale_after * 1000
        dock = data.get("dock", {})
        if not isinstance(dock, dict):
            raise ValueError("dock must be an object")
        dock_position_known = all(_finite(dock.get(k)) for k in ("x", "y"))
        dock = dict({"x": 0, "y": 0}, **dock)
        dock["valid"] = dock.get("valid") is True and dock_position_known
        view.update(scenario="reported", profile=s.get("profile", "unknown"), state=STATES[s["state"]],
            reason=str(s.get("reason", "")), time=s.get("active_ms", 0) / 1000,
            pose=pose, battery=s.get("battery") if battery_fresh else None,
            **{"pass": pass_index + 1, "passes": 2 if s.get("profile") == "deep" else 1},
            coverage=round(100 * len(main) / known, 1) if known else None,
            fine_covered=main, side_covered=side, covered=sorted({i // 4 for i in main}),
            cells=m["cells"], cols=m["cols"], rows=m["rows"], resolution=m["resolution"],
            map_origin={"x": m["origin_x"], "y": m["origin_y"]},
            pending_main_m2=round((known - len(main)) * m["resolution"]**2 / 4, 3),
            pass_axis="horizontal" if pass_index == 0 else "vertical",
            dock=dock,
            trace=self.segments[-1] if self.segments else [], trace_segments=deepcopy(self.segments),
            flags={"lidar": s["scan_valid"], "localization": pose_valid, "cliff": s["cliff"]},
            geometry={"calibrated": s.get("geometry_calibrated", False)},
            identity={"robot_id": self.robot_id, "battery_id": self.battery_id, "mission_id": data["mission_id"],
                "map_id": data["map_id"], "map_revision": data["map_revision"], "boot_id": data["boot_id"]},
            charging={"phase": s.get("charge_phase"), "held_reason": s.get("charge_reason"),
                "state": s.get("charging_state"), "sources": s.get("charging_sources"),
                "valid": s.get("charger_valid", False)},
            progress={"program_complete": s["state"] == 10, "coverage_scope": "observed_floor_current_pass",
                "reported_active_s": s.get("active_ms", 0) / 1000,
                "reported_cleaning_s": s["cleaning_only_ms"] / 1000 if _integer(s.get("cleaning_only_ms")) else None,
                "known_floor_samples": known, "main_samples": len(main), "side_only_samples": len(side),
                "excluded": data["coverage"]["excluded"], "gaps": self.gaps, "gaps_scope": "observer_history",
                "sample_provenance": data["source"]})
        self.view, self.last = view, data
        self.received, self.received_at, self.error = self.clock(), utc_now(), None
        self.break_trace = False
        self.store.record(self.source, self.storage_id, data, self._summary(data), envelope, usage)
        return True

    def _summary(self, data):
        s = data["status"]
        active = s["state"] != 0
        return {"mission_id": data["mission_id"] if active else None, "map_id": data["map_id"], "battery_id": self.battery_id,
            "state": STATES[s["state"]], "program_complete": s["state"] == 10,
            "coverage": self.view.get("coverage"), "coverage_scope": "observed_floor_current_pass",
            "reported_active_s": s.get("active_ms", 0) / 1000,
            "reported_cleaning_s": s["cleaning_only_ms"] / 1000 if _integer(s.get("cleaning_only_ms")) else None,
            "gaps": self.gaps, "gaps_scope": "observer_history"}

    def health(self):
        if not self.analyzer:
            missing = [name for name, value in (("policy", self.policy), ("robot_id", self.robot_id),
                ("battery_id", self.battery_id), ("workload_id", self.workload_id)) if not value]
            return {"classification": "unconfigured", "missing": missing, "mode": "advisory_only",
                    "capacity_percent": None, "replacement_required": None, "observation_source": self.source}
        return dict(self.analyzer.report(), observation_source=self.source)

    def state(self):
        age = self.clock() - self.received if self.received is not None else None
        fresh = "disconnected" if self.error else "waiting" if age is None else "stale" if age > self.stale_after else "fresh"
        view = deepcopy(self.view)
        view.update(source=self.source, read_only=True,
            freshness={"status": fresh, "age_s": round(age, 2) if age is not None else None,
                "last_received_at": self.received_at, "error": self.error, "replay": self.source == "replay"},
            battery_health=self.health(), history=self.store.history(self.source, self.storage_id),
            maintenance=self.store.maintenance(self.source, self.storage_id))
        if fresh != "fresh":
            view["pose"]["valid"] = False
        return view


def read_replay(path):
    """Read firmware snapshots or server-export envelopes; never performs I/O to a robot."""
    rows = []
    with open(path, encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("expected a snapshot object")
                rows.append(validate_snapshot(row.get("snapshot", row)))
            except (ValueError, TypeError, KeyError) as exc:
                raise ValueError("replay line %d: %s" % (number, exc)) from exc
    if not rows:
        raise ValueError("replay has no snapshots")
    return rows
