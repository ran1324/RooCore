"""Loopback-only simulator UI. No serial, Wi-Fi or robot command endpoint."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
from urllib.parse import urlparse
from .sim import Simulation

WEB = Path(__file__).with_name("web")


class Server(ThreadingHTTPServer):
    def __init__(self, address):
        super().__init__(address, Handler)
        self.sim = Simulation()
        self.lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, data, status=200, mime="application/json; charset=utf-8"):
        content = json.dumps(data, allow_nan=False).encode() if mime.startswith("application/json") else data
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(content)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/state":
            with self.server.lock:
                self.reply(self.server.sim.state())
        elif path == "/api/checkpoint":
            with self.server.lock:
                self.reply(self.server.sim.mission.snapshot())
        elif path in ("/", "/app.js", "/style.css"):
            name = "index.html" if path == "/" else path[1:]
            mime = {"index.html": "text/html", "app.js": "text/javascript", "style.css": "text/css"}[name]
            self.reply((WEB / name).read_bytes(), mime=mime + "; charset=utf-8")
        else:
            self.reply({"error": "not found"}, 404)

    def do_POST(self):
        # Same-origin browser only; the server is always bound to loopback.
        origin = self.headers.get("Origin")
        host = self.headers.get("Host", "")
        if host not in ("127.0.0.1:%d" % self.server.server_port, "localhost:%d" % self.server.server_port):
            return self.reply({"error": "invalid host"}, 403)
        if origin and origin not in ("http://" + host,):
            return self.reply({"error": "cross-origin request"}, 403)
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096 or self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                raise ValueError("expected a small JSON body")
            body = json.loads(self.rfile.read(length))
            action = self.path
            with self.server.lock:
                sim = self.server.sim
                if action == "/api/reset":
                    sim = self.server.sim = Simulation(body.get("scenario", "furniture"), body.get("profile", "daily"))
                elif action == "/api/start":
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
                    if sim.mission.state not in ("CLEANING", "RESUMING"):
                        raise ValueError("home requires an active localized mission")
                    sim.mission.return_home(sim.pose)
                elif action == "/api/inject":
                    sim.inject(body["fault"])
                else:
                    return self.reply({"error": "not found"}, 404)
                self.reply(sim.state())
        except (ValueError, KeyError, TypeError) as exc:
            self.reply({"error": str(exc)}, 400)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()
    server = Server(("127.0.0.1", args.port))
    print("RooCore simulation: http://127.0.0.1:%d" % server.server_port, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
