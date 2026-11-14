import logging
import threading
import time

import paho.mqtt.client as mqtt
from pydantic import ValidationError

from .store import ROBOTS, Telemetry, utcnow

log = logging.getLogger("roocore.mqtt")


class Receiver:
    """Receive only. No publishing API, command encoder or robot connection exists."""

    def __init__(self, settings, store):
        self.settings, self.store = settings, store
        self.connected = False
        self.fresh_after = utcnow()
        self.rejected = 0
        self.last_accepted = {}
        self.stop_event = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id="roocore-reader", clean_session=True)
        self.client.username_pw_set("api-reader", settings.mqtt_password)
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.client.on_subscribe = self.on_subscribe

    def on_connect(self, client, userdata, flags, reason_code, properties):
        self.connected = False
        self.fresh_after = utcnow()
        if reason_code.is_failure:
            log.warning("MQTT authentication/connection rejected")
            return
        topics = [(f"roocore/v1/robots/{r}/{kind}", 1)
                  for r in ROBOTS for kind in ("telemetry", "availability")]
        result, _ = client.subscribe(topics)
        if result != mqtt.MQTT_ERR_SUCCESS:
            log.error("MQTT subscription could not be sent")

    def on_subscribe(self, client, userdata, mid, reason_codes, properties):
        self.connected = len(reason_codes) == 4 and all(not code.is_failure for code in reason_codes)

    def on_disconnect(self, *args):
        self.connected = False
        self.fresh_after = utcnow()

    def on_message(self, client, userdata, msg):
        parts = msg.topic.split("/")
        if (msg.retain or len(msg.payload) > 3072 or len(parts) != 5
                or parts[:3] != ["roocore", "v1", "robots"] or parts[3] not in ROBOTS):
            self.rejected += 1
            return
        robot_id, kind = parts[3:]
        try:
            if kind == "availability" and msg.payload == b"offline":
                self.store.offline(robot_id)
            elif kind == "telemetry":
                # Bound storage to at most one sample per second per identity.
                now = time.monotonic()
                if now - self.last_accepted.get(robot_id, -1e9) < 1:
                    raise ValueError("telemetry rate exceeded")
                data = Telemetry.model_validate_json(msg.payload)
                self.store.ingest(robot_id, data)
                self.last_accepted[robot_id] = now
            else:
                raise ValueError("unsupported message")
        except (ValueError, ValidationError):
            self.rejected += 1
        except Exception:
            # No payloads, credentials, or database URLs in logs.
            self.rejected += 1
            log.error("Telemetry could not be stored; sample discarded")

    def maintenance(self):
        while not self.stop_event.wait(60):
            try:
                self.store.prune(self.settings.retention_days)
            except Exception:
                log.error("History retention job failed")

    def start(self):
        self.client.connect_async(self.settings.mqtt_host, self.settings.mqtt_port, keepalive=20)
        self.client.loop_start()
        self.worker = threading.Thread(target=self.maintenance, daemon=True)
        self.worker.start()

    def stop(self):
        self.stop_event.set()
        self.client.disconnect()
        self.client.loop_stop()
        self.worker.join(timeout=6)
