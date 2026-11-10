"""Synthetic policy/telemetry fixtures, not measured Roomba battery calibration."""

from copy import deepcopy
from dataclasses import asdict
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from navigation.battery_health import BatteryHealth, Policy, report


def policy(**changes):
    values = dict(max_gap_ms=100000, max_sensor_age_ms=500, charge_tolerance_pct=2,
                  baseline_missions=2, recent_missions=2, decline_ratio=.75,
                  abrupt_ratio=.5, repeated_recharges=2, max_step_pct=20)
    values.update(changes)
    return Policy(**values)


class Recording:
    def __init__(self, start=0):
        self.now = start
        self.rows = []

    def row(self, mission="m1", state=1, battery=100, reason=0, dt=1000, **changes):
        self.now = (self.now + dt) % 2**32
        row = dict(schema=1, robot_id="synthetic-620", battery_id="pack-a", source="robot",
                   mission_id=mission, boot_id="boot-a", workload_id="fixed-floor-load",
                   profile="daily", battery_valid=True,
                   status=dict(now=self.now, sensors_ms=self.now, battery=battery,
                               state=state, reason=reason, brush_pwm=85 if state == 1 else 0,
                               vacuum_pwm=85 if state == 1 else 0, docked=state == 4,
                               charging=state == 4, localized=True, scan_valid=True,
                               cliff=False, wheel_drop=False, battery_mv=None))
        for key, value in changes.items():
            if key in row["status"]:
                row["status"][key] = value
            else:
                row[key] = value
        self.rows.append(row)
        return row

    def leg(self, mission, seconds=100, start=100, end=25, reason=0, cause="battery", charged=True, **changes):
        if charged:
            self.row(mission, state=4, battery=start, **changes)
        self.row(mission, battery=start, **changes)
        for i in range(1, 11):
            row = self.row(mission, state=2 if i == 10 else 1, reason=reason if i == 10 else 0,
                           battery=start - (start - end) * i / 10, dt=round(seconds * 100), **changes)
            if i == 10 and cause is not None:
                row["return_cause"] = dict(cause=cause, provenance="firmware_mission_event", confirmed=True,
                                           mission_id=mission, boot_id=row["boot_id"], at_ms=self.now)
        self.row(mission, state=4, battery=end, **changes)
        return self


def analyze(recording, settings=None):
    analyzer = BatteryHealth(settings or policy())
    for row in recording.rows:
        analyzer.feed(row)
    return analyzer.report()


class BatteryHealthTests(unittest.TestCase):
    def test_single_short_run_does_not_diagnose_battery(self):
        result = analyze(Recording().leg("m1", seconds=20))
        self.assertEqual(result["classification"], "insufficient_data")
        self.assertEqual(result["legs"][0]["runtime_s"], 20)
        self.assertIsNone(result["capacity_percent"])
        self.assertIsNone(result["replacement_required"])

    def test_abrupt_new_decline_then_sustained_distinct_missions(self):
        r = Recording().leg("m1").leg("m2").leg("m3", seconds=40)
        result = analyze(r)
        self.assertEqual(result["classification"], "needs_measurement")
        self.assertEqual(result["alerts"][0]["kind"], "abrupt_runtime_decline")
        self.assertAlmostEqual(result["alerts"][0]["latest_to_baseline_ratio"], .4)
        r.leg("m4", seconds=50)
        result = analyze(r)
        self.assertEqual(result["classification"], "suspected_degradation")
        self.assertEqual(result["comparisons"][0]["baseline_mission_ids"], ["m1", "m2"])
        self.assertEqual(result["comparisons"][0]["recent_mission_ids"], ["m3", "m4"])

    def test_single_low_then_recovery_is_not_sustained_decline(self):
        r = Recording().leg("m1").leg("m2").leg("m3", seconds=30).leg("m4", seconds=100)
        result = analyze(r)
        self.assertEqual(result["classification"], "no_decline_detected")
        self.assertFalse(result["alerts"])

    def test_same_mission_recharges_cannot_manufacture_baseline(self):
        result = analyze(Recording().leg("m1").leg("m1").leg("m1"))
        self.assertEqual(len(result["missions"]), 1)
        self.assertEqual(result["missions"][0]["observed_recharges"], 3)
        self.assertEqual(result["classification"], "needs_measurement")
        self.assertEqual(result["alerts"][0]["kind"], "repeated_recharges")
        self.assertEqual(result["comparisons"][0]["comparable_missions"], 1)

    def test_manual_return_and_completed_mission_are_not_battery_evidence(self):
        r = Recording().leg("manual", seconds=20, reason=14, cause="manual")
        r.row("complete", state=4)
        r.row("complete")
        r.row("complete", state=10, battery=90)
        result = analyze(r)
        self.assertEqual(sum(m["battery_returns"] for m in result["missions"]), 0)
        self.assertFalse(any(x["comparable"] for x in result["legs"]))
        self.assertEqual(result["classification"], "insufficient_data")

    def test_matching_start_return_charge_load_and_replacement_identity(self):
        for changes in ({"battery_id": "replacement"}, {"workload_id": "carpet"}, {"profile": "deep"}):
            with self.subTest(changes=changes):
                r = Recording().leg("m1").leg("m2").leg("m3", seconds=20, **changes)
                self.assertFalse(analyze(r)["alerts"])
        for start, end in ((80, 25), (100, 60)):
            with self.subTest(start=start, end=end):
                r = Recording().leg("m1").leg("m2").leg("m3", seconds=20, start=start, end=end)
                self.assertFalse(analyze(r)["alerts"])

    def test_load_change_mid_leg_excludes_it(self):
        r = Recording().leg("m1")
        r.rows[5]["status"]["vacuum_pwm"] = 127
        result = analyze(r)
        self.assertIn("cleaning_power_changed", result["legs"][0]["exclusions"])
        self.assertFalse(result["legs"][0]["comparable"])

    def test_unknown_departure_cannot_be_short_battery_life(self):
        result = analyze(Recording().leg("m1", seconds=20, charged=False))
        self.assertIn("departure_not_observed_after_charge", result["legs"][0]["exclusions"])
        self.assertEqual(result["classification"], "insufficient_data")

    def test_pause_navigation_and_safety_interruptions_exclude_comparison(self):
        for field, value in (("localized", False), ("cliff", True), ("state", 8)):
            with self.subTest(field=field):
                r = Recording().leg("m1")
                r.rows[5]["status"][field] = value
                self.assertFalse(any(x["comparable"] for x in analyze(r)["legs"]))

    def test_gap_stale_data_and_invalid_readings_do_not_inflate_runtime(self):
        for change in ("gap", "stale", "invalid"):
            with self.subTest(change=change):
                r = Recording().leg("m1")
                if change == "gap":
                    for row in r.rows[5:]:
                        row["status"]["now"] += 200000
                        row["status"]["sensors_ms"] += 200000
                elif change == "stale":
                    r.rows[5]["status"]["sensors_ms"] -= 1000
                else:
                    r.rows[5]["battery_valid"] = False
                    r.rows[5]["status"]["battery"] = None
                result = analyze(r)
                self.assertFalse(any(x["comparable"] for x in result["legs"]))
                self.assertLessEqual(sum(x["runtime_s"] for x in result["legs"]), 100)

    def test_duplicates_reboots_late_old_boot_and_clock_rollover(self):
        r = Recording(start=2**32 - 30000).leg("m1")
        duplicate = deepcopy(r.rows[4])
        r.rows.insert(5, duplicate)
        result = analyze(r)
        self.assertEqual(result["duplicate_samples"], 1)
        self.assertEqual(result["legs"][0]["runtime_s"], 100)
        self.assertTrue(result["legs"][0]["comparable"])
        r = Recording().leg("m1")
        old = deepcopy(r.rows[6])
        for row in r.rows[5:]:
            row["boot_id"] = "boot-b"
        r.rows.insert(7, old)
        result = analyze(r)
        self.assertFalse(any(x["comparable"] for x in result["legs"]))
        self.assertEqual(result["out_of_order_samples"], 1)

    def test_same_boot_clock_reset_is_not_large_runtime(self):
        r = Recording().leg("m1")
        r.rows[5]["status"]["now"] = 10
        r.rows[5]["status"]["sensors_ms"] = 10
        result = analyze(r)
        self.assertEqual(result["out_of_order_samples"], 1)
        self.assertFalse(result["legs"][0]["comparable"])

    def test_collector_can_invalidate_cached_snapshot_on_connection_loss(self):
        r = Recording().leg("m1")
        invalid = deepcopy(r.rows[5])
        invalid["battery_valid"] = False
        r.rows.insert(6, invalid)
        result = analyze(r)
        self.assertFalse(any(x["comparable"] for x in result["legs"]))
        self.assertIn("invalid_or_stale_battery", result["legs"][0]["exclusions"])

    def test_percent_jump_is_measurement_warning_not_capacity_loss(self):
        r = Recording().leg("m1")
        r.rows[5]["status"]["battery"] = 1
        result = analyze(r)
        self.assertEqual(result["classification"], "needs_measurement")
        self.assertEqual(result["alerts"][0]["kind"], "charge_reading_jump")
        self.assertFalse(result["legs"][0]["comparable"])
        self.assertIsNone(result["capacity_percent"])

    def test_return_without_observed_charging_is_not_recharge(self):
        r = Recording().leg("m1")
        r.rows.pop()
        result = analyze(r)
        self.assertEqual(result["missions"][0]["battery_returns"], 1)
        self.assertEqual(result["missions"][0]["observed_recharges"], 0)

    def test_current_firmware_return_reason_zero_is_unknown_without_event(self):
        # The actual low-battery path calls goHome without assigning reason.
        result = analyze(Recording().leg("m1", reason=0, cause=None))
        self.assertEqual(result["legs"][0]["return_reason"], "unknown_return")
        self.assertFalse(result["legs"][0]["comparable"])
        self.assertEqual(result["missions"][0]["battery_returns"], 0)
        self.assertEqual(result["missions"][0]["observed_recharges"], 0)

    def test_manual_home_with_retained_battery_reason_never_counts(self):
        # Manual Home uses the same goHome function and can retain an old reason.
        for cause in ("manual", None):
            with self.subTest(cause=cause):
                result = analyze(Recording().leg("m1", reason=7, cause=cause))
                self.assertFalse(result["legs"][0]["comparable"])
                self.assertEqual(result["missions"][0]["battery_returns"], 0)
                self.assertEqual(result["missions"][0]["observed_recharges"], 0)

    def test_confirmed_battery_event_works_independently_of_status_reason(self):
        result = analyze(Recording().leg("m1", reason=0))
        self.assertTrue(result["legs"][0]["comparable"])
        self.assertEqual(result["legs"][0]["return_evidence"]["cause"], "battery")
        self.assertEqual(result["missions"][0]["observed_recharges"], 1)

    def test_unconfirmed_wrong_frame_and_stale_return_events_are_unknown(self):
        for name, value in (("confirmed", False), ("mission_id", "other"), ("boot_id", "old"),
                            ("at_ms", 0), ("provenance", "unknown"), ("cause", "unknown")):
            with self.subTest(name=name):
                r = Recording().leg("m1")
                r.rows[-2]["return_cause"][name] = value
                result = analyze(r)
                self.assertFalse(result["legs"][0]["comparable"])
                self.assertEqual(result["missions"][0]["battery_returns"], 0)

    def test_simulation_is_not_robot_health_evidence(self):
        r = Recording().leg("m1", source="simulation").leg("m1", source="simulation")
        result = analyze(r)
        self.assertEqual(result["classification"], "insufficient_data")
        self.assertFalse(result["alerts"])
        self.assertFalse(result["comparisons"])

    def test_persistent_log_replay_cli_and_invalid_line(self):
        r = Recording().leg("m1").leg("m2").leg("m3", seconds=20)
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "battery.jsonl"
            config = Path(folder) / "policy.json"
            log.write_text("\n".join(json.dumps(row) for row in r.rows))
            config.write_text(json.dumps(asdict(policy())))
            self.assertEqual(report(log, policy()), analyze(r))
            proc = subprocess.run([sys.executable, "-m", "navigation.battery_health", str(log),
                                   "--policy", str(config)], text=True, capture_output=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(json.loads(proc.stdout)["classification"], "needs_measurement")
            log.write_text('{"schema": 1}\n')
            with self.assertRaisesRegex(ValueError, "line 1.*robot_id"):
                report(log, policy())

    def test_policy_and_schema_fail_explicitly(self):
        for changes in ({"decline_ratio": 1}, {"abrupt_ratio": float("nan")},
                        {"recent_missions": 1}, {"max_gap_ms": True}, {"max_step_pct": 0}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                policy(**changes)
        row = Recording().row()
        for field, value in (("battery", float("nan")), ("charging", "false"), ("state", True)):
            bad = deepcopy(row)
            bad["status"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                BatteryHealth(policy()).feed(bad)


if __name__ == "__main__":
    unittest.main()
