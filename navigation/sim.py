"""Deterministic differential-drive simulator; truth is confined to this module."""

import argparse
import json
import math
from .core import Mission, Observation, Pose, Grid, wrap
from .native import NativeMission

SCENARIOS = ("room", "furniture", "doorway", "battery", "dirt")


class World:
    def __init__(self, scenario="furniture"):
        if scenario not in SCENARIOS:
            raise ValueError("unknown scenario")
        self.width, self.height = 4.8, 3.6
        self.obstacles = []
        if scenario in ("furniture", "battery", "dirt"):
            self.obstacles = [(1.8, 1.4, 2.4, 2.1), (3.5, 0.6, 4.1, 1.2)]
        if scenario == "doorway":
            self.obstacles = [(2.25, 0, 2.55, 1.1), (2.25, 2.3, 2.55, 3.6)]
        self.dirt = (1.1, 1.0, 0.35) if scenario == "dirt" else None

    def collision(self, x, y, radius=0.17):
        if (
            x < radius
            or y < radius
            or x > self.width - radius
            or y > self.height - radius
        ):
            return True
        return any(
            math.hypot(x - max(a, min(c, x)), y - max(b, min(d, y))) < radius
            for a, b, c, d in self.obstacles
        )

    def ray(self, x, y, angle):
        ux, uy = math.cos(angle), math.sin(angle)
        walls = []
        if abs(ux) > 1e-9:
            walls.append(((self.width if ux > 0 else 0) - x) / ux)
        if abs(uy) > 1e-9:
            walls.append(((self.height if uy > 0 else 0) - y) / uy)
        best = min(walls)
        for a, b, c, d in self.obstacles:
            lo, hi = 0.0, float("inf")
            for origin, direction, low, high in ((x, ux, a, c), (y, uy, b, d)):
                if abs(direction) < 1e-9:
                    if not low <= origin <= high:
                        hi = -1
                        break
                else:
                    p, q = (low - origin) / direction, (high - origin) / direction
                    lo, hi = max(lo, min(p, q)), min(hi, max(p, q))
            if hi >= lo:
                best = min(best, lo)
        # Keep outer-wall endpoints just inside the finite map.
        return max(0.001, best - 0.001)

    def scan(self, pose):
        return tuple(
            (
                i * math.tau / 180,
                self.ray(pose.x, pose.y, pose.yaw + i * math.tau / 180),
            )
            for i in range(180)
        )


class Simulation:
    def __init__(self, scenario="furniture", profile="daily", engine="native"):
        self.scenario = scenario
        self.world = World(scenario)
        self.mission = (
            NativeMission(profile) if engine == "native" else Mission(profile)
        )
        self.pose = self.mission.dock
        self.battery = 26.0 if scenario == "battery" else 90.0
        self.time, self.distance, self.collisions = 0.0, 0.0, 0
        self.docked, self.charging = False, False
        self.lidar_ok, self.localized = True, True
        self.cliff, self.bumper = False, False
        self.trace = []
        self.command = None
        self.temporary_obstacles = []
        self.dock_time = 0.0
        self.last_scan = self.world.scan(self.pose)
        self.scan_time = 0.0
        self.cleaned_truth = set()
        self.side_truth = set()
        self.truth_floor = {
            (ix, iy)
            for iy in range(144)
            for ix in range(192)
            if not self.world.collision(
                (ix + 0.5) * 0.025, (iy + 0.5) * 0.025, radius=0.0001
            )
        }

    def observation(self):
        if self.lidar_ok:
            self.last_scan = self.world.scan(self.pose)
            self.scan_time = self.time
        dirt = 0
        if (
            self.world.dirt
            and math.dist((self.pose.x, self.pose.y), self.world.dirt[:2])
            < self.world.dirt[2]
        ):
            dirt = 140
        return Observation(
            self.time,
            self.pose,
            self.last_scan,
            self.battery,
            self.scan_time,
            self.time,
            localized=self.localized,
            cliff=self.cliff,
            bumper=self.bumper,
            docked=self.docked,
            charging=self.charging,
            dirt=dirt,
        )

    def step(self, count=1):
        for _ in range(count):
            dt = 0.2
            self.time = round(self.time + dt, 6)
            for box, expiry in list(self.temporary_obstacles):
                if self.time >= expiry:
                    self.world.obstacles.remove(box)
                    self.temporary_obstacles.remove((box, expiry))
            self.command = self.mission.tick(self.observation())
            cmd = self.command
            if cmd.action == "drive":
                self.docked, self.charging = False, False
                # Integrate at 20 ms, collision geometry independent of planner.
                for _ in range(10):
                    yaw = wrap(self.pose.yaw + cmd.turn * dt / 10)
                    x = (
                        self.pose.x
                        + cmd.speed
                        * math.cos(self.pose.yaw + cmd.turn * dt / 20)
                        * dt
                        / 10
                    )
                    y = (
                        self.pose.y
                        + cmd.speed
                        * math.sin(self.pose.yaw + cmd.turn * dt / 20)
                        * dt
                        / 10
                    )
                    if self.world.collision(x, y):
                        self.collisions += 1
                        self.bumper = True
                        break
                    self.pose = Pose(x, y, yaw)
                    self.distance += abs(cmd.speed) * dt / 10
                    if cmd.cleaning_pwm or getattr(cmd, "side_pwm", 0):
                        self.mark_truth(cmd)
                self.battery = max(
                    0, self.battery - dt * 0.004 - abs(cmd.speed) * dt * 0.18
                )
            if cmd.action == "seek_dock":
                self.dock_time += dt
                if (
                    self.dock_time >= 2
                    and math.dist(
                        (self.pose.x, self.pose.y),
                        (self.mission.dock.x, self.mission.dock.y),
                    )
                    < 0.15
                ):
                    self.docked = self.charging = True
            else:
                self.dock_time = 0
            if self.docked and self.charging:
                self.battery = min(
                    100, self.battery + dt * 3
                )  # accelerated test charger
            if (
                len(self.trace) == 0
                or math.dist(self.trace[-1], (self.pose.x, self.pose.y)) > 0.04
            ):
                self.trace.append((self.pose.x, self.pose.y))
                self.trace = self.trace[-6000:]
            if self.mission.state in ("COMPLETE", "FAULT", "PAUSED", "READY"):
                break
        return self.state()

    def mark_truth(self, cmd):
        # Independent 2.5 cm ground-truth floor samples, never passed to planner.
        pitch = 0.025
        cs = math.cos(self.pose.yaw)
        sn = math.sin(self.pose.yaw)
        cx = int(self.pose.x / pitch)
        cy = int(self.pose.y / pitch)
        for iy in range(cy - 12, cy + 13):
            for ix in range(cx - 12, cx + 13):
                x = (ix + 0.5) * pitch
                y = (iy + 0.5) * pitch
                if (ix, iy) not in self.truth_floor:
                    continue
                dx = x - self.pose.x
                dy = y - self.pose.y
                forward = dx * cs + dy * sn
                left = -dx * sn + dy * cs
                if cmd.cleaning_pwm and abs(forward) <= 0.02 and abs(left) <= 0.08:
                    self.cleaned_truth.add((ix, iy))
                if (
                    getattr(cmd, "side_pwm", cmd.cleaning_pwm)
                    and math.hypot(forward - 0.10, left + 0.13) <= 0.10
                ):
                    self.side_truth.add((ix, iy))

    def inject(self, name):
        if name == "battery":
            self.battery = 18
        elif name == "lidar":
            self.lidar_ok = not self.lidar_ok
        elif name == "localization":
            self.localized = not self.localized
        elif name == "cliff":
            self.cliff = not self.cliff
        elif name in ("obstacle", "moving_obstacle"):
            # Place a 30 cm object ahead, outside the robot footprint.
            x = self.pose.x + 0.65 * math.cos(self.pose.yaw)
            y = self.pose.y + 0.65 * math.sin(self.pose.yaw)
            box = (x - 0.15, y - 0.15, x + 0.15, y + 0.15)
            self.world.obstacles.append(box)
            if name == "moving_obstacle":
                self.temporary_obstacles.append((box, self.time + 20))
                return  # floor under a person remains part of the cleaning denominator
            self.truth_floor = {
                p
                for p in self.truth_floor
                if not self.world.collision(
                    (p[0] + 0.5) * 0.025, (p[1] + 0.5) * 0.025, radius=0.0001
                )
            }
        else:
            raise ValueError("unknown fault")

    def state(self):
        m = self.mission
        navigable_truth = self.truth_floor
        reachable = m.grid.safe
        return {
            "scenario": self.scenario,
            "profile": m.profile,
            "state": m.state,
            "reason": m.reason,
            "time": round(self.time, 1),
            "pose": vars(self.pose),
            "battery": round(self.battery, 1),
            "distance": round(self.distance, 2),
            "collisions": self.collisions,
            "pass": m.pass_index + 1,
            "passes": len(m.covered),
            "coverage": (
                round(
                    100
                    * len(
                        set(getattr(m, "fine_covered", [[]])[m.pass_index])
                        & {
                            c * 4 + sub
                            for c, v in enumerate(m.grid.cells)
                            if v == -1
                            for sub in range(4)
                        }
                    )
                    / max(1, 4 * sum(v == -1 for v in m.grid.cells)),
                    1,
                )
                if isinstance(m, NativeMission)
                else round(
                    100
                    * len(m.covered[m.pass_index] & reachable)
                    / max(1, len(reachable)),
                    1,
                )
            ),
            "truth_coverage": round(
                100
                * len(self.cleaned_truth & navigable_truth)
                / max(1, len(navigable_truth)),
                1,
            ),
            "truth_combined": round(
                100
                * len((self.cleaned_truth | self.side_truth) & navigable_truth)
                / max(1, len(navigable_truth)),
                1,
            ),
            "fine_covered": (
                getattr(m, "fine_covered", [[]])[m.pass_index]
                if isinstance(m, NativeMission)
                else []
            ),
            "side_covered": getattr(m, "side_covered", []),
            "contacts": getattr(m, "contacts", []),
            "cleared_obstacles": getattr(m, "cleared_obstacles", 0),
            "obstacle_waits": getattr(m, "obstacle_waits", 0),
            "temporary_obstacles": len(self.temporary_obstacles),
            "room": getattr(m, "room", []),
            "portals": getattr(m, "portals", []),
            "rooms_completed": getattr(m, "rooms", 0),
            "edge_incomplete": getattr(m, "edge_incomplete", 0),
            "pass_axis": "horizontal" if m.pass_index == 0 else "vertical",
            "pending_main_m2": (
                round(
                    (
                        4 * sum(v == -1 for v in m.grid.cells)
                        - len(m.fine_covered[m.pass_index])
                    )
                    * 0.0025,
                    2,
                )
                if isinstance(m, NativeMission)
                else None
            ),
            "engine": (
                "native C++" if isinstance(m, NativeMission) else "Python reference"
            ),
            "geometry": {
                "diameter": 0.34,
                "brush_width": 0.16,
                "brush_depth": 0.04,
                "side_x": 0.10,
                "side_y": -0.13,
                "side_radius": 0.10,
                "calibrated": False,
            },
            "cells": m.grid.cells,
            "cols": m.grid.cols,
            "rows": m.grid.rows,
            "resolution": m.grid.resolution,
            "safe": sorted(reachable),
            "covered": sorted(m.covered[m.pass_index]),
            "path": [m.grid.center(c) for c in m.path],
            "checkpoint": (
                m.grid.center(m.checkpoint) if m.checkpoint is not None else None
            ),
            "dock": vars(m.dock),
            "events": m.events,
            "trace": self.trace,
            "obstacles": self.world.obstacles,
            "dirt": self.world.dirt,
            "flags": {
                "lidar": self.lidar_ok,
                "localization": self.localized,
                "cliff": self.cliff,
            },
            "command": vars(self.command) if self.command else None,
        }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=SCENARIOS, default="furniture")
    parser.add_argument("--profile", choices=("daily", "deep"), default="daily")
    parser.add_argument("--steps", type=int, default=20000)
    parser.add_argument("--output")
    args = parser.parse_args()
    sim = Simulation(args.scenario, args.profile)
    sim.mission.start()
    for step in range(0, args.steps, 100):
        sim.step(min(100, args.steps - step))
        if sim.mission.state in ("COMPLETE", "FAULT"):
            break
    state = sim.state()
    if args.output:
        with open(args.output, "w") as handle:
            json.dump(state, handle)
    print(
        json.dumps(
            {
                k: state[k]
                for k in (
                    "scenario",
                    "profile",
                    "state",
                    "reason",
                    "time",
                    "distance",
                    "coverage",
                    "truth_coverage",
                    "collisions",
                    "events",
                )
            },
            indent=2,
        )
    )
    return 0 if sim.mission.state == "COMPLETE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
