"""Local users, opaque sessions and one-use recovery tokens. No robot transport."""
import hashlib
import secrets
import threading
import time
import re
from sqlalchemy import Boolean, Float, Integer, JSON, String, delete, select
from sqlalchemy.orm import Mapped, mapped_column
from fastapi import HTTPException
from .store import Base
from .mfa import EmailMFA

PUBLIC_ORIGIN = 'https://roocore.saymour.co.il'
COOKIE = '__Host-roocore'
_hash_lock = threading.Lock()


def digest(value):
    return hashlib.sha256(value.encode()).hexdigest()


def password_hash(password):
    if not isinstance(password, str) or not 15 <= len(password) <= 128:
        raise HTTPException(422, 'Choose a password of 15–128 characters')
    salt = secrets.token_hex(16)
    with _hash_lock:
        value = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=131072,
                               r=8, p=1, maxmem=256 * 1024 * 1024).hex()
    return salt + ':' + value


def verify(password, encoded):
    if not isinstance(password, str) or len(password) > 128:
        return False
    salt, expected = encoded.split(':')
    with _hash_lock:
        actual = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=131072,
                                r=8, p=1, maxmem=256 * 1024 * 1024).hex()
    return secrets.compare_digest(actual, expected)


class User(Base):
    __tablename__ = 'users'
    id: Mapped[str] = mapped_column(String, primary_key=True)
    username: Mapped[str] = mapped_column(String, unique=True)
    email: Mapped[str] = mapped_column(String, unique=True)
    name: Mapped[str] = mapped_column(String)
    password: Mapped[str | None] = mapped_column(String)
    admin: Mapped[bool] = mapped_column(Boolean, default=False)
    enabled: Mapped[bool] = mapped_column(Boolean, default=True)
    grants: Mapped[dict] = mapped_column(JSON, default=dict)


class Session(Base):
    __tablename__ = 'sessions'
    token: Mapped[str] = mapped_column(String, primary_key=True)
    user_id: Mapped[str] = mapped_column(String, index=True)
    expires: Mapped[float] = mapped_column(Float)
    seen: Mapped[float] = mapped_column(Float)


class Recovery(Base):
    __tablename__ = 'recovery_tokens'
    token: Mapped[str] = mapped_column(String, primary_key=True)
    user_id: Mapped[str] = mapped_column(String, index=True)
    expires: Mapped[float] = mapped_column(Float)


class Throttle(Base):
    __tablename__ = 'auth_throttle'
    key: Mapped[str] = mapped_column(String, primary_key=True)
    until: Mapped[float] = mapped_column(Float)
    count: Mapped[int] = mapped_column(Integer)


class Audit(Base):
    __tablename__ = 'account_audit'
    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    at: Mapped[float] = mapped_column(Float, default=time.time)
    actor: Mapped[str] = mapped_column(String)
    action: Mapped[str] = mapped_column(String)
    target: Mapped[str] = mapped_column(String)


class Accounts:
    def __init__(self, store, settings):
        self.store, self.settings = store, settings
        self.lock = threading.RLock()
        self.dummy = None
        self.mfa = EmailMFA(self)

    def start(self):
        # Only bootstrap a completely empty user table. A restart never resurrects an account.
        with self.store.session.begin() as s:
            if s.scalar(select(User.id).limit(1)) is None:
                s.add(User(id=secrets.token_hex(16), username=self.settings.admin_user,
                           email='thfcxv@gmail.com', name='מנהל המערכת',
                           password=password_hash(self.settings.admin_password), admin=True, grants={}))
        self.dummy = password_hash(secrets.token_urlsafe(24))
        self.mfa.start()

    def throttle(self, key, limit, seconds):
        now = time.time()
        with self.lock, self.store.session.begin() as s:
            s.execute(delete(Throttle).where(Throttle.until < now))
            row = s.get(Throttle, key)
            if row and row.count >= limit:
                raise HTTPException(429, 'Too many attempts. Try later.', headers={'Retry-After': str(seconds)})
            if row:
                row.count += 1
            else:
                s.add(Throttle(key=key, count=1, until=now + seconds))

    def login(self, username, password):
        self.throttle('login-global', 40, 300)
        self.throttle('login-' + digest(username.lower()), 8, 900)
        with self.store.session() as s:
            user = s.scalar(select(User).where(User.username == username.lower()))
            ok = verify(password, user.password if user and user.password else self.dummy)
            if not ok or not user or not user.enabled or not user.password:
                raise HTTPException(401, 'Incorrect username or password')
            return self.mfa.begin(user.id, digest(user.password))

    def current(self, token):
        if not token or len(token) > 128:
            raise HTTPException(401, 'Sign in required')
        with self.store.session.begin() as s:
            session = s.get(Session, digest(token))
            if not session or session.expires < time.time() or session.seen < time.time() - 1800:
                raise HTTPException(401, 'Sign in required')
            user = s.get(User, session.user_id)
            if not user or not user.enabled:
                raise HTTPException(401, 'Sign in required')
            session.seen = time.time()
            return self.describe(user)

    @staticmethod
    def describe(user):
        return dict(id=user.id, username=user.username, email=user.email, name=user.name,
                    admin=user.admin, enabled=user.enabled, grants=user.grants)

    def issue(self, user_id):
        token = secrets.token_urlsafe(32)
        with self.store.session.begin() as s:
            s.execute(delete(Recovery).where((Recovery.user_id == user_id) | (Recovery.expires < time.time())))
            s.add(Recovery(token=digest(token), user_id=user_id, expires=time.time() + 1800))
        return token

    def reset(self, token, password):
        self.throttle('reset-global', 20, 300)
        hashed = password_hash(password)
        with self.lock, self.store.session.begin() as s:
            row = s.get(Recovery, digest(token))
            if not row or row.expires < time.time():
                raise HTTPException(400, 'Link is invalid or expired')
            user = s.get(User, row.user_id)
            if not user or not user.enabled:
                raise HTTPException(400, 'Link is invalid or expired')
            user.password = hashed
            s.execute(delete(Recovery).where(Recovery.user_id == user.id))
            s.execute(delete(Session).where(Session.user_id == user.id))
            self.mfa.revoke(s, user.id)
            s.add(Audit(actor=user.id, action='password_reset', target=user.id))
            return user.email


def permitted(user, robot, permission='view'):
    return user['admin'] or permission in user['grants'].get(robot, [])
