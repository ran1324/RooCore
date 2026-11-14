"""Mail transport and monitor tests: no real email, credentials, hardware or service changes."""
from email import policy
from email.parser import BytesParser
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from urllib.error import HTTPError, URLError

from app.mailer import MailConfig, MailError, MailgunMailer, NoRedirect

spec = importlib.util.spec_from_file_location("mail_monitor", Path(__file__).parents[1] / "scripts/mail_monitor.py")
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


class FakeOpener:
    def __init__(self, error=None, payload=None):
        self.calls = []
        self.error = error
        self.payload = payload or {"id": "<test@saymour.co.il>", "message": "Queued. Thank you."}

    def open(self, request, timeout):
        self.calls.append((request, timeout))
        if self.error:
            raise self.error
        return io.BytesIO(json.dumps(self.payload).encode())


class MailTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.key = Path(self.temp.name) / "sending_key"
        self.key.write_text("test_only_" + "x" * 40)
        self.key.chmod(0o600)
        self.config = MailConfig(key_file=str(self.key))
        self.opener = FakeOpener()
        self.mailer = MailgunMailer(self.config, self.opener)

    def tearDown(self):
        self.temp.cleanup()

    def test_utf8_authenticated_private_transport_and_acceptance(self):
        self.assertEqual(self.mailer.send("owner@example.com", "בדיקת דואר", "קוד בדיקה בלבד", category="auth"),
                         "<test@saymour.co.il>")
        request, timeout = self.opener.calls[0]
        self.assertEqual(request.full_url, "https://api.mailgun.net/v3/saymour.co.il/messages")
        self.assertEqual(timeout, 15)
        self.assertTrue(request.get_header("Authorization").startswith("Basic "))
        mime = BytesParser(policy=policy.default).parsebytes(
            ("Content-Type: " + request.get_header("Content-type") + "\r\n\r\n").encode() + request.data)
        fields = {p.get_param("name", header="content-disposition"): p.get_content().rstrip("\r\n")
                  for p in mime.iter_parts()}
        self.assertEqual(fields["text"], "קוד בדיקה בלבד")
        self.assertEqual(fields["subject"], "בדיקת דואר")
        self.assertEqual(fields["to"], "owner@example.com")
        self.assertEqual(fields["from"], "RooCore <no-reply@saymour.co.il>")
        for option in ("o:tracking", "o:tracking-clicks", "o:tracking-opens", "o:skip-verification"):
            self.assertEqual(fields[option], "no")
        self.assertEqual(fields["o:require-tls"], "yes")
        self.assertEqual(fields["o:tag"], "roocore-auth")
        self.assertNotIn(self.key.read_bytes(), request.data)

    def test_rejects_header_recipient_and_sender_injection(self):
        for recipient in ("owner@example.com\r\nBcc: other@example.com", "a@example.com,b@example.com"):
            with self.assertRaises(ValueError):
                self.mailer.send(recipient, "subject", "body")
        with self.assertRaises(ValueError):
            self.mailer.send("owner@example.com", "hello\nBcc: other@example.com", "body")
        with self.assertRaises(ValueError):
            MailConfig(sender="a@other.example")
        self.assertEqual(self.opener.calls, [])

    def test_missing_public_or_symlinked_key_never_sends(self):
        self.key.chmod(0o644)
        with self.assertRaises(MailError):
            self.mailer.send("owner@example.com", "s", "body")
        self.key.unlink()
        with self.assertRaises(MailError):
            self.mailer.send("owner@example.com", "s", "body")
        target = self.key.with_name("target")
        target.write_text("x" * 40)
        target.chmod(0o600)
        self.key.symlink_to(target)
        with self.assertRaises(MailError):
            self.mailer.send("owner@example.com", "s", "body")
        self.assertEqual(self.opener.calls, [])

    def test_provider_errors_are_sanitized_and_not_retried(self):
        for error in (HTTPError("https://api.mailgun.net", 401, "secret", {}, io.BytesIO(b"private body")),
                      URLError("private token or request")):
            opener = FakeOpener(error)
            with self.assertRaises(MailError) as result:
                MailgunMailer(self.config, opener).send("owner@example.com", "s", "body")
            self.assertNotIn("private", str(result.exception))
            self.assertNotIn("secret", str(result.exception))
            self.assertEqual(len(opener.calls), 1)
        self.assertIsNone(NoRedirect().redirect_request(None, None, 302, "", {}, "https://other.example"))

    def test_invalid_provider_response_is_not_success(self):
        for payload in ({"message": "Queued"}, ["wrong"], {"id": "value\nwith control"}):
            with self.assertRaises(MailError):
                MailgunMailer(self.config, FakeOpener(payload=payload)).send("owner@example.com", "s", "body")

    def test_monitor_debounce_persistence_cooldown_and_recovery(self):
        sent = []
        send = lambda subject, body: sent.append((subject, body))
        state = {}
        for now in (10000, 10060):
            state = monitor.tick(state, ["RooCore service failed"], now, send)
        self.assertEqual(sent, [])
        state = monitor.tick(state, ["RooCore service failed"], 10120, send)
        self.assertEqual(len(sent), 1)
        state = json.loads(json.dumps(state))  # Simulate a new timer process.
        state = monitor.tick(state, ["RooCore service failed"], 11020, send)
        self.assertEqual(len(sent), 1)
        state = monitor.tick(state, [], 11021, send)
        self.assertEqual(len(sent), 2)
        self.assertFalse(state["alerted"])
        state = monitor.tick(state, [], 12000, send)
        self.assertEqual(len(sent), 2)

    def test_monitor_retries_failed_alert_and_failed_recovery(self):
        def fail(*args):
            raise MailError("sanitized failure")
        sent = []
        send = lambda *args: sent.append(args)
        state = monitor.tick({"failures": 2}, ["failure"], 10000, fail)
        self.assertFalse(state.get("alerted", False))
        state = monitor.tick(state, ["failure"], 10060, send)
        self.assertEqual(sent, [])
        state = monitor.tick(state, ["failure"], 10900, send)
        self.assertTrue(state["alerted"])
        state = monitor.tick(state, [], 11800, fail)
        self.assertTrue(state["alerted"])
        state = monitor.tick(state, [], 12700, send)
        self.assertFalse(state["alerted"])
        self.assertEqual(len(sent), 2)


if __name__ == "__main__":
    unittest.main()
