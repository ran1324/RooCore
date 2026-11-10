"""Loopback dashboard: simulator controls or read-only robot/replay telemetry."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import threading
import uuid
from urllib.parse import urlparse
from .sim import Simulation
from .battery_health import Policy
from .history import History
from .monitor import Monitor, RobotReader, read_replay

WEB = Path(__file__).with_name("web")


class Server(ThreadingHTTPServer):
    def __init__(self, address, source="simulation", store_path="recordings/dashboard.sqlite3",
                 robot_id=None, battery_id=None, workload_id=None, policy=None,
                 reader=None, replay=None, stale_after=5):
        if address[0] not in ("127.0.0.1", "localhost"):
            raise ValueError("dashboard binds only to loopback")
        if source not in ("simulation", "robot", "replay"):
            raise ValueError("unsupported dashboard source")
        if source == "robot" and (reader is None or not robot_id):
            raise ValueError("robot source requires a configured reader and robot identity")
        if source == "replay" and not replay:
            raise ValueError("replay source requires recorded snapshots")
        super().__init__(address, Handler)
        self.source, self.reader, self.replay = source, reader, replay or []
        self.replay_index = 0
        self.store = History(store_path)
        self.sim = Simulation() if source == "simulation" else None
        self.sim_mission = "simulation-" + uuid.uuid4().hex
        self.sim_previous = None
        self.monitor = Monitor(self.store, source, robot_id, battery_id, workload_id, policy, stale_after) if source != "simulation" else None
        self.lock = threading.Lock()
        self.stop_polling = threading.Event()
        self.poll_thread = None

    @property
    def storage_id(self):
        return "simulator" if self.source == "simulation" else self.monitor.storage_id

    def state(self):
        if self.monitor:
            return self.monitor.state()
        state = self.sim.state()
        previous = self.sim_previous
        delta = max(0, state["time"] - previous["time"]) if previous else 0
        usage = {key: max(0, state.get(key, 0) - (previous.get(key, 0) if previous else 0))
                 for key in ("brush_s", "vacuum_s")}
        usage["observed_s"] = delta
        summary = {"mission_id": self.sim_mission if state["state"] != "READY" else None, "map_id": "simulation-" + state["scenario"],
            "battery_id": "synthetic-battery", "state": state["state"], "program_complete": state["state"] == "COMPLETE",
            "coverage": state["coverage"], "coverage_scope": "observed_floor_current_pass", "reported_active_s": state.get("active_ms", 0) / 1000,
            "reported_cleaning_s": None, "gaps": 0}
        self.store.record("simulation", "simulator", {"mission_id": self.sim_mission, "view": state}, summary,
            usage=usage)
        self.sim_previous = state
        state.update(source="simulation", read_only=False,
            identity={"robot_id": "simulator", "battery_id": "synthetic-battery", "mission_id": self.sim_mission,
                "map_id": summary["map_id"], "boot_id": self.sim_mission},
            map_origin={"x": 0, "y": 0}, progress=dict(summary, sample_provenance="simulation"),
            freshness={"status": "fresh", "age_s": 0, "last_received_at": None, "error": None},
            battery_health={"classification": "insufficient_data", "mode": "simulation", "alerts": [],
                "capacity_percent": None, "replacement_required": None,
                "note": "Simulated charge is not evidence of physical battery aging."},
            history=self.store.history("simulation", "simulator"), maintenance=self.store.maintenance("simulation", "simulator"))
        return state

    def poll_once(self):
        if self.source == "simulation":
            return
        try:
            if self.source == "robot":
                data = self.reader.fetch()
            elif self.replay_index < len(self.replay):
                data = self.replay[self.replay_index]
                self.replay_index += 1
            else:
                return
            with self.lock:
                self.monitor.ingest(data)
        except (OSError, ValueError, KeyError, TypeError):
            with self.lock:
                self.monitor.failed()

    def start_polling(self):
        if self.source == "simulation" or self.poll_thread:
            return
        def run():
            while not self.stop_polling.is_set():
                self.poll_once()
                self.stop_polling.wait(1)
        self.poll_thread = threading.Thread(target=run, daemon=True)
        self.poll_thread.start()

    def server_close(self):
        self.stop_polling.set()
        if self.poll_thread:
            self.poll_thread.join(timeout=4)
        super().server_close()
        self.store.close()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, data, status=200, mime="application/json; charset=utf-8"):
        content = (
            json.dumps(data, allow_nan=False).encode()
            if mime.startswith("application/json")
            else data
        )
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(content)

    def do_GET(self):
        if not self.valid_origin():
            return
        path = urlparse(self.path).path
        if path == "/api/state":
            with self.server.lock:
                self.reply(self.server.state())
        elif path in ("/api/health", "/api/history", "/api/maintenance"):
            key = {"/api/health": "battery_health", "/api/history": "history", "/api/maintenance": "maintenance"}[path]
            with self.server.lock:
                self.reply(self.server.state()[key])
        elif path == "/api/checkpoint":
            with self.server.lock:
                if self.server.sim:
                    self.reply(self.server.sim.mission.snapshot())
                elif self.server.monitor.last:
                    self.reply({"source": self.server.source, "read_only": True, "snapshot": self.server.monitor.last})
                else:
                    self.reply({"error": "no verified snapshot available"}, 409)
        elif path in ("/", "/app.js", "/style.css"):
            name = "index.html" if path == "/" else path[1:]
            mime = {
                "index.html": "text/html",
                "app.js": "text/javascript",
                "style.css": "text/css",
            }[name]
            self.reply((WEB / name).read_bytes(), mime=mime + "; charset=utf-8")
        else:
            self.reply({"error": "not found"}, 404)

    def valid_origin(self):
        # Apply Host validation to reads too (DNS rebinding). No CORS is enabled.
        origin = self.headers.get("Origin")
        host = self.headers.get("Host", "")
        if host not in (
            "127.0.0.1:%d" % self.server.server_port,
            "localhost:%d" % self.server.server_port,
        ):
            self.reply({"error": "invalid host"}, 403)
            return False
        if origin and origin not in ("http://" + host,):
            self.reply({"error": "cross-origin request"}, 403)
            return False
        return True

    def do_POST(self):
        if not self.valid_origin():
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if (
                not 0 < length <= 4096
                or self.headers.get("Content-Type", "").split(";")[0]
                != "application/json"
            ):
                raise ValueError("expected a small JSON body")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("expected a JSON object")
            action = urlparse(self.path).path
            with self.server.lock:
                if action == "/api/maintenance/settings":
                    self.server.store.settings(self.server.source, self.server.storage_id, body)
                    return self.reply(self.server.state())
                if action == "/api/maintenance/event":
                    self.server.store.event(self.server.source, self.server.storage_id, body)
                    return self.reply(self.server.state())
                if self.server.source != "simulation":
                    return self.reply({"error": "robot and replay sources are read-only; no command is forwarded"}, 403)
                sim = self.server.sim
                if action == "/api/reset":
                    sim = self.server.sim = Simulation(
                        body.get("scenario", "furniture"), body.get("profile", "daily")
                    )
                    self.server.sim_mission = "simulation-" + uuid.uuid4().hex
                    self.server.sim_previous = None
                elif action == "/api/start":
                    if sim.mission.state != "READY":
                        raise ValueError(
                            "mission already exists; resume or reset explicitly"
                        )
                    sim.mission.start()
                elif action == "/api/step":
                    count = body.get("count", 1)
                    if type(count) is not int or not 1 <= count <= 100:
                        raise ValueError("count must be 1..100")
                    sim.step(count)
                elif action == "/api/pause":
                    sim.mission.pause()
                elif action == "/api/resume":
                    sim.mission.resume(sim.observation())
                elif action == "/api/home":
                    if sim.mission.state not in (
                        "CLEANING",
                        "RESUMING",
                        "EDGING",
                        "TOUCHUP",
                    ):
                        raise ValueError("home requires an active localized mission")
                    sim.mission.return_home(sim.pose)
                elif action == "/api/inject":
                    sim.inject(body["fault"])
                else:
                    return self.reply({"error": "not found"}, 404)
                self.reply(self.server.state())
        except (ValueError, KeyError, TypeError) as exc:
            self.reply({"error": str(exc)}, 400)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--source", choices=("simulation", "robot", "replay"), default="simulation")
    parser.add_argument("--store", default="recordings/dashboard.sqlite3")
    parser.add_argument("--replay-file", help="local JSONL /progress recording; never contacts robot")
    parser.add_argument("--health-policy", help="JSON file with all explicit battery analysis policy fields")
    parser.add_argument("--stale-after", type=float, default=5, help="host freshness window, seconds; not a battery limit")
    args = parser.parse_args()
    policy = Policy(**json.loads(Path(args.health_policy).read_text())) if args.health_policy else None
    reader = RobotReader(os.environ.get("ROOCORE_ROBOT_URL", ""), os.environ.get("ROOCORE_ROBOT_TOKEN", "")) if args.source == "robot" else None
    replay = read_replay(args.replay_file) if args.source == "replay" and args.replay_file else None
    server = Server(("127.0.0.1", args.port), source=args.source, store_path=args.store,
        robot_id=os.environ.get("ROOCORE_ROBOT_ID"), battery_id=os.environ.get("ROOCORE_BATTERY_ID"),
        workload_id=os.environ.get("ROOCORE_WORKLOAD_ID"), policy=policy, reader=reader,
        replay=replay, stale_after=args.stale_after)
    server.start_polling()
    print("RooCore %s: http://127.0.0.1:%d" % (args.source, server.server_port), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
