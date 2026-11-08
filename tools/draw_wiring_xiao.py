"""Generate the current XIAO wiring sheet. Stdlib only; no external SVG assets."""
from html import escape
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "docs/diagrams/wiring-xiao.svg"
W, H = 2000, 3480
C = dict(bat="#b45a15", v5="#c13a40", v3="#147957", gnd="#425267",
         tx="#266fc1", rx="#008c94", ctl="#8250ac", ink="#183044", muted="#586b7c",
         amber="#986300", line="#d3dfe5")
p = [f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-labelledby="title desc">
<title id="title">RooCore — XIAO ESP32S3 wiring, power, fuse, UART and shutdown</title>
<desc id="desc">Bench wiring plan, not physically validated. Direct battery branch through one 5x20 fuse and XT30 into D36V28F5. Five volts feed XIAO and the LiDAR MOSFET switch. Two TXB0104 modules isolate UART. Two 2N3904 transistors drive BRC and regulator EN. All grounds are common. Unverified fuse and battery cutoff values are marked amber. Automatic D10 shutdown is implemented; measured power calibration is required before enabling it.</desc>
<defs>''']
for k in ("bat", "v5", "v3", "tx", "rx", "ctl"):
    p.append(f'<marker id="arrow-{k}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10Z" fill="{C[k]}"/></marker>')
p += ['''</defs><style>
text{font-family:Arial,"Arial Hebrew",Helvetica,sans-serif;fill:#183044}
.mono{font-family:Menlo,Consolas,monospace}
.he{direction:rtl;unicode-bidi:plaintext}
</style>''', f'<rect width="{W}" height="{H}" fill="#eef3f5"/>']


def rect(x, y, w, h, fill="white", stroke=None, radius=14, dashed=False):
    p.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke or C["line"]}" stroke-width="2"'+(' stroke-dasharray="8 6"' if dashed else '')+'/>')


def text(x, y, s, size=24, color="ink", bold=False, anchor="start", he=False, mono=False):
    p.append(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{700 if bold else 400}" text-anchor="{anchor}" class="{"he" if he else "mono" if mono else ""}" style="fill:{C.get(color,color)}">{escape(s)}</text>')


def wire(points, color, arrow=False, dashed=False, width=4):
    d = "M" + " L".join(f"{x},{y}" for x, y in points)
    p.append(f'<path d="{d}" fill="none" stroke="white" stroke-width="{width+5}" stroke-linejoin="round"/>')
    p.append(f'<path d="{d}" fill="none" stroke="{C[color]}" stroke-width="{width}" stroke-linejoin="round"'+(f' marker-end="url(#arrow-{color})"' if arrow else '')+(' stroke-dasharray="8 6"' if dashed else '')+'/>')


def dot(x, y, color):
    p.append(f'<circle cx="{x}" cy="{y}" r="6" fill="{C[color]}"/>')


def tag(x, y, label, color, width=130):
    rect(x-width/2, y-21, width, 36, "white", C[color], 7)
    text(x, y+5, label, 21, color, True, "middle", mono=True)


def ground(x, y):
    wire([(x, y), (x, y+12)], "gnd", width=3)
    for dy, half in ((12, 16), (19, 10), (26, 4)):
        p.append(f'<path d="M{x-half} {y+dy}H{x+half}" stroke="{C["gnd"]}" stroke-width="3"/>')


def resistor(x1, y1, x2, y2, label, color="ctl", label_side=1, dashed=False):
    wire([(x1, y1), (x2, y2)], color, dashed=dashed, width=3)
    mx, my = (x1+x2)/2, (y1+y2)/2
    if y1 == y2:
        rect(mx-38, my-10, 76, 20, "#fff", C[color], 1, dashed)
        text(mx, my-19, label, 21, color, anchor="middle")
    else:
        rect(mx-10, my-30, 20, 60, "#fff", C[color], 1, dashed)
        text(mx+23*label_side, my+7, label, 21, color, anchor="start" if label_side>0 else "end")


def panel(y, h, n, title):
    rect(40, y, 1920, h)
    rect(1882, y+20, 52, 44, "#183044", "#183044", 10)
    text(1908, y+51, n, 26, "white", True, "middle")
    text(1860, y+52, title, 31, bold=True, he=True)


def component(x, y, w, h, name, subtitle=""):
    rect(x, y, w, h, "#f7fafb", "#9bafbc", 10)
    text(x+w/2, y+34, name, 25, bold=True, anchor="middle")
    if subtitle:
        text(x+w/2, y+65, subtitle, 20, "muted", anchor="middle")


def npn(x, y, name):
    # Physical straight-lead TO-92, flat face toward viewer. Verify kit manufacturer.
    p.append(f'<path d="M{x-62} {y+90}V{y+23}Q{x-62} {y} {x-39} {y}H{x+39}Q{x+62} {y} {x+62} {y+23}V{y+90}Z" fill="#26333b" stroke="#16212a" stroke-width="2"/>')
    text(x, y+32, name, 22, "white", True, "middle")
    text(x, y+59, "2N3904", 20, "white", anchor="middle")
    for dx, pin in ((-40, "E"), (0, "B"), (40, "C")):
        p.append(f'<path d="M{x+dx} {y+90}V{y+140}" stroke="#8f9eab" stroke-width="6"/>')
        text(x+dx, y+118, pin, 20, "ink", True, "middle")


text(70, 70, "RooCore", 45, bold=True)
text(1920, 70, "מפת חיווט מלאה — בקר, ליידר ורומבה", 40, bold=True, he=True)
text(70, 112, "XIAO ESP32S3 · D36V28F5 · T-mini Plus · Rev 2 / 2026-11-08", 24, "muted")
text(1920, 153, "תכנון לבדיקת שולחן • טרם נבדק על החומרה • פרטי ההרכבה המסומנים בכתום דורשים אימות", 24, "amber", he=True)
for x, k, s in ((100, "bat", "BAT+ fused"), (410, "v5", "+5V"), (660, "v3", "+3V3"), (910, "gnd", "GND"), (1160, "tx", "UART TX"), (1460, "rx", "UART RX"), (1750, "ctl", "Control")):
    wire([(x-25, 190), (x+25, 190)], k)
    text(x+40, 197, s, 22, k)

# A — power. No current for the add-on flows through the Roomba OI power pins.
panel(220, 490, "1", "הספק — נתיך אחד בענף התוספת, סמוך לסוללה")
component(80, 325, 230, 130, "Roomba battery", "branch point: VERIFY")
text(99, 420, "BAT+", 23, "bat", True)
text(99, 446, "BAT−", 23, "gnd", True)
component(390, 325, 200, 130, "F1 · 5×20 mm", "QBGTFAK holder")
rect(425, 405, 130, 26, "#e8f5fb", "#637782", 8)
rect(425, 405, 22, 26, "#aab7be", "#637782", 3)
rect(533, 405, 22, 26, "#aab7be", "#637782", 3)
component(660, 325, 150, 130, "XT30", "18 AWG")
rect(696, 403, 78, 34, "#f4cb4f", "#b99530", 4)
text(710, 429, "+   −", 23, bold=True)
component(890, 325, 270, 130, "D36V28F5", "Pololu #3782")
text(908, 425, "VIN", 24, "bat", True)
text(1144, 425, "VOUT", 24, "v5", True, "end")
component(1280, 325, 260, 130, "MOSFET switch", "#2810 LV — verify board")
text(1296, 425, "VIN", 24, "v5", True)
text(1524, 425, "VOUT", 24, "v5", True, "end")
component(1660, 325, 270, 130, "T-mini Plus", "5V power input")
text(1677, 425, "VCC", 24, "v5", True)
wire([(310, 416), (390, 416)], "bat", True)
wire([(590, 416), (660, 416)], "bat", True)
wire([(810, 416), (890, 416)], "bat", True)
wire([(1160, 416), (1280, 416)], "v5", True)
wire([(1540, 416), (1660, 416)], "v5", True)
text(850, 390, "BAT_F", 20, "bat", True, "middle")
text(1220, 390, "+5V", 22, "v5", True, "middle")
text(1600, 390, "+5V_L", 21, "v5", True, "middle")
component(1000, 515, 370, 115, "XIAO · 5V input", "onboard regulator → 3V3 output")
wire([(1210, 416), (1210, 490), (1185, 490), (1185, 515)], "v5", True)
dot(1210, 416, "v5")
text(80, 500, "F1: 2A candidate — DC rating / breaking capacity / inrush NOT verified", 23, "amber")
text(80, 543, "BAT− → XT30 (−) → common GND", 24, "gnd", True)
text(80, 578, "Original Roomba battery wiring stays separate from this fuse.", 23, "muted")
text(80, 613, "Battery tap location is not identified. Never use OI pins 1/2 for this load.", 22, "amber")
text(1425, 532, "Switch ON ← D3 / GPIO4", 23, "ctl")
text(1425, 566, "Onboard slide: OFF · SW: unused", 22, "muted")
text(1425, 600, "LiDAR: 4.8–5.2V at its connector", 22, "v5")
text(1425, 632, "PG on regulator: unused", 22, "muted")
wire([(95, 674), (1905, 674)], "gnd", width=5)
for x, label in ((150, "BAT− / XT30−"), (470, "D36 GND ×2"), (760, "XIAO GND"), (1050, "switch GND"), (1320, "LiDAR GND"), (1590, "TXB ×2 GND"), (1855, "OI 6/7")):
    dot(x, 674, "gnd")
    text(x, 661, label, 20, "gnd", anchor="middle")

# B — two independent UART paths with endpoint labels and direction arrows.
panel(740, 535, "2", "תקשורת — משדר מתחבר למקלט, דרך שני המתאמים")
component(80, 845, 230, 200, "Roomba OI", "Mini-DIN 7 · 5V logic")
component(440, 845, 260, 200, "U1 · TXB0104", "B side 5V · A side 3V3")
component(850, 845, 300, 200, "XIAO ESP32S3", "3.3V GPIO only")
component(1300, 845, 260, 200, "U2 · TXB0104", "A side 3V3 · B side 3V3")
component(1690, 845, 240, 200, "T-mini Plus", "UART · 230400 8N1")
for y, left, right, color in ((945, "4 · TXD", "D1 / G2", "rx"), (1010, "3 · RXD", "D0 / G1", "tx")):
    text(96, y+8, left, 25, color, True)
    text(455, y+8, "B2" if y==945 else "B1", 23, color, True)
    text(685, y+8, "A2" if y==945 else "A1", 23, color, True, "end")
    text(865, y+8, right, 23, color, True)
    if y == 945:
        wire([(310,y),(440,y)],color,True); wire([(700,y),(850,y)],color,True)
    else:
        wire([(850,y),(700,y)],color,True); wire([(440,y),(310,y)],color,True)
for y, pin, channel, end, color in ((945, "D7 / G44", "2", "TX", "rx"), (1010, "D6 / G43", "1", "RX", "tx")):
    text(1135,y+8,pin,23,color,True,"end")
    text(1315,y+8,"A"+channel,23,color,True)
    text(1545,y+8,"B"+channel,23,color,True,"end")
    text(1706,y+8,end,25,color,True)
    if y==945:
        wire([(1690,y),(1560,y)],color,True); wire([(1300,y),(1150,y)],color,True)
    else:
        wire([(1150,y),(1300,y)],color,True); wire([(1560,y),(1690,y)],color,True)
for x, a, b, oe in ((440,"VCCA ← 3V3","VCCB ← 5V","OE: divider below"),(1300,"VCCA ← 3V3","VCCB ← 3V3","OE ← D8 / GPIO7")):
    text(x,1080,a,23,"v3",True)
    text(x,1114,b,23,"v5" if x==440 else "v3",True)
    text(x,1148,"GND → common GND",22,"gnd")
    text(x,1182,oe,23,"ctl",True)
text(80,1080,"5 · BRC ← Q2 collector",22,"ctl")
text(80,1114,"6/7 · GND → common",22,"gnd")
text(80,1148,"1/2 · Vpwr: NOT CONNECTED",21,"amber",True)
text(1690,1080,"VCC ← switched +5V_L",21,"v5",True)
text(1690,1114,"GND → common",22,"gnd")
text(1910,1235,"שמות רשת זהים הם אותו חיבור חשמלי • ערוצים לא בשימוש נשארים פנויים • קווי אות קצרים",23,"muted",he=True)

# C — control circuits with all discrete resistors.
panel(1305, 690, "3", "מעגלי הבקרה — שני טרנזיסטורים ואחד־עשר נגדים")
text(80,1404,"A · Regulator EN / POWER_OFF",28,bold=True)
text(1050,1404,"B · Roomba BRC / WAKE",28,bold=True)
tag(150,1455,"BAT_F","bat",130)
resistor(215,1455,510,1455,"R1 100k","bat")
tag(570,1455,"EN","ctl",90)
wire([(510,1455),(525,1455)],"ctl")
dot(510,1455,"ctl")
resistor(150,1505,510,1505,"100k INTERNAL to D36","bat",dashed=True)
wire([(150,1470),(150,1505)],"bat",dashed=True)
wire([(510,1455),(510,1505)],"ctl",dashed=True)
resistor(510,1505,510,1605,"R2 4.7k","ctl")
resistor(510,1605,510,1705,"R3 470Ω","ctl")
ground(510,1705)
npn(760,1515,"Q1")
wire([(800,1655),(920,1655),(920,1455),(615,1455)],"ctl")
wire([(720,1655),(720,1710)],"gnd"); ground(720,1710)
resistor(620,1800,760,1800,"R4 10k","ctl")
wire([(760,1800),(760,1655)],"ctl"); dot(760,1765,"ctl")
resistor(760,1765,900,1765,"R5 100k","ctl")
wire([(900,1765),(930,1765),(930,1820)],"gnd"); ground(930,1820)
tag(485,1800,"D10 / GPIO9","ctl",210)
wire([(590,1800),(620,1800)],"ctl")
text(80,1766,"EN is NOT connected directly to GPIO.",22,"ctl",True)
text(80,1870,"Divider values: BENCH STARTING POINT, not approved battery limits.",23,"amber",True)
text(80,1906,"Nominal model: OFF ≈12.81V · ON ≈14.41V; battery/rebound unverified.",23,"amber")
text(80,1942,"GPIO9: automatic cutoff implemented; measured calibration required.",22,"amber")

tag(1145,1460,"OI pin 5","ctl",160)
npn(1480,1490,"Q2")
wire([(1225,1460),(1640,1460),(1640,1630),(1520,1630)],"ctl")
wire([(1440,1630),(1440,1675)],"gnd"); ground(1440,1675)
tag(1150,1728,"D9 / GPIO8","ctl",190)
resistor(1245,1728,1480,1728,"R6 4.7k","ctl")
wire([(1480,1728),(1480,1630)],"ctl"); dot(1480,1690,"ctl")
resistor(1480,1690,1710,1690,"R7 10k","ctl")
wire([(1710,1690),(1760,1690),(1760,1735)],"gnd"); ground(1760,1735)
text(1050,1796,"D9 HIGH pulls BRC LOW; D9 LOW releases it.",23,"ctl")
text(1050,1840,"Q1 / Q2: TO-92, flat face toward you, leads down.",22,"muted")
text(1050,1875,"Shown E–B–C: onsemi 2N3904 reference only.",23,"muted")
text(1050,1910,"VERIFY actual BOJACK pin order before soldering.",23,"amber",True)
text(1050,1945,"These are NPN 2N3904, not the legacy PNP 2N3906.",22,"muted")

# D — pulls and a compact full pin assignment table.
panel(2025, 470, "4", "נגדי ברירת מחדל ומפת הפינים של הבקר")
text(80,2122,"C · LiDAR control",27,bold=True)
tag(170,2175,"D3 / GPIO4","ctl",180)
wire([(260,2175),(475,2175)],"ctl",True)
text(490,2182,"switch ON",24,"ctl",True)
dot(345,2175,"ctl")
resistor(345,2175,345,2290,"R8 10k","ctl"); ground(345,2290)
tag(170,2370,"D8 / GPIO7","ctl",180)
wire([(260,2370),(475,2370)],"ctl",True)
text(490,2377,"U2 OE",24,"ctl",True)
dot(345,2370,"ctl")
resistor(345,2370,345,2450,"R9 10k","ctl"); ground(345,2450)
text(725,2122,"D · Roomba translator OE",27,bold=True)
tag(800,2175,"3V3","v3",100)
resistor(850,2175,1085,2175,"R10 10k","v3")
tag(1160,2175,"U1 OE","ctl",130)
wire([(1085,2175),(1095,2175)],"ctl"); dot(1085,2175,"ctl")
resistor(1085,2175,1085,2310,"R11 100k","ctl",label_side=-1)
ground(1085,2310)
text(725,2370,"Verify module OE jumpers / supply ramp.",22,"amber")
text(725,2406,"Check local bypass caps on both TXB boards.",22,"amber")
text(725,2442,"No external pull-ups on A/B UART lines.",22,"muted")

rect(1285,2100,645,366,"#f4f8fa")
text(1310,2136,"XIAO label",23,bold=True)
text(1515,2136,"GPIO",23,bold=True)
text(1630,2136,"Destination",23,bold=True)
rows=[("D0 / D1","1 / 2","U1 A1 / A2"),("D6 / D7","43 / 44","U2 A1 / A2"),("D3","4","switch ON"),("D8","7","U2 OE"),("D9","8","R6 → Q2 base"),("D10","9","R4 → Q1 base"),("D4 / D5","5 / 6","reserved I²C"),("D2","3","unused / strap"),("BAT pads","—","NOT CONNECTED")]
for i,row in enumerate(rows):
    for x,s in zip((1310,1515,1630),row):
        text(x,2172+i*32,s,22,"ink" if i<6 else "muted")

# E — connector orientation, redrawn from manufacturer views rather than wire colors.
panel(2525,630,"5","זיהוי פינים — כיוון המבט חשוב")
text(90,2626,"XIAO · component side / USB at top",26,bold=True)
rect(295,2670,230,335,"#52665c","#334a40",25)
rect(365,2655,90,83,"#cad2d5","#778993",9)
rect(379,2664,62,14,"#253640","#778993",3)
rect(330,2768,160,150,"#d7dfdf","#a4b1b4",6)
text(410,2820,"seeed studio",21,bold=True,anchor="middle")
text(410,2860,"XIAO S3",26,bold=True,anchor="middle")
text(410,2890,"TOP VIEW",19,"muted",anchor="middle")
for i,(left,right) in enumerate(zip(["D0 / G1","D1 / G2","D2 / G3","D3 / G4","D4 / G5","D5 / G6","D6 / G43"],["5V / VBUS","GND","3V3 OUT","D10 / G9","D9 / G8","D8 / G7","D7 / G44"])):
    y=2725+i*40
    for x in (300,520):
        p.append(f'<circle cx="{x}" cy="{y}" r="10" fill="#d3b963" stroke="#f7eab4" stroke-width="3"/>')
    wire([(235,y),(285,y)],"gnd",width=2)
    text(224,y+7,left,23,anchor="end")
    wire([(535,y),(560,y)],"gnd",width=2)
    text(570,y+7,right,23)
text(90,3065,"BAT pads on back: unused",23,"muted")
text(90,3100,"Connect the supplied Wi-Fi antenna to its socket.",22,"muted")

text(785,2626,"Roomba · female OI socket",26,bold=True)
text(785,2661,"Looking INTO the robot socket, NOT plug solder side",20,"muted")
p.append('<circle cx="1050" cy="2845" r="145" fill="#e8eef1" stroke="#758a98" stroke-width="4"/>')
rect(1026,2697,48,23,"white","#758a98",1)
rect(1005,2826,90,44,"white","#758a98",1)
for num,dx,dy,k in [(7,-70,-70,"gnd"),(6,0,-70,"gnd"),(5,70,-70,"ctl"),(4,-106,7,"rx"),(3,106,7,"tx"),(2,-38,90,"amber"),(1,38,90,"amber")]:
    x,y=1050+dx,2845+dy
    p.append(f'<circle cx="{x}" cy="{y}" r="22" fill="white" stroke="{C[k]}" stroke-width="3"/>')
    text(x,y+8,str(num),24,k,True,"middle")
text(1050,3027,"6/7 edge faces the robot's outer circumference",20,"muted",anchor="middle")
text(785,3065,"3 RXD · 4 TXD · 5 BRC · 6/7 GND",23)
text(785,3100,"1/2 Vpwr: isolate both wires",23,"amber",True)

text(1420,2626,"LiDAR · GH1.25-4P socket",26,bold=True)
text(1420,2661,"Looking INTO the sensor, as in YDLIDAR drawing",20,"muted")
rect(1450,2740,420,180,"#f7fafb","#758a98",2)
rect(1465,2755,390,150,"white","#758a98",2)
rect(1555,2768,210,48,"#e8eef1","#758a98",1)
for x in (1495,1815):
    rect(x,2768,27,28,"#e8eef1","#758a98",1)
for x in (1520,1605,1690,1775):
    rect(x,2893,28,12,"#f7fafb","#758a98",0)
for x,label,k in ((1534,"VCC","v5"),(1619,"TX","rx"),(1704,"GND","gnd"),(1789,"RX","tx")):
    wire([(x,2920),(x,2950)],k,width=2)
    text(x,2980,label,24,k,True,"middle")
text(1420,3065,"Match socket shape before mapping the cable.",22,"muted")
text(1420,3100,"Wire colors are NOT a pinout.",23,"amber",True)

# F — pre-power gates and navigation legend.
rect(40,3185,1920,240,"#fff8e8","#d9b66a")
text(1920,3230,"לפני הפעלה — ארבע נקודות שעדיין דורשות בדיקה",29,"amber",True,he=True)
text(1920,3273,"זיהוי נקודת הסוללה והמחברים; נתיך מתאים לזרם ולניתוק זרם ישר; ספי הכיבוי; מתח הליידר בהתנעה ובטעינה.",24,"amber",he=True)
text(1920,3313,"בתצורה ללא דיודה: לנתק פיזית את הזנת חמשת הוולט לבקר לפני חיבור כבל USB. כיבוי דרך EN אינו ניתוק החוט.",24,"amber",he=True)
text(1920,3353,"בחלקים 1–4 הפינים מסודרים לקריאות, לא לפי מיקום הפדים. בחלק 5 מוצגים מבטי היצרן. לא להסתמך על צבעי חוטים.",24,"muted",he=True)
text(1920,3393,"כבל Mini-DIN 7 לא נמצא ברשימת ההזמנות. קבלי סינון נוספים ייקבעו לפי הלוחות והמדידות; אינם מוצגים כאילו כבר נרכשו.",23,"muted",he=True)
text(70,3458,"Matching net names = same wire network · dots = junctions · arrows = signal / power flow · see docs/wiring-xiao.md",21,"muted")
p.append("</svg>")
OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_text("\n".join(p)+"\n",encoding="utf-8")
print(OUT)
