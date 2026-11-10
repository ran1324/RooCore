"""ctypes adapter to the production C++ navigation library, built locally once."""

import ctypes as C
import hashlib
from pathlib import Path
import subprocess
import sys
from dataclasses import dataclass
from .core import Pose, Grid
from .zones import KINDS

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
    "YIELDING",
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
    sources = [ROOT / "navigation/native_bridge.cpp"] + sorted(
        (ROOT / "lib/RooNavigation/src").glob("*.h")
    )
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
    lib.roo_set_zones.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.c_int, C.c_int, C.c_float, C.c_float]
    lib.roo_set_zones.restype = C.c_int
    lib.roo_zone_status.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
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
        self._map = (C.c_int * (48 * 36 * 8))()
        self.zones = []
        self._zone_map = (C.c_int * (48 * 36 * 3))()
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
        self.base_passes = v[3]
        zv = (C.c_int * 1)()
        self.lib.roo_zone_status(self.handle, zv, self._zone_map)
        self.passes = zv[0]
        self.zone_blocked = [i for i, v in enumerate(self._zone_map[0::3]) if v]
        self.zone_extra = list(self._zone_map[1::3])
        self.cleared_obstacles = v[11]
        self.obstacle_waits = v[12]
        self.active_ms = v[13]
        self.return_cause = ("unknown", "manual", "battery", "completed")[v[14]]
        self.return_at_ms = v[15]
        self.contacts = [c for c, m in enumerate(self._map[7::8]) if m]
        self.excluded = [c for c, m in enumerate(self._map[6::8]) if m]
        self.skipped = v[4]
        self.rooms = v[5]
        self.edge_segments = v[6]
        self.edge_incomplete = v[7]
        self.target = (self._f[0], self._f[1]) if v[8] >= 0 else None
        self.checkpoint = self.grid.cell(self._f[2], self._f[3])
        self.dock_guard = {"issue": int(self._f[4]), "mode": int(self._f[5]), "anchor_valid": bool(self._f[6]), "synthetic_geometry": True}
        self.grid.cells = list(self._map[0::8])
        self.grid.safe = {i for i, x in enumerate(self._map[3::8]) if x}
        masks = self._map[1::8]
        extra = self._zone_map[2::3]
        pass_masks = [(masks, p) for p in range(self.base_passes)] + [(extra, p) for p in range(self.passes - self.base_passes)]
        self.covered = [
            {c for c, m in enumerate(pm) if m & (15 << (p * 4)) == (15 << (p * 4))}
            for pm, p in pass_masks
        ]
        self.fine_covered = [
            [
                c * 4 + sub
                for c, m in enumerate(pm)
                for sub in range(4)
                if m & (1 << (sub + p * 4))
            ]
            for pm, p in pass_masks
        ]
        self.side_covered = [
            c * 4 + sub
            for c, m in enumerate(self._map[2::8])
            for sub in range(4)
            if m & (1 << sub)
        ]
        self.room = [c for c, m in enumerate(self._map[4::8]) if m]
        self.portals = [c for c, m in enumerate(self._map[5::8]) if m]
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

    def _zones_array(self, zones):
        flat = [v for z in zones for v in (KINDS.index(z["kind"]), z["x1"], z["y1"], z["x2"], z["y2"],
                                           z["enabled"], z["present"], z["boost"], z["extra_passes"])]
        return (C.c_float * len(flat))(*flat)

    def validate_zones(self, zones, pose):
        if not self.lib.roo_set_zones(self.handle, self._zones_array(zones), len(zones), 1, pose.x, pose.y):
            raise ValueError("zones overlap the robot or dock clearance")

    def set_zones(self, zones):
        if not self.lib.roo_set_zones(self.handle, self._zones_array(zones), len(zones), 0, self.dock.x, self.dock.y):
            raise ValueError("cannot apply zones in this mission state")
        self.zones = zones
        self.refresh()

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
        values = (C.c_float * 191)()
        values[0:4] = [obs.pose.x, obs.pose.y, obs.pose.yaw, obs.battery]
        for angle, r in obs.scan:
            values[4 + round(angle * 90 / 3.141592653589793) % 180] = r
        values[184] = obs.dirt
        values[185:191] = [obs.charger_state, obs.charger_sources,
                          obs.charger_time, obs.battery_valid,
                          obs.charge_contact_confirmed, obs.charge_ready]
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
            | (2048 if obs.charger_valid else 0)
            | (4096 if obs.dock_moved else 0)
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
