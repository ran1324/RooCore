"""Cloud map planning only. No deployment or motion dispatcher exists here."""
import math
import secrets
from typing import Literal
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import Integer, String, JSON, select
from sqlalchemy.orm import Mapped, mapped_column
from .accounts import permitted, Audit
from .store import Base, ROBOTS
from .zone_validation import validate_zones


class CloudMap(Base):
    __tablename__ = 'cloud_maps'
    id: Mapped[str] = mapped_column(String, primary_key=True)
    robot_id: Mapped[str] = mapped_column(String, index=True)
    name: Mapped[str] = mapped_column(String)
    revision: Mapped[int] = mapped_column(Integer)
    document: Mapped[dict] = mapped_column(JSON)


class MissionPlan(Base):
    __tablename__ = 'mission_plans'
    id: Mapped[str] = mapped_column(String, primary_key=True)
    robot_id: Mapped[str] = mapped_column(String)
    map_id: Mapped[str] = mapped_column(String)
    revision: Mapped[int] = mapped_column(Integer)
    actor: Mapped[str] = mapped_column(String)
    document: Mapped[dict] = mapped_column(JSON)


class MapBody(BaseModel):
    model_config = ConfigDict(extra='forbid', strict=True)
    name: str = Field(min_length=1, max_length=80)
    revision: int = Field(ge=0)
    geometry: dict
    cells: list[int] = Field(max_length=65536)
    zones: list[dict] = Field(max_length=32)
    source: Literal['planning', 'simulation', 'hardware_import', 'hardware_stream'] = 'planning'
    source_map_id: str | None = Field(default=None, max_length=200)


class PlanBody(BaseModel):
    model_config = ConfigDict(extra='forbid')
    map_id: str = Field(max_length=64)
    revision: int = Field(ge=1)
    action: Literal['clean', 'goto']
    zone_id: str | None = Field(default=None, max_length=64)
    target: tuple[float, float] | None = None


def validate_map(body):
    g = body.geometry
    if set(g) != {'cols', 'rows', 'resolution', 'origin_x', 'origin_y'}:
        raise HTTPException(422, 'Invalid map geometry')
    if any(type(g[k]) is not int or not 1 <= g[k] <= 256 for k in ('cols', 'rows')):
        raise HTTPException(422, 'Grid must be at most 256 by 256')
    if any(type(g[k]) not in (int, float) or not math.isfinite(g[k]) for k in ('resolution', 'origin_x', 'origin_y')):
        raise HTTPException(422, 'Invalid map coordinates')
    if not .01 <= g['resolution'] <= 1 or any(abs(g[k]) > 1000 for k in ('origin_x', 'origin_y')):
        raise HTTPException(422, 'Invalid map scale')
    if body.cells and (len(body.cells) != g['cols'] * g['rows'] or any(v not in (-1, 0, 1) for v in body.cells)):
        raise HTTPException(422, 'Invalid occupancy grid')
    try:
        zones = validate_zones(body.zones, g)
    except ValueError as e:
        raise HTTPException(422, str(e))
    return dict(geometry=g, cells=body.cells, zones=zones, source=body.source, source_map_id=body.source_map_id,
                application='annotations_only', robot_acknowledged=False)


def install(app, store, current):
    router = APIRouter()
    def check(user, robot, permission='view'):
        if robot not in ROBOTS or not permitted(user, robot, permission):
            raise HTTPException(403, 'Robot permission required')

    def describe(row):
        return dict(id=row.id, robot_id=row.robot_id, name=row.name, revision=row.revision, **row.document)

    @router.get('/api/robots/{robot}/maps')
    def maps(robot: str, user=Depends(current)):
        check(user, robot)
        with store.session() as s:
            return {'maps': [describe(r) for r in s.scalars(select(CloudMap).where(CloudMap.robot_id == robot))]}

    @router.post('/api/robots/{robot}/maps')
    def create(robot: str, body: MapBody, user=Depends(current)):
        check(user, robot, 'map_edit')
        document = validate_map(body)
        if body.revision != 0:
            raise HTTPException(409, 'A new map must start at revision zero')
        with store.lock, store.session.begin() as s:
            if len(list(s.scalars(select(CloudMap.id).where(CloudMap.robot_id == robot)))) >= 20:
                raise HTTPException(409, 'Map limit reached')
            row = CloudMap(id=secrets.token_hex(16), robot_id=robot, name=body.name, revision=1, document=document)
            s.add(row)
            s.add(Audit(actor=user['id'], action='map_created', target=row.id))
            return describe(row)

    @router.put('/api/robots/{robot}/maps/{map_id}')
    def update(robot: str, map_id: str, body: MapBody, user=Depends(current)):
        check(user, robot, 'map_edit')
        document = validate_map(body)
        with store.lock, store.session.begin() as s:
            row = s.get(CloudMap, map_id)
            if not row or row.robot_id != robot:
                raise HTTPException(404)
            if row.revision != body.revision:
                raise HTTPException(409, 'Map changed. Reload before saving.')
            # Geometry and provenance are immutable for this identity. Import creates a new map.
            if document['geometry'] != row.document['geometry'] or document['source'] != row.document['source'] or document['source_map_id'] != row.document.get('source_map_id'):
                raise HTTPException(409, 'Create a new map for different geometry or source')
            document.update({k:v for k,v in row.document.items() if k in ("coverage", "device_map_revision")})
            row.name, row.document, row.revision = body.name, document, row.revision + 1
            s.add(Audit(actor=user['id'], action='map_updated', target=map_id))
            return describe(row)

    @router.post('/api/robots/{robot}/plans')
    def plan(robot: str, body: PlanBody, user=Depends(current)):
        check(user, robot, 'control')
        with store.lock, store.session.begin() as s:
            row = s.get(CloudMap, body.map_id)
            if not row or row.robot_id != robot:
                raise HTTPException(404)
            if row.revision != body.revision:
                raise HTTPException(409, 'Map revision changed')
            zones = row.document['zones']
            if body.action == 'clean':
                if not any(z['id'] == body.zone_id and z['kind'] == 'room' and z['enabled'] for z in zones):
                    raise HTTPException(422, 'Select a room to clean')
            else:
                if not body.target or not all(math.isfinite(v) for v in body.target):
                    raise HTTPException(422, 'Select a target')
                x, y = body.target
                g = row.document['geometry']
                if not (g['origin_x'] <= x < g['origin_x'] + g['cols'] * g['resolution'] and
                        g['origin_y'] <= y < g['origin_y'] + g['rows'] * g['resolution']):
                    raise HTTPException(422, 'Target outside map')
                if any(z['kind'] == 'no_go' and z['enabled'] and z['present'] and
                       z['x1'] <= x <= z['x2'] and z['y1'] <= y <= z['y2'] for z in zones):
                    raise HTTPException(422, 'Target inside a forbidden zone')
            if len(list(s.scalars(select(MissionPlan.id)))) >= 500:
                raise HTTPException(409, 'Plan archive limit reached')
            item = MissionPlan(id=secrets.token_hex(16), robot_id=robot, map_id=row.id,
                               revision=row.revision, actor=user['id'], document=body.model_dump())
            s.add(item)
            s.add(Audit(actor=user['id'], action='plan_saved', target=item.id))
            return {'id': item.id, 'status': 'draft', 'dispatched': False,
                    'detail': 'Saved as a draft. Physical control is not integrated.'}

    @router.get('/api/robots/{robot}/plans')
    def plans(robot: str, user=Depends(current)):
        check(user, robot)
        with store.session() as s:
            return {'plans': [dict(id=r.id, map_id=r.map_id, revision=r.revision,
                                   status='draft', dispatched=False, **{k:v for k,v in r.document.items() if k not in ('map_id','revision')})
                             for r in s.scalars(select(MissionPlan).where(MissionPlan.robot_id == robot).limit(100))]}
    app.include_router(router)
