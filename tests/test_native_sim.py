"""Adapter/simulator integration; exhaustive navigation physics is tested in C++."""

import unittest
from navigation.sim import Simulation


class NativeSimulatorTests(unittest.TestCase):
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
