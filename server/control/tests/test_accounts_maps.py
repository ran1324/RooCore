import time
from types import SimpleNamespace
from fastapi.testclient import TestClient
import pytest
from sqlalchemy import select
from app.accounts import COOKIE, User, Session, Recovery, Audit, digest, password_hash
from app.maps import CloudMap
from test_server import client, settings, signin, FakeReceiver
from app.main import create_app


def map_body():
    return dict(name='Home',revision=0,geometry=dict(cols=40,rows=30,resolution=.25,origin_x=0,origin_y=0),cells=[],zones=[],source='planning')


def test_sessions_csrf_expiry_logout_and_basic_removed(client,settings):
    response=signin(client,settings)
    cookie=response.headers['set-cookie']
    assert 'Secure' in cookie and 'HttpOnly' in cookie and 'SameSite=lax' in cookie
    del client.headers['X-CSRF-Token']
    assert client.post('/api/auth/logout',json={}).status_code==403
    client.headers['X-CSRF-Token']=client.get('/api/me').json()['csrf']
    assert client.post('/api/auth/logout',json={},headers={'Origin':'https://evil.test'}).status_code==403
    assert client.post('/api/auth/logout',json={}).status_code==200
    assert client.get('/api/status').status_code==401
    assert client.get('/api/status',auth=('admin',settings.admin_password)).status_code==401
    signin(client,settings)
    with client.app.state.store.session.begin() as s:
        row=s.scalar(select(Session));row.seen=time.time()-1801
    assert client.get('/api/me').status_code==401


def test_account_invitation_permissions_and_revocation(client,settings,monkeypatch):
    monkeypatch.setattr('app.account_routes.MailgunMailer.send',lambda *a,**kw:'<fake>')
    monkeypatch.setattr('app.account_routes.MailConfig.load',lambda:SimpleNamespace())
    signin(client,settings)
    owner_cookie=client.cookies.get(COOKIE);owner_csrf=client.headers['X-CSRF-Token']
    created=client.post('/api/users',json=dict(username='mom',name='Mom',email='mom@example.com',grants={'roomba-620':['view','control']}))
    assert created.status_code==200,created.text
    token=created.json()['activation_url'].split('#')[1];uid=created.json()['id']
    assert client.post('/api/auth/reset',json={'token':token,'password':'a very strong family password'}).status_code==200
    assert client.post('/api/auth/reset',json={'token':token,'password':'another strong family password'}).status_code==400
    client.cookies.clear()
    signin(client,settings,username='mom',password='a very strong family password')
    client.headers['X-CSRF-Token']=client.get('/api/me').json()['csrf']
    assert client.get('/api/users').status_code==403
    assert [r['id'] for r in client.get('/api/status').json()['robots']]==['roomba-620']
    assert client.get('/api/robots/sim-620/maps').status_code==403
    assert client.post('/api/robots/roomba-620/maps',json=map_body()).status_code==403
    assert client.post('/api/robots/roomba-620/commands',json={}).status_code==423
    family_cookie=client.cookies.get(COOKIE)
    client.cookies.clear();client.cookies.set(COOKIE,owner_cookie)
    client.headers['X-CSRF-Token']=owner_csrf
    assert client.patch('/api/users/'+uid,json={'enabled':False,'grants':{}}).status_code==200
    client.cookies.clear();client.cookies.set(COOKIE,family_cookie)
    assert client.get('/api/me').status_code==401


def test_map_storage_conflict_limits_and_no_dispatch(client,settings):
    signin(client,settings)
    body=map_body();body['zones']=[dict(id='room',name='Living',kind='room',x1=1,y1=1,x2=3,y2=3),dict(id='barrier',name='Bowl',kind='no_go',x1=4,y1=4,x2=5,y2=5)]
    created=client.post('/api/robots/roomba-620/maps',json=body)
    assert created.status_code==200,created.text
    data=created.json();mid=data['id']
    assert data['robot_acknowledged'] is False
    body['revision']=1
    body['name']='Renamed'
    assert client.put('/api/robots/roomba-620/maps/'+mid,json=body).status_code==200
    assert client.put('/api/robots/roomba-620/maps/'+mid,json=body).status_code==409
    assert client.put('/api/robots/sim-620/maps/'+mid,json=body).status_code==404
    plan=dict(map_id=mid,revision=2,action='clean',zone_id='room')
    assert client.post('/api/robots/roomba-620/plans',json=plan).json()['dispatched'] is False
    plan.update(action='goto',zone_id=None,target=[4.5,4.5])
    assert client.post('/api/robots/roomba-620/plans',json=plan).status_code==422
    plan['target']=[100,100]
    assert client.post('/api/robots/roomba-620/plans',json=plan).status_code==422
    bad=map_body();bad['geometry']['cols']=100000
    assert client.post('/api/robots/roomba-620/maps',json=bad).status_code==422
    assert len(client.get('/api/robots/roomba-620/maps').json()['maps'])==1
    assert client.post('/api/robots/roomba-620/commands',json={}).status_code==423
    # Persisted maps and user identity survive service recreation.
    with TestClient(create_app(settings,FakeReceiver),base_url='https://roocore.saymour.co.il',headers={'Origin':'https://roocore.saymour.co.il'}) as other:
        signin(other,settings)
        assert other.get('/api/robots/roomba-620/maps').json()['maps'][0]['name']=='Renamed'


def test_recovery_revokes_sessions_generic_response_rate_limits(client,settings,monkeypatch):
    sent=[]
    monkeypatch.setattr('app.account_routes.MailConfig.load',lambda:SimpleNamespace())
    monkeypatch.setattr('app.account_routes.MailgunMailer.send',lambda self,to,subject,text,**kw:sent.append(text))
    signin(client,settings)
    known=client.post('/api/auth/forgot',json={'email':'thfcxv@gmail.com'})
    unknown=client.post('/api/auth/forgot',json={'email':'nobody@example.com'})
    assert known.json()==unknown.json()
    assert len(sent)==1 and 'https://roocore.saymour.co.il/reset#' in sent[0]
    token=sent[0].split('/reset#')[1].split()[0]
    assert client.post('/api/auth/reset',json={'token':token,'password':'a changed administrator password'}).status_code==200
    assert client.get('/api/me').status_code==401
    for _ in range(8):
        client.post('/api/auth/login',json={'username':'admin','password':'bad'})
    assert client.post('/api/auth/login',json={'username':'admin','password':'bad'}).status_code==429


def test_expired_recovery_and_body_limits(client,settings):
    signin(client,settings)
    with client.app.state.store.session() as s:
        uid=s.scalar(select(User.id))
    token=client.app.state.accounts.issue(uid)
    with client.app.state.store.session.begin() as s:
        s.get(Recovery,digest(token)).expires=0
    assert client.post('/api/auth/reset',json={'token':token,'password':'an otherwise valid password'}).status_code==400
    assert client.post('/api/auth/login',content=b'x'*(1024*1024+1)).status_code==413


def test_zone_validator_matches_existing_navigation_contract():
    from pathlib import Path
    import ast
    from app import zone_validation
    owner=Path(__file__).resolve().parents[3] / 'navigation' / 'zones.py'
    if not owner.exists():
        pytest.skip('Owning navigation source is outside the standalone release')
    def contract(path):
        tree=ast.parse(path.read_text())
        return next(ast.dump(node,include_attributes=False) for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='validate_zones')
    assert contract(owner)==contract(Path(zone_validation.__file__))
