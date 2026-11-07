"""Read-only DirtDetect capture/report. No robot command or motor endpoint is used."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import statistics
import time
from urllib.error import HTTPError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


def utc_now():
    return datetime.now(timezone.utc).isoformat()


class Capture:
    def __init__(self, base, token, output, label="unlabelled"):
        url = urlparse(base)
        if url.scheme != "http" or not url.hostname or url.username or url.password:
            raise ValueError("expected trusted-LAN http URL without credentials")
        if len(token) < 24:
            raise ValueError("ROOCORE_TOKEN must have at least 24 characters")
        self.base, self.token, self.output = base.rstrip("/"), token, Path(output)
        self.label, self.boot, self.cursor = label, None, 0
        self.cursor_file = self.output.with_suffix(self.output.suffix + ".cursor.json")
        if self.cursor_file.exists():
            if not self.output.exists():
                raise ValueError("cursor exists without recording; choose a new output")
            state = json.loads(self.cursor_file.read_text())
            if state["base"] != self.base:
                raise ValueError("recording belongs to a different robot URL")
            self.boot, self.cursor = state["boot_id"], int(state["cursor"])
        self.output.parent.mkdir(parents=True, exist_ok=True)

    def fetch(self, after):
        req = Request(
            self.base + "/dirt?after=" + str(after),
            headers={"Authorization": "Bearer " + self.token},
        )
        with urlopen(req, timeout=2) as response:
            payload = response.read(65537)
        if len(payload) > 65536:
            raise ValueError("oversized dirt snapshot")
        return json.loads(payload)

    def append(self, events):
        if not events:
            return
        with self.output.open("a", encoding="utf-8") as stream:
            for event in events:
                stream.write(
                    json.dumps(event, ensure_ascii=False, allow_nan=False) + "\n"
                )
            stream.flush()
            os.fsync(stream.fileno())

    def poll(self):
        try:
            snapshot = self.fetch(self.cursor)
        except HTTPError as error:
            if error.code != 409:
                raise
            snapshot = self.fetch(0)
        if self.boot != snapshot["boot_id"]:
            # A different boot can have a larger sequence than our old cursor too.
            snapshot = self.fetch(0)
            cursor = 0
        else:
            cursor = self.cursor
        if (
            snapshot.get("schema") != 1
            or snapshot.get("automatic_cleaning") is not False
        ):
            raise ValueError("unsupported observation schema/mode")
        rows = snapshot["records"]
        oldest, latest = int(snapshot["oldest"]), int(snapshot["latest"])
        expected = max(cursor + 1, oldest)
        for row in rows:
            if row["seq"] != expected or row["seq"] > latest:
                raise ValueError("non-contiguous record sequence")
            expected += 1
        next_cursor = rows[-1]["seq"] if rows else cursor
        if snapshot["next"] != next_cursor or (snapshot["has_more"] and not rows):
            raise ValueError("invalid cursor progress")
        now = utc_now()
        common = {
            "boot_id": snapshot["boot_id"],
            "host_received_at": now,
            "label": self.label,
        }
        events = []
        if self.boot != snapshot["boot_id"]:
            events.append(
                {
                    **common,
                    "type": "boot",
                    "config": snapshot["config"],
                    "mode": snapshot["mode"],
                }
            )
        lost = max(0, oldest - cursor - 1)
        if lost:
            events.append(
                {**common, "type": "record_gap", "lost_records": lost, "after": cursor}
            )
        events.extend({**common, "type": "record", "record": row} for row in rows)
        self.append(events)
        self.boot, self.cursor = snapshot["boot_id"], next_cursor
        temp = self.cursor_file.with_suffix(self.cursor_file.suffix + ".tmp")
        temp.write_text(
            json.dumps({"base": self.base, "boot_id": self.boot, "cursor": self.cursor})
        )
        temp.replace(self.cursor_file)  # data is fsynced before advancing the cursor
        return snapshot["has_more"]


def report(path):
    values, seen = [], set()
    located = regions = revisits = comparisons = decreased = missing = source_gaps = (
        maintenance
    ) = 0
    with Path(path).open(encoding="utf-8") as stream:
        for line in stream:
            event = json.loads(line)
            if event["type"] == "record_gap":
                missing += event["lost_records"]
            if event["type"] != "record":
                continue
            row = event["record"]
            key = event["boot_id"], row["seq"]
            if key in seen:
                continue  # a crash after fsync but before cursor rename can repeat a batch
            seen.add(key)
            if row["kind"] == "reading":
                values.append(row["value"])
                located += row["pose"] is not None
            regions += row["kind"] == "region_detected"
            revisits += row["kind"] == "natural_revisit_result"
            source_gaps += row["kind"] == "source_gap"
            maintenance += row["kind"] == "maintenance"
            comparisons += bool(row.get("comparison_valid"))
            decreased += row.get("readings_decreased") is True
    values.sort()
    return {
        "readings": len(values),
        "with_estimated_pose": located,
        "minimum": min(values) if values else None,
        "median": statistics.median(values) if values else None,
        "p95": (
            values[min(len(values) - 1, int(0.95 * (len(values) - 1)))]
            if values
            else None
        ),
        "maximum": max(values) if values else None,
        "detected_regions": regions,
        "natural_revisit_results": revisits,
        "comparable_revisits": comparisons,
        "lower_readings": decreased,
        "missing_export_records": missing,
        "source_gap_events": source_gaps,
        "maintenance_events": maintenance,
        "note": "Descriptive readings only; no calibrated dirt quantity, threshold recommendation or cleaning proof.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=os.environ.get("ROOCORE_URL"))
    parser.add_argument("--output", default="recordings/dirt.jsonl")
    parser.add_argument("--label", default="unlabelled")
    parser.add_argument("--report", metavar="JSONL")
    args = parser.parse_args()
    if args.report:
        print(json.dumps(report(args.report), indent=2))
        return
    capture = Capture(
        args.url or "", os.environ.get("ROOCORE_TOKEN", ""), args.output, args.label
    )
    disconnected = False
    try:
        while True:
            try:
                more = capture.poll()
                if disconnected:
                    capture.append(
                        [{"type": "connection_restored", "host_received_at": utc_now()}]
                    )
                disconnected = False
                if not more:
                    time.sleep(0.25)
            except (OSError, ValueError, KeyError) as error:
                if not disconnected:
                    # Do not log request headers, token, or arbitrary response bodies.
                    capture.append(
                        [
                            {
                                "type": "connection_lost",
                                "host_received_at": utc_now(),
                                "error_type": type(error).__name__,
                            }
                        ]
                    )
                disconnected = True
                time.sleep(1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
