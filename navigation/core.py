"""Shared navigation engine: observations in, bounded motion intentions out.

SI units, counter-clockwise radians, map origin at bottom left. The caller owns
localization and timestamps; this is a coverage planner, not a SLAM estimator.
"""
from dataclasses import dataclass
from collections import deque
import json
import math
import os
import tempfile


@dataclass(frozen=True)
class Pose:
    x: float
    y: float
    yaw: float = 0.0


@dataclass(frozen=True)
class Observation:
    time: float
    pose: Pose
    scan: tuple  # (angle relative to robot, range metres); finite returns only
    battery: float
    scan_time: float
    pose_time: float
    localized: bool = True
    cliff: bool = False
    wheel_drop: bool = False
    bumper: bool = False
    docked: bool = False
    charging: bool = False
    dirt: int = 0
    frame: str = "home"


@dataclass(frozen=True)
class Command:
    speed: float = 0.0
    turn: float = 0.0
    cleaning_pwm: int = 0
    action: str = "stop"  # drive, stop, seek_dock, charge

    def wheels(self, separation=0.235):
        return (round(1000 * (self.speed - self.turn * separation / 2)),
                round(1000 * (self.speed + self.turn * separation / 2)))


PROFILES = {
    "daily": {"speed": .22, "pwm": 85, "passes": 1},
    "deep": {"speed": .14, "pwm": 127, "passes": 2},
}


def wrap(angle):
    return (angle + math.pi) % (2 * math.pi) - math.pi


class Grid:
    def __init__(self, width=4.8, height=3.6, resolution=.15):
        self.resolution = resolution
        self.cols = round(width / resolution)
        self.rows = round(height / resolution)
        self.width, self.height = self.cols * resolution, self.rows * resolution
        self.cells = [0] * (self.cols * self.rows)  # 0 unknown, -1 free, 1 occupied
        self.safe = set()

    def cell(self, x, y):
        if not (0 <= x < self.width and 0 <= y < self.height):
            return None
        return int(y / self.resolution) * self.cols + int(x / self.resolution)

    def center(self, c):
        return ((c % self.cols + .5) * self.resolution,
                (c // self.cols + .5) * self.resolution)

    def neighbors(self, c):
        x, y = c % self.cols, c // self.cols
        for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
            if 0 <= x + dx < self.cols and 0 <= y + dy < self.rows:
                yield (y + dy) * self.cols + x + dx

    def observe(self, pose, scan):
        # Accumulate free and endpoint evidence separately: a free ray cannot
        # erase another ray's occupied endpoint in the same scan.
        free, hits = set(), set()
        for angle, distance in scan:
            if not (math.isfinite(angle) and math.isfinite(distance)
                    and .05 <= distance <= 8):
                continue
            a = pose.yaw + angle
            ux, uy = math.cos(a), math.sin(a)
            for step in range(max(0, int((distance - self.resolution) / (self.resolution / 3)))):
                d = step * self.resolution / 3
                c = self.cell(pose.x + ux * d, pose.y + uy * d)
                if c is not None:
                    free.add(c)
            c = self.cell(pose.x + ux * distance, pose.y + uy * distance)
            if c is not None:
                hits.add(c)
        for c in free - hits:
            if self.cells[c] != 1:
                self.cells[c] = -1
        for c in hits:
            self.cells[c] = 1
        # Circular footprint plus uncertainty margin and half cell diagonal.
        radius = .18 + .04 + self.resolution * math.sqrt(2) / 2
        offsets = [(dx, dy) for dx in range(-3, 4) for dy in range(-3, 4)
                   if math.hypot(dx, dy) * self.resolution <= radius]
        self.safe = set()
        for c, value in enumerate(self.cells):
            if value != -1:
                continue
            x, y = c % self.cols, c // self.cols
            if all(0 <= x + dx < self.cols and 0 <= y + dy < self.rows
                   and self.cells[(y + dy) * self.cols + x + dx] == -1
                   for dx, dy in offsets):
                self.safe.add(c)

    def route(self, start, targets):
        if start not in self.safe:
            return None
        queue, parent = deque([start]), {start: None}
        while queue:
            c = queue.popleft()
            if c in targets:
                path = []
                while c is not None:
                    path.append(c)
                    c = parent[c]
                return path[::-1]
            for n in self.neighbors(c):
                if n in self.safe and n not in parent:
                    parent[n] = c
                    queue.append(n)
        return None


class Mission:
    def __init__(self, profile="daily", grid=None, dock=Pose(.675, .675), frame="home"):
        if profile not in PROFILES:
            raise ValueError("unknown profile")
        self.profile = profile
        self.grid = grid or Grid()
        self.dock, self.frame = dock, frame
        self.state, self.reason = "READY", ""
        self.pass_index = 0
        self.covered = [set() for _ in range(PROFILES[profile]["passes"])]
        self.path = []
        self.checkpoint = None
        self.events = []
        self.time = 0.0
        self.finished = False
        self.dirty_zones = []
        self.extra = deque()
        self.last_clean = None
        self.last_time = None
        self.dock_started = 0.0
        self.restore_pending = False
        self.resume_state = "CLEANING"
        self.progress_at = 0.0
        self.progress_count = 0

    def event(self, name):
        self.events.append({"time": round(self.time, 1), "message": name})
        self.events = self.events[-100:]

    def transition(self, state, reason=""):
        if self.state != state or self.reason != reason:
            self.state, self.reason = state, reason
            self.progress_at = self.time
            self.path = []
            self.event(state + (": " + reason if reason else ""))

    def start(self):
        if self.state == "READY":
            self.transition("CLEANING")

    def pause(self):
        if self.state not in ("PAUSED", "FAULT", "READY", "COMPLETE"):
            self.resume_state = self.state
            self.transition("PAUSED", "operator")

    def resume(self, observation):
        # Explicit acknowledgement, with a current pose in the original map.
        if self.state not in ("PAUSED", "FAULT"):
            return False
        if self.invalid(observation):
            return False
        self.restore_pending = False
        self.last_time = None
        self.transition(self.resume_state)
        return True

    def invalid(self, obs):
        values = (obs.time, obs.scan_time, obs.pose_time, obs.pose.x,
                  obs.pose.y, obs.pose.yaw, obs.battery)
        if not all(math.isfinite(x) for x in values) or not 0 <= obs.battery <= 100:
            return "invalid telemetry"
        if not obs.localized or obs.frame != self.frame:
            return "localization lost / map frame mismatch"
        if self.grid.cell(obs.pose.x, obs.pose.y) is None:
            return "pose outside map"
        if not 0 <= obs.time - obs.pose_time <= .5:
            return "stale localization"
        if not 0 <= obs.time - obs.scan_time <= .5 or len(obs.scan) < 12:
            return "stale or missing LiDAR"
        if sum(math.isfinite(a) and math.isfinite(d) and .05 <= d <= 8
               for a, d in obs.scan) < 12:
            return "invalid LiDAR"
        if obs.cliff or obs.wheel_drop or obs.bumper:
            return "cliff / wheel drop / bumper"
        if obs.battery <= 6 and not (obs.docked and obs.charging):
            return "critical battery"
        return ""

    def return_home(self, pose, finished=False):
        self.checkpoint = self.grid.cell(pose.x, pose.y)
        self.finished = finished
        self.extra.clear()
        self.transition("RETURNING", "finished" if finished else "battery / operator")

    def _follow(self, obs, targets, pwm=0):
        current = self.grid.cell(obs.pose.x, obs.pose.y)
        if not self.path or self.path[-1] not in targets or any(c not in self.grid.safe for c in self.path):
            self.path = self.grid.route(current, targets) or []
        if not self.path:
            self.resume_state = self.state
            self.transition("FAULT", "no safe route")
            return Command(), False
        while self.path:
            x, y = self.grid.center(self.path[0])
            distance = math.hypot(x - obs.pose.x, y - obs.pose.y)
            if distance >= .025:
                break
            self.path.pop(0)
        if not self.path:
            return Command(), True
        error = wrap(math.atan2(y - obs.pose.y, x - obs.pose.x) - obs.pose.yaw)
        turn = max(-1.2, min(1.2, error * 3))
        speed = min(PROFILES[self.profile]["speed"], distance * 2.5) if abs(error) < .2 else 0
        # Independent near-field check; rotating a circular footprint needs no
        # forward clearance. Real bumper/cliff safety is still required.
        if speed and any(abs(wrap(a)) < .55 and .05 <= d < .29 for a, d in obs.scan):
            self.resume_state = self.state
            self.transition("FAULT", "obstacle inside stopping margin")
            return Command(), False
        return Command(speed, turn, pwm, "drive"), False

    def tick(self, obs):
        self.time = obs.time
        if self.state in ("READY", "PAUSED", "FAULT", "COMPLETE"):
            return Command(action="charge" if self.state == "COMPLETE" and obs.docked else "stop")
        invalid = self.invalid(obs)
        if self.last_time is not None and obs.time <= self.last_time:
            invalid = "non-monotonic telemetry"
        self.last_time = obs.time
        if invalid:
            self.resume_state = self.state
            self.transition("FAULT", invalid)
            return Command()
        self.grid.observe(obs.pose, obs.scan)
        current = self.grid.cell(obs.pose.x, obs.pose.y)
        dock_cell = self.grid.cell(self.dock.x, self.dock.y)
        if self.state in ("CLEANING", "RESUMING"):
            route = self.grid.route(current, {dock_cell})
            # Initial conservative model, must be calibrated from real logs.
            reserve = max(20, (len(route) * self.grid.resolution * .25 + 8) if route else 100)
            if obs.battery <= reserve:
                self.return_home(obs.pose)
        if self.state == "RETURNING":
            command, reached = self._follow(obs, {dock_cell})
            if reached:
                self.dock_started = obs.time
                self.transition("DOCKING")
                return Command(action="seek_dock")
            return command
        if self.state == "DOCKING":
            if obs.docked and obs.charging:
                self.transition("COMPLETE" if self.finished else "CHARGING")
                return Command(action="charge")
            if obs.time - self.dock_started > 60:
                self.resume_state = "RETURNING"
                self.transition("FAULT", "docking timeout")
                return Command()
            return Command(action="seek_dock")
        if self.state == "CHARGING":
            if not obs.docked or not obs.charging:
                self.resume_state = "CHARGING"
                self.transition("FAULT", "charging contact lost")
            elif obs.battery >= 80:
                self.transition("RESUMING")
            return Command(action="charge" if self.state == "CHARGING" else "stop")
        if self.state == "RESUMING":
            command, reached = self._follow(obs, {self.checkpoint})
            if reached:
                self.transition("CLEANING", "resumed checkpoint")
            return command
        # Coverage counts a footprint once per pass, not time spent stationary.
        if self.last_clean is None or math.hypot(obs.pose.x - self.last_clean.x, obs.pose.y - self.last_clean.y) >= .04:
            for c in self.grid.safe:
                x, y = self.grid.center(c)
                if math.hypot(x - obs.pose.x, y - obs.pose.y) <= .16:
                    self.covered[self.pass_index].add(c)
            self.last_clean = obs.pose
        count = sum(len(cells) for cells in self.covered)
        if count > self.progress_count:
            self.progress_count, self.progress_at = count, obs.time
        elif obs.time - self.progress_at > 60:
            self.resume_state = "CLEANING"
            self.transition("FAULT", "no coverage progress within 60 seconds")
            return Command()
        # Dirt response is disabled pending physical calibration, including this reference engine.
        targets = self.grid.safe - self.covered[self.pass_index]
        if self.grid.route(current, targets) is None:
            if current not in self.grid.safe:
                self.resume_state = "CLEANING"
                self.transition("FAULT", "robot footprint not observed free")
                return Command()
            if self.pass_index + 1 < len(self.covered):
                self.pass_index += 1
                self.last_clean = None
                self.path = []
                self.event("PASS: " + str(self.pass_index + 1))
            else:
                self.return_home(obs.pose, finished=True)
            return Command()
        command, _ = self._follow(obs, targets, PROFILES[self.profile]["pwm"])
        return command

    def snapshot(self):
        return {"version": 1, "profile": self.profile, "frame": self.frame,
                "map": {"width": self.grid.width, "height": self.grid.height,
                        "resolution": self.grid.resolution, "cells": self.grid.cells},
                "dock": vars(self.dock), "state": self.state,
                "resume_state": self.resume_state, "pass_index": self.pass_index,
                "covered": [sorted(s) for s in self.covered], "checkpoint": self.checkpoint,
                "finished": self.finished, "dirty_zones": self.dirty_zones}

    def save(self, path):
        folder = os.path.dirname(os.path.abspath(path))
        fd, temp = tempfile.mkstemp(dir=folder, prefix=".mission-")
        try:
            with os.fdopen(fd, "w") as handle:
                json.dump(self.snapshot(), handle, allow_nan=False)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temp, path)
        finally:
            if os.path.exists(temp):
                os.unlink(temp)

    @classmethod
    def restore(cls, data):
        if data.get("version") != 1:
            raise ValueError("unsupported checkpoint version")
        g = data["map"]
        if not (.1 <= g["resolution"] <= .3 and 1 <= g["width"] <= 20 and 1 <= g["height"] <= 20):
            raise ValueError("invalid map dimensions")
        grid = Grid(g["width"], g["height"], g["resolution"])
        count = len(grid.cells)
        if len(g["cells"]) != count or any(v not in (-1, 0, 1) for v in g["cells"]):
            raise ValueError("invalid map cells")
        result = cls(data["profile"], grid, Pose(**data["dock"]), data["frame"])
        valid_cell = lambda c: type(c) is int and 0 <= c < count
        if len(data["covered"]) != len(result.covered) or any(not valid_cell(c) for s in data["covered"] for c in s):
            raise ValueError("invalid coverage")
        if not 0 <= data["pass_index"] < len(result.covered):
            raise ValueError("invalid pass")
        if data["checkpoint"] is not None and not valid_cell(data["checkpoint"]):
            raise ValueError("invalid checkpoint cell")
        grid.cells = list(g["cells"])
        result.covered = [set(s) for s in data["covered"]]
        result.pass_index, result.checkpoint = data["pass_index"], data["checkpoint"]
        result.finished, result.dirty_zones = bool(data["finished"]), list(data["dirty_zones"])
        state = data["state"]
        result.resume_state = data["resume_state"] if state in ("PAUSED", "FAULT") else state
        if result.resume_state not in ("CLEANING", "RETURNING", "DOCKING", "CHARGING", "RESUMING", "COMPLETE", "READY"):
            raise ValueError("invalid mission state")
        if result.resume_state == "RESUMING" and result.checkpoint is None:
            raise ValueError("missing checkpoint")
        if result.resume_state == "DOCKING":
            result.resume_state = "RETURNING"
        result.restore_pending = True
        result.transition("PAUSED", "restore requires localization in saved map and explicit resume")
        return result
