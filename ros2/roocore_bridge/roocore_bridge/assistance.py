"""Read real-time MCU help requests; propose bounded, independently checked recovery.

Runs beside Nav2 or alone on the existing home computer. Never writes motor
velocities and never reacquires remote ownership after a reconnect.
"""

import json
import os
import time
from .client import RobotClient
from .relocalize import locate


def reply(client, status, action, **fields):
    fields.update(
        action=action,
        id=status["help_id"],
        frame=status["frame"],
        expires=(status["now"] + 1000) & 0xFFFFFFFF,
    )
    return client._request("/command", fields)


def main():
    client = RobotClient(
        os.environ["ROOCORE_URL"], os.environ["ROOCORE_TOKEN"], timeout=0.5
    )
    last_key = None
    last_attempt = 0.0
    attempts = 0
    while True:
        try:
            s = client.poll()
            key = (s.get("help_id"), s.get("frame"))
            if not s.get("help_pending"):
                time.sleep(0.25)
                continue
            if key != last_key:
                last_key = key
                attempts = 0
                last_attempt = 0
                reply(client, s, "help-continue")
                print(
                    "Assistance requested:",
                    key,
                    "reason",
                    s.get("help_reason"),
                    flush=True,
                )
            # NoRoute is a path problem, not evidence that the pose moved. Local
            # alternative cleaning / native IR docking is already active on MCU.
            if (
                s.get("help_reason") != 2
                or s.get("help_status") == 3
                or attempts >= 8
                or time.monotonic() - last_attempt < 10
            ):
                time.sleep(0.25)
                continue
            last_attempt = time.monotonic()
            attempts += 1
            snapshot = json.loads(client._request("/help/map"))
            if (snapshot.get("id"), snapshot.get("frame")) != key:
                continue
            candidate = locate(snapshot, s.get("ranges", []), max_seconds=3)
            if candidate:
                current = client.poll()
                if (
                    current.get("help_id"),
                    current.get("frame"),
                ) != key or not current.get("help_pending"):
                    continue
                reply(
                    client,
                    current,
                    "help-relocalize",
                    x=candidate["x"],
                    y=candidate["y"],
                    yaw=candidate["yaw"],
                    ref_x=s["x"],
                    ref_y=s["y"],
                    ref_yaw=s["yaw"],
                )
                print(
                    "Candidate sent for MCU verification; score",
                    round(candidate["score"], 3),
                    flush=True,
                )
            else:
                print("No unique match; local mission remains independent.", flush=True)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            print("Assistance unavailable:", type(exc).__name__, flush=True)
        time.sleep(0.25)


if __name__ == "__main__":
    main()
