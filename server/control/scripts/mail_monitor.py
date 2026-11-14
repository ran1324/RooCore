"""Independent, rate-limited host health mail; never reads robot telemetry or secrets into email."""
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from urllib.request import build_opener, ProxyHandler

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from app.mailer import MailConfig, MailError, MailgunMailer
from app.email_templates import notice

STATE_DIR = Path("/var/lib/roocore-mail")
UNITS = ("roocore.service", "mosquitto.service", "roocore-tunnel.service")


def health_issues():
    issues = []
    try:
        # Never use ambient proxies for the loopback health check.
        with build_opener(ProxyHandler({})).open("http://127.0.0.1:8780/health/ready", timeout=5) as r:
            payload = json.loads(r.read(4096))
            if r.status != 200 or not isinstance(payload, dict) or payload.get("status") != "ready":
                issues.append("RooCore database/MQTT readiness failed")
    except (OSError, ValueError):
        issues.append("RooCore database/MQTT readiness failed")
    for unit in UNITS:
        try:
            result = subprocess.run(["systemctl", "is-active", unit], capture_output=True, timeout=5)
            active = result.returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            active = False
        if not active:
            issues.append(unit + " is not active")
    try:
        result = subprocess.run(["systemctl", "is-failed", "roocore-backup.service"], capture_output=True, timeout=5)
        if result.stdout.strip() == b"failed":
            issues.append("RooCore database backup failed")
    except (OSError, subprocess.TimeoutExpired):
        issues.append("RooCore backup status could not be checked")
    disk = shutil.disk_usage("/var/lib/roocore")
    if disk.free < 2 * 1024**3 or disk.free / disk.total < .05:
        issues.append("RooCore data volume has less than 2 GiB or 5% free space")
    return issues


def tick(state, issues, now, send):
    state = dict(state)
    count = state["failures"] = state.get("failures", 0) + 1 if issues else 0
    alerted = state.get("alerted", False)
    due = bool(issues and count >= 3 and (not alerted or now - state.get("sent_at", 0) >= 3600))
    recovery = bool(not issues and alerted)
    if not (due or recovery) or now - state.get("attempted_at", 0) < 900:
        return state
    state["attempted_at"] = now
    stamp = datetime.fromtimestamp(now, timezone.utc).isoformat()
    subject = "RooCore — השירות חזר לפעול" if recovery else "RooCore — נדרשת בדיקת תקלה"
    body = ("בדיקות השירות חזרו למצב תקין." if recovery else "נמצאו בעיות:\n" + "\n".join("• " + x for x in issues))
    body += f"\n\nבדיקה: {stamp}\nמערכת: https://roocore.saymour.co.il/\n"
    body += "הודעה זו מתייחסת לבדיקות שרת בלבד.\n"
    try:
        send(subject, body)
    except MailError:
        print("RooCore health email failed; next attempt in at least 15 minutes", file=sys.stderr)
    else:
        state.update(alerted=not recovery, sent_at=now)
        print("Mailgun accepted RooCore " + ("recovery" if recovery else "alert") + " message")
    return state


def main():
    os.umask(0o077)
    STATE_DIR.mkdir(mode=0o700, exist_ok=True)
    with (STATE_DIR / "monitor.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        path = STATE_DIR / "monitor.json"
        state = json.loads(path.read_text()) if path.exists() else {}
        config = MailConfig.load()
        mailer = MailgunMailer(config)
        issues = health_issues()
        def deliver(subject, body):
            message = notice(subject.removeprefix('RooCore — '), body)
            return mailer.send(config.alert_to, message.subject, message.text,
                               html=message.html, category='alert')
        state = tick(state, issues, time.time(), deliver)
        temporary = path.with_suffix(".new")
        temporary.write_text(json.dumps(state))
        temporary.replace(path)
        print("RooCore health check: " + ("unhealthy" if issues else "ready"))


if __name__ == "__main__":
    main()
