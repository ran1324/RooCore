"""Local SQLite recordings and service log. No robot control or wear estimates."""

import calendar
from datetime import date, datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import sqlite3


CARE_SOURCE = "https://support.irobot.co.uk/articles/en_GB/Knowledge/11179"
PROCEDURE_SOURCE = "https://homesupport.irobot.com/articles/en_US/Knowledge/8247"
PARTS = {
    "bin": ("מיכל", "after_use", None),
    "filter": ("פילטר", "weekly", (2, 2)),
    "main_brushes": ("מברשות מרכזיות", "weekly", (6, 12)),
    "front_caster": ("גלגל קדמי", "fortnightly", (12, 12)),
    "side_brush": ("מברשת צד", "monthly", None),
    "cliff_sensors": ("חיישני מצוק", "monthly", None),
    "charging_contacts": ("מגעי טעינה", "monthly", None),
}


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def _json(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"), sort_keys=True)


def _months(day, count):
    month = day.year * 12 + day.month - 1 + count
    year, month = divmod(month, 12)
    return date(year, month + 1, min(day.day, calendar.monthrange(year, month + 1)[1]))


class History:
    def __init__(self, path):
        if str(path) != ":memory:":
            Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(str(path), check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS samples (
                id INTEGER PRIMARY KEY, source TEXT, robot TEXT, received TEXT,
                digest TEXT, payload TEXT, envelope TEXT,
                UNIQUE(source, robot, digest));
            CREATE TABLE IF NOT EXISTS missions (
                source TEXT, robot TEXT, mission TEXT, first_seen TEXT,
                last_seen TEXT, summary TEXT, PRIMARY KEY(source, robot, mission));
            CREATE TABLE IF NOT EXISTS usage (
                source TEXT, robot TEXT, brush_s REAL DEFAULT 0, vacuum_s REAL DEFAULT 0,
                observed_s REAL DEFAULT 0, gaps INTEGER DEFAULT 0, PRIMARY KEY(source, robot));
            CREATE TABLE IF NOT EXISTS settings (
                source TEXT, robot TEXT, payload TEXT, PRIMARY KEY(source, robot));
            CREATE TABLE IF NOT EXISTS service (
                id INTEGER PRIMARY KEY, source TEXT, robot TEXT, part TEXT, action TEXT,
                day TEXT, identity TEXT, recorded TEXT, usage TEXT);
        """)

    def close(self):
        self.db.close()

    def record(self, source, robot, payload, summary, envelope=None, usage=None):
        encoded = _json(payload)
        digest = hashlib.sha256(encoded.encode()).hexdigest()
        now = utc_now()
        with self.db:
            cursor = self.db.execute(
                "INSERT OR IGNORE INTO samples(source,robot,received,digest,payload,envelope) VALUES(?,?,?,?,?,?)",
                (source, robot, now, digest, encoded, _json(envelope) if envelope else None))
            if not cursor.rowcount:
                return False
            mission = summary.get("mission_id")
            if mission:
                self.db.execute("""INSERT INTO missions VALUES(?,?,?,?,?,?)
                    ON CONFLICT(source,robot,mission) DO UPDATE SET last_seen=excluded.last_seen,summary=excluded.summary""",
                    (source, robot, mission, now, now, _json(summary)))
            values = usage or {}
            self.db.execute("""INSERT INTO usage VALUES(?,?,?,?,?,?)
                ON CONFLICT(source,robot) DO UPDATE SET
                brush_s=brush_s+excluded.brush_s,vacuum_s=vacuum_s+excluded.vacuum_s,
                observed_s=observed_s+excluded.observed_s,gaps=gaps+excluded.gaps""",
                (source, robot, values.get("brush_s", 0), values.get("vacuum_s", 0),
                 values.get("observed_s", 0), values.get("gaps", 0)))
        return True

    def envelopes(self, source, robot):
        for row in self.db.execute("SELECT envelope FROM samples WHERE source=? AND robot=? AND envelope IS NOT NULL ORDER BY id", (source, robot)):
            yield json.loads(row[0])

    def usage(self, source, robot):
        row = self.db.execute("SELECT brush_s,vacuum_s,observed_s,gaps FROM usage WHERE source=? AND robot=?", (source, robot)).fetchone()
        result = dict(row) if row else dict(brush_s=0, vacuum_s=0, observed_s=0, gaps=0)
        result["missions"] = self.db.execute("SELECT count(*) FROM missions WHERE source=? AND robot=?", (source, robot)).fetchone()[0]
        result["meaning"] = "observed commanded time; not mechanical wear or complete lifetime usage"
        return result

    def history(self, source, robot, limit=30):
        rows = self.db.execute("SELECT first_seen,last_seen,summary FROM missions WHERE source=? AND robot=? ORDER BY last_seen DESC LIMIT ?", (source, robot, limit))
        return {"missions": [dict(json.loads(r["summary"]), first_seen=r["first_seen"], last_seen=r["last_seen"]) for r in rows], "source": source}

    def settings(self, source, robot, changes=None):
        row = self.db.execute("SELECT payload FROM settings WHERE source=? AND robot=?", (source, robot)).fetchone()
        values = json.loads(row[0]) if row else {"pets": False}
        if changes is not None:
            if set(changes) != {"pets"} or type(changes["pets"]) is not bool:
                raise ValueError("settings expects {pets: boolean}")
            values.update(changes)
            with self.db:
                self.db.execute("INSERT OR REPLACE INTO settings VALUES(?,?,?)", (source, robot, _json(values)))
        return values

    def event(self, source, robot, body, today=None):
        today = today or date.today()
        if not isinstance(body, dict) or set(body) - {"part", "action", "date", "identity"}:
            raise ValueError("invalid maintenance event")
        part, action = body.get("part"), body.get("action")
        if part not in PARTS or action not in ("installed", "cleaned", "replaced", "snoozed"):
            raise ValueError("unknown part or maintenance action")
        try:
            day = date.fromisoformat(body["date"])
        except (TypeError, ValueError, KeyError):
            raise ValueError("date must be YYYY-MM-DD") from None
        if (action != "snoozed" and day > today) or (action == "snoozed" and day < today):
            raise ValueError("service date cannot be in the future; snooze cannot be in the past")
        identity = body.get("identity")
        if identity is not None and (not isinstance(identity, str) or len(identity) > 200):
            raise ValueError("part identity must be a short string")
        with self.db:
            self.db.execute("INSERT INTO service(source,robot,part,action,day,identity,recorded,usage) VALUES(?,?,?,?,?,?,?,?)",
                (source, robot, part, action, day.isoformat(), identity, utc_now(), _json(self.usage(source, robot))))
        return self.maintenance(source, robot, today)

    def maintenance(self, source, robot, today=None):
        today = today or date.today()
        settings, usage = self.settings(source, robot), self.usage(source, robot)
        events = [dict(r) for r in self.db.execute("SELECT * FROM service WHERE source=? AND robot=? ORDER BY id", (source, robot))]
        parts = []
        for key, (name, frequency, replace) in PARTS.items():
            selected = [e for e in events if e["part"] == key]
            installation = next((e for e in reversed(selected) if e["action"] in ("installed", "replaced")), None)
            cleaned = next((e for e in reversed(selected) if e["action"] == "cleaned"), None)
            snoozed = next((e for e in reversed(selected) if e["action"] == "snoozed"), None)
            installed = date.fromisoformat(installation["day"]) if installation else None
            clean_day = date.fromisoformat(cleaned["day"]) if cleaned else None
            clean_base = max(d for d in (clean_day, installed) if d) if clean_day or installed else None
            effective_frequency = "twice_weekly" if frequency == "weekly" and settings["pets"] else frequency
            clean_due = None
            if clean_base and effective_frequency != "after_use":
                if effective_frequency == "monthly":
                    clean_due = _months(clean_base, 1)
                else:
                    # Date-only reminders prompt on day three for twice-weekly
                    # care; this conservative UI schedule does not estimate wear.
                    days = {"weekly": 7, "twice_weekly": 3, "fortnightly": 14}[effective_frequency]
                    clean_due = clean_base + timedelta(days=days)
            window = [_months(installed, n).isoformat() for n in replace] if installed and replace else None
            baseline = json.loads(installation["usage"]) if installation else None
            cleaned_usage = json.loads(cleaned["usage"]) if cleaned else None
            after_use_due = effective_frequency == "after_use" and (
                usage["brush_s"] > (cleaned_usage["brush_s"] if cleaned_usage else 0))
            snoozed_until = snoozed["day"] if snoozed else None
            parts.append({"part": key, "label": name, "identity": installation["identity"] if installation else None,
                "installed": installed.isoformat() if installed else None, "last_cleaned": cleaned["day"] if cleaned else None,
                "clean_frequency": effective_frequency, "clean_due": clean_due.isoformat() if clean_due else None,
                "cleaning_due": bool(after_use_due or (clean_due and clean_due <= today)),
                "replacement_window": window, "replacement_review_due": bool(window and window[0] <= today.isoformat()),
                "replacement_overdue": bool(window and window[1] <= today.isoformat()),
                "snoozed_until": snoozed_until, "snoozed": bool(snoozed_until and snoozed_until > today.isoformat()),
                "wear_percent": None, "usage_since_recorded_installation": ({k: round(max(0, usage[k] - baseline[k]), 2) for k in ("brush_s", "vacuum_s", "missions")} if baseline else None),
                "usage_date_limit": "Counters start when the date was recorded; backdated service cannot reconstruct missing usage.",
                "note": "אין לשטוף את הפילטר" if key == "filter" else "לנקות גם את המיסבים" if key == "main_brushes" else None,
                "source_url": CARE_SOURCE, "procedure_url": PROCEDURE_SOURCE})
        return {"source": source, "settings": settings, "parts": parts, "usage": usage,
                "events": [{k: e[k] for k in ("id", "part", "action", "day", "identity", "recorded")} for e in events[-100:]],
                "date_provenance": "user_reported", "wear_percent": None}
