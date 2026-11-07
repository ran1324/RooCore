"""Deterministic differential-drive simulator; truth is confined to this module."""
import argparse
import json
import math
from .core import Mission, Observation, Pose, Grid, wrap

SCENARIOS = ("room", "furniture", "doorway", "battery", "dirt")


class World:
    def __init__(self, scenario="furniture"):
        if scenario not in SCENARIOS:
            raise ValueError("unknown scenario")
        self.width, self.height = 4.8, 3.6
        self.obstacles = []
        if scenario in ("furniture", "battery", "dirt"):
            self.obstacles = [(1.8, 1.4, 2.4, 2.1), (3.5, .6, 4.1, 1.2)]
        if scenario == "doorway":
            self.obstacles = [(2.25, 0, 2.55, 1.1), (2.25, 2.3, 2.55, 3.6)]
        self.dirt = (1.1, 1.0, .35) if scenario == "dirt" else None

    def collision(self, x, y, radius=.18):
        if x < radius or y < radius or x > self.width - radius or y > self.height - radius:
            return True
        return any(math.hypot(x - max(a, min(c, x)), y - max(b, min(d, y))) < radius
                   for a, b, c, d in self.obstacles)

    def ray(self, x, y, angle):
        ux, uy = math.cos(angle), math.sin(angle)
        walls = []
        if abs(ux) > 1e-9:
            walls.append(((self.width if ux > 0 else 0) - x) / ux)
        if abs(uy) > 1e-9:
            walls.append(((self.height if uy > 0 else 0) - y) / uy)
        best = min(walls)
        for a, b, c, d in self.obstacles:
            lo, hi = 0., float("inf")
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
        return max(.001, best - .001)

    def scan(self, pose):
        return tuple((i * math.tau / 180, self.ray(pose.x, pose.y, pose.yaw + i * math.tau / 180))
                     for i in range(180))


class Simulation:
    def __init__(self, scenario="furniture", profile="daily"):
        self.scenario = scenario
        self.world = World(scenario)
        self.mission = Mission(profile)
        self.pose = self.mission.dock
        self.battery = 26. if scenario == "battery" else 90.
        self.time, self.distance, self.collisions = 0., 0., 0
        self.docked, self.charging = False, False
        self.lidar_ok, self.localized = True, True
        self.cliff, self.bumper = False, False
        self.trace = []
        self.command = None
        self.dock_time = 0.
        self.last_scan = self.world.scan(self.pose)
        self.scan_time = 0.
        self.cleaned_truth = set()

    def observation(self):
        if self.lidar_ok:
            self.last_scan = self.world.scan(self.pose)
            self.scan_time = self.time
        dirt = 0
        if self.world.dirt and math.dist((self.pose.x, self.pose.y), self.world.dirt[:2]) < self.world.dirt[2]:
            dirt = 140
        return Observation(self.time, self.pose, self.last_scan, self.battery,
                           self.scan_time, self.time, localized=self.localized,
                           cliff=self.cliff, bumper=self.bumper, docked=self.docked,
                           charging=self.charging, dirt=dirt)

    def step(self, count=1):
        for _ in range(count):
            dt = .2
            self.time = round(self.time + dt, 6)
            self.command = self.mission.tick(self.observation())
            cmd = self.command
            if cmd.action == "drive":
                self.docked, self.charging = False, False
                # Integrate at 20 ms, collision geometry independent of planner.
                for _ in range(10):
                    yaw = wrap(self.pose.yaw + cmd.turn * dt / 10)
                    x = self.pose.x + cmd.speed * math.cos(self.pose.yaw + cmd.turn * dt / 20) * dt / 10
                    y = self.pose.y + cmd.speed * math.sin(self.pose.yaw + cmd.turn * dt / 20) * dt / 10
                    if self.world.collision(x, y):
                        self.collisions += 1
                        self.bumper = True
                        break
                    self.pose = Pose(x, y, yaw)
                    self.distance += abs(cmd.speed) * dt / 10
                self.battery = max(0, self.battery - dt * .004 - abs(cmd.speed) * dt * .18)
                if cmd.cleaning_pwm:
                    # Ground-truth coverage metric, never sent to Mission.
                    for c in range(len(self.mission.grid.cells)):
                        x, y = self.mission.grid.center(c)
                        if math.hypot(x - self.pose.x, y - self.pose.y) < .18:
                            self.cleaned_truth.add(c)
            if cmd.action == "seek_dock":
                self.dock_time += dt
                if self.dock_time >= 2 and math.dist((self.pose.x, self.pose.y), (self.mission.dock.x, self.mission.dock.y)) < .15:
                    self.docked = self.charging = True
            else:
                self.dock_time = 0
            if self.docked and self.charging:
                self.battery = min(100, self.battery + dt * 3)  # accelerated test charger
            if len(self.trace) == 0 or math.dist(self.trace[-1], (self.pose.x, self.pose.y)) > .04:
                self.trace.append((self.pose.x, self.pose.y))
                self.trace = self.trace[-6000:]
            if self.mission.state in ("COMPLETE", "FAULT", "PAUSED", "READY"):
                break
        return self.state()

    def inject(self, name):
        if name == "battery":
            self.battery = 18
        elif name == "lidar":
            self.lidar_ok = not self.lidar_ok
        elif name == "localization":
            self.localized = not self.localized
        elif name == "cliff":
            self.cliff = not self.cliff
        elif name == "obstacle":
            # Place a 30 cm object ahead, outside the robot footprint.
            x = self.pose.x + .65 * math.cos(self.pose.yaw)
            y = self.pose.y + .65 * math.sin(self.pose.yaw)
            self.world.obstacles.append((x - .15, y - .15, x + .15, y + .15))
        else:
            raise ValueError("unknown fault")

    def state(self):
        m = self.mission
        navigable_truth = {c for c in range(len(m.grid.cells))
                           if not self.world.collision(*m.grid.center(c), radius=.23)}
        reachable = m.grid.safe
        return {"scenario": self.scenario, "profile": m.profile, "state": m.state,
                "reason": m.reason, "time": round(self.time, 1), "pose": vars(self.pose),
                "battery": round(self.battery, 1), "distance": round(self.distance, 2),
                "collisions": self.collisions, "pass": m.pass_index + 1,
                "passes": len(m.covered), "coverage": round(100 * len(m.covered[m.pass_index] & reachable) / max(1, len(reachable)), 1),
                "truth_coverage": round(100 * len(self.cleaned_truth & navigable_truth) / max(1, len(navigable_truth)), 1),
                "cells": m.grid.cells, "cols": m.grid.cols, "rows": m.grid.rows,
                "resolution": m.grid.resolution, "safe": sorted(reachable),
                "covered": sorted(m.covered[m.pass_index]), "path": [m.grid.center(c) for c in m.path],
                "checkpoint": m.grid.center(m.checkpoint) if m.checkpoint is not None else None,
                "dock": vars(m.dock), "events": m.events, "trace": self.trace,
                "obstacles": self.world.obstacles, "dirt": self.world.dirt,
                "flags": {"lidar": self.lidar_ok, "localization": self.localized, "cliff": self.cliff},
                "command": vars(self.command) if self.command else None}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=SCENARIOS, default="furniture")
    parser.add_argument("--profile", choices=("daily", "deep"), default="daily")
    parser.add_argument("--steps", type=int, default=20000)
    parser.add_argument("--output")
    args = parser.parse_args()
    sim = Simulation(args.scenario, args.profile)
    sim.mission.start()
    for _ in range(args.steps):
        sim.step()
        if sim.mission.state in ("COMPLETE", "FAULT"):
            break
    state = sim.state()
    if args.output:
        with open(args.output, "w") as handle:
            json.dump(state, handle)
    print(json.dumps({k: state[k] for k in ("scenario", "profile", "state", "reason", "time", "distance", "coverage", "truth_coverage", "collisions", "events")}, indent=2))
    return 0 if sim.mission.state == "COMPLETE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
