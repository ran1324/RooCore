"""Consistency checks for the current guide; not electrical ERC or a hardware test."""
import csv
import hashlib
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from urllib.parse import unquote, urlsplit
from zipfile import ZipFile
from guide_content import ENDPOINTS, STEPS

out = Path(__file__).resolve().parent
repo = out.parents[1]
rows = json.loads((out / 'connections.json').read_text())
expected = [r for step in STEPS for r in step['rows']]
assert len(rows) == len(expected) == 13
assert [r['id'] for r in rows] == list(range(1, 14))
with (out / 'connections.csv').open(encoding='utf-8-sig', newline='') as f:
    csv_rows = list(csv.DictReader(f))
assert len(csv_rows) == len(rows)
for actual, reference, csv_row in zip(rows, expected, csv_rows):
    for key, value in reference.items():
        assert actual[key] == value, (key, actual)
    assert csv_row == {key: str(value) for key, value in actual.items()}

# Every external wire and the known U3 ground link must preserve net separation.
parent = {name: name for name in ENDPOINTS}
def find(name):
    while parent[name] != name:
        name = parent[name]
    return name
def join(a, b):
    parent[find(a)] = find(b)
for row in rows:
    join(row['first'], row['last'])
join('U3 GND 1', 'U3 GND 2')
groups = {}
for name in ENDPOINTS:
    groups.setdefault(find(name), []).append(name)
assert len(groups) == 7, groups
for names in groups.values():
    assert len({ENDPOINTS[name][3] for name in names}) == 1, names
assert find('S1 VIN') != find('S1 VOUT')
assert find('XIAO D4 / GPIO5') == find('LiDAR cable RX')
assert find('XIAO D5 / GPIO6') == find('LiDAR cable TX')

for filename in ['electrical-schematic.svg', 'combined-wiring.svg']:
    root = ET.parse(out / filename).getroot()
    wires = [node for node in root.iter() if node.get('data-wire')]
    assert len(wires) == len(rows), filename
    for node, row in zip(wires, rows):
        for attribute, key in [('data-wire', 'id'), ('data-from', 'first'),
                               ('data-to', 'last'), ('data-net', 'net')]:
            assert node.get(attribute) == str(row[key]), (filename, attribute)

for path in list(out.glob('*.svg')) + list(out.glob('*.drawio')):
    if path.name.startswith('._'):
        continue
    root = ET.parse(path).getroot()
    for model in ([root] if root.tag == 'mxGraphModel' else root.findall('.//mxGraphModel')):
        cells = model.findall('.//mxCell')
        ids = [cell.get('id') for cell in cells]
        assert len(ids) == len(set(ids)), path
        for cell in cells:
            for key in ['parent', 'source', 'target']:
                if cell.get(key):
                    assert cell.get(key) in ids, (path, key, cell.get(key))
assert len(ET.parse(out / 'RooCore-assembly.drawio').getroot().findall('diagram')) == 13

# Bind the published pin map and source snapshot to the full firmware actually
# built. Offline ZIP readers may use snapshots; a live checkout must match.
integration = json.loads((out / 'firmware-integration.json').read_text())
source = repo / integration['source_root']
assert integration['physical_validation'] is False
if (source / 'firmware/xiao/RooCoreXiao/RooCoreXiao.ino').is_file():
    for name, digest in integration['sha256'].items():
        assert hashlib.sha256((source / name).read_bytes()).hexdigest() == digest, ('changed since build', name)
    for name in ['RooCoreXiao.ino', 'config.example.h']:
        assert (out / 'firmware-source' / name).read_bytes() == (source / 'firmware/xiao/RooCoreXiao' / name).read_bytes()
    for path in (repo / 'lib/RooCoreLidarPower').rglob('*'):
        if path.is_file() and not path.name.startswith('.'):
            assert path.read_bytes() == (source / path.relative_to(repo)).read_bytes(), ('library copy mismatch', path)
adapter = (repo / 'lib/RooCoreLidarPower/src/RooCoreLidarPower.h').read_text()
for name, pin in [('Enable', 4), ('Tx', 5), ('Rx', 6)]:
    assert re.search(r'k' + name + r'\s*=\s*' + str(pin) + r';', adapter)
assert 'HardwareSerial port_{2}' in adapter
sketch = (out / 'firmware-source/RooCoreXiao.ino').read_text()
assert 'HardwareSerial roombaSerial(1)' in sketch
assert rows[11]['first'] == 'XIAO D4 / GPIO5' and rows[11]['last'] == 'LiDAR cable RX'
assert rows[12]['first'] == 'LiDAR cable TX' and rows[12]['last'] == 'XIAO D5 / GPIO6'

class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
    def handle_starttag(self, tag, attrs):
        self.links += [v for k, v in attrs if k in ('href', 'src') and v]

def targets(path):
    text = path.read_text()
    if path.suffix == '.html':
        parser = Links()
        parser.feed(text)
        raw = parser.links
    else:
        raw = re.findall(r'\[[^\]]*\]\(([^)]+)\)', text)
    for value in raw:
        parsed = urlsplit(html.unescape(value))
        if parsed.scheme or parsed.netloc or not parsed.path:
            continue
        yield (path.parent / unquote(parsed.path)).resolve()

active = [p for p in out.glob('*.md') if not p.name.startswith('._')] + [out / n for n in ['RooCore-assembly.html', 'combined-wiring.html']]
active += [repo / n for n in ['HARDWARE_INVENTORY.md', 'WIRING_GUIDE.md', 'POWER_DESIGN.md',
                              'POWER_SHUTDOWN.md', 'lib/RooCoreLidarPower/README.md',
                              'docs/POWER_SHUTDOWN.md', 'docs/POWER_SHUTDOWN-he.md', 'docs/diagrams/wiring-xiao.svg',
                              'hardware/README.md']]
english = out / 'en'
active += [p for p in english.glob('*.md') if not p.name.startswith('._')] + [english / n for n in ['RooCore-assembly.html', 'combined-wiring.html']]
english_rows = json.loads((english / 'connections.json').read_text())
assert len(english_rows) == len(rows)
with (english / 'connections.csv').open(encoding='utf-8-sig', newline='') as f:
    english_csv = list(csv.DictReader(f))
for he_row, en_row, csv_row in zip(rows, english_rows, english_csv):
    for key in ['id', 'connection', 'first', 'last', 'net', 'color', 'component', 'step', 'status']:
        assert he_row[key] == en_row[key], ('translated electrical identity changed', key)
    assert csv_row == {key:str(value) for key,value in en_row.items()}
for path in english.iterdir():
    if path.name.startswith('._') or path.suffix not in ('.md','.html','.svg','.drawio','.json','.csv'):
        continue
    assert not re.search('[\u0590-\u05ff]', path.read_text()), ('untranslated text', path)
    if path.suffix in ('.svg','.drawio'):
        tree = ET.parse(path)
        if path.name in ('combined-wiring.svg','electrical-schematic.svg'):
            original = ET.parse(out / path.name)
            def wire_geometry(root):
                return [(node.attrib, [child.attrib for child in node]) for node in root.iter() if node.get('data-wire')]
            assert wire_geometry(tree.getroot()) == wire_geometry(original.getroot()), ('translated route changed', path)
assert len(ET.parse(english / 'RooCore-assembly.drawio').getroot().findall('diagram')) == 13
count = 0
for path in active:
    for target in targets(path):
        assert target.is_file(), (path, target)
        count += 1

with ZipFile(out / 'RooCore-assembly-kit.zip') as z:
    assert z.testzip() is None
    for path in active:
        relative = path.relative_to(repo).as_posix()
        local = path.read_text()
        if path.suffix == '.html':
            local = local.replace('<a href="RooCore-assembly-kit.zip">כל הערכה</a>', '')
            local = local.replace('<a href="../RooCore-assembly-kit.zip">Complete kit</a>', '')
        if relative in ['HARDWARE_INVENTORY.md', 'WIRING_GUIDE.md', 'POWER_DESIGN.md', 'POWER_SHUTDOWN.md']:
            local = re.sub(r'\[([^\]]+)\]\([^)]*2026-11-09-withdrawn-before-module-audit\.zip\)',
                           r'\1 (ארכיון נפרד בסביבת העבודה, אינו בערכה הזאת)', local)
        assert z.read(relative).decode() == local, ('stale ZIP entry', relative)
    for path in (repo / 'lib/RooCoreLidarPower').rglob('*'):
        if path.is_file() and not path.name.startswith('.'):
            assert z.read(path.relative_to(repo).as_posix()) == path.read_bytes()
    for path in [out / 'firmware-integration.json', out / 'sources/firmware-integration-build.txt',
                 out / 'firmware-source/RooCoreXiao.ino', out / 'firmware-source/config.example.h']:
        assert z.read(path.relative_to(repo).as_posix()) == path.read_bytes(), ('stale packaged firmware', path)
    assert z.read('hardware/check_shutdown.py') == (repo / 'hardware/check_shutdown.py').read_bytes()

print('PASS: 13 wires; 7 separate nets; two drawing maps; XML; 13 draw.io pages;')
print(f'{count} local links in both languages; identical English wiring geometry; firmware hashes/pin map; ZIP CRC and document/code parity. Not hardware validation.')
