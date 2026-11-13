"""Synthetic /progress fixtures and loopback mocks only; no robot connection."""

from copy import deepcopy
from datetime import date
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from unittest.mock import patch

from navigation.history import History
from navigation.monitor import Monitor, RobotReader, read_replay, validate_snapshot
from navigation.server import Server
from tests.test_battery_health import policy


def frame(now=1000, **changes):
    data = {"schema": 1, "source": "robot", "boot_id": "fixture-boot", "mission_id": "fixture-mission",
        "map_id": "fixture-map", "map_revision": 1, "sequence": now // 1000, "at_ms": now,
        "status": {"now": now, "sensors_ms": now, "battery_sample_ms": now,
            "state": 1, "reason": 0, "battery": 90, "battery_valid": True,
            "brush_pwm": 85, "vacuum_pwm": 85, "docked": False, "charging": False,
            "localized": True, "scan_valid": True, "cliff": False, "wheel_drop": False,
            "active_ms": now, "cleaning_only_ms": now, "profile": "daily", "pass_index": 0,
            "return_cause": None},
        "map": {"cols": 2, "rows": 2, "resolution": .1, "origin_x": -1., "origin_y": -2., "cells": [-1, -1, 0, 1]},
        "coverage": {"main": [3, 0, 15, 0], "side": [7, 0, 0, 0], "excluded": [0, 0, 0, 0]},
        "pose": {"x": -.8, "y": -1.8, "yaw": 0., "valid": True},
        "dock": {"x": -.9, "y": -1.9, "valid": True, "anchor_revision": 1}}
    for key, value in changes.items():
        if key in data["status"]:
            data["status"][key] = value
        else:
            data[key] = value
    return data


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.store = History(":memory:")
        self.now = 10.
        self.monitor = Monitor(self.store, "replay", "fixture-robot", "fixture-battery", "fixture-load", policy(), clock=lambda: self.now)

    def tearDown(self):
        self.store.close()

    def test_coverage_is_observed_floor_not_program_completion(self):
        self.monitor.ingest(frame(state=10))
        view = self.monitor.state()
        self.assertEqual(view["coverage"], 25)
        self.assertEqual(view["fine_covered"], [0, 1])
        self.assertEqual(view["side_covered"], [2])
        self.assertTrue(view["progress"]["program_complete"])
        self.assertEqual(view["pending_main_m2"], .015)
        self.assertEqual(view["map_origin"], {"x": -1., "y": -2.})
        self.assertEqual(view["history"]["missions"][0]["coverage"], 25)
        self.assertIsNone(view["truth_coverage"])

    def test_brush_geometry_comes_from_telemetry_without_inventing_missing_fields(self):
        self.monitor.ingest(frame())
        self.assertEqual(self.monitor.state()["geometry"], {"calibrated": False})
        data = frame(2000)
        data["status"].update(brush_width_m=.16, brush_depth_m=.07, brush_offset_m=-.035,
                              side_brush_x_m=.10, side_brush_y_m=-.13, side_brush_radius_m=.07)
        self.monitor.ingest(data)
        self.assertEqual(self.monitor.state()["geometry"], {
            "calibrated": False, "brush_width": .16, "brush_depth": .07, "brush_offset": -.035,
            "side_x": .10, "side_y": -.13, "side_radius": .07,
        })

    def test_invalid_pose_freezes_marker_stale_duplicate_does_not_refresh(self):
        self.monitor.ingest(frame())
        bad = frame(2000, localized=False, pose={"x": 5., "y": 5., "yaw": 1., "valid": False})
        self.monitor.ingest(bad)
        self.assertEqual(self.monitor.state()["pose"]["x"], -.8)
        self.assertFalse(self.monitor.state()["pose"]["valid"])
        self.now += 6
        self.monitor.ingest(bad)
        self.assertEqual(self.monitor.state()["freshness"]["status"], "stale")

    def test_gaps_reboots_and_jumps_never_connect_traces(self):
        self.monitor.ingest(frame())
        self.monitor.ingest(frame(2000, pose={"x": -.75, "y": -1.8, "yaw": 0., "valid": True}))
        self.assertEqual(len(self.monitor.state()["trace"]), 2)
        self.monitor.ingest(frame(3000, pose={"x": 1., "y": 1., "yaw": 0., "valid": True}))
        self.assertEqual(len(self.monitor.state()["trace"]), 1)
        self.monitor.failed()
        self.monitor.ingest(frame(4000))
        self.assertEqual(len(self.monitor.state()["trace"]), 1)
        self.monitor.ingest(frame(10, boot_id="fixture-boot-2"))
        self.assertEqual(len(self.monitor.state()["trace_segments"]), 1)
        self.assertFalse(self.monitor.ingest(frame(5000)))

    def test_health_requires_identity_policy_and_explicit_fresh_battery_packet(self):
        unconfigured = Monitor(self.store, "robot", "fixture-robot")
        unconfigured.ingest(frame())
        self.assertEqual(unconfigured.health()["classification"], "unconfigured")
        self.assertIn("battery_id", unconfigured.health()["missing"])
        missing = frame()
        del missing["status"]["battery_sample_ms"]
        self.assertFalse(self.monitor._envelope(missing)["battery_valid"])

    def test_explicit_battery_event_integrates_analyzer_and_ignores_reason(self):
        self.monitor.ingest(frame(state=4, docked=True, charging=True, brush_pwm=0, vacuum_pwm=0))
        self.monitor.ingest(frame(2000))
        event = {"cause": "battery", "confirmed": True, "provenance": "firmware_mission_event",
            "mission_id": "fixture-mission", "boot_id": "fixture-boot", "at_ms": 3000}
        self.monitor.ingest(frame(3000, state=2, battery=80, reason=0, return_cause=event))
        self.monitor.ingest(frame(4000, state=4, battery=80, docked=True, charging=True))
        report = self.monitor.health()
        self.assertTrue(report["legs"][0]["comparable"])
        self.assertEqual(report["missions"][0]["observed_recharges"], 1)
        self.assertEqual(report["observation_source"], "replay")

    def test_reported_and_observed_usage_do_not_include_missing_intervals(self):
        self.monitor.ingest(frame())
        self.monitor.ingest(frame(2000))
        self.monitor.ingest(frame(9000))
        usage = self.store.usage("replay", "fixture-robot")
        self.assertEqual(usage["brush_s"], 1)
        self.assertEqual(usage["gaps"], 1)
        self.assertEqual(self.monitor.state()["progress"]["reported_active_s"], 9)

    def test_real_128_square_map_supported_and_invalid_frame_is_rejected(self):
        data = frame()
        data["map"].update(cols=128, rows=128, cells=[0] * 16384)
        data["coverage"] = {key: [0] * 16384 for key in ("main", "side", "excluded")}
        validate_snapshot(data)
        data["coverage"]["main"].pop()
        with self.assertRaises(ValueError):
            self.monitor.ingest(data)
        self.assertIsNone(self.monitor.last)

    def test_same_map_identity_requires_fixed_frame_and_monotonic_revision_sequence(self):
        self.monitor.ingest(frame())
        changed = frame(2000)
        changed["map"]["origin_x"] = -5
        with self.assertRaisesRegex(ValueError, "geometry"):
            self.monitor.ingest(changed)
        with self.assertRaisesRegex(ValueError, "revision"):
            self.monitor.ingest(frame(2000, map_revision=0))
        self.assertFalse(self.monitor.ingest(frame(2000, sequence=1)))
        self.assertFalse(self.monitor.ingest(frame(2000, sequence=0)))
        self.assertEqual(self.monitor.last["at_ms"], 1000)
        self.monitor.ingest(frame(2000, map_id="new-frame", map_revision=0))
        self.assertEqual(len(self.monitor.state()["trace"]), 1)

    def test_counter_wrap_is_accepted_and_idle_is_not_a_cleaning_mission(self):
        first = frame(2**32 - 1000, sequence=2**32 - 1, map_revision=2**32 - 1,
                      state=0, mission_active=False)
        self.monitor.ingest(first)
        self.assertEqual(self.monitor.state()["history"]["missions"], [])
        self.assertEqual(self.monitor.health()["open_legs"], [])
        self.monitor.ingest(frame(0, sequence=0, map_revision=0))
        self.assertEqual(self.monitor.last["at_ms"], 0)

    def test_recorded_connection_gap_survives_health_replay(self):
        self.monitor.ingest(frame(state=4, charging=True, docked=True))
        self.monitor.ingest(frame(2000))
        self.monitor.failed()
        restored = Monitor(self.store, "replay", "fixture-robot", "fixture-battery", "fixture-load", policy())
        self.assertEqual(restored.health()["open_legs"], [])
        self.assertIn("invalid_or_stale_battery", restored.health()["legs"][0]["exclusions"])

    def test_host_gap_is_not_hidden_by_small_mcu_increment(self):
        self.monitor.ingest(frame())
        self.now += 30
        self.monitor.ingest(frame(2000))
        self.assertEqual(self.monitor.state()["maintenance"]["usage"]["brush_s"], 0)
        self.assertEqual(len(self.monitor.state()["trace"]), 1)

    def test_terminal_inactive_flag_still_records_completion_and_interruptions(self):
        self.monitor.ingest(frame(state=4, docked=True, charging=True))
        self.monitor.ingest(frame(2000))
        self.monitor.ingest(frame(3000, state=8, mission_active=False))
        self.assertEqual(self.monitor.health()["legs"][0]["return_reason"], "interrupted")
        self.monitor.ingest(frame(4000, state=10, mission_active=False))
        self.assertTrue(self.monitor.state()["history"]["missions"][0]["program_complete"])

    def test_shipped_replay_fixture_is_synthetic_and_station_requires_position(self):
        rows = read_replay(Path(__file__).parent / "fixtures/progress-synthetic.jsonl")
        self.assertTrue(all(r["source"] == "simulation" for r in rows))
        for row in rows:
            self.monitor.ingest(row)
        self.assertEqual(self.monitor.state()["source"], "replay")
        self.assertEqual(self.monitor.health()["alerts"], [])
        self.assertTrue(self.monitor.state()["progress"]["program_complete"])
        self.monitor.ingest(frame(7000, dock={"valid": True, "anchor_revision": 1}))
        self.assertFalse(self.monitor.state()["dock"]["valid"])

    def test_persistence_replay_provenance_and_no_simulation_battery_alert(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "records.jsonl"
            path.write_text(json.dumps(frame(source="simulation")) + "\n")
            rows = read_replay(path)
            self.monitor.ingest(rows[0])
            self.assertEqual(self.monitor.state()["source"], "replay")
            self.assertEqual(self.monitor.state()["progress"]["sample_provenance"], "simulation")
            self.assertEqual(self.monitor.health()["alerts"], [])
            db = Path(folder) / "history.sqlite"
            store = History(db)
            monitor = Monitor(store, "replay", "r", "b", "w", policy())
            monitor.ingest(rows[0])
            store.close()
            store = History(db)
            restored = Monitor(store, "replay", "r", "b", "w", policy())
            self.assertEqual(len(restored.health()["open_legs"]), 1)
            self.assertEqual(len(store.history("replay", "r")["missions"]), 1)
            self.assertEqual(store.history("robot", "r")["missions"], [])
            store.close()


class MaintenanceTests(unittest.TestCase):
    def test_dates_cleaning_replacement_ranges_persistence_and_source_isolation(self):
        with tempfile.TemporaryDirectory() as folder:
            store = History(Path(folder) / "history.sqlite")
            today = date(2026, 11, 10)
            self.assertTrue(all(p["installed"] is None for p in store.maintenance("robot", "r", today)["parts"]))
            store.event("robot", "r", {"part": "main_brushes", "action": "installed", "date": "2026-01-31", "identity": "user label"}, today)
            store.event("robot", "r", {"part": "main_brushes", "action": "cleaned", "date": "2026-11-09"}, today)
            store.settings("robot", "r", {"pets": True})
            parts = {p["part"]: p for p in store.maintenance("robot", "r", today)["parts"]}
            brushes = parts["main_brushes"]
            self.assertEqual(brushes["installed"], "2026-01-31")
            self.assertEqual(brushes["replacement_window"], ["2026-07-31", "2027-01-31"])
            self.assertEqual(brushes["clean_frequency"], "twice_weekly")
            self.assertFalse(brushes["replacement_overdue"])
            self.assertIsNone(brushes["wear_percent"])
            store.close()
            store = History(Path(folder) / "history.sqlite")
            self.assertEqual(len(store.maintenance("robot", "r", today)["events"]), 2)
            self.assertEqual(store.maintenance("simulation", "r", today)["events"], [])
            store.close()

    def test_service_validation_unknown_identity_and_snooze(self):
        store = History(":memory:")
        today = date(2026, 11, 10)
        for body in ({"part": "battery", "action": "replaced", "date": "2026-01-01"},
                     {"part": "filter", "action": "installed", "date": "2030-01-01"},
                     {"part": "filter", "action": "snoozed", "date": "2020-01-01"}):
            with self.assertRaises(ValueError):
                store.event("robot", "r", body, today)
        result = store.event("robot", "r", {"part": "filter", "action": "snoozed", "date": "2026-11-20"}, today)
        self.assertTrue(next(p for p in result["parts"] if p["part"] == "filter")["snoozed"])
        store.close()


class ReadOnlyServerTests(unittest.TestCase):
    def setUp(self):
        self.calls = []
        calls = self.calls
        class RobotHandler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass
            def do_GET(self):
                calls.append((self.command, self.path, self.headers.get("Authorization")))
                body = json.dumps(frame()).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        self.robot = ThreadingHTTPServer(("127.0.0.1", 0), RobotHandler)
        self.robot_thread = threading.Thread(target=self.robot.serve_forever, daemon=True)
        self.robot_thread.start()
        self.token = "fixture-token-" + "x" * 24
        reader = RobotReader("http://127.0.0.1:%d" % self.robot.server_port, self.token)
        self.server = Server(("127.0.0.1", 0), source="robot", store_path=":memory:", robot_id="fixture-robot", reader=reader)
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()
        self.base = "http://127.0.0.1:%d" % self.server.server_port

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.robot.shutdown()
        self.robot.server_close()

    def request(self, path, body=None, headers=None):
        request = Request(self.base + path, data=json.dumps(body).encode() if body is not None else None,
            headers=dict({"Content-Type": "application/json"}, **(headers or {})))
        try:
            with urlopen(request) as response:
                return response.status, json.loads(response.read())
        except HTTPError as exc:
            return exc.code, json.loads(exc.read())

    def test_only_get_progress_credentials_stay_server_side_and_controls_rejected(self):
        self.assertEqual(self.request("/api/state")[1]["freshness"]["status"], "waiting")
        self.assertEqual(self.request("/api/checkpoint")[0], 409)
        self.server.poll_once()
        self.assertEqual(self.calls, [("GET", "/progress", "Bearer " + self.token)])
        state = self.request("/api/state")[1]
        self.assertNotIn(self.token, json.dumps(state))
        self.assertTrue(state["read_only"])
        for path in ("/api/start", "/api/home", "/api/reset", "/api/inject", "/api/step", "/api/resume"):
            self.assertEqual(self.request(path, {})[0], 403)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(self.request("/api/checkpoint")[1]["read_only"])

    def test_maintenance_is_local_only_and_host_origin_checked_on_reads(self):
        status, state = self.request("/api/maintenance/event", {"part": "filter", "action": "installed", "date": "2026-01-01"})
        self.assertEqual(status, 200)
        self.assertEqual(len(state["maintenance"]["events"]), 1)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.request("/api/state", headers={"Host": "attacker.example"})[0], 403)
        self.assertEqual(self.request("/api/maintenance/settings", {"pets": True}, {"Origin": "https://attacker.example"})[0], 403)

    def test_saved_robot_map_zones_are_annotations_without_command_upload(self):
        from navigation.zone_service import ZoneService
        with tempfile.TemporaryDirectory() as directory:
            self.server.zones = ZoneService(directory)
            self.server.monitor.ingest(frame(state=8))
            status, document = self.request("/api/zones")
            self.assertEqual(status, 200)
            self.assertEqual(document["application"], "annotations_only")
            document["zones"] = [{"id": "bathroom", "name": "חדר רחצה", "kind": "no_go",
                                  "x1": -1., "y1": -2., "x2": -.9, "y2": -1.9}]
            status, saved = self.request("/api/zones", document)
            self.assertEqual(status, 200, saved)
            self.assertEqual(saved["revision"], 1)
            self.assertEqual(saved["zones"][0]["name"], "חדר רחצה")
            self.assertEqual(self.calls, [])
            self.assertTrue(self.request("/api/state")[1]["read_only"])

    def test_configuration_fails_closed(self):
        with self.assertRaises(ValueError):
            Server(("0.0.0.0", 0), store_path=":memory:")
        for url in ("http://user:secret@localhost", "http://localhost/private", "http://localhost/?token=x"):
            with self.assertRaises(ValueError):
                RobotReader(url, self.token)

    def test_simulation_consumes_cumulative_usage_not_last_command_times_batch(self):
        class SimulationFixture:
            value = {"time": 0., "state": "READY", "scenario": "fixture", "coverage": 0,
                     "active_ms": 0, "brush_s": 0., "vacuum_s": 0.}
            def state(self):
                return dict(self.value)
        # This fixture deliberately has only cumulative usage, not navigation/map state.
        # Map loading is exercised by the real simulator integration tests.
        with patch("navigation.server.Simulation", SimulationFixture), patch("navigation.server.ZoneService.activate"):
            sim_server = Server(("127.0.0.1", 0), store_path=":memory:")
            sim_server.state()
            sim_server.sim.value.update(time=25., state="CHARGING", active_ms=15000, brush_s=10., vacuum_s=8.)
            state = sim_server.state()
            self.assertEqual(state["maintenance"]["usage"]["brush_s"], 10)
            self.assertEqual(state["maintenance"]["usage"]["vacuum_s"], 8)
            self.assertEqual(state["history"]["missions"][0]["reported_active_s"], 15)
            self.assertEqual(sim_server.state()["maintenance"]["usage"]["brush_s"], 10)
            self.assertEqual(state["battery_health"]["alerts"], [])
            sim_server.server_close()


if __name__ == "__main__":
    unittest.main()
