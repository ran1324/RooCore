# coding: utf-8
"""Translate generated artifacts without rebuilding or duplicating wire geometry."""
import csv
import hashlib
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
from urllib.parse import urlsplit
import guide_english

ROOT = Path(__file__).resolve().parent
EN = ROOT / 'en'
HEBREW = re.compile('[\u0590-\u05ff]')
BIDI = re.compile('[\u200e\u200f\u202a-\u202e\u2066-\u2069]')
missing = {}
catalog = guide_english.translations()
ui_path = ROOT / 'ui.en.json'
if ui_path.exists():
    catalog.update(json.loads(ui_path.read_text()))
replacements = sorted(((BIDI.sub('',a),b) for a,b in catalog.items()), key=lambda p:len(p[0]), reverse=True)

def translate(value):
    if not HEBREW.search(value):
        return value
    clean = BIDI.sub('',value)
    result = clean
    for a,b in replacements:
        result = result.replace(a,b)
    if HEBREW.search(result):
        missing[clean] = result
    return result

def link(value):
    parsed = urlsplit(value)
    if parsed.scheme or parsed.netloc or not parsed.path:
        return value
    if value == 'en/README.md':
        return '../README.md'
    if value == 'en/RooCore-assembly.html':
        return '../RooCore-assembly.html'
    if value == '../../HARDWARE_INVENTORY.md':
        return 'HARDWARE_INVENTORY.md'
    if value.startswith(('assets/','sources/','firmware-source/')) or value in ('firmware-integration.json','RooCore-assembly-kit.zip'):
        return '../'+value
    return value

def css(value):
    return value.replace('text-align:right','text-align:left').replace('border-right:','border-left:')

class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.output=[]
        self.raw=None
    def handle_decl(self,decl): self.output.append('<!'+decl+'>')
    def handle_comment(self,data): self.output.append('<!--'+data+'-->')
    def start(self,tag,attrs,closing):
        result=[]
        for key,value in attrs:
            if value is None:
                result.append(key)
                continue
            if key in ('href','src'): value=link(value)
            elif key in ('dir','direction') and value=='rtl': value='ltr'
            elif key=='lang' and value=='he': value='en'
            elif key in ('alt','title','aria-label','placeholder'): value=translate(value)
            result.append(key+'="'+html.escape(value,quote=True)+'"')
        self.output.append('<'+tag+(' '+' '.join(result) if result else '')+closing)
        if tag in ('script','style'): self.raw=tag
    def handle_starttag(self,tag,attrs): self.start(tag,attrs,'>')
    def handle_startendtag(self,tag,attrs): self.start(tag,attrs,'/>')
    def handle_endtag(self,tag):
        self.output.append('</'+tag+'>')
        if tag==self.raw:self.raw=None
    def handle_data(self,data):
        if self.raw: self.output.append(css(data) if self.raw=='style' else data)
        else: self.output.append(html.escape(translate(data),quote=False))

def translate_xml(path):
    tree=ET.parse(path)
    for node in tree.iter():
        if node.text: node.text=translate(node.text)
        if node.tail: node.tail=translate(node.tail)
        for key,value in list(node.attrib.items()):
            if key in ('direction','dir') and value=='rtl': value='ltr'
            if key in ('value','name','aria-label'):
                value=translate(html.unescape(value)).replace('dir="rtl"','dir="ltr"')
            node.set(key,value)
    # Preserve the common namespace without introducing ns0 in stand-alone SVGs.
    ET.register_namespace('','http://www.w3.org/2000/svg')
    tree.write(EN/path.name,encoding='utf-8',xml_declaration=True)

def localized(value):
    if isinstance(value,str):return translate(value)
    if isinstance(value,list):return [localized(v) for v in value]
    if isinstance(value,dict):return {k:localized(v) for k,v in value.items()}
    return value

def build(inventory=False):
    # A source edit requires an explicit translation review, not an automatic
    # assumption that the old English wording still represents the new source.
    stamp_path=ROOT/'translation-sources.json'
    if stamp_path.exists():
        for name,digest in json.loads(stamp_path.read_text()).items():
            assert hashlib.sha256((ROOT.parents[1]/name).read_bytes()).hexdigest()==digest, ('English translation needs review',name)
    EN.mkdir(exist_ok=True)
    for name in ['RooCore-assembly.html','combined-wiring.html']:
        parser=Page();parser.feed((ROOT/name).read_text())
        rendered=''.join(parser.output)
        # Reciprocal language navigation does not alter the assembly contents.
        rendered=rendered.replace('href="../RooCore-assembly.html">English','href="../RooCore-assembly.html">Hebrew / &#1506;&#1489;&#1512;&#1497;&#1514;')
        (EN/name).write_text(rendered)
    active_xml=[p for p in ROOT.iterdir() if p.suffix in ('.svg','.drawio') and
                (p.stem.startswith(('photo-','connect-')) or p.stem in ('RooCore-assembly','combined-wiring','electrical-schematic'))]
    for path in active_xml:translate_xml(path)
    rows=localized(json.loads((ROOT/'connections.json').read_text()))
    for row in rows:
        row['source']='; '.join(link(value) for value in row['source'].split('; '))
    (EN/'connections.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
    with (EN/'connections.csv').open('w',encoding='utf-8-sig',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    md=translate((ROOT/'README.md').read_text())
    md=re.sub(r'(\[[^\]]*\]\()([^)]+)(\))',lambda m:m[1]+link(m[2])+m[3],md)
    md=md.replace('[English version](../README.md)','[Hebrew version](../README.md)')
    (EN/'README.md').write_text(md)
    if missing:
        (ROOT/'translation-missing.json').write_text(json.dumps(missing,ensure_ascii=False,indent=2)+'\n')
        if not inventory:
            raise ValueError(f'{len(missing)} untranslated segments; see translation-missing.json')
        print(f'{len(missing)} untranslated segments recorded')
    else:
        (ROOT/'translation-missing.json').unlink(missing_ok=True)
        print('Built English HTML, Markdown, diagrams and wire tables from the same source geometry.')

if __name__=='__main__':build('--inventory' in sys.argv)
