# coding: utf-8
"""Exact module-terminal schematic of the preparation harness, not a PCB/ERC model."""
import html
import xml.etree.ElementTree as ET


def build_schematic(rows, out):
    model = ET.Element('mxGraphModel', adaptiveColors='auto', page='1', pageWidth='1200', pageHeight='930')
    root = ET.SubElement(model, 'root')
    ET.SubElement(root, 'mxCell', id='0')
    ET.SubElement(root, 'mxCell', id='1', parent='0')
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="930" viewBox="0 0 1200 930" role="img" aria-label="RooCore harness: module terminal schematic, 13 wires"><rect width="1200" height="930" fill="white"/>']
    counter = 0

    def cell(ident, x, y, w, h, value='', style=''):
        c = ET.SubElement(root, 'mxCell', id=ident, value=value, style=style+';html=1;fontSize=16;', vertex='1', parent='1')
        ET.SubElement(c, 'mxGeometry', x=str(x), y=str(y), width=str(w), height=str(h), **{'as': 'geometry'})

    def text(x, y, value, size=18, anchor='middle', color='#17314a'):
        nonlocal counter
        counter += 1
        svg.append(f'<text x="{x}" y="{y}" fill="{color}" text-anchor="{anchor}" font-family="Arial,sans-serif" font-size="{size}">{html.escape(value)}</text>')
        width = max(70, len(value)*size*.65)
        left = x if anchor == 'start' else x-width if anchor == 'end' else x-width/2
        cell('t'+str(counter), left, y-size, width, size+5, html.escape(value), f'text;strokeColor=none;fillColor=none;align={"left" if anchor == "start" else "right" if anchor == "end" else "center"};fontColor={color}')

    def body(ident, x, y, w, h, labels):
        svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#edf3f8" stroke="#52728c" stroke-width="2"/>')
        cell(ident, x, y, w, h, '', 'fillColor=#edf3f8;strokeColor=#52728c;strokeWidth=2;')
        for offset, line in enumerate(labels):
            text(x+w/2, y+h-58+offset*24, line, 17)

    text(600, 35, 'RooCore — schematic of the 13 harness wires', 27)
    text(600, 68, 'UNPOWERED • USB disconnected • robot and LiDAR unplugged', 20, color='#9b342a')
    text(600, 99, 'Module pad names, not IC lead numbers. Dots are junctions; crossings without dots are separate.', 17)
    body('J1', 40, 150, 135, 155, ['J1 · XT30', 'LOAD half'])
    body('U3', 300, 150, 250, 245, ['U3 · D36V28F5', '5V fixed', 'GND 1 = GND 2'])
    body('S1', 800, 150, 180, 245, ['S1 · Pololu 2810', 'slide OFF'])
    body('J2', 1080, 150, 100, 280, ['J2 · GH', 'cable'])
    body('U4', 300, 440, 250, 200, ['U4 · XIAO', 'ESP32S3'])
    terminals = {
        'XT30 LOAD +': (175, 185, '+', 'end'),
        'XT30 LOAD −': (175, 225, '−', 'end'),
        'U3 VIN': (300, 185, 'VIN', 'start'),
        'U3 GND 1': (300, 225, 'GND 1', 'start'),
        'U3 GND 2': (550, 245, 'GND 2', 'end'),
        'U3 VOUT': (550, 185, 'VOUT', 'end'),
        '5V BUS': (660, 185, '', 'start'),
        'GND BUS': (600, 620, '', 'start'),
        'S1 VIN': (800, 185, 'VIN', 'start'),
        'S1 GND': (800, 245, 'GND', 'start'),
        'S1 ON': (800, 305, 'ON', 'start'),
        'S1 VOUT': (980, 185, 'VOUT', 'end'),
        'XIAO GND': (550, 565, 'GND', 'end'),
        'XIAO 5V': (550, 465, '5V/VBUS', 'end'),
        'XIAO D3 / GPIO4': (550, 515, 'D3/GPIO4', 'end'),
        'XIAO D4 / GPIO5': (300, 485, 'D4/GPIO5', 'start'),
        'XIAO D5 / GPIO6': (300, 530, 'D5/GPIO6', 'start'),
        'LiDAR cable VCC': (1080, 185, 'VCC', 'start'),
        'LiDAR cable GND': (1080, 275, 'GND', 'start'),
        'LiDAR cable TX': (1080, 230, 'TX', 'start'),
        'LiDAR cable RX': (1080, 320, 'RX', 'start'),
    }
    # The audit locks each route to specific endpoint names; row order alone is insufficient.
    routes = [
        ('XT30 LOAD +', 'U3 VIN', [(175,185),(300,185)], (230,170)),
        ('XT30 LOAD −', 'U3 GND 1', [(175,225),(300,225)], (230,215)),
        ('U3 VOUT', '5V BUS', [(550,185),(660,185)], (600,170)),
        ('U3 GND 2', 'GND BUS', [(550,245),(600,245),(600,620)], (585,380)),
        ('5V BUS', 'S1 VIN', [(660,185),(800,185)], (735,170)),
        ('GND BUS', 'S1 GND', [(600,620),(760,620),(760,245),(800,245)], (777,231)),
        ('GND BUS', 'XIAO GND', [(600,620),(600,565),(550,565)], (577,553)),
        ('5V BUS', 'XIAO 5V', [(660,185),(660,465),(550,465)], (625,449)),
        ('XIAO D3 / GPIO4', 'S1 ON', [(550,515),(700,515),(700,305),(800,305)], (723,290)),
        ('S1 VOUT', 'LiDAR cable VCC', [(980,185),(1080,185)], (1030,170)),
        ('GND BUS', 'LiDAR cable GND', [(600,620),(1020,620),(1020,275),(1080,275)], (1050,261)),
        ('XIAO D4 / GPIO5', 'LiDAR cable RX', [(300,485),(220,485),(220,675),(1050,675),(1050,320),(1080,320)], (245,470)),
        ('LiDAR cable TX', 'XIAO D5 / GPIO6', [(1080,230),(1010,230),(1010,700),(190,700),(190,530),(300,530)], (245,518)),
    ]
    ids = {}
    for i, (name, (x,y,label,anchor)) in enumerate(terminals.items()):
        ids[name] = f'pin{i}'
        cell(ids[name], x-3, y-3, 6, 6, '', 'ellipse;fillColor=#17314a;strokeColor=none;')
        if label:
            text(x+10 if anchor == 'start' else x-10, y+5, label, 16, anchor)
    colors = {'red': '#c82e30', 'black': '#26384a', 'blue': '#006fc4', 'green': '#007b54', 'purple': '#8545a4'}
    assert len(rows) == len(routes)
    for row, (first,last,points,label_at) in zip(rows, routes):
        assert (row['first'],row['last']) == (first,last)
        color=colors[row['color']]
        path=' '.join(f'{x},{y}' for x,y in points)
        svg.append(f'<g data-wire="{row["id"]}" data-from="{html.escape(first)}" data-to="{html.escape(last)}" data-net="{row["net"]}"><polyline points="{path}" fill="none" stroke="white" stroke-width="8"/><polyline points="{path}" fill="none" stroke="{color}" stroke-width="3"/></g>')
        c=ET.SubElement(root,'mxCell',id='wire'+str(row['id']),value=str(row['id']),source=ids[first],target=ids[last],edge='1',parent='1',style=f'html=1;endArrow=none;startArrow=none;strokeColor={color};strokeWidth=3;edgeStyle=orthogonalEdgeStyle;fontSize=14;')
        geometry=ET.SubElement(c,'mxGeometry',relative='1',**{'as':'geometry'})
        # Local CAD artifact retains fixed electrical routing, without an automatic router.
        if len(points)>2:
            array=ET.SubElement(geometry,'Array',**{'as':'points'})
            for x,y in points[1:-1]:ET.SubElement(array,'mxPoint',x=str(x),y=str(y))
        text(*label_at,str(row['id']),16,color=color)
    # External wires share the two explicitly designated bus junctions.
    for x,y,color in [(660,185,'#c82e30'),(600,620,'#26384a'),(600,565,'#26384a'),(760,620,'#26384a')]:
        svg.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{color}"/>')
    text(670,135,'5V BUS',18,'start','#c82e30')
    text(850,655,'GND BUS — common return',18)

    for x,y,label,side in [(300,275,'EN','left'),(300,305,'PG','left'),(980,285,'SW','right')]:
        if side=='left':
            text(x+10,y+5,label,16,'start');nx=x-18
        else:
            text(x-10,y+5,label,16,'end');nx=x+18
        svg.append(f'<path d="M{x} {y}H{nx}M{nx-5} {y-5}l10 10M{nx-5} {y+5}l10 -10" fill="none" stroke="#7d4d50" stroke-width="2"/>')
        cell('nc-'+label, nx-8, y-10, 16, 20, '×', 'text;strokeColor=none;fillColor=none;fontColor=#7d4d50;')
        if side == 'right':
            text(nx,y-14,'NC',13,'middle','#7d4d50')
        else:
            text(nx-14,y+5,'NC',13,'end','#7d4d50')
    text(600,745,'UART: D4 TX → sensor RX; sensor TX → D5 RX. 3.3V, 230400, 8N1.',18)
    text(600,780,'OFF order: UART detached → D4 LOW / D5 input pulldown → D3 LOW.',18)
    text(600,815,'NC: XIAO 3V3 / BAT / D0–D2 / D6–D10 / USB; S1 external switch pads.',18)
    text(600,850,'NOYITO: U1 planned for Roomba; U2 spare. Fuse / robot wiring are separate.',18)
    text(600,900,'Software sequencing is required. Read LIDAR_POWER_UART.md; hardware tests are pending.',18)
    svg.append('</svg>')
    rendered=''.join(svg)
    (out/'electrical-schematic.svg').write_text(rendered)
    ET.ElementTree(model).write(out/'electrical-schematic.drawio',encoding='utf-8',xml_declaration=True)
    return rendered, model
