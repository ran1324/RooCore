"""Draw the agreed pin-to-pin wiring with colored wires and labeled endpoints."""
from html import escape
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / 'docs/diagrams/wiring-simple.svg'
p = ['''<svg xmlns="http://www.w3.org/2000/svg" width="1440" height="940" viewBox="0 0 1440 940" role="img" aria-labelledby="title desc">
<title id="title">RooCore pin-to-pin wiring</title>
<desc id="desc">Roomba pin 1 directly to buck IN+. Buck OUT+ at 3.3V to ESP VCC and EN. Roomba pin 4 TXD to transistor B, transistor E to ESP GPIO3 RX. ESP GPIO1 TX to Roomba pin 3 RXD. ESP GPIO2 to Roomba pin 5 BRC. Roomba pin 6 or 7, buck IN- and OUT-, transistor C and ESP GND share ground. No external RX resistor.</desc>
<rect width="1440" height="940" fill="white"/>
<style>text{font-family:Arial,Helvetica,sans-serif;fill:#20252b} .pin{font-family:Consolas,Menlo,monospace}</style>''']

def text(x,y,s,size=20,bold=False,anchor='start',mono=False):
    p.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{700 if bold else 400}" text-anchor="{anchor}" class="{"pin" if mono else ""}">{escape(s)}</text>')

def box(x,y,w,h):
    p.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="4" fill="#fafafa" stroke="#72777e" stroke-width="1.5"/>')

def wire(d,c):
    # White clearance at crossings keeps separate nets visibly separate.
    p.append(f'<path d="{d}" fill="none" stroke="white" stroke-width="11" stroke-linejoin="round"/>')
    p.append(f'<path d="{d}" fill="none" stroke="{c}" stroke-width="4" stroke-linejoin="round"/>')

def node(x,y,c,solid=False):
    p.append(f'<circle cx="{x}" cy="{y}" r="{6 if solid else 5}" fill="{c if solid else "white"}" stroke="{c}" stroke-width="3"/>')

orange='#b85b05'; red='#ca3030'; green='#168047'; blue='#2466c5'; purple='#8549b4'; ground='#353b43'
text(50,55,'RooCore wiring',32,True)
text(50,87,'Follow the pin labels. Wire colors identify each connection.',18)
box(50,135,270,600); text(75,174,'Roomba OI port',25,True)
box(1080,135,310,600); text(1105,174,'ESP-01S',25,True)
box(570,135,280,180); text(710,172,'Buck converter',24,True,'middle')
text(710,252,'Set output to 3.3V',18,False,'middle')
# TO-92 package, flat face toward the viewer, straight leads downward.
p.append('<path d="M650 445 V390 Q650 365 675 365 H745 Q770 365 770 390 V445 Z" fill="#303030" stroke="#141414" stroke-width="2"/>')
p.append('<path d="M656 440 V390 Q656 372 675 372 H745 Q764 372 764 390 V440 Z" fill="#252525"/>')
p.append('<text x="710" y="405" text-anchor="middle" font-family="Arial,sans-serif" font-size="22" style="fill:white">2N3906</text>')
p.append('<text x="710" y="430" text-anchor="middle" font-family="Arial,sans-serif" font-size="16" style="fill:white">PNP</text>')
for x in [670,710,750]:
    p.append(f'<path d="M{x} 445 V505" stroke="#90959b" stroke-width="7"/>')
text(793,392,'TO-92 · front view',16)
text(793,418,'Flat face toward you',15)

# Ground first; signal crossings have white gaps and no junction dot.
wire('M320 680 H1080',ground)
wire('M610 315 V340 H520 V680',ground)
wire('M800 315 V340 H980 V680',ground)
wire('M750 505 V680',ground)
# Permanent battery connection.
wire('M320 215 H570',orange)
wire('M850 215 H1025 V275 H1080',red)
wire('M1025 215 H1080',red); node(1025,215,red,True)
text(928,195,'3.3V',18,True,'middle')
# Separate receive-side segments meet only through the transistor.
wire('M320 450 H590 V525 H710 V505',green)
wire('M670 505 H630 V355 H950 V450 H1080',green)
wire('M320 555 H1080',blue)
wire('M320 610 H1080',purple)
text(450,433,'Roomba → ESP',16,False,'middle')
text(865,540,'ESP → Roomba',16,False,'middle')
text(865,595,'BRC / wake',16,False,'middle')
# Pin labels stay inside the component boxes.
for y,label,c in [(215,'1  BATTERY',orange),(450,'4  TXD',green),(555,'3  RXD',blue),(610,'5  BRC',purple),(680,'6 or 7  GND',ground)]:
    text(75,y+7,label,21,mono=True); node(320,y,c)
for y,label,c in [(215,'VCC',red),(275,'EN',red),(450,'RX / GPIO3',green),(555,'TX / GPIO1',blue),(610,'GPIO2',purple),(680,'GND',ground)]:
    text(1105,y+7,label,21,mono=True); node(1080,y,c)
text(75,717,'Pin 2: leave disconnected',16)
text(592,222,'IN+',20,True);node(570,215,orange)
text(828,222,'OUT+',20,True,'end');node(850,215,red)
text(610,298,'IN−',19,False,'middle'); node(610,315,ground)
text(800,298,'OUT−',19,False,'middle'); node(800,315,ground)
for x,label,c in [(670,'1 E',green),(710,'2 B',green),(750,'3 C',ground)]:
    text(x,485,label,16,True,'middle'); node(x,505,c)
for x in [520,750,980]: node(x,680,ground,True)
text(710,711,'Common ground',17,False,'middle')
p.append('<path d="M50 770 H1390" stroke="#ddd"/>')
text(50,805,'Dots mark connections. Crossings without dots are not connected.',18)
text(50,839,'Roomba and ESP pins are arranged for clarity. Transistor: onsemi 2N3906 pin order; verify your BOJACK part.',18)
text(50,873,'RX uses the internal pull-up. Keep GPIO0 and RST high at boot. Disconnect the programmer before use.',17)
text(50,911,'Use the wiring guide for connector orientation and power details. Hardware validation is pending.',16)
p.append('</svg>')
OUT.write_text('\n'.join(p)+'\n')
