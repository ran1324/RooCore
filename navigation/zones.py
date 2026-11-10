"""Versioned map annotations, validated before persistence or native activation."""
import copy
import hashlib
import json
import math
from pathlib import Path
import os
import tempfile

KINDS = ("room", "no_go", "wall", "carpet")
ROOM_TYPES = ("living_room", "bedroom", "kitchen", "bathroom", "hallway", "other")
REASONS = ("general", "pet_bowls", "bathroom", "low_clearance")


class Conflict(ValueError):
    pass


def geometry(state):
    origin = state.get("map_origin", {"x": 0, "y": 0})
    return {"cols": state["cols"], "rows": state["rows"], "resolution": state["resolution"],
            "origin_x": origin["x"], "origin_y": origin["y"]}


def map_key(state):
    source = state.get("source", "simulation")
    identity = state.get("identity", {})
    map_id = identity.get("map_id") or ("simulation-" + state["scenario"] if source == "simulation" else None)
    robot_id = identity.get("robot_id") or ("simulator" if source == "simulation" else None)
    if not map_id or not robot_id:
        raise ValueError("a stable robot and map identity is required")
    return {"source": source, "robot_id": robot_id, "map_id": map_id, "geometry": geometry(state)}


def validate_zones(items, grid):
    if not isinstance(items, list) or len(items) > 32:
        raise ValueError("up to 32 zones are supported")
    result, seen = [], set()
    ox, oy, r = grid["origin_x"], grid["origin_y"], grid["resolution"]
    xmax, ymax = ox + grid["cols"] * r, oy + grid["rows"] * r
    for item in items:
        if not isinstance(item, dict):
            raise ValueError("zone must be an object")
        kind, name, zid = item.get("kind"), item.get("name"), item.get("id")
        if kind not in KINDS or not isinstance(name, str) or not 1 <= len(name.strip()) <= 80:
            raise ValueError("zone needs a supported kind and a name of 1–80 characters")
        if not isinstance(zid, str) or not 1 <= len(zid) <= 64 or zid in seen:
            raise ValueError("zone IDs must be unique, 1–64 characters")
        seen.add(zid)
        z = {"id": zid, "name": name.strip(), "kind": kind}
        for k in ("x1", "y1", "x2", "y2"):
            v = item.get(k)
            if type(v) not in (int, float) or not math.isfinite(v):
                raise ValueError("zone coordinates must be finite metres")
            if not (ox <= v <= xmax if k.startswith("x") else oy <= v <= ymax):
                raise ValueError("zone is outside this map")
            z[k] = float(v)
        if kind == "wall":
            if math.hypot(z["x2"] - z["x1"], z["y2"] - z["y1"]) <= .001:
                raise ValueError("a virtual wall needs two distinct endpoints")
        elif z["x2"] <= z["x1"] or z["y2"] <= z["y1"]:
            raise ValueError("rectangle must have positive width and height")
        for k, default in (("enabled", True), ("present", True), ("boost", True)):
            v = item.get(k, default)
            if type(v) is not bool:
                raise ValueError(k + " must be boolean")
            z[k] = v
        extra = item.get("extra_passes", 0)
        if type(extra) is not int or not 0 <= extra <= 2:
            raise ValueError("extra passes must be 0, 1 or 2")
        z["extra_passes"] = extra if kind == "carpet" else 0
        z["room_type"] = item.get("room_type", "other")
        z["reason"] = item.get("reason", "general")
        if z["room_type"] not in ROOM_TYPES or z["reason"] not in REASONS:
            raise ValueError("unsupported room type or exclusion reason")
        result.append(z)
    return result


class ZoneStore:
    def __init__(self, directory):
        self.directory = Path(directory)

    def path(self, key):
        digest = hashlib.sha256(json.dumps(key, sort_keys=True).encode()).hexdigest()
        return self.directory / (digest + ".json")

    def load(self, key):
        path = self.path(key)
        if not path.exists():
            return {"version": 1, "key": key, "revision": 0, "name": "המפה שלי", "zones": [], "cells": []}
        document = json.loads(path.read_text())
        if document.get("version") != 1 or document.get("key") != key:
            raise ValueError("saved map identity is invalid; refusing to discard restrictions")
        document["zones"] = validate_zones(document["zones"], key["geometry"])
        if type(document.get("revision")) is not int or document["revision"] < 1:
            raise ValueError("invalid map revision")
        cells = document.get("cells")
        if not isinstance(cells, list) or len(cells) not in (0, key["geometry"]["cols"] * key["geometry"]["rows"]) or any(type(v) is not int or v not in (-1, 0, 1) for v in cells):
            raise ValueError("invalid saved map cells")
        return document

    def save(self, key, body, cells):
        if body.get("key") != key:
            raise Conflict("map identity changed; reload before saving")
        current = self.load(key)
        if type(body.get("revision")) is not int or body["revision"] != current["revision"]:
            raise Conflict("map was edited elsewhere; reload before saving")
        name = body.get("name")
        if not isinstance(name, str) or not 1 <= len(name.strip()) <= 80:
            raise ValueError("map needs a name of 1–80 characters")
        document = {"version": 1, "key": copy.deepcopy(key), "revision": current["revision"] + 1,
                    "name": name.strip(), "zones": validate_zones(body.get("zones"), key["geometry"]),
                    "cells": list(cells)}
        self.directory.mkdir(parents=True, exist_ok=True)
        fd, path = tempfile.mkstemp(dir=self.directory, prefix=".zones-")
        try:
            with os.fdopen(fd, "w") as f:
                json.dump(document, f, ensure_ascii=False, allow_nan=False)
                f.flush()
                os.fsync(f.fileno())
            os.replace(path, self.path(key))
        finally:
            if os.path.exists(path):
                os.unlink(path)
        return document
