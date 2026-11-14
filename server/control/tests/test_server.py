from datetime import timedelta
import json
from types import SimpleNamespace
from uuid import uuid4
from unittest.mock import patch

from fastapi.testclient import TestClient
import pytest

from app.config import Settings
from app.main import create_app
from app.mqtt import Receiver
from app.store import Store, Telemetry, utcnow


def sample(**overrides):
    data = dict(schema_version=1, boot_id=str(uuid4()), seq=1, sent_at=utcnow().isoformat(),
                uptime_ms=1000, battery_percent=70.0, state="held", firmware="test-1",
                local_safety="unverified")
    data.update(overrides)
    return Telemetry.model_validate_json(json.dumps(data))


@pytest.fixture
def settings(tmp_path):
    return Settings(f"sqlite:///{tmp_path}/test.sqlite3", "admin", "a" * 48, "b" * 48)


@pytest.fixture
def store(settings):
    db = Store(settings.database_url)
    db.start()
    yield db
    db.engine.dispose()


class FakeReceiver:
    def __init__(self, settings, store):
        self.connected, self.rejected = True, 0
        self.fresh_after = utcnow()
    def start(self):
        pass
    def stop(self):
        pass


@pytest.fixture
def client(settings):
    app = create_app(settings, FakeReceiver)
    with TestClient(app, base_url="https://roocore.saymour.co.il", headers={"Origin":"https://roocore.saymour.co.il"}, raise_server_exceptions=False) as c:
        yield c


def signin(client, settings, username='admin', password=None):
    # Test mailbox + OTP generator only; production has no bypass or test mode.
    from app.accounts import Throttle
    from sqlalchemy import delete
    with client.app.state.store.session.begin() as s:
        s.execute(delete(Throttle).where(Throttle.key.like('otp-cooldown-%')))
    with patch('app.mfa.secrets.randbelow', return_value=12345678), \
            patch('app.account_routes.MailConfig.load', return_value=None), \
            patch('app.account_routes.MailgunMailer.send', return_value='<test>'):
        response = client.post('/api/auth/login', json={'username': username, 'password': password or settings.admin_password})
    assert response.status_code == 200, response.text
    assert response.json()['mfa_required'] is True
    response = client.post('/api/auth/verify', json={'code': '12345678'})
    assert response.status_code == 200, response.text
    client.headers['X-CSRF-Token'] = client.get('/api/me').json()['csrf']
    return response


def test_auth_and_security_headers(client, settings):
    for url in ('/api/status', '/api/events'):
        assert client.get(url).status_code == 401
    assert client.post('/api/auth/login', json={'username':'admin','password':'bad'}).status_code == 401
    signin(client, settings)
    for url in ('/', '/api/status', '/api/events', '/assets/app.js', '/assets/style.css', '/assets/fonts/heebo-variable.ttf'):
        result = client.get(url)
        assert result.status_code == 200
        assert result.headers['cache-control'] == 'no-store'
        assert "frame-ancestors 'none'" in result.headers['content-security-policy']
    assert client.get('/docs').status_code == 404
    assert client.get('/assets/secret').status_code == 404


def test_commands_always_locked(client, settings):
    for robot in ('roomba-620', 'sim-620'):
        client.cookies.clear()
        url = f'/api/robots/{robot}/commands'
        assert client.post(url, json={'action': 'clean'}).status_code == 401
        signin(client, settings)
        assert client.post(url, json={'action': 'clean', 'sci_verified': True}).status_code == 423
    assert client.app.state.store.history() == []
    body = client.get('/api/status').json()
    assert body['hardware_commands_enabled'] is False
    assert body['sci_verified'] is False
    assert body['firmware_mqtt_integrated'] is False


def test_health_fails_on_broker_or_database_loss(client, settings):
    signin(client, settings)
    assert client.get('/health/ready').status_code == 200
    client.app.state.receiver.connected = False
    assert client.get('/health/ready').status_code == 503
    assert client.get('/health/live').status_code == 200
    client.app.state.receiver.connected = True
    def fail():
        raise RuntimeError('secret database detail')
    client.app.state.store.healthy = fail
    assert client.get('/health/ready').status_code == 503
    r = client.get('/api/status')
    assert r.status_code == 503
    assert 'secret' not in r.text


def test_freshness_disconnect_and_restart_require_new_data(store):
    now = utcnow()
    store.ingest("roomba-620", sample(sent_at=now.isoformat()), now)
    def status(connected=True, since=now - timedelta(seconds=1), at=now):
        return next(r for r in store.robots(connected, since, 15, at) if r["id"] == "roomba-620")
    assert status()["online"] is True
    assert status(at=now + timedelta(seconds=15))["online"] is False
    assert status(connected=False)["online"] is False
    assert status(since=now + timedelta(seconds=1))["online"] is False
    store.offline("roomba-620", now + timedelta(seconds=1))
    assert status()["online"] is False


def test_sources_are_distinct_and_persistent(store, settings):
    store.ingest("sim-620", sample())
    reopened = Store(settings.database_url)
    reopened.start()
    rows = {r["id"]: r for r in reopened.robots(True, utcnow(), 15)}
    assert rows["roomba-620"]["telemetry"] is None
    assert rows["sim-620"]["source"] == "simulation"
    assert rows["sim-620"]["online"] is False
    assert reopened.history()[0]["source"] == "simulation"
    reopened.engine.dispose()


def test_replay_and_retired_boot_rejected_across_restart(store, settings):
    first = sample()
    store.ingest("roomba-620", first)
    reopened = Store(settings.database_url)
    reopened.start()
    with pytest.raises(ValueError):
        reopened.ingest("roomba-620", first)
    reopened.ingest("roomba-620", sample())
    with pytest.raises(ValueError):
        reopened.ingest("roomba-620", first.model_copy(update={"seq": 100, "uptime_ms": 9999}))
    reopened.engine.dispose()


@pytest.mark.parametrize("offset", [-16, 6])
def test_delayed_and_future_messages_rejected(store, offset):
    with pytest.raises(ValueError):
        store.ingest("roomba-620", sample(sent_at=(utcnow() + timedelta(seconds=offset)).isoformat()))
    assert store.history() == []


@pytest.mark.parametrize("bad", [{"battery_percent": 101}, {"battery_percent": float("nan")},
                                 {"seq": -1}, {"seq": "1"}, {"state": "moving"},
                                 {"sci_verified": True}, {"schema_version": 2},
                                 {"firmware": "<script>"}, {"sent_at": "2026-11-13T10:00:00"}])
def test_schema_rejects_unsafe_or_malformed_data(bad):
    with pytest.raises(ValueError):
        sample(**bad)


def test_receiver_drops_retained_unknown_and_oversized_messages(settings, store):
    receiver = Receiver(settings, store)
    topic = "roocore/v1/robots/sim-620/telemetry"
    payload = sample().model_dump_json().encode()
    for t, p, retained in [(topic, payload, True), (topic.replace("sim-620", "alien"), payload, False),
                           (topic, b"x" * 3073, False), (topic, b"bad json", False)]:
        receiver.on_message(None, None, SimpleNamespace(topic=t, payload=p, retain=retained))
    assert receiver.rejected == 4
    assert store.history() == []
    receiver.on_message(None, None, SimpleNamespace(topic=topic, payload=payload, retain=False))
    assert len(store.history()) == 1
    receiver.on_message(None, None, SimpleNamespace(topic=topic, payload=payload, retain=False))
    assert receiver.rejected == 5
    assert len(store.history()) == 1


def test_retention_and_offline_deduplication(store):
    now = utcnow() - timedelta(days=31)
    store.ingest("sim-620", sample(sent_at=now.isoformat()), now)
    store.offline("sim-620")
    store.offline("sim-620")
    assert len(store.history()) == 2
    store.prune(30)
    assert [x["kind"] for x in store.history()] == ["offline"]
