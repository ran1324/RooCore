"""ACL tests use a separate loopback-only broker with throwaway credentials."""
import os
from pathlib import Path
import pwd
import secrets
import shutil
import socket
import subprocess
import threading
import time

import paho.mqtt.client as mqtt
import pytest


@pytest.fixture(scope="module")
def broker(tmp_path_factory):
    if not shutil.which("mosquitto"):
        pytest.skip("Native Mosquitto required for ACL integration checks")
    folder = tmp_path_factory.mktemp("isolated-broker")
    password = secrets.token_urlsafe(32)
    passwd = folder / "passwords"
    passwd.write_text("".join(f"{u}:{password}\n" for u in ("api-reader", "roomba-620", "sim-620")))
    passwd.chmod(0o600)
    subprocess.run(["mosquitto_passwd", "-U", str(passwd)], check=True, capture_output=True)
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    acl = Path(__file__).resolve().parent.parent / "mosquitto/acl"
    config = folder / "mosquitto.conf"
    config.write_text(f"listener {port} 127.0.0.1\nallow_anonymous false\n"
                      f"user {pwd.getpwuid(os.getuid()).pw_name}\npassword_file {passwd}\n"
                      f"acl_file {acl}\nretain_available false\npersistence false\nlog_dest stderr\n")
    process = subprocess.Popen(["mosquitto", "-c", str(config)], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                with socket.create_connection(("127.0.0.1", port), .1):
                    break
            except OSError:
                time.sleep(.1)
        else:
            raise RuntimeError("Isolated test broker did not start")
        yield port, password
    finally:
        process.terminate()
        process.wait(timeout=5)


def connect(broker, user=None, password=None):
    port, valid_password = broker
    ready = threading.Event()
    result = []
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv5)
    if user:
        client.username_pw_set(user, password or valid_password)
    def on_connect(c, u, flags, reason, props):
        result.append(reason)
        ready.set()
    client.on_connect = on_connect
    client.connect("127.0.0.1", port)
    client.loop_start()
    assert ready.wait(5)
    return client, result[0]


@pytest.mark.parametrize("user,password", [(None, None), ("sim-620", "wrong")])
def test_broker_rejects_anonymous_and_bad_password(broker, user, password):
    client, reason = connect(broker, user, password)
    try:
        assert reason.is_failure
    finally:
        client.disconnect()
        client.loop_stop()


@pytest.mark.parametrize("user,topic,allowed", [
    ("sim-620", "roocore/v1/robots/sim-620/telemetry", True),
    ("roomba-620", "roocore/v1/robots/roomba-620/telemetry", True),
    ("sim-620", "roocore/v1/robots/roomba-620/telemetry", False),
    ("roomba-620", "roocore/v1/robots/sim-620/telemetry", False),
    ("api-reader", "roocore/v1/robots/roomba-620/telemetry", False),
    ("api-reader", "roocore/v1/robots/roomba-620/commands", False),
    ("roomba-620", "roocore/v1/robots/roomba-620/commands", False),
    ("sim-620", "roocore/v1/robots/sim-620/commands", False),
])
def test_broker_write_permissions(broker, user, topic, allowed):
    client, reason = connect(broker, user)
    ack = threading.Event()
    results = []
    def published(c, u, mid, reason, props):
        results.append(reason)
        ack.set()
    client.on_publish = published
    try:
        assert not reason.is_failure
        # Inert string to an isolated test broker, never an instruction or real robot.
        client.publish(topic, "permission-check", qos=1)
        assert ack.wait(5)
        assert (not results[0].is_failure) == allowed
    finally:
        client.disconnect()
        client.loop_stop()
