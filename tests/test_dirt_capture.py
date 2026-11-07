import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.parse import parse_qs, urlparse

from navigation.dirt_capture import Capture, report


def row(seq, value=120, pose=None):
    return {
        "seq": seq,
        "kind": "reading",
        "received_ms": seq * 100,
        "value": value,
        "pose": pose,
        "comparison_valid": False,
        "readings_decreased": None,
    }


class DirtCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.output = Path(self.temp.name) / "dirt.jsonl"
        self.rows = [row(1), row(2, pose={"x": 1, "y": 2, "yaw": 0, "estimated": True})]
        self.boot = 111
        self.requests = []
        self.broken = False
        case = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                case.requests.append(
                    (self.command, self.path, self.headers.get("Authorization"))
                )
                after = int(parse_qs(urlparse(self.path).query)["after"][0])
                latest = case.rows[-1]["seq"] if case.rows else 0
                if after > latest:
                    self.send_response(409)
                    self.end_headers()
                    return
                rows = [r for r in case.rows if r["seq"] > after][:2]
                payload = {
                    "schema": 1,
                    "boot_id": case.boot,
                    "automatic_cleaning": False,
                    "mode": "observe_only",
                    "oldest": case.rows[0]["seq"] if case.rows else 1,
                    "latest": latest,
                    "next": rows[-1]["seq"] if rows else after,
                    "has_more": bool(rows and rows[-1]["seq"] < latest),
                    "records": rows,
                    "config": {"threshold": 0},
                }
                if case.broken:
                    payload["next"] = 9999
                data = json.dumps(payload).encode()
                self.send_response(200)
                self.end_headers()
                self.wfile.write(data)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = "http://127.0.0.1:" + str(self.server.server_port)
        self.token = "synthetic-local-test-token-only"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp.cleanup()

    def test_readonly_capture_resume_and_report(self):
        capture = Capture(self.url, self.token, self.output, "synthetic")
        self.assertFalse(capture.poll())
        self.assertEqual(capture.cursor, 2)
        self.assertFalse(capture.poll())
        result = report(self.output)
        self.assertEqual(result["readings"], 2)
        self.assertEqual(result["with_estimated_pose"], 1)
        self.assertEqual(result["median"], 120)
        resumed = Capture(self.url, self.token, self.output)
        self.assertEqual(resumed.cursor, 2)
        self.rows.append(row(3, 20))
        resumed.poll()
        self.assertEqual(report(self.output)["readings"], 3)
        self.assertTrue(
            all(
                method == "GET"
                and path.startswith("/dirt?")
                and auth == "Bearer " + self.token
                for method, path, auth in self.requests
            )
        )
        self.assertNotIn(self.token, self.output.read_text())

    def test_ring_gap_and_boot_change_even_with_higher_new_sequence(self):
        capture = Capture(self.url, self.token, self.output)
        capture.poll()
        self.rows = [row(i) for i in range(10, 15)]
        while capture.poll():
            pass
        self.assertEqual(report(self.output)["missing_export_records"], 7)
        self.boot = 222
        self.rows = [row(i, 30) for i in range(12, 17)]
        while capture.poll():
            pass
        events = [json.loads(line) for line in self.output.read_text().splitlines()]
        new = [
            e["record"]["seq"]
            for e in events
            if e.get("type") == "record" and e["boot_id"] == 222
        ]
        self.assertEqual(new, list(range(12, 17)))
        self.boot = 333
        self.rows = [row(1, 40)]
        capture.poll()
        self.assertEqual((capture.boot, capture.cursor), (333, 1))

    def test_bad_page_never_advances_cursor_or_appends_records(self):
        capture = Capture(self.url, self.token, self.output)
        capture.poll()
        before = self.output.read_text()
        self.rows.append(row(3))
        self.broken = True
        with self.assertRaises(ValueError):
            capture.poll()
        self.assertEqual(capture.cursor, 2)
        self.assertEqual(self.output.read_text(), before)

    def test_report_deduplicates_replayed_page_and_keeps_unlocated(self):
        capture = Capture(self.url, self.token, self.output)
        capture.poll()
        with self.output.open("a") as stream:
            stream.write(
                json.dumps({"type": "record", "boot_id": 111, "record": row(1)}) + "\n"
            )
        self.assertEqual(report(self.output)["readings"], 2)
