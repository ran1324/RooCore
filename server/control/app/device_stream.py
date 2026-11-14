"""Outbound HTTPS device channel. Never opens a connection to a home address."""
import hashlib
import hmac
import json
import math
import re
import secrets
import struct
import time
from typing import Literal
from fastapi import Depends, HTTPException, Request, Response
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import Boolean, Float, Integer, JSON, String, select
from sqlalchemy.orm import Mapped, mapped_column
from .store import Base, ROBOTS
from .accounts import permitted, Audit
from .maps import CloudMap
from .zone_validation import validate_zones

DEVICE_PATH='/api/device/v1/snapshot'

class DeviceKey(Base):
    __tablename__='device_keys'
    robot_id: Mapped[str]=mapped_column(String,primary_key=True)
    token_hash: Mapped[str]=mapped_column(String)
    enabled: Mapped[bool]=mapped_column(Boolean,default=True)

class DeviceStream(Base):
    __tablename__='device_stream'
    robot_id: Mapped[str]=mapped_column(String,primary_key=True)
    boot: Mapped[str]=mapped_column(String)
    sequence: Mapped[int]=mapped_column(Integer)
    at_ms: Mapped[int]=mapped_column(Integer)
    received: Mapped[float]=mapped_column(Float)
    cloud_map_id: Mapped[str | None]=mapped_column(String)
    snapshot: Mapped[dict]=mapped_column(JSON)

class DeviceBoot(Base):
    __tablename__='device_boots'
    key: Mapped[str]=mapped_column(String,primary_key=True)
    robot_id: Mapped[str]=mapped_column(String,index=True)

class DevicePolicy(Base):
    __tablename__='device_policy'
    robot_id: Mapped[str]=mapped_column(String,primary_key=True)
    generation: Mapped[int]=mapped_column(Integer)
    map_id: Mapped[str]=mapped_column(String)
    revision: Mapped[int]=mapped_column(Integer)
    checksum: Mapped[int]=mapped_column(Integer)
    document: Mapped[dict]=mapped_column(JSON)

class Snapshot(BaseModel):
    model_config=ConfigDict(extra='forbid',strict=True,allow_inf_nan=False)
    protocol: Literal[1]
    boot_id: str=Field(pattern=r'^[a-f0-9]{16}$')
    sequence: int=Field(ge=1,le=2**32-1)
    at_ms: int=Field(ge=0,le=2**32-1)
    sent_unix_ms: int=Field(ge=0)
    age_ms: int=Field(ge=0,le=5000)
    map_id: str=Field(pattern=r'^[a-zA-Z0-9_.-]{1,79}$')
    map_revision: int=Field(ge=0,le=2**32-1)
    geometry: dict
    status: dict
    pose: dict
    scan_at_ms: int=Field(ge=0,le=2**32-1)
    ranges_m: list[float | None]=Field(min_length=180,max_length=180)
    sensors: list[dict]=Field(max_length=52)
    policy_ack: dict
    map: dict | None=None


def checksum(data):
    h=2166136261
    for i,b in enumerate(data): h=((h^(0 if 148<=i<152 else b))*16777619)&0xffffffff
    return h


def encode_policy(document,generation,revision,cloud_id,source_id):
    g=document['geometry'];zones=validate_zones(document['zones'],g)
    data=bytearray(152+20*len(zones));data[:4]=b'RCZ1'
    struct.pack_into('<HHIIHHfff',data,4,len(data),len(zones),generation,revision,g['cols'],g['rows'],g['resolution'],g['origin_x'],g['origin_y'])
    data[32:65]=cloud_id.encode().ljust(33,b'\0');data[65:145]=source_id.encode().ljust(80,b'\0')
    for i,z in enumerate(zones):
        flags=int(z['enabled'])+2*int(z['present'])+4*int(z['boost'])
        struct.pack_into('<BBBBffff',data,152+i*20,('room','no_go','wall','carpet').index(z['kind']),flags,z['extra_passes'],0,z['x1'],z['y1'],z['x2'],z['y2'])
    struct.pack_into('<I',data,148,checksum(data))
    return bytes(data)


def bounded(value,depth=0):
    if depth>6: raise ValueError('Nested status')
    if value is None or type(value) is bool:return
    if type(value) in (int,float) and math.isfinite(value) and abs(value)<2**64:return
    if isinstance(value,str) and len(value)<=512:return
    if isinstance(value,list) and len(value)<=256:
        for v in value:bounded(v,depth+1)
        return
    if isinstance(value,dict) and len(value)<=256:
        for k,v in value.items():
            if not isinstance(k,str) or len(k)>80:raise ValueError('Invalid key')
            bounded(v,depth+1)
        return
    raise ValueError('Invalid snapshot data')


def validate(body):
    g=body.geometry
    if set(g)!={'cols','rows','resolution','origin_x','origin_y'}:raise ValueError('Invalid geometry')
    if any(type(g[k]) is not int or not 4<=g[k]<=256 for k in ('cols','rows')) or g['cols']*g['rows']>16384:raise ValueError('Invalid grid')
    if any(type(g[k]) not in (int,float) or not math.isfinite(g[k]) for k in ('resolution','origin_x','origin_y')) or not .1<=g['resolution']<=1:raise ValueError('Invalid scale')
    if abs(g['origin_x'])>1000 or abs(g['origin_y'])>1000:raise ValueError('Invalid origin')
    delay=time.time()*1000-body.sent_unix_ms
    if abs(delay)>5000 or max(0,delay)+body.age_ms>5000:raise ValueError('Snapshot outside freshness window')
    bounded(body.status);bounded(body.pose);bounded(body.policy_ack)
    ack=body.policy_ack
    if set(ack)!={'generation','revision','checksum','map_id'} or ack['map_id']!=body.map_id or any(type(ack[k]) is not int or not 0<=ack[k]<=2**32-1 for k in ('generation','revision','checksum')):raise ValueError('Invalid policy acknowledgement')
    s=body.status
    if s.get('boot_id')!=body.boot_id or s.get('now')!=body.at_ms:raise ValueError('Inconsistent acquisition identity')
    for name in ('sensors_valid','battery_valid','localized','scan_valid','cliff','wheel_drop'):
        if type(s.get(name)) is not bool:raise ValueError('Missing validity')
    if s.get('battery_valid') and (type(s.get('battery')) not in (int,float) or not 0<=s['battery']<=100):raise ValueError('Invalid battery percentage')
    if type(s.get('state')) is not int or not 0<=s['state']<=13:raise ValueError('Invalid robot state')
    for k in ('x','y','yaw'):
        if type(body.pose.get(k)) not in (int,float) or not math.isfinite(body.pose[k]):raise ValueError('Invalid pose')
    if any(v is not None and (not math.isfinite(v) or not 0<v<=100) for v in body.ranges_m):raise ValueError('Invalid scan')
    ids=set()
    for item in body.sensors:
        if set(item)!={'id','value','at_ms','generation'} or any(type(v) is not int for v in item.values()) or not 7<=item['id']<=58 or item['id'] in ids or not -2**31<=item['value']<2**31 or any(not 0<=item[k]<=2**32-1 for k in ('at_ms','generation')):raise ValueError('Invalid sensor sample')
        ids.add(item['id'])
    if body.map is not None:
        n=g['cols']*g['rows']
        if set(body.map)!={'cells','main','side','excluded'}:raise ValueError('Invalid map arrays')
        for key,lo,hi in [('cells',-1,1),('main',0,255),('side',0,15),('excluded',0,255)]:
            if not isinstance(body.map[key],list) or len(body.map[key])!=n or any(type(v) is not int or not lo<=v<=hi for v in body.map[key]):raise ValueError('Invalid map cells')


def install(app,store,current):
    fresh_after=time.time()
    app.state.device_fresh_after=fresh_after
    throttle_lock=__import__('threading').Lock();attempts=[0.,0]
    def device(request: Request):
        cached=getattr(request.state,'device_robot',None)
        if cached:return cached
        # Global cap before database work; no claimed client IP behind the local tunnel.
        with throttle_lock:
            now=time.monotonic()
            if now-attempts[0]>60:attempts[:]=[now,0]
            attempts[1]+=1
            if attempts[1]>240:raise HTTPException(429)
        auth=request.headers.get('authorization','')
        if not auth.startswith('Bearer ') or not 32<=len(auth[7:])<=128:raise HTTPException(401)
        token_hash=hashlib.sha256(auth[7:].encode()).hexdigest()
        with store.session() as s:
            key=s.scalar(select(DeviceKey).where(DeviceKey.token_hash==token_hash,DeviceKey.enabled==True))
            if not key:raise HTTPException(401)
            request.state.device_robot=key.robot_id
            return key.robot_id

    app.state.authenticate_device=device

    @app.post(DEVICE_PATH)
    def ingest(body: Snapshot,robot=Depends(device)):
        try:validate(body)
        except ValueError as e:raise HTTPException(422,str(e))
        now=time.time();value=body.model_dump();map_data=value.pop('map')
        with store.lock,store.session.begin() as s:
            stream=s.get(DeviceStream,robot);boot_key=robot+':'+body.boot_id
            if stream and stream.boot==body.boot_id:
                if body.sequence<=stream.sequence or ((body.at_ms-stream.at_ms)&0xffffffff)>0x7fffffff:raise HTTPException(409,'Old snapshot')
                if now-stream.received<.5:raise HTTPException(429)
            else:
                if s.get(DeviceBoot,boot_key):raise HTTPException(409,'Retired boot')
                if len(list(s.scalars(select(DeviceBoot.key).where(DeviceBoot.robot_id==robot))))>=4096:raise HTTPException(409,'Boot archive requires operator maintenance')
                s.add(DeviceBoot(key=boot_key,robot_id=robot))
            row=s.scalar(select(CloudMap).where(CloudMap.robot_id==robot,CloudMap.id==(stream.cloud_map_id if stream else '')))
            if not row or row.document.get('source_map_id')!=body.map_id:
                row=s.scalar(select(CloudMap).where(CloudMap.robot_id==robot).filter(CloudMap.document['source_map_id'].as_string()==body.map_id))
            if map_data is not None:
                if not row:
                    if len(list(s.scalars(select(CloudMap.id).where(CloudMap.robot_id==robot))))>=20:raise HTTPException(409,'Map archive full')
                    row=CloudMap(id=secrets.token_hex(16),robot_id=robot,name='מפת הרובוט' if robot=='roomba-620' else 'מפת סימולטור',revision=1,document={})
                    s.add(row)
                old=row.document
                if old and old.get('geometry')!=body.geometry:raise HTTPException(409,'Map identity changed geometry')
                row.document=dict(old,geometry=body.geometry,cells=map_data['cells'],zones=old.get('zones',[]),source='hardware_stream' if robot=='roomba-620' else 'simulation',source_map_id=body.map_id,application='annotations_only',robot_acknowledged=False,device_map_revision=body.map_revision,coverage={k:map_data[k] for k in ('main','side','excluded')})
            if not stream:
                stream=DeviceStream(robot_id=robot);s.add(stream)
            stream.boot,stream.sequence,stream.at_ms,stream.received=body.boot_id,body.sequence,body.at_ms,now
            stream.cloud_map_id=row.id if row else None
            stream.snapshot=value
            policy=s.get(DevicePolicy,robot)
            if policy and row and policy.map_id==row.id:
                ack=body.policy_ack
                active=all(ack.get(k)==v for k,v in dict(generation=policy.generation,revision=policy.revision,checksum=policy.checksum,map_id=body.map_id).items())
                doc=dict(row.document);doc['robot_acknowledged']=active and row.revision==policy.revision
                doc['application']='controller_persisted' if doc['robot_acknowledged'] else 'pending_controller'
                row.document=doc
                if not active and policy.document['source_map_id']==body.map_id:
                    packet=encode_policy(policy.document,policy.generation,policy.revision,row.id,body.map_id)
                    return Response(packet,media_type='application/octet-stream')
        return Response(status_code=204)

    @app.get('/api/robots/{robot}/stream')
    def stream(robot: str,user=Depends(current)):
        if robot not in ROBOTS or not permitted(user,robot):raise HTTPException(403)
        with store.session() as s:
            row=s.get(DeviceStream,robot)
            if not row:return {'online':False,'snapshot':None}
            return dict(online=row.received>=fresh_after and time.time()-row.received<5,received_at=row.received,cloud_map_id=row.cloud_map_id,snapshot=row.snapshot,hardware_verified=False)

    @app.post('/api/robots/{robot}/maps/{map_id}/deploy')
    def deploy(robot: str,map_id: str,body: dict,user=Depends(current)):
        if robot not in ROBOTS or not permitted(user,robot,'map_edit'):raise HTTPException(403)
        with store.lock,store.session.begin() as s:
            row=s.get(CloudMap,map_id);stream=s.get(DeviceStream,robot)
            if not row or row.robot_id!=robot:raise HTTPException(404)
            if set(body)!={'revision'} or type(body['revision']) is not int or body['revision']!=row.revision:raise HTTPException(409)
            if not stream or stream.cloud_map_id!=row.id or row.document.get('source')!=('hardware_stream' if robot=='roomba-620' else 'simulation'):raise HTTPException(409,'Only the current device map can receive a policy')
            previous=s.get(DevicePolicy,robot);generation=previous.generation+1 if previous else 1
            if generation>2**32-1 or row.revision>2**32-1:raise HTTPException(409,'Policy counter exhausted')
            document=dict(row.document);validate_zones(document['zones'],document['geometry'])
            packet=encode_policy(document,generation,row.revision,row.id,document['source_map_id'])
            if not previous:previous=DevicePolicy(robot_id=robot);s.add(previous)
            previous.generation,previous.map_id,previous.revision=generation,row.id,row.revision
            previous.checksum=struct.unpack_from('<I',packet,148)[0];previous.document=document
            row.document=dict(document,application='pending_controller',robot_acknowledged=False)
            s.add(Audit(actor=user['id'],action='zone_policy_requested',target=row.id))
            return {'generation':generation,'status':'pending_controller','motion_command':False}
