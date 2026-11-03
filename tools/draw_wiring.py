"""Generate English wiring diagrams for the upstream PNP layout."""
from pathlib import Path
from html import escape

def draw(he=False):
    p=['''<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="1330" viewBox="0 0 1400 1330" role="img" aria-labelledby="title desc">
<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10Z" fill="#222"/></marker></defs>
<rect width="1400" height="1330" fill="white"/>
<style>text{font-family:Arial,Helvetica,sans-serif;fill:#222}.mono{font-family:Menlo,Consolas,monospace}</style>''']
    title='RooCore wiring with a PNP transistor'
    p.append('<title id="title">'+title+'</title><desc id="desc">ESP-01S, Roomba battery, 3.3V buck, 2N3906, GPIO1 TX, GPIO3 RX and GPIO2 BRC. Based on thehookup, with RooCore recording.</desc>')
    def text(x,y,s,size=18,bold=False,mono=False,rtl=False):
        p.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{700 if bold else 400}" class="{"mono" if mono else ""}"'+(' direction="rtl" unicode-bidi="embed" text-anchor="start"' if rtl else '')+f'>{escape(s)}</text>')
    def box(x,y,w,h): p.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="white" stroke="#333" stroke-width="1.5"/>')
    def line(x,y,X,Y,arrow=False): p.append(f'<path d="M{x} {y} L{X} {Y}" fill="none" stroke="#333" stroke-width="1.8"'+(' marker-end="url(#arrow)"' if arrow else '')+'/>')
    def dot(x,y): p.append(f'<circle cx="{x}" cy="{y}" r="4" fill="#222"/>')
    def ground(x,y):
        line(x,y,x,y+15)
        for i,w in enumerate([15,10,5]): line(x-w,y+15+i*6,x+w,y+15+i*6)
    text(45, 48, 'RooCore wiring', 30, True)
    text(45, 80, 'thehookup hardware baseline + our sensor recorder')
    text(45, 118, 'Power from the robot', 21, True)
    box(45,140,300,80);text(60, 170, 'Roomba OI pin 1', 20, True)
    text(60,199,'Vpwr → switch → buck IN+',17,mono=True)
    box(500,140,350,80);text(515, 170, 'Buck converter → 3.3V', 20, True)
    text(515, 199, 'Input rating: 24V or higher', 17)
    line(345,180,500,180,True);text(390,163,'Vpwr',17)
    line(850,180,1045,180,True);text(910,163,'3.3V',17)
    box(1045,140,310,80);text(1060,173,'ESP VCC / EN',20,True)
    text(1060,200,'100nF + 100µF to GND',16)
    text(45, 252, 'Common ground: OI pin 6/7, buck IN-/OUT-, ESP GND and Q1 collector.', 18)
    text(45, 280, 'OI supply: 200mA continuous total; below 500mA peak, including startup.', 18, True)
    text(45, 323, 'Serial connection', 21, True)
    text(870,354,'115200 baud · 8N1 · Passive',17)
    box(45,380,300,430);text(65,417,'ESP-01S',23,True)
    box(1045,380,310,430);text(1065, 417, 'Roomba socket', 23, True)
    text(65,473,'RX / GPIO3',20,mono=True);text(1065,473,'4 · TXD',20,mono=True)
    box(515,430,360,120);text(550,456,'Q1 · 2N3906 · PNP',19,True)
    text(530,491,'E',21,True);text(843,491,'B',21,True);text(688,533,'C',21,True)
    line(1045,485,875,485,True);line(515,485,345,485,True)
    text(898, 459, 'Sensor data', 16)
    ground(695,550);text(724,577,'GND',15)
    text(65,624,'TX / GPIO1',20,mono=True);text(1065,624,'3 · RXD',20,mono=True)
    line(345,636,1045,636,True)
    text(550, 620, 'Sensor requests', 17)
    text(65,732,'GPIO2',20,mono=True);text(1065,732,'5 · BRC',20,mono=True)
    line(345,744,1045,744,True)
    text(480, 726, '1 second low every 60 seconds', 17)
    text(65,788,'GND',17,mono=True);text(1065,788,'6 / 7 · GND',17,mono=True)
    line(195,810,195,837);line(1195,810,1195,837);line(195,837,1195,837)
    dot(195,837);dot(1195,837)
    text(45, 875, 'ESP connects to our server over Wi-Fi/TCP. No MQTT broker.', 18)
    line(45,898,1355,898)
    text(45, 932, 'Robot socket, looking into the holes', 20, True)
    p.append('<circle cx="160" cy="1047" r="85" fill="white" stroke="#333" stroke-width="1.5"/>')
    p.append('<rect x="150" y="957" width="20" height="12" fill="white"/>')
    box(134,1037,52,24)
    for n,x,y in [(7,117,1008),(6,160,1008),(5,203,1008),(4,95,1050),(3,225,1050),(2,137,1096),(1,183,1096)]:
        p.append(f'<circle cx="{x}" cy="{y}" r="16" fill="white" stroke="#333"/>');text(x-5,y+6,str(n),17)
    text(270, 1007, '1: buck IN+', 17)
    text(270, 1040, '2: unused', 17)
    text(270, 1073, '5: GPIO2', 17)
    text(45, 1167, 'Notch points toward the robot’s edge.', 17)
    text(500, 932, 'PNP from your BOJACK kit', 20, True)
    text(500,972,'2N3906',22,True)
    for i,s in enumerate(['E → ESP RX / GPIO3','B → Roomba TXD / pin 4','C → GND']):text(500,1012+36*i,s,18,mono=True)
    text(500, 1126, 'Follow E/B/C labels, not the image’s leg order.', 16)
    text(975, 932, 'Run mode', 20, True)
    text(975,972,'GPIO0 · EN · RST',18,mono=True)
    text(975, 1005, 'Keep high at boot.', 17)
    text(975, 1042, 'GPIO2 starts high, then pulses BRC.', 16)
    text(975, 1080, 'Set buck output before plugging in the ESP.', 16)
    text(975, 1117, 'Only 3.3V goes to ESP VCC.', 17, True)
    text(975, 1167, 'Use short, secured wires.', 17)
    line(45,1192,1355,1192)
    text(45, 1228, 'Programming only:', 19, True)
    text(245,1228,'Mac USB → CH340 (PROG) → ESP-01S',18,mono=True)
    text(45, 1263, 'Remove the ESP from the robot circuit before using the programmer.', 17)
    text(45, 1303, 'RooCore v2 · Based on thehookup · Firmware build tested; hardware not tested here.', 16)
    p.append('</svg>')
    return '\n'.join(p)+'\n'

if __name__=='__main__':
    root=Path(__file__).resolve().parents[1]/'docs/diagrams'
    for he,name in [(False,'wiring'),(True,'wiring-he')]:
        (root/(name+'.svg')).write_text(draw(he),encoding='utf-8')
