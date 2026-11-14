from datetime import datetime, timedelta, timezone
import threading
from uuid import UUID
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, AwareDatetime
from sqlalchemy import create_engine, DateTime, String, Integer, BigInteger, JSON, select, delete, text, event
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, sessionmaker

ROBOTS = {"roomba-620": "hardware", "sim-620": "simulation"}


def utcnow():
    return datetime.now(timezone.utc)


def aware(value):
    return value.replace(tzinfo=timezone.utc) if value.tzinfo is None else value


class Telemetry(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, allow_inf_nan=False)
    schema_version: Literal[1]
    boot_id: UUID
    seq: int = Field(ge=0, le=2**63 - 1)
    sent_at: AwareDatetime
    uptime_ms: int = Field(ge=0, le=2**63 - 1)
    battery_percent: float | None = Field(default=None, ge=0, le=100)
    battery_mv: int | None = Field(default=None, ge=0, le=30000)
    state: Literal["idle", "charging", "held", "fault", "unknown"] = "unknown"
    firmware: str = Field(min_length=1, max_length=64, pattern=r"^[a-zA-Z0-9._+-]+$")
    local_safety: Literal["unverified", "held", "fault"] = "unverified"


class Base(DeclarativeBase):
    pass


class Robot(Base):
    __tablename__ = "robots"
    id: Mapped[str] = mapped_column(String(32), primary_key=True)
    boot: Mapped[str | None] = mapped_column(String(36))
    last_seen: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    offline_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    snapshot: Mapped[dict | None] = mapped_column(JSON)


class Cursor(Base):
    __tablename__ = "boot_cursors"
    robot_id: Mapped[str] = mapped_column(String(32), primary_key=True)
    boot: Mapped[str] = mapped_column(String(36), primary_key=True)
    seq: Mapped[int] = mapped_column(BigInteger)
    uptime: Mapped[int] = mapped_column(BigInteger)


class Event(Base):
    __tablename__ = "events"
    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    robot_id: Mapped[str] = mapped_column(String(32), index=True)
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
    kind: Mapped[str] = mapped_column(String(32))
    payload: Mapped[dict] = mapped_column(JSON)


class Store:
    def __init__(self, url):
        kwargs = {"pool_pre_ping": True}
        if str(url).startswith("sqlite"):
            kwargs["connect_args"] = {"check_same_thread": False, "timeout": 5}
        else:
            kwargs.update(connect_args={"connect_timeout": 5, "options": "-c statement_timeout=5000"},
                          pool_size=4, max_overflow=2, pool_timeout=5)
        self.engine = create_engine(url, **kwargs)
        if str(url).startswith("sqlite"):
            @event.listens_for(self.engine, "connect")
            def sqlite_pragmas(conn, record):
                conn.execute("PRAGMA journal_mode=WAL")
                conn.execute("PRAGMA synchronous=FULL")
                conn.execute("PRAGMA busy_timeout=5000")
        self.session = sessionmaker(self.engine)
        self.lock = threading.Lock()

    def start(self):
        Base.metadata.create_all(self.engine)
        with self.session.begin() as s:
            for robot_id in ROBOTS:
                if not s.get(Robot, robot_id):
                    s.add(Robot(id=robot_id))

    def healthy(self):
        with self.engine.connect() as conn:
            conn.execute(text("SELECT 1"))

    def ingest(self, robot_id, data: Telemetry, now=None):
        now = now or utcnow()
        if robot_id not in ROBOTS:
            raise ValueError("unknown robot")
        # Device clock must be synchronized. Receipt time cannot make old data fresh.
        age = (now - data.sent_at).total_seconds()
        if not -5 <= age <= 15:
            raise ValueError("timestamp outside acceptance window")
        with self.lock, self.session.begin() as s:
            robot = s.get(Robot, robot_id)
            boot = str(data.boot_id)
            cursor = s.get(Cursor, (robot_id, boot))
            if cursor:
                if robot.boot != boot or data.seq <= cursor.seq or data.uptime_ms < cursor.uptime:
                    raise ValueError("replayed or retired boot")
                cursor.seq, cursor.uptime = data.seq, data.uptime_ms
            else:
                s.add(Cursor(robot_id=robot_id, boot=boot, seq=data.seq, uptime=data.uptime_ms))
            payload = data.model_dump(mode="json")
            robot.boot, robot.last_seen, robot.snapshot = boot, now, payload
            s.add(Event(robot_id=robot_id, received_at=now, kind="telemetry", payload=payload))

    def offline(self, robot_id, now=None):
        with self.lock, self.session.begin() as s:
            robot = s.get(Robot, robot_id)
            if robot and (not robot.offline_at or (robot.last_seen and
                          aware(robot.last_seen) > aware(robot.offline_at))):
                robot.offline_at = now or utcnow()
                s.add(Event(robot_id=robot_id, received_at=robot.offline_at,
                            kind="offline", payload={}))

    def robots(self, connected, fresh_after, stale_seconds, now=None):
        now = now or utcnow()
        rows = []
        with self.session() as s:
            for r in s.scalars(select(Robot).order_by(Robot.id)):
                age = max(0, (now - aware(r.last_seen)).total_seconds()) if r.last_seen else None
                online = bool(connected and r.last_seen and aware(r.last_seen) >= fresh_after
                              and age < stale_seconds and (not r.offline_at or
                                  aware(r.last_seen) > aware(r.offline_at)))
                rows.append(dict(id=r.id, source=ROBOTS[r.id], online=online,
                                 age_seconds=round(age, 1) if age is not None else None,
                                 last_seen=aware(r.last_seen).isoformat() if r.last_seen else None,
                                 telemetry=r.snapshot, hardware_commands_enabled=False))
        return rows

    def history(self, limit=50):
        with self.session() as s:
            return [dict(id=e.id, robot_id=e.robot_id, source=ROBOTS[e.robot_id], kind=e.kind,
                         received_at=aware(e.received_at).isoformat(), payload=e.payload)
                    for e in s.scalars(select(Event).order_by(Event.id.desc()).limit(limit))]

    def prune(self, days):
        with self.session.begin() as s:
            s.execute(delete(Event).where(Event.received_at < utcnow() - timedelta(days=days)))
        # Boot cursors are deliberately retained to reject a replay after restart.
