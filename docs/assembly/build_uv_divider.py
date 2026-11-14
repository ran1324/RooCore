"""Conditional EN-divider appendix; separate from the 13-wire preparation harness."""
from pathlib import Path
import csv
import json
import xml.etree.ElementTree as ET

OUT = Path(__file__).resolve().parent
PARTS = [
    dict(ref='R_UV_A', value_ohm=10000, tolerance_percent=1, power_watt=0.25,
         identity='ELEGOO B072BL2VX1 assortment; individual manufacturer MPN not published'),
    dict(ref='R_UV_B', value_ohm=1000, tolerance_percent=1, power_watt=0.25,
         identity='ELEGOO B072BL2VX1 assortment; individual manufacturer MPN not published'),
]
ROWS = [
    dict(net='EN_SENSE', first='U3 D36V28F5 #3782 P1 EN', last='R_UV_A lead 1'),
    dict(net='UV_MID', first='R_UV_A lead 2', last='R_UV_B lead 1'),
    dict(net='GND', first='R_UV_B lead 2', last='U3 D36V28F5 #3782 P3 or P4 GND'),
]


def build():
    (OUT/'uv-divider.json').write_text(json.dumps(dict(
        status='CONDITIONAL: unpowered preparation only; no hardware measurements',
        viewing_direction='Manufacturer component-side photograph; five outer pads at left: EN VIN GND GND VOUT',
        construction='Inline series pair, no separate PCB; short flexible insulated signal leads, individually heat-shrunk joints, removable strain relief; verify cover clearance with all power removed',
        parts=PARTS, connections=ROWS, intentionally_unconnected=['U3 P6 PG', 'UV_MID to every other circuit'],
        internal_pullup_ohm=100000, internal_pullup_tolerance='UNKNOWN',
        nominal_off_volt=1.2*111/11, nominal_on_volt=1.35*111/11,
        source='https://www.pololu.com/product/3782#connections'), indent=2)+'\n')
    with (OUT/'uv-divider.csv').open('w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=list(ROWS[0]))
        writer.writeheader(); writer.writerows(ROWS)
    svg = ET.Element('svg', xmlns='http://www.w3.org/2000/svg', width='1100', height='470', viewBox='0 0 1100 470')
    ET.SubElement(svg, 'rect', width='1100', height='470', fill='white')
    model = ET.Element('mxGraphModel', page='1', pageWidth='1100', pageHeight='470')
    root = ET.SubElement(model, 'root')
    ET.SubElement(root, 'mxCell', id='0'); ET.SubElement(root, 'mxCell', id='1', parent='0')

    def label(key, text, x, y, width=400, size=21):
        ET.SubElement(svg, 'text', x=str(x), y=str(y), fill='#172b45',
                      **{'font-family':'Arial,sans-serif', 'font-size':str(size)}).text=text
        cell=ET.SubElement(root, 'mxCell', id=key, value=text, vertex='1', parent='1',
                           style=f'text;html=0;fontSize={size};align=left;')
        ET.SubElement(cell, 'mxGeometry', x=str(x), y=str(y-25), width=str(width), height='35', **{'as':'geometry'})

    def line(key, x1, y1, x2, y2):
        ET.SubElement(svg, 'line', x1=str(x1), y1=str(y1), x2=str(x2), y2=str(y2), stroke='#172b45', **{'stroke-width':'3'})
        cell=ET.SubElement(root,'mxCell',id=key,edge='1',parent='1',style='endArrow=none;startArrow=none;strokeWidth=3;')
        geo=ET.SubElement(cell,'mxGeometry',relative='1',**{'as':'geometry'})
        ET.SubElement(geo,'mxPoint',x=str(x1),y=str(y1),**{'as':'sourcePoint'})
        ET.SubElement(geo,'mxPoint',x=str(x2),y=str(y2),**{'as':'targetPoint'})

    def resistor(key, x, y, value):
        ET.SubElement(svg,'rect',x=str(x),y=str(y),width='110',height='34',fill='white',stroke='#172b45',**{'stroke-width':'3'})
        cell=ET.SubElement(root,'mxCell',id=key,value=value,vertex='1',parent='1',style='rounded=0;fillColor=#ffffff;fontSize=20;')
        ET.SubElement(cell,'mxGeometry',x=str(x),y=str(y),width='110',height='34',**{'as':'geometry'})
        label(key+'value',value,x,y-14,160)

    label('title','D36V28F5 #3782: conditional hardware cutoff',35,40,1000,27)
    label('status','Prepare with all power removed. Thresholds calculated; not measured.',35,78,1000,19)
    label('vin','U3 P2 VIN',35,197,150)
    line('vin-wire',180,190,245,190)
    resistor('Rint',245,173,'100k internal')
    line('en-left',355,190,450,190)
    ET.SubElement(svg,'circle',cx='450',cy='190',r='6',fill='#172b45')
    label('en','U3 P1 EN',390,148,200)
    line('en-a',450,190,510,190)
    resistor('R_UV_A',510,173,'10k (A)')
    line('middle',620,190,690,190)
    resistor('R_UV_B',690,173,'1k (B)')
    line('return',800,190,890,190)
    label('gnd','U3 P3/P4 GND',865,242,220)
    line('gnd-stem',890,190,890,270)
    for i,(half,y) in enumerate([(23,270),(15,280),(7,290)]):
        line('ground'+str(i),890-half,y,890+half,y)
    label('spec','A + B = 11k. Each: 1%, 0.25W minimum; no polarity.',35,340,1000)
    label('limits','Nominal VIN: OFF 12.11V / ON 13.62V. Full tolerance UNKNOWN.',35,380,1000)
    label('nc','PG unused. No GPIO connection. Internal 100k is already on U3.',35,420,1000)
    ET.ElementTree(svg).write(OUT/'uv-divider.svg',encoding='utf-8',xml_declaration=True)
    ET.ElementTree(model).write(OUT/'uv-divider.drawio',encoding='utf-8',xml_declaration=True)


if __name__ == '__main__':
    build()
