import re
import time
from concurrent.futures import ThreadPoolExecutor
from types import SimpleNamespace
from unittest.mock import patch
import pytest
from sqlalchemy import delete, select
from fastapi import HTTPException
from app.accounts import COOKIE, Recovery, Session, Throttle, User, digest
from app.mfa import CHALLENGE_COOKIE, LoginChallenge, AuthPolicy
from app.email_templates import render
from app.mailer import MailError
from test_server import client, settings, signin


@pytest.fixture
def mailbox(monkeypatch):
    sent = []
    monkeypatch.setattr('app.account_routes.MailConfig.load', lambda: None)
    monkeypatch.setattr('app.account_routes.MailgunMailer.send',
                        lambda self, to, subject, text, **kw: sent.append((to, subject, text, kw)) or '<fake>')
    return sent


def begin(client, settings, code=34567890):
    with patch('app.mfa.secrets.randbelow', return_value=code):
        response = client.post('/api/auth/login', json={'username': 'admin', 'password': settings.admin_password})
    assert response.status_code == 200, response.text
    return response


def test_password_alone_never_issues_session_cookie_bound_code(client, settings, mailbox):
    response = begin(client, settings)
    assert response.json()['mfa_required']
    assert COOKIE not in client.cookies and CHALLENGE_COOKIE in client.cookies
    assert 'HttpOnly' in response.headers['set-cookie'] and 'Secure' in response.headers['set-cookie']
    assert client.get('/api/me').status_code == 401
    assert client.get('/api/status').status_code == 401
    assert '34567890' not in response.text and 'thfcxv@gmail.com' not in response.text
    with client.app.state.store.session() as s:
        assert s.scalar(select(Session)) is None
        row = s.scalar(select(LoginChallenge))
        assert row.verifier != digest('34567890') and '34567890' not in row.verifier
    cookie = client.cookies.get(CHALLENGE_COOKIE)
    client.cookies.clear()
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 401
    client.cookies.set(CHALLENGE_COOKIE, cookie)
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 200
    assert client.get('/api/me').status_code == 200
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 401
    assert mailbox[0][0] == 'thfcxv@gmail.com' and 'html' in mailbox[0][3]


def test_bad_password_does_not_email_and_attempt_limit_is_persistent(client, settings, mailbox):
    assert client.post('/api/auth/login', json={'username': 'admin', 'password': 'wrong'}).status_code == 401
    assert not mailbox
    begin(client, settings)
    for attempt in range(5):
        assert client.post('/api/auth/verify', json={'code': '00000000'}).status_code == 401
        with client.app.state.store.session() as s:
            row = s.scalar(select(LoginChallenge))
            assert (row.attempts == attempt + 1) if attempt < 4 else row is None
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 401
    assert client.get('/api/me').status_code == 401


def test_expiry_resend_cooldown_and_old_code_revocation(client, settings, mailbox):
    begin(client, settings)
    old_token = client.cookies.get(CHALLENGE_COOKIE)
    assert client.post('/api/auth/resend', json={}).status_code == 429
    with client.app.state.store.session.begin() as s:
        s.execute(delete(Throttle).where(Throttle.key.like('otp-cooldown-%')))
    with patch('app.mfa.secrets.randbelow', return_value=87654321):
        assert client.post('/api/auth/resend', json={}).status_code == 200
    assert client.app.state.accounts.mfa.info(old_token) is None
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 401
    with client.app.state.store.session.begin() as s:
        s.scalar(select(LoginChallenge)).expires = time.time() - 1
    assert client.post('/api/auth/verify', json={'code': '87654321'}).status_code == 401
    assert client.get('/api/auth/challenge').json() == {'mfa_required': False}


def test_mail_failure_is_closed_and_challenge_not_kept(client, settings, mailbox, monkeypatch):
    def fail(*a, **kw): raise MailError('safe failure')
    monkeypatch.setattr('app.account_routes.MailgunMailer.send', fail)
    response = client.post('/api/auth/login', json={'username': 'admin', 'password': settings.admin_password})
    assert response.status_code == 503 and COOKIE not in client.cookies
    with client.app.state.store.session() as s:
        assert s.scalar(select(LoginChallenge)) is None
        assert s.scalar(select(Session)) is None


def test_reset_invalidates_pending_code_and_all_sessions(client, settings, mailbox):
    signin(client, settings)
    session_cookie = client.cookies.get(COOKIE)
    with client.app.state.store.session.begin() as s:
        s.execute(delete(Throttle).where(Throttle.key.like('otp-cooldown-%')))
    begin(client, settings)
    assert client.post('/api/auth/forgot', json={'email': 'thfcxv@gmail.com'}).status_code == 200
    token = re.search(r'/reset#([A-Za-z0-9_-]+)', mailbox[-1][2]).group(1)
    assert client.post('/api/auth/reset', json={'token': token, 'password': 'brand new password for this test'}).status_code == 200
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 401
    assert client.get('/api/me').status_code == 401
    assert client.post('/api/auth/reset', json={'token': token, 'password': 'another new password for testing'}).status_code == 400
    assert 'הסיסמה עודכנה' in mailbox[-1][1]


def test_account_disabled_or_password_revision_changed_blocks_code(client, settings, mailbox):
    begin(client, settings)
    with client.app.state.store.session.begin() as s:
        s.scalar(select(User)).enabled = False
    assert client.post('/api/auth/verify', json={'code': '34567890'}).status_code == 401
    assert client.get('/api/auth/challenge').json()['mfa_required'] is False


def test_simultaneous_code_consumption_has_one_winner(client, settings, mailbox):
    begin(client, settings)
    token = client.cookies.get(CHALLENGE_COOKIE)
    def consume(_):
        try:
            client.app.state.accounts.mfa.complete(token, '34567890')
            return True
        except HTTPException:
            return False
    with ThreadPoolExecutor(max_workers=2) as pool:
        assert sorted(pool.map(consume, range(2))) == [False, True]
    with client.app.state.store.session() as s:
        assert len(list(s.scalars(select(Session)))) == 1


def test_first_mfa_migration_revokes_legacy_sessions_once(client, settings, mailbox):
    signin(client, settings)
    with client.app.state.store.session.begin() as s:
        s.execute(delete(AuthPolicy))
    client.app.state.accounts.mfa.start()
    assert client.get('/api/me').status_code == 401
    signin(client, settings)
    client.app.state.accounts.mfa.start()
    assert client.get('/api/me').status_code == 200


def test_otp_origin_validation_and_no_untrusted_forwarded_email(client, settings, mailbox):
    response = client.post('/api/auth/login', json={'username': 'admin', 'password': settings.admin_password},
                           headers={'Origin': 'https://other.example', 'CF-Connecting-IP': '1.2.3.4'})
    assert response.status_code == 403 and not mailbox
    assert client.post('/api/auth/login', json={'username': 'admin', 'password': settings.admin_password,
                                               'email': 'attacker@example.com'}).status_code == 422
    redirect = client.get('/login', headers={'X-Forwarded-Proto': 'http', 'Host': 'attacker.example'}, follow_redirects=False)
    assert redirect.status_code == 308
    assert redirect.headers['location'] == 'https://roocore.saymour.co.il/login'


def test_email_templates_have_rtl_plaintext_no_external_assets_or_injection():
    for kind, args in [('login', {'code': '00001234'}), ('recovery', {'token': 'x' * 43}),
                       ('invitation', {'token': 'x' * 43}), ('password_changed', {}), ('test', {})]:
        email = render(kind, **args)
        assert 'dir="rtl"' in email.html and email.text and email.subject.startswith('RooCore')
        assert '<script' not in email.html and '<img' not in email.html
        if kind in ('recovery', 'invitation'):
            assert 'https://roocore.saymour.co.il/reset#' in email.html
    with pytest.raises(ValueError): render('login', code='<script>')
    with pytest.raises(ValueError): render('recovery', token='https://evil.example/')
