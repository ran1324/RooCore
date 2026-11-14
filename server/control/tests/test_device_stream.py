import hashlib
import time
from copy import deepcopy
from sqlalchemy import select
from app.device_stream import DeviceKey, DeviceBoot, DeviceStream, DevicePolicy, checksum, encode_policy
from app.maps import CloudMap
from test_server import client, settings, signin
import pytest

KEY='device-only-test-key-'+'x'*40

def frame(boot='a'*16,sequence=1,full=True):
    return dict(protocol=1,boot_id=boot,sequence=sequence,at_ms=sequence*1000,sent_unix_ms=int(time.time()*1000),age_ms=50,
                map_id='test-map',map_revision=sequence,geometry=dict(cols=4,rows=4,resolution=.1,origin_x=0.,origin_y=0.),
                status=dict(boot_id=boot,now=sequence*1000,state=0,sensors_valid=True,battery_valid=True,battery=70.,localized=True,scan_valid=True,cliff=False,wheel_drop=False),
                pose=dict(x=.1,y=.1,yaw=0.),scan_at_ms=sequence*1000,ranges_m=[1.]*180,
                sensors=[dict(id=22,value=14500,at_ms=sequence*1000,generation=sequence)],
                policy_ack=dict(generation=0,revision=0,checksum=0,map_id='test-map'),
                map=dict(cells=[-1]*16,main=[0]*16,side=[0]*16,excluded=[0]*16) if full else None)


def provision(client,robot='roomba-620',key=KEY):
    with client.app.state.store.session.begin() as s:
        s.add(DeviceKey(robot_id=robot,token_hash=hashlib.sha256(key.encode()).hexdigest(),enabled=True))


def send(client,body=None,key=KEY):
    # Device channel deliberately has no browser Origin or session/CSRF credentials.
    client.headers.pop('Origin',None)
    return client.post('/api/device/v1/snapshot',json=body or frame(),headers={'Authorization':'Bearer '+key})


def age_channel(client):
    with client.app.state.store.session.begin() as s:
        for row in s.scalars(select(DeviceStream)):row.received-=1


def web(client,settings):
    client.headers['Origin']='https://roocore.saymour.co.il';signin(client,settings)


def test_device_auth_isolation_web_auth_and_revocation(client,settings):
    provision(client)
    assert send(client,key='wrong-'+'q'*40).status_code==401
    assert send(client).status_code==204
    assert client.get('/api/robots/roomba-620/stream').status_code==401
    # Bearer device keys must never authorize user endpoints.
    assert client.get('/api/users',headers={'Authorization':'Bearer '+KEY}).status_code==401
    with client.app.state.store.session() as s:
        assert s.get(DeviceStream,'sim-620') is None
    web(client,settings)
    result=client.get('/api/status').json()
    r=next(x for x in result['robots'] if x['id']=='roomba-620')
    assert r['online'] and r['transport']=='https_device' and r['telemetry']['battery_percent']==70
    assert result['hardware_commands_enabled'] is False
    with client.app.state.store.session.begin() as s:s.get(DeviceKey,'roomba-620').enabled=False
    assert send(client,frame(sequence=2)).status_code==401


def test_replay_retired_boot_and_timestamp_rejected(client):
    provision(client)
    assert send(client).status_code==204
    assert send(client).status_code==409
    age_channel(client)
    assert send(client,frame(boot='b'*16)).status_code==204
    assert send(client,frame(sequence=2)).status_code==409
    old=frame(boot='c'*16);old['sent_unix_ms']-=30000
    assert send(client,old).status_code==422
    old=frame(boot='c'*16);old['age_ms']=5001
    assert send(client,old).status_code==422


@pytest.mark.parametrize('change',[
    lambda x:x['map']['cells'].append(0),lambda x:x['geometry'].update(cols=10000),
    lambda x:x['status'].update(sensors_valid=1),lambda x:x['ranges_m'].append(1.),
    lambda x:x['sensors'].append(deepcopy(x['sensors'][0])),lambda x:x['status'].update(boot_id='z'*16),
    lambda x:x.update(robot_id='sim-620'),lambda x:x['map']['cells'].__setitem__(0,True),
])
def test_invalid_frames_rejected(client,change):
    provision(client);body=frame();change(body);assert send(client,body).status_code==422


def test_policy_roundtrip_identity_binding_and_ack(client,settings):
    provision(client);assert send(client).status_code==204
    web(client,settings)
    doc=client.get('/api/robots/roomba-620/maps').json()['maps'][0];mid=doc['id']
    assert doc['source']=='hardware_stream' and doc['source_map_id']=='test-map'
    # Uploads cannot erase annotations. Edits do not become active until durable device ack.
    body={k:doc[k] for k in ('name','revision','geometry','cells','zones','source','source_map_id')}
    body['zones']=[dict(id='ban',kind='no_go',name='Bowl',x1=.2,y1=.2,x2=.3,y2=.3)]
    updated=client.put('/api/robots/roomba-620/maps/'+mid,json=body)
    assert updated.status_code==200,updated.text
    assert client.post('/api/robots/roomba-620/maps/'+mid+'/deploy',json={'revision':1}).status_code==409
    response=client.post('/api/robots/roomba-620/maps/'+mid+'/deploy',json={'revision':2})
    assert response.status_code==200,response.text
    assert response.json()['motion_command'] is False
    age_channel(client)
    reply=send(client,frame(sequence=2))
    assert reply.status_code==200 and reply.content[:4]==b'RCZ1'
    assert len(reply.content)==172
    policy_crc=int.from_bytes(reply.content[148:152],'little')
    assert checksum(reply.content)==policy_crc
    body=frame(sequence=3,full=False);body['policy_ack']=dict(generation=1,revision=2,checksum=policy_crc,map_id='test-map')
    age_channel(client);assert send(client,body).status_code==204
    web(client,settings)
    doc=client.get('/api/robots/roomba-620/maps').json()['maps'][0]
    assert doc['robot_acknowledged'] and doc['application']=='controller_persisted'
    assert doc['zones'][0]['id']=='ban'
    assert client.post('/api/robots/roomba-620/commands',json={}).status_code==423
    # Geometry changes under an existing identity fail rather than move no-go zones.
    body=frame(sequence=4);body['geometry']['resolution']=.2
    age_channel(client);assert send(client,body).status_code==409


def test_policy_requires_observed_map_and_permission(client,settings):
    provision(client);web(client,settings)
    body=dict(name='Draft',revision=0,geometry=dict(cols=4,rows=4,resolution=.1,origin_x=0.,origin_y=0.),cells=[],zones=[],source='planning')
    draft=client.post('/api/robots/roomba-620/maps',json=body).json()
    assert client.post('/api/robots/roomba-620/maps/'+draft['id']+'/deploy',json={'revision':1}).status_code==409
    # User-session auth cannot be used as a device key.
    client.headers.pop('Origin',None)
    assert client.post('/api/device/v1/snapshot',json=frame()).status_code==401


def test_wire_fixture_matches_cpp_contract():
    from pathlib import Path
    fixture=Path(__file__).resolve().parents[3]/'tests/fixtures/cloud-policy-v1.bin'
    if not fixture.exists():pytest.skip('Native fixture is outside standalone server release')
    document=dict(geometry=dict(cols=128,rows=128,resolution=.1,origin_x=-6.4,origin_y=-6.4),zones=[dict(id='ban',name='ban',kind='no_go',x1=2,y1=2,x2=3,y2=3),dict(id='wall',name='wall',kind='wall',x1=0,y1=0,x2=0,y2=1)])
    assert encode_policy(document,7,3,'a'*32,'test-map')==fixture.read_bytes()


def test_restart_freshness_and_invalid_ack(client,settings):
    provision(client);bad=frame();bad['policy_ack']['generation']=True
    assert send(client,bad).status_code==422
    bad=frame();bad['status']['battery']='unknown'
    assert send(client,bad).status_code==422
    assert send(client).status_code==204
    web(client,settings)
    with client.app.state.store.session.begin() as s:
        s.get(DeviceStream,'roomba-620').received=client.app.state.device_fresh_after-.1
    assert client.get('/api/robots/roomba-620/stream').json()['online'] is False
    r=next(x for x in client.get('/api/status').json()['robots'] if x['id']=='roomba-620')
    assert r['online'] is False


def test_credentials_checked_before_large_json_decode(client):
    client.headers.pop('Origin',None)
    result=client.post('/api/device/v1/snapshot',content=b'['*500000,headers={'Content-Type':'application/json'})
    assert result.status_code==401
    client.headers['Origin']='https://roocore.saymour.co.il'
    result=client.post('/api/robots/roomba-620/maps',content=b'['*500000,headers={'Content-Type':'application/json'})
    assert result.status_code==401
    result=client.post('/api/auth/login',content=b'['*17000,headers={'Content-Type':'application/json'})
    assert result.status_code==413
