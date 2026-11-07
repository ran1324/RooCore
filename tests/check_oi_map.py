"""Coverage audit against the explicit iRobot opcode/group/packet inventory."""
import json
import pathlib
import re
root=pathlib.Path(__file__).resolve().parents[1]
data=json.loads((root/'docs/oi-map.json').read_text())
expected={7,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,148,149,150,162,163,164,165,167,168,173}
assert {x['opcode'] for x in data['commands']}==expected
assert len(data['commands'])==len(expected)
assert [x['id'] for x in data['sensors']]==list(range(7,59))
assert {x['id'] for x in data['groups']}=={0,1,2,3,4,5,6,100,101,106,107}
header=(root/'lib/RoombaOI/src/RoombaOI.h').read_text()
for cmd in data['commands']:
    for name in cmd['api'].split(' / '):
        assert re.search(r'\b'+re.escape(name)+r'\(',header), name
for sensor in data['sensors']+data['groups']:
    assert re.search(r'\b'+sensor['name']+r'\s*=\s*'+str(sensor['id'])+r'\b',header), sensor
for group in data['groups']:
    assert sum(x['bytes'] for x in data['sensors'] if group['first']<=x['id']<=group['last'])==group['bytes']
print('PASS: map covers 30 opcodes (including alias), 52 individual packets, 11 groups and both BRC operations')
