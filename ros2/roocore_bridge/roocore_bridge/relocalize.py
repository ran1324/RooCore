"""Bounded global scan-to-known-map search. Ambiguity returns no candidate.

Not a SLAM loop-closure implementation. Never merges unknown submaps or invents a
match for a room absent from the saved map. The MCU independently validates it.
"""

import math
import time
import numpy as np


def locate(snapshot, ranges, max_seconds=3.0):
    start = time.monotonic()
    cols, rows = int(snapshot["cols"]), int(snapshot["rows"])
    resolution = float(snapshot["resolution"])
    if not (4 <= cols <= 128 and 4 <= rows <= 128 and 0.05 <= resolution <= 0.3):
        return None
    if len(snapshot["cells"]) != cols * rows or len(snapshot["safe"]) != cols * rows:
        return None
    cells = np.frombuffer(snapshot["cells"].encode("ascii"), dtype=np.uint8).reshape(
        rows, cols
    )
    occupied = cells == ord("o")
    safe = np.frombuffer(snapshot["safe"].encode("ascii"), dtype=np.uint8).reshape(
        rows, cols
    ) == ord("1")
    if occupied.sum() < 30:
        return None
    r = np.asarray(ranges, dtype=float)[::2]
    if len(r) != 90:
        return None
    a = np.arange(90) * math.tau / 90
    valid = np.isfinite(r) & (r > 0.1) & (r < 8)
    if valid.sum() < 60:
        return None
    r = r[valid]
    a = a[valid]
    # One-cell tolerance for occupancy quantization; free-ray conflicts penalize matches.
    near = occupied.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            yy, xx = np.nonzero(occupied)
            y = yy + dy
            x = xx + dx
            good = (x >= 0) & (x < cols) & (y >= 0) & (y < rows)
            near[y[good], x[good]] = True
    ox, oy = float(snapshot["origin_x"]), float(snapshot["origin_y"])
    yy, xx = np.nonzero(safe)
    if not len(xx):
        return None
    points = np.column_stack(
        (ox + (xx + 0.5) * resolution, oy + (yy + 0.5) * resolution)
    )
    # Bound the search grid, but retain spatial distribution across the whole map.
    points = points[:: max(1, math.ceil(len(points) / 4096))]

    def score(poses):
        headings = poses[:, 2, None] + a[None, :]
        ux = np.cos(headings)
        uy = np.sin(headings)

        def samples(factor, grid):
            x = np.floor(
                (poses[:, 0, None] + ux * r * factor - ox) / resolution
            ).astype(int)
            y = np.floor(
                (poses[:, 1, None] + uy * r * factor - oy) / resolution
            ).astype(int)
            good = (x >= 0) & (x < cols) & (y >= 0) & (y < rows)
            if factor == 1:
                # Same one-cell endpoint tolerance on both sides of map edges.
                good = (x >= -1) & (x <= cols) & (y >= -1) & (y <= rows)
                x = np.clip(x, 0, cols - 1)
                y = np.clip(y, 0, rows - 1)
            values = np.zeros(x.shape, dtype=bool)
            values[good] = grid[y[good], x[good]]
            return values

        hits = samples(1, near).mean(axis=1)
        conflicts = np.maximum.reduce(
            [samples(t, occupied) for t in (0.25, 0.5, 0.75)]
        ).mean(axis=1)
        return hits - 0.75 * conflicts

    ranked = []
    for yaw in np.arange(36) * math.tau / 36:
        for i in range(0, len(points), 256):
            if time.monotonic() - start > max_seconds:
                return None
            batch = np.column_stack(
                (points[i : i + 256], np.full(len(points[i : i + 256]), yaw))
            )
            scores = score(batch)
            for j in np.argsort(scores)[-4:]:
                if scores[j] >= 0.45:
                    ranked.append((float(scores[j]), batch[j]))
    ranked.sort(key=lambda item: item[0], reverse=True)
    if not ranked:
        return None
    clusters = []

    def apart(p, q):
        return (
            np.linalg.norm(p[:2] - q[:2]) > 0.35
            or abs((p[2] - q[2] + math.pi) % math.tau - math.pi) > 0.35
        )

    for value, pose in ranked:
        if all(apart(pose, p) for _, p in clusters):
            clusters.append((value, pose))
        if len(clusters) >= 16:
            break
    refined = []
    offsets = [
        (x, y, a)
        for x in (-0.15, -0.1, -0.05, 0, 0.05, 0.1, 0.15)
        for y in (-0.15, -0.1, -0.05, 0, 0.05, 0.1, 0.15)
        for a in np.deg2rad([-5, -3.75, -2.5, -1.25, 0, 1.25, 2.5, 3.75, 5])
    ]
    for _, pose in clusters:
        if time.monotonic() - start > max_seconds:
            return None
        batch = pose + np.array(offsets)
        scores = score(batch)
        k = int(np.argmax(scores))
        refined.append((float(scores[k]), batch[k]))
    refined.sort(key=lambda item: item[0], reverse=True)
    best, pose = refined[0]
    alternative = max((value for value, p in refined[1:] if apart(pose, p)), default=0)
    margin = best - alternative
    if best < 0.90 or margin < 0.10:
        return None
    return dict(
        x=float(pose[0]),
        y=float(pose[1]),
        yaw=float((pose[2] + math.pi) % math.tau - math.pi),
        score=best,
        margin=margin,
    )
