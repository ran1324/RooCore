"""MCU-clock leases. Credentials stay in the caller's environment, not the repo."""

import json
import math
import time
from urllib.parse import urlencode, urlparse
from urllib.request import Request, urlopen


class RobotClient:
    def __init__(self, base, token, timeout=0.15):
        if urlparse(base).scheme != "http" or not urlparse(base).hostname:
            raise ValueError("expected trusted-LAN http URL")
        if len(token) < 24:
            raise ValueError("API token must have at least 24 characters")
        self.base, self.token, self.timeout = base.rstrip("/"), token, timeout
        self.status = None
        self.received = 0
        self.sequence = 0
        self.epoch = None

    def _request(self, path, fields=None):
        data = urlencode(fields).encode() if fields is not None else None
        request = Request(
            self.base + path,
            data=data,
            headers={"Authorization": "Bearer " + self.token},
        )
        with urlopen(request, timeout=self.timeout) as response:
            return response.read(65536)

    def poll(self):
        status = json.loads(self._request("/status"))
        if status["epoch"] != self.epoch:
            self.sequence = 0
            self.epoch = status["epoch"]
        self.status, self.received = status, time.monotonic()
        return status

    def action(self, action):
        return self._request("/command", {"action": action})

    def velocity(self, linear, angular, pwm=85):
        if not all(math.isfinite(v) for v in (linear, angular)):
            raise ValueError("non-finite velocity")
        if (
            not self.status
            or self.status["owner"] != 1
            or time.monotonic() - self.received > 0.15
        ):
            return False
        # Expiry is based on the MCU time in the latest status, never on host time.
        self.sequence += 1
        left = round(1000 * (linear - angular * 0.235 / 2))
        right = round(1000 * (linear + angular * 0.235 / 2))
        if abs(left) > 250 or abs(right) > 250:
            raise ValueError("wheel velocity exceeds commissioned envelope")
        self._request(
            "/command",
            {
                "action": "velocity",
                "epoch": self.epoch,
                "sequence": self.sequence,
                "expires": (self.status["now"] + 200) & 0xFFFFFFFF,
                "left": left,
                "right": right,
                "pwm": pwm,
            },
        )
        return True


def global_frame_valid(status):
    return bool(
        status.get("global_localized", status.get("localized", False))
        and not status.get("provisional", False)
        and not status.get("help_pending", False)
    )
