"""Email verification challenges, bound to an HttpOnly browser cookie and password revision."""
import hashlib
import hmac
import secrets
import time
from fastapi import HTTPException
from sqlalchemy import Boolean, Float, Integer, String, delete, select
from sqlalchemy.orm import Mapped, mapped_column
from .store import Base

CHALLENGE_COOKIE = '__Host-roocore-login'
TTL = 600


def hashed(value):
    return hashlib.sha256(value.encode()).hexdigest()


class LoginChallenge(Base):
    __tablename__ = 'email_login_challenges'
    token: Mapped[str] = mapped_column(String, primary_key=True)
    user_id: Mapped[str] = mapped_column(String, index=True)
    verifier: Mapped[str] = mapped_column(String)
    credential: Mapped[str] = mapped_column(String)
    expires: Mapped[float] = mapped_column(Float)
    attempts: Mapped[int] = mapped_column(Integer, default=0)
    ready: Mapped[bool] = mapped_column(Boolean, default=False)


class AuthPolicy(Base):
    __tablename__ = 'auth_policy'
    name: Mapped[str] = mapped_column(String, primary_key=True)


class EmailMFA:
    def __init__(self, accounts):
        self.accounts = accounts

    def verifier(self, token, code):
        # The separate server secret prevents offline enumeration of eight-digit OTPs
        # after a database-only disclosure. Test Settings use a non-production secret.
        key = self.accounts.settings.auth_secret or self.accounts.settings.admin_password
        return hmac.new(key.encode(), (token + ':' + code).encode(), hashlib.sha256).hexdigest()

    def start(self):
        from .accounts import Session
        with self.accounts.lock, self.accounts.store.session.begin() as s:
            if not s.get(AuthPolicy, 'email-otp-v1'):
                # Pre-upgrade password-only sessions cannot bypass the new second step.
                s.execute(delete(Session))
                s.add(AuthPolicy(name='email-otp-v1'))

    @staticmethod
    def revoke(session, uid):
        session.execute(delete(LoginChallenge).where(LoginChallenge.user_id == uid))

    def begin(self, uid, expected_credential=None):
        from .accounts import User
        self.accounts.throttle('otp-send-' + uid, 5, 900)
        self.accounts.throttle('otp-cooldown-' + uid, 1, 60)
        token, code, now = secrets.token_urlsafe(32), f'{secrets.randbelow(100_000_000):08d}', time.time()
        with self.accounts.lock, self.accounts.store.session.begin() as s:
            user = s.get(User, uid)
            if (not user or not user.enabled or not user.password or
                    (expected_credential and hashed(user.password) != expected_credential)):
                raise HTTPException(401, 'Sign in again')
            s.execute(delete(LoginChallenge).where((LoginChallenge.expires < now) | (LoginChallenge.user_id == uid)))
            s.add(LoginChallenge(token=hashed(token), user_id=uid, verifier=self.verifier(token, code),
                                 credential=hashed(user.password), expires=now + TTL, attempts=0, ready=False))
            email = user.email
        return token, code, email

    def resend(self, token):
        if not token or len(token) > 128:
            raise HTTPException(401, 'Sign in again')
        with self.accounts.lock, self.accounts.store.session() as s:
            challenge = s.get(LoginChallenge, hashed(token))
            if not challenge or challenge.expires <= time.time():
                raise HTTPException(401, 'Sign in again')
            return self.begin(challenge.user_id, challenge.credential)

    def delivery(self, token, accepted):
        with self.accounts.lock, self.accounts.store.session.begin() as s:
            row = s.get(LoginChallenge, hashed(token))
            if row:
                if accepted:
                    row.ready = True
                else:
                    s.delete(row)

    def info(self, token):
        from .accounts import User
        if not token or len(token) > 128:
            return None
        with self.accounts.store.session() as s:
            row = s.get(LoginChallenge, hashed(token))
            user = s.get(User, row.user_id) if row else None
            if not row or not row.ready or row.expires <= time.time() or not user or not user.enabled or not user.password or row.credential != hashed(user.password):
                return None
            local, domain = user.email.split('@', 1)
            return {'mfa_required': True, 'email': local[:1] + '***@' + domain,
                    'expires_in': max(0, int(row.expires - time.time())),
                    'resend_after': max(0, int(row.expires - TTL + 61 - time.time()))}

    def complete(self, token, code):
        from .accounts import User, Session, Audit, digest
        self.accounts.throttle('otp-verify-global', 100, 300)
        if not token or len(token) > 128:
            raise HTTPException(401, 'Code is invalid or expired')
        session_token, now, success = secrets.token_urlsafe(32), time.time(), False
        with self.accounts.lock, self.accounts.store.session.begin() as s:
            row = s.get(LoginChallenge, hashed(token))
            user = s.get(User, row.user_id) if row else None
            if row and (not row.ready or row.expires <= now or not user or not user.enabled or
                        not user.password or row.credential != hashed(user.password)):
                s.delete(row)
            elif row:
                row.attempts += 1
                if hmac.compare_digest(row.verifier, self.verifier(token, code)):
                    s.execute(delete(Session).where((Session.expires < now) | (Session.seen < now - 1800)))
                    s.add(Session(token=digest(session_token), user_id=user.id, expires=now + 28800, seen=now))
                    s.add(Audit(actor=user.id, action='login_email_verified', target=user.id))
                    s.delete(row)
                    success = True
                else:
                    if row.attempts >= 5:
                        s.delete(row)
                    s.add(Audit(actor=user.id, action='login_code_rejected', target=user.id))
        # Raise after committing the attempt count/deletion, never roll it back.
        if not success:
            raise HTTPException(401, 'Code is invalid or expired')
        return session_token
