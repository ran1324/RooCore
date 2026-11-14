"""Shared transactional email transport. No account keys or message bodies in logs."""
import base64
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import re
import stat
from urllib.error import HTTPError, URLError
from urllib.request import Request, build_opener, HTTPRedirectHandler
from uuid import uuid4


class MailError(RuntimeError):
    """Safe to log; deliberately excludes provider bodies and credentials."""


def address(value):
    # One bare mailbox only. Never allow callers to add headers or recipients.
    if not isinstance(value, str) or not re.fullmatch(
            r"[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", value):
        raise ValueError("Expected one email address")
    return value


@dataclass(frozen=True)
class MailConfig:
    sender: str = "no-reply@saymour.co.il"
    alert_to: str = "thfcxv@gmail.com"
    key_file: str = "/etc/roocore/secrets/mailgun_sending_key"
    domain: str = "saymour.co.il"

    def __post_init__(self):
        address(self.sender)
        address(self.alert_to)
        if self.domain != "saymour.co.il" or self.sender.split("@")[1] != self.domain:
            raise ValueError("Mail sender must use the verified saymour.co.il domain")

    @classmethod
    def load(cls, path="/etc/roocore/mail.json"):
        return cls(**json.loads(Path(path).read_text()))


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Do not forward Basic Authorization to a redirect destination.
        return None


@dataclass
class MailgunMailer:
    config: MailConfig
    opener: object = field(default_factory=lambda: build_opener(NoRedirect()), repr=False)

    def _key(self):
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        try:
            with os.fdopen(os.open(self.config.key_file, flags)) as handle:
                info = os.fstat(handle.fileno())
                if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077:
                    raise MailError("Mailgun key must be a private regular file (0600)")
                key = handle.read(1025).strip()
        except OSError:
            raise MailError("Mailgun sending key is not provisioned or readable") from None
        if not 20 <= len(key) <= 1024 or not re.fullmatch(r"[A-Za-z0-9_-]+", key):
            raise MailError("Mailgun sending key has an invalid format")
        return key

    def send(self, recipient, subject, text, *, category="system", html=None):
        address(recipient)
        if not isinstance(subject, str) or not 1 <= len(subject) <= 200 or any(
                ord(c) < 32 or ord(c) == 127 for c in subject):
            raise ValueError("Invalid email subject")
        if not isinstance(text, str) or not 1 <= len(text.encode("utf-8")) <= 65536:
            raise ValueError("Email body must contain 1–65536 UTF-8 bytes")
        if category not in {"system", "alert", "auth", "test"}:
            raise ValueError("Unknown transactional email category")
        if html is not None and (not isinstance(html, str) or not 1 <= len(html.encode('utf-8')) <= 131072):
            raise ValueError('Invalid email HTML')
        fields = {
            "from": f"RooCore <{self.config.sender}>", "to": recipient,
            "subject": subject, "text": text, "o:tag": f"roocore-{category}",
            "o:tracking": "no", "o:tracking-clicks": "no", "o:tracking-opens": "no",
            "o:require-tls": "yes", "o:skip-verification": "no", "o:dkim": "yes",
        }
        if html is not None:
            fields['html'] = html
        boundary = "roocore" + uuid4().hex
        parts = []
        for name, value in fields.items():
            parts.append((f"--{boundary}\r\nContent-Disposition: form-data; name=\"{name}\"\r\n"
                          "Content-Type: text/plain; charset=utf-8\r\n\r\n" + value + "\r\n").encode())
        body = b"".join(parts) + f"--{boundary}--\r\n".encode()
        key = self._key()
        auth = base64.b64encode(("api:" + key).encode()).decode()
        request = Request(f"https://api.mailgun.net/v3/{self.config.domain}/messages", data=body,
                          headers={"Authorization": "Basic " + auth,
                                   "Content-Type": f"multipart/form-data; boundary={boundary}"},
                          method="POST")
        try:
            with self.opener.open(request, timeout=15) as response:
                payload = json.loads(response.read(16385))
        except HTTPError as error:
            code = error.code
            error.close()
            raise MailError(f"Mailgun rejected the message (HTTP {code})") from None
        except (URLError, OSError, ValueError):
            # An ambiguous timeout may already have queued the message. No automatic
            # retry here: future authentication callers must manage expiry and reuse.
            raise MailError("Mailgun request failed; acceptance is unknown") from None
        message_id = payload.get("id") if isinstance(payload, dict) else None
        if not isinstance(message_id, str) or not re.fullmatch(r"<[^<>\s]{1,250}>", message_id):
            raise MailError("Mailgun returned an invalid acceptance response")
        return message_id  # Queued by Mailgun, not proof of inbox delivery.
