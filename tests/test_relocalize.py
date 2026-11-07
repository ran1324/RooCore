import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ros2/roocore_bridge"))
from roocore_bridge.relocalize import locate
from roocore_bridge.client import global_frame_valid
from navigation.sim import World
from navigation.core import Pose


def snapshot(world):
    cells = []
    safe = []
    res = 0.1
    for y in range(36):
        for x in range(48):
            px = (x + 0.5) * res
            py = (y + 0.5) * res
            cells.append("o" if world.collision(px, py, radius=0.051) else "f")
            safe.append("0" if world.collision(px, py, radius=0.28) else "1")
    return dict(
        cols=48,
        rows=36,
        resolution=res,
        origin_x=0,
        origin_y=0,
        cells="".join(cells),
        safe="".join(safe),
    )


class RelocalizationTests(unittest.TestCase):
    def test_provisional_pose_never_enters_global_ros_frame(self):
        self.assertTrue(global_frame_valid(dict(localized=True, global_localized=True)))
        self.assertFalse(
            global_frame_valid(
                dict(localized=True, global_localized=False, provisional=True)
            )
        )
        self.assertFalse(
            global_frame_valid(
                dict(localized=True, global_localized=True, help_pending=True)
            )
        )
        self.assertFalse(global_frame_valid({}))

    def test_repeated_rectangle_is_ambiguous(self):
        world = World("room")
        pose = Pose(1.25, 1.05, 0.2)
        self.assertIsNone(locate(snapshot(world), [r for _, r in world.scan(pose)]))

    def test_unique_furnished_geometry(self):
        world = World("furniture")
        pose = Pose(1.25, 0.85, 0.3)
        answer = locate(
            snapshot(world), [r for _, r in world.scan(pose)], max_seconds=5
        )
        self.assertIsNotNone(answer)
        self.assertLess(math.hypot(answer["x"] - pose.x, answer["y"] - pose.y), 0.16)
        self.assertLess(
            abs((answer["yaw"] - pose.yaw + math.pi) % math.tau - math.pi), 0.12
        )
        self.assertGreaterEqual(answer["score"], 0.9)
        self.assertGreaterEqual(answer["margin"], 0.1)

    def test_unknown_room_and_bad_scan_do_not_invent_pose(self):
        map_ = snapshot(World("furniture"))
        self.assertIsNone(locate(map_, [0.0] * 180))
        self.assertIsNone(locate(map_, [float("nan")] * 180))
        self.assertIsNone(locate(map_, [6.0] * 180))
        self.assertIsNone(locate(map_, [1.0] * 180, max_seconds=0))
