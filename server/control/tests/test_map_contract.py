import ast
from pathlib import Path
import pytest
from fastapi import HTTPException
from app.maps import MapBody, validate_map
from app import zone_validation


def test_existing_zone_validator_parity():
    owner=Path(__file__).resolve().parents[3] / 'navigation' / 'zones.py'
    if not owner.exists():
        pytest.skip('Owning source not packaged in standalone server release')
    def contract(path):
        return next(ast.dump(n,include_attributes=False) for n in ast.parse(path.read_text()).body
                    if isinstance(n,ast.FunctionDef) and n.name=='validate_zones')
    assert contract(owner)==contract(Path(zone_validation.__file__))


def test_legacy_occupancy_and_provenance_preserved():
    body=MapBody(name='Recorded home',revision=0,geometry=dict(cols=3,rows=1,resolution=.1,origin_x=-1,origin_y=0),
                 cells=[-1,0,1],zones=[],source='hardware_import',source_map_id='original-map-id')
    doc=validate_map(body)
    assert doc['cells']==[-1,0,1]
    assert doc['source_map_id']=='original-map-id'
    assert doc['robot_acknowledged'] is False
    with pytest.raises(HTTPException):
        validate_map(body.model_copy(update={'cells':[0,1,2]}))
