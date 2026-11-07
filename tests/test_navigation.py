import copy
from dataclasses import replace
import json
import math
from pathlib import Path
import tempfile
import unittest
from navigation.core import Command, Grid, Mission, Pose, PROFILES
from navigation.sim import Simulation as EngineSimulation, World


def Simulation(*args, **kwargs):
    return EngineSimulation(*args, **kwargs, engine="reference")


class NavigationTests(unittest.TestCase):
    def test_wheel_units_and_profiles(self):
        self.assertEqual(Command(0.2, 0).wheels(), (200, 200))
        left, right = Command(0, 1).wheels()
        self.assertLess(left, 0)
        self.assertGreater(right, 0)
        self.assertLess(PROFILES["deep"]["speed"], PROFILES["daily"]["speed"])
        self.assertGreater(PROFILES["deep"]["pwm"], PROFILES["daily"]["pwm"])
        self.assertEqual(PROFILES["deep"]["passes"], 2)

    def test_map_does_not_know_hidden_obstacles(self):
        sim = Simulation("furniture")
        self.assertEqual(set(sim.mission.grid.cells), {0})
        sim.mission.start()
        sim.step()
        self.assertIn(0, sim.mission.grid.cells)
        self.assertIn(1, sim.mission.grid.cells)
        self.assertFalse(hasattr(sim.mission, "world"))
        # Obstacle and all unknown cells are excluded from safe centers.
        self.assertTrue(
            all(sim.mission.grid.cells[c] == -1 for c in sim.mission.grid.safe)
        )
        for c in sim.mission.grid.safe:
            self.assertFalse(
                sim.world.collision(*sim.mission.grid.center(c), radius=0.22)
            )

    def test_invalid_ranges_never_create_free_space(self):
        grid = Grid()
        grid.observe(
            Pose(1, 1), ((0, float("nan")), (1, float("inf")), (2, -1), (0, 9))
        )
        self.assertEqual(set(grid.cells), {0})
        self.assertEqual(grid.safe, set())

    def test_rotation_does_not_move_wall(self):
        world = World("room")
        grid = Grid()
        for yaw in (0, 0.24, 0.48, 0.72):
            pose = Pose(2, 1, yaw)
            grid.observe(pose, world.scan(pose))
        for c, v in enumerate(grid.cells):
            if v == 1:
                x, y = grid.center(c)
                self.assertTrue(x < 0.16 or y < 0.16 or x > 4.64 or y > 3.44)

    def test_no_diagonal_corner_cutting(self):
        grid = Grid(1.2, 1.2)
        grid.safe = {0, grid.cols + 1}
        self.assertIsNone(grid.route(0, {grid.cols + 1}))
        grid.safe.add(1)
        self.assertEqual(grid.route(0, {grid.cols + 1}), [0, 1, grid.cols + 1])

    def test_faults_stop_and_require_ack(self):
        for mutation in (
            dict(localized=False),
            dict(cliff=True),
            dict(wheel_drop=True),
            dict(bumper=True),
            dict(battery=5),
            dict(battery=float("nan")),
            dict(scan_time=-1),
            dict(pose_time=-1),
            dict(frame="other"),
            dict(scan=()),
            dict(pose=Pose(float("nan"), 1)),
        ):
            with self.subTest(mutation=mutation):
                sim = Simulation("room")
                sim.mission.start()
                obs = replace(sim.observation(), **mutation)
                self.assertEqual(sim.mission.tick(obs), Command())
                self.assertEqual(sim.mission.state, "FAULT")
                self.assertFalse(sim.mission.resume(obs))
                self.assertEqual(sim.mission.tick(sim.observation()), Command())
                self.assertTrue(sim.mission.resume(sim.observation()))

    def test_no_silent_resume_after_clock_rollback(self):
        sim = Simulation()
        sim.mission.start()
        sim.step()
        sim.mission.tick(sim.observation())
        self.assertEqual(sim.mission.state, "FAULT")
        self.assertIn("monotonic", sim.mission.reason)

    def test_pause_stops_and_preserves_coverage(self):
        sim = Simulation()
        sim.mission.start()
        sim.step(100)
        before = copy.deepcopy(sim.mission.covered)
        sim.mission.pause()
        sim.step(100)
        self.assertEqual(sim.command, Command())
        self.assertEqual(before, sim.mission.covered)
        self.assertTrue(sim.mission.resume(sim.observation()))

    def test_checkpoint_atomic_and_requires_localization(self):
        sim = Simulation()
        sim.mission.start()
        sim.step(50)
        sim.mission.return_home(sim.pose)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "mission.json"
            sim.mission.save(path)
            saved = json.loads(path.read_text())
            restored = Mission.restore(saved)
        self.assertEqual(restored.covered, sim.mission.covered)
        self.assertEqual(restored.checkpoint, sim.mission.checkpoint)
        self.assertEqual(restored.state, "PAUSED")
        self.assertEqual(restored.tick(sim.observation()), Command())
        self.assertFalse(restored.resume(replace(sim.observation(), localized=False)))
        self.assertFalse(restored.resume(replace(sim.observation(), frame="new-map")))
        self.assertTrue(restored.resume(sim.observation()))
        self.assertEqual(restored.state, "RETURNING")
        for key, val in [("version", 2), ("pass_index", 99), ("checkpoint", -1)]:
            broken = copy.deepcopy(saved)
            broken[key] = val
            with self.assertRaises(ValueError):
                Mission.restore(broken)

    def test_dirt_revisit_is_bounded(self):
        sim = Simulation("room")
        sim.mission.start()
        obs = replace(sim.observation(), dirt=140)
        sim.mission.tick(obs)
        self.assertEqual(len(sim.mission.dirty_zones), 1)
        for i in range(1, 20):
            sim.mission.tick(
                replace(obs, time=i * 0.2, scan_time=i * 0.2, pose_time=i * 0.2)
            )
        self.assertEqual(len(sim.mission.dirty_zones), 1)
        self.assertLessEqual(len(sim.mission.extra), 5)

    def test_new_obstacle_never_causes_collision(self):
        sim = Simulation("room")
        sim.mission.start()
        sim.step(100)
        sim.inject("obstacle")
        sim.step(250)
        self.assertEqual(sim.collisions, 0)

    def test_stale_lidar_and_localization_in_simulator(self):
        for name in ("lidar", "localization", "cliff"):
            sim = Simulation()
            sim.mission.start()
            sim.step(10)
            sim.inject(name)
            sim.step(10)
            self.assertEqual(sim.mission.state, "FAULT")
            self.assertEqual(sim.command, Command())
            self.assertEqual(sim.collisions, 0)

    def test_charging_contact_loss_and_docking_timeout(self):
        sim = Simulation()
        sim.mission.state = "CHARGING"
        sim.step()
        self.assertEqual(sim.mission.state, "FAULT")
        sim = Simulation()
        sim.mission.state = "DOCKING"
        sim.time = 61
        sim.step()
        self.assertEqual(sim.mission.state, "FAULT")
        self.assertEqual(sim.command, Command())

    def test_low_battery_charges_and_resumes_original_point(self):
        sim = Simulation("room")
        sim.mission.start()
        sim.step(200)
        covered = copy.deepcopy(sim.mission.covered)
        point = sim.mission.grid.cell(sim.pose.x, sim.pose.y)
        sim.inject("battery")
        seen = set()
        for i in range(2000):
            sim.step()
            seen.add(sim.mission.state)
            self.assertNotEqual(sim.mission.state, "FAULT", sim.mission.reason)
            if "RESUMING" in seen and sim.mission.state == "CLEANING":
                break
        self.assertTrue(
            {"RETURNING", "DOCKING", "CHARGING", "RESUMING", "CLEANING"} <= seen
        )
        self.assertEqual(sim.mission.checkpoint, point)
        self.assertTrue(covered[0] <= sim.mission.covered[0])
        self.assertLess(
            math.dist((sim.pose.x, sim.pose.y), sim.mission.grid.center(point)), 0.04
        )
        self.assertEqual(sim.collisions, 0)


if __name__ == "__main__":
    unittest.main()
