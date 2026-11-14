#!/usr/bin/env python3
"""Publish synthetic telemetry to sim-620 only. Never connects to a robot."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import threading
import time
from uuid import uuid4
import paho.mqtt.client as mqtt

parser = argparse.ArgumentParser()
parser.add_argument("--password-file", default="/root/roocore-access/sim-620_password")
parser.add_argument("--samples", type=int, default=3)
parser.add_argument("--host", default="127.0.0.1")
parser.add_argument("--port", type=int, default=18883)
args = parser.parse_args()
if args.host not in ("localhost", "127.0.0.1"):
    parser.error("Use the server's loopback interface or an encrypted SSH forward")
connected = threading.Event()
def on_connect(client, userdata, flags, reason, props):
    if not reason.is_failure:
        connected.set()
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="roocore-sim-" + uuid4().hex[:8])
client.on_connect = on_connect
client.username_pw_set("sim-620", Path(args.password_file).read_text().strip())
base = "roocore/v1/robots/sim-620/"
client.will_set(base + "availability", "offline", qos=1, retain=False)
client.connect(args.host, args.port, keepalive=20)
client.loop_start()
if not connected.wait(5):
    raise SystemExit("Simulator authentication failed")
boot = str(uuid4())
try:
    for seq in range(args.samples):
        payload = dict(schema_version=1, boot_id=boot, seq=seq,
                       sent_at=datetime.now(timezone.utc).isoformat(), uptime_ms=seq * 2000,
                       battery_percent=72.0, battery_mv=14500, state="held",
                       firmware="server-smoke-test", local_safety="unverified")
        client.publish(base + "telemetry", json.dumps(payload), qos=1, retain=False).wait_for_publish(5)
        time.sleep(2)
    client.publish(base + "availability", "offline", qos=1, retain=False).wait_for_publish(5)
finally:
    client.disconnect()
    client.loop_stop()
print("Synthetic telemetry sent to sim-620 only")
