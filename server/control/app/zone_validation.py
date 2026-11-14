"""Shared zone contract from navigation/zones.py; tested for parity."""
import math
KINDS = ("room", "no_go", "wall", "carpet")
ROOM_TYPES = ("living_room", "bedroom", "kitchen", "bathroom", "hallway", "other")
REASONS = ("general", "pet_bowls", "bathroom", "low_clearance")

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

