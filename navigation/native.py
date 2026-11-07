"""ctypes adapter to the production C++ navigation library, built locally once."""

import ctypes as C
import hashlib
from pathlib import Path
import subprocess
import sys
from dataclasses import dataclass
from .core import Pose, Grid

ROOT = Path(__file__).resolve().parents[1]
STATES = (
    "READY",
    "CLEANING",
    "RETURNING",
    "DOCKING",
    "CHARGING",
    "RESUMING",
    "SETTLING",
    "RECOVERING",
    "PAUSED",
    "FAULT",
    "COMPLETE",
    "EDGING",
    "TOUCHUP",
)
REASONS = (
    "",
    "telemetry stale or invalid",
    "localization lost",
    "cliff",
    "wheel drop",
    "bumper",
    "overcurrent",
    "battery",
    "no safe route",
    "no progress",
    "recovery exhausted",
    "dock timeout",
    "charge lost",
    "map boundary",
    "operator",
    "restore",
)
LIB = None


def library():
    global LIB
    if LIB is not None:
        return LIB
    sources = [
        ROOT / "navigation/native_bridge.cpp",
        ROOT / "lib/RooNavigation/src/RooNavigation.h",
    ]
    digest = hashlib.sha256(b"".join(p.read_bytes() for p in sources)).hexdigest()[:16]
    folder = ROOT / ".roocore-build"
    folder.mkdir(exist_ok=True)
    target = folder / (
        "navigation-" + digest + (".dylib" if sys.platform == "darwin" else ".so")
    )
    if not target.exists():
        subprocess.run(
            [
                "clang++",
                "-std=c++11",
                "-O2",
                "-shared",
                "-fPIC",
                "-I" + str(ROOT / "lib/RooNavigation/src"),
                str(sources[0]),
                "-o",
                str(target),
            ],
            check=True,
        )
    lib = C.CDLL(str(target))
    lib.roo_create.restype = C.c_void_p
    lib.roo_destroy.argtypes = [C.c_void_p]
    lib.roo_command.argtypes = [
        C.c_void_p,
        C.c_int,
        C.c_int,
        C.c_uint,
        C.c_float,
        C.c_float,
        C.c_float,
    ]
    lib.roo_step.argtypes = [
        C.c_void_p,
        C.POINTER(C.c_float),
        C.c_uint,
        C.c_uint,
        C.POINTER(C.c_float),
        C.c_int,
    ]
    lib.roo_status.argtypes = [
        C.c_void_p,
        C.POINTER(C.c_int),
        C.POINTER(C.c_float),
        C.POINTER(C.c_int),
    ]
    lib.roo_path.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.c_int]
    lib.roo_path.restype = C.c_int
    LIB = lib
    return lib


@dataclass(frozen=True)
class NativeCommand:
    speed: float = 0
    turn: float = 0
    cleaning_pwm: int = 0
    action: str = "stop"
    side_pwm: int = 0


class NativeMission:
    def __init__(self, profile="daily"):
        if profile not in ("daily", "deep"):
            raise ValueError("unknown profile")
        self.lib = library()
        self.handle = self.lib.roo_create()
        if not self.handle:
            raise MemoryError("navigation allocation failed")
        self.profile = profile
        self.dock = Pose(0.675, 0.675)
        self.grid = Grid(resolution=0.1)
        self.events = []
        self.now = 0
        self.command = NativeCommand()
        self._path = (C.c_int * 1728)()
        self._v = (C.c_int * 16)()
        self._f = (C.c_float * 16)()
        self._map = (C.c_int * (48 * 36 * 7))()
        self.refresh()

    def __del__(self):
        if getattr(self, "handle", None):
            self.lib.roo_destroy(self.handle)

    def refresh(self):
        old = getattr(self, "state", None)
        self.lib.roo_status(self.handle, self._v, self._f, self._map)
        v = self._v
        self.state = STATES[v[0]]
        self.reason = REASONS[v[1]]
        self.pass_index = v[2]
        self.passes = v[3]
        self.skipped = v[4]
        self.rooms = v[5]
        self.edge_segments = v[6]
        self.edge_incomplete = v[7]
        self.target = (self._f[0], self._f[1]) if v[8] >= 0 else None
        self.checkpoint = self.grid.cell(self._f[2], self._f[3])
        self.grid.cells = list(self._map[0::7])
        self.grid.safe = {i for i, x in enumerate(self._map[3::7]) if x}
        masks = self._map[1::7]
        self.covered = [
            {c for c, m in enumerate(masks) if m & (15 << (p * 4)) == (15 << (p * 4))}
            for p in range(self.passes)
        ]
        self.fine_covered = [
            [
                c * 4 + sub
                for c, m in enumerate(masks)
                for sub in range(4)
                if m & (1 << (sub + p * 4))
            ]
            for p in range(self.passes)
        ]
        self.side_covered = [
            c * 4 + sub
            for c, m in enumerate(self._map[2::7])
            for sub in range(4)
            if m & (1 << sub)
        ]
        self.room = [c for c, m in enumerate(self._map[4::7]) if m]
        self.portals = [c for c, m in enumerate(self._map[5::7]) if m]
        length = self.lib.roo_path(self.handle, self._path, 1728)
        self.path = list(self._path[:length])

        if old != self.state:
            self.events.append(
                {
                    "time": self.now,
                    "message": self.state + (": " + self.reason if self.reason else ""),
                }
            )
            self.events = self.events[-100:]

    def _command(self, command, pose):
        self.lib.roo_command(
            self.handle,
            command,
            self.profile == "deep",
            round(self.now * 1000),
            pose.x,
            pose.y,
            pose.yaw,
        )
        self.refresh()

    def start(self):
        self._command(0, self.dock)

    def pause(self):
        self._command(1, self.dock)

    def return_home(self, pose):
        self._command(2, pose)

    def _input(self, obs):
        values = (C.c_float * 185)()
        values[0:4] = [obs.pose.x, obs.pose.y, obs.pose.yaw, obs.battery]
        for angle, r in obs.scan:
            values[4 + round(angle * 90 / 3.141592653589793) % 180] = r
        values[184] = obs.dirt
        flags = (
            int(obs.localized)
            | (2 if obs.time - obs.scan_time <= 0.4 and len(obs.scan) >= 90 else 0)
            | 4
            | (8 if obs.cliff else 0)
            | (16 if obs.wheel_drop else 0)
            | (32 if obs.bumper else 0)
            | (64 if obs.docked else 0)
            | (128 if obs.charging else 0)
            | (512 if self.command.cleaning_pwm else 0)
            | (1024 if self.command.side_pwm else 0)
        )
        return values, flags

    def tick(self, obs):
        self.now = obs.time
        values, flags = self._input(obs)
        out = (C.c_float * 5)()
        self.lib.roo_step(self.handle, values, round(obs.time * 1000), flags, out, 0)
        self.command = NativeCommand(
            out[0],
            out[1],
            round(out[2]),
            ("stop", "drive", "seek_dock", "charge")[round(out[3])],
            round(out[4]),
        )
        self.refresh()
        return self.command

    def resume(self, obs):
        values, flags = self._input(obs)
        out = (C.c_float * 5)()
        self.lib.roo_step(self.handle, values, round(obs.time * 1000), flags, out, 1)
        self.refresh()
        return bool(out[0])

    def snapshot(self):
        return {
            "version": 2,
            "engine": "RooNavigation C++",
            "state": self.state,
            "profile": self.profile,
            "pass": self.pass_index + 1,
            "cells": self.grid.cells,
            "fine_covered": self.fine_covered,
            "side_covered": self.side_covered,
            "geometry_calibrated": False,
            "brush_width_m": 0.16,
            "rooms": self.rooms,
            "edge_incomplete": self.edge_incomplete,
            "note": "Inspection export; hardware resumes from its own checksummed LittleFS snapshot.",
        }
