"""Adapter/simulator integration; exhaustive navigation physics is tested in C++."""

import unittest
from navigation.sim import Simulation


class NativeSimulatorTests(unittest.TestCase):
    def charging_fixture(self):
        sim = Simulation("battery")
        sim.mission.start()
        for _ in range(800):
            sim.step()
            if sim.mission.state in ("CHARGING", "FAULT"):
                break
        self.assertEqual(sim.mission.state, "CHARGING", sim.mission.reason)
        self.assertEqual(sim.collisions, 0)
        self.assertEqual(sim.dock_brush_contacts, 0)
        return sim

    def test_real_core_charge_return_resume_and_cause(self):
        sim = self.charging_fixture()
        self.assertEqual(sim.mission.return_cause, "battery")
        before = set(sim.mission.fine_covered[0])
        active = sim.mission.active_ms
        sim.step(50)
        self.assertEqual(sim.mission.state, "CHARGING")
        self.assertEqual(sim.mission.active_ms, active)
        for _ in range(600):
            sim.step()
            if sim.mission.state in ("CLEANING", "FAULT"):
                break
        self.assertEqual(sim.mission.state, "CLEANING", sim.mission.reason)
        self.assertTrue(before <= set(sim.mission.fine_covered[0]))
        self.assertGreater(sim.battery, 95)
        self.assertEqual(sim.collisions, 0)
        self.assertEqual(sim.dock_brush_contacts, 0)

    def test_long_charging_does_not_exhaust_active_budget(self):
        sim = self.charging_fixture()
        sim.charge_rate = 0
        active = sim.mission.active_ms
        sim.time += 3 * 60 * 60
        sim.step()
        self.assertEqual(sim.mission.state, "CHARGING")
        self.assertEqual(sim.mission.active_ms, active)
        self.assertEqual(sim.command.speed, 0)

    def test_pause_during_reverse_exit_cancels_motion(self):
        sim = self.charging_fixture()
        sim.battery = 100
        sim.step(3)
        self.assertEqual(sim.mission.state, "RESUMING")
        self.assertLess(sim.command.speed, 0)
        sim.mission.pause()
        pose = sim.pose
        sim.step(5)
        self.assertEqual(sim.mission.state, "PAUSED")
        self.assertEqual(sim.pose, pose)
        self.assertEqual(sim.command.speed, 0)

    def test_full_percentage_cannot_override_waiting_or_reconditioning(self):
        for field in ("charger_waiting", "charger_reconditioning"):
            with self.subTest(field=field):
                sim = self.charging_fixture()
                sim.battery = 100
                setattr(sim, field, True)
                sim.step(10)
                self.assertEqual(sim.mission.state, "CHARGING")
                self.assertEqual(sim.command.speed, 0)

    def test_charger_fault_and_missing_packet_hold_motion(self):
        for field in ("charger_fault", "charger_valid"):
            with self.subTest(field=field):
                sim = self.charging_fixture()
                sim.inject(field)
                sim.step()
                self.assertEqual(sim.mission.state, "FAULT")
                self.assertEqual(sim.command.speed, 0)
                self.assertEqual(sim.command.cleaning_pwm, 0)

    def test_unexpected_charge_and_moved_base_stop(self):
        for event in ("accidental_charge", "dock_moved"):
            with self.subTest(event=event):
                sim = Simulation("room")
                sim.mission.start()
                sim.step(10)
                sim.inject(event)
                sim.step()
                self.assertEqual(sim.mission.state, "FAULT")
                self.assertEqual(sim.command.speed, 0)
                self.assertEqual(sim.command.cleaning_pwm, 0)

    def test_manual_return_is_explicit_and_new_mission_reuses_occupancy(self):
        sim = Simulation("room")
        sim.mission.start()
        sim.step(20)
        cells = list(sim.mission.grid.cells)
        self.assertTrue(sim.mission.fine_covered[0])
        sim.mission.return_home(sim.pose)
        self.assertEqual(sim.mission.return_cause, "manual")
        self.assertGreater(sim.mission.return_at_ms, 0)
        sim.mission.start()
        self.assertEqual(sim.mission.grid.cells, cells)
        self.assertFalse(sim.mission.fine_covered[0])

    def test_ui_runs_production_core_and_does_not_count_body_disk(self):
        sim = Simulation("room")
        self.assertEqual(sim.state()["engine"], "native C++")
        sim.mission.start()
        sim.step(15)
        state = sim.state()
        self.assertTrue(state["fine_covered"])
        self.assertFalse(state["geometry"]["calibrated"])
        self.assertEqual(state["geometry"]["brush_width"], 0.16)
        self.assertLess(state["coverage"], 2)
        self.assertEqual(state["collisions"], 0)
        self.assertGreaterEqual(state["truth_combined"], state["truth_coverage"])

    def test_lidar_loss_stops_production_core(self):
        sim = Simulation()
        sim.mission.start()
        sim.step(30)
        sim.inject("lidar")
        sim.step(10)
        self.assertEqual(sim.mission.state, "FAULT")
        self.assertEqual(sim.command.speed, 0)
        self.assertEqual(sim.command.cleaning_pwm, 0)
        self.assertEqual(sim.command.side_pwm, 0)

    def test_pause_preserves_independent_pass_masks(self):
        sim = Simulation("room", "deep")
        sim.mission.start()
        sim.step(20)
        before = [set(p) for p in sim.mission.fine_covered]
        self.assertTrue(before[0])
        self.assertFalse(before[1])
        sim.mission.pause()
        sim.step(5)
        self.assertEqual(before, [set(p) for p in sim.mission.fine_covered])
        self.assertEqual(sim.command.speed, 0)
        self.assertEqual(sim.mission.snapshot()["version"], 2)

    def test_new_obstacle_stops_without_collision(self):
        sim = Simulation("room")
        sim.mission.start()
        sim.step(50)
        sim.inject("obstacle")
        sim.step(100)
        self.assertEqual(sim.collisions, 0)

    def test_temporary_obstacle_does_not_remove_cleaning_target_floor(self):
        sim = Simulation("room")
        sim.mission.start()
        sim.step(50)
        floor = set(sim.truth_floor)
        sim.inject("moving_obstacle")
        self.assertEqual(sim.state()["temporary_obstacles"], 1)
        self.assertEqual(sim.truth_floor, floor)
        sim.step(101)
        self.assertEqual(sim.state()["temporary_obstacles"], 0)
        self.assertEqual(sim.truth_floor, floor)
        self.assertEqual(sim.collisions, 0)
        self.assertNotEqual(sim.mission.state, "FAULT")

    def test_dirt_never_changes_motion_or_pwm_before_physical_calibration(self):
        clean = Simulation("room")
        dirty = Simulation("room")
        dirty.world.dirt = (0.675, 0.675, 10.0)
        clean.mission.start()
        dirty.mission.start()
        for _ in range(5):
            clean.step(20)
            dirty.step(20)
            self.assertEqual(clean.pose, dirty.pose)
            self.assertEqual(clean.command, dirty.command)
            self.assertEqual(clean.mission.state, dirty.mission.state)
