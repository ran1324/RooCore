# coding: utf-8
from pathlib import Path
import base64,html,json,re
O=Path(__file__).resolve().parent
S=[]
def txt(x,y,t,size=23,anchor='middle'):
 rtl=bool(re.search('[\u0590-\u05ff]',t));t=re.sub(r'[A-Za-z0-9]+(?:[A-Za-z0-9 /._+−→–-]*[A-Za-z0-9+−])?',lambda m:'\u2066'+m[0]+'\u2069',t) if rtl else t
 S.append(f'<text x="{x}" y="{y}" text-anchor="{anchor}" direction="{"rtl" if rtl else "ltr"}" font-size="{size}" fill="var(--foreground)">{html.escape(t)}</text>')
def box(x,y,w,h,t):
 S.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="var(--card)" stroke="var(--border)"/>');txt(x+w/2,y+h/2+8,t)
def photo(file,x,y,w,h,crop=None):
 ext='png' if file.endswith('.png') else 'jpeg';data=base64.b64encode((O/'assets'/file).read_bytes()).decode();url=f'data:image/{ext};base64,{data}'
 if crop:
  vx,vy,vw,vh,iw,ih=crop;S.append(f'<svg x="{x}" y="{y}" width="{w}" height="{h}" viewBox="{vx} {vy} {vw} {vh}"><image href="{url}" width="{iw}" height="{ih}"/></svg>')
 else:S.append(f'<image href="{url}" x="{x}" y="{y}" width="{w}" height="{h}"/>')
S.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1600 1590" role="img" aria-label="חיווט משולב של הממיר, הבקר ומפסק הליידר לפי 13 חוטי הצמה, כולל תקשורת קבועה" style="font-family:Arial,sans-serif;background:var(--background)">')
txt(800,45,'כל צמת ההכנה בתמונה אחת',32)
txt(800,86,'ללא מתח · USB מנותק · אין עדיין חיבור לסוללה',23)
txt(800,122,'עיגול בקצה = פד חיבור · נקודה מלאה = פיצול משותף · הצטלבות ללא נקודה אינה חיבור',20)
photo('xt30-product.jpg',40,175,230,190);txt(155,165,'XT30 — תמונת זוגות הרכש')
box(30,380,230,50,'LOAD +');box(30,430,230,50,'LOAD −')
txt(155,520,'צד עומס, לאחר מיפוי',20);txt(155,550,'מתח הסוללה — לפני ההמרה',20)
photo('d36v28-pinout.jpg',330,230,540,351.45);txt(600,190,'הממיר D36V28F5',26)
txt(600,620,'EN / PG פנויים',21)
photo('xiao-product.jpg',1130,200,420,315);txt(1340,175,'בקר XIAO ESP32S3',26)
txt(1335,550,'USB מנותק · 3V3 / BAT פנויים',20)
photo('switch-2810-original.jpg',1130,700,420,323.75);txt(1400,1155,'מפסק Pololu 2810',26)
txt(1380,1200,'מתג OFF · אין נגד נוסף ל־ON',21)
photo('lidar-cover.png',330,800,250,235,(340,640,170,170,849,1200));txt(455,770,'T-mini Plus — צילום היצרן',24)
box(630,810,230,60,'כבל LiDAR · VCC');box(630,910,230,60,'כבל LiDAR · GND')
txt(600,1055,'התקע נשאר מנותק מהחיישן; נדרש מיפוי GH',21)
txt(540,1090,'12–13: תקשורת קבועה עם כיבוי בתוכנה',21)
box(630,1100,230,60,'כבל LiDAR · TX')
box(630,1190,230,60,'כבל LiDAR · RX')
# Separate positive distribution and return rails. These are conceptual solder junctions, not extra modules.
for x,color,y1,y2 in [(940,'var(--red)',270,1150),(1030,'var(--ground)',300,1150)]:
 S.append(f'<path d="M{x},{y1}V{y2}" stroke="{color}" stroke-width="8" fill="none"/>')
box(820,200,220,50,'5V — אחרי הממיר');box(900,1180,195,50,'GND — משותף')
txt(470,1275,'5V ו־GND נפרדים; אסור לגשר ביניהם',20)
rows=json.loads((O/'connections.json').read_text())
from guide_content import REVISION
# A route belongs to endpoints, not merely to its position in a list.
expected_endpoints=[('XT30 LOAD +','U3 VIN'),('XT30 LOAD −','U3 GND 1'),('U3 VOUT','5V BUS'),('U3 GND 2','GND BUS'),('5V BUS','S1 VIN'),('GND BUS','S1 GND'),('GND BUS','XIAO GND'),('5V BUS','XIAO 5V'),('XIAO D3 / GPIO4','S1 ON'),('S1 VOUT','LiDAR cable VCC'),('GND BUS','LiDAR cable GND'),('XIAO D4 / GPIO5','LiDAR cable RX'),('LiDAR cable TX','XIAO D5 / GPIO6')]
assert [(r['first'],r['last']) for r in rows]==expected_endpoints
assert [r['id'] for r in rows]==list(range(1,14))
# Exact pad positions measured on the source images in the audited individual diagrams.
routes=[
 [(260,405),(489.75,405.5)],
 [(260,455),(489.75,455)],
 [(489.75,556.25),(940,556.25)],
 [(489.75,504.5),(1030,504.5)],
 [(940,849.8),(1176.9,849.8)],
 [(1030,898.45),(1241.3,898.45)],
 [(1030,368.6),(1431.8,368.6)],
 [(940,354.5),(1450.4,354.5)],
 [(1285.1,314.3),(1100,314.3),(1100,1115),(1307.8,1115),(1307.8,948.15)],
 [(1338.25,735),(1338.25,600),(885,600),(885,840),(860,840)],
 [(1030,940),(860,940)],
 [(1265.6,327.8),(1115,327.8),(1115,1270),(885,1270),(885,1220),(860,1220)],
 [(630,1130),(300,1130),(300,135),(1120,135),(1120,240),(1246.4,240),(1246.4,341.6)]
]
assert len(rows)==len(routes)==13
colors={'red':'var(--red)','black':'var(--ground)','blue':'var(--blue)','green':'var(--green)','purple':'var(--purple)'}
for i,(r,pts) in enumerate(zip(rows,routes),1):
 color=colors[r['color']];points=' '.join(f'{x},{y}' for x,y in pts);title=f'{i}: {r["first"]} → {r["last"]}'
 S.append(f'<g class="wire" data-wire="{i}" data-from="{html.escape(r["first"])}" data-to="{html.escape(r["last"])}" data-net="{r["net"]}" role="button" tabindex="0" aria-label="{html.escape(title)}"><title>{html.escape(title)}</title><polyline points="{points}" fill="none" stroke="var(--background)" stroke-width="12" stroke-linejoin="round"/><polyline points="{points}" fill="none" stroke="{color}" stroke-width="5" stroke-linejoin="round"/></g>')
 for x,y in [pts[0],pts[-1]]:
  bus=x in (940,1030);S.append(f'<circle cx="{x}" cy="{y}" r="{6 if bus else 7}" fill="{color if bus else "var(--background)"}" stroke="{color}" stroke-width="3"/>')
# Permanent numbers remain visible in the standalone image, as well as in hover/table UI.
number_positions=[(350,384),(350,434),(800,580),(700,482),(1130,872),(1170,920),(1200,393),(1210,333),(1125,655),(865,710),(910,967),(1140,750),(325,675)]
for r,(x,y) in zip(rows,number_positions):
 color=colors[r['color']]
 S.append(f'<circle cx="{x}" cy="{y}" r="14" fill="var(--background)" stroke="{color}" stroke-width="2"/>')
 txt(x,y+6,str(r['id']),18)
# Essential pad labels beside endpoints, kept off the wires.
for x,y,t in [(490,391,'VIN'),(485,442,'GND'),(765,490,'GND'),(765,542,'VOUT 5V'),(1475,342,'5V'),(1475,398,'GND'),(1300,299,'D3'),(1190,318,'D4'),(1205,359,'D5'),(1162,828,'VIN'),(1245,881,'GND'),(1340,972,'ON'),(1380,724,'VOUT')]:txt(x,y,t,20)
S.append('<path d="M30 1300H1570" stroke="var(--border)" stroke-width="2"/>')
photo('noyito-product-photo.jpg',50,1340,220,170);photo('boj-product.jpg',370,1340,200,170)
txt(160,1540,'NOYITO: אחד לרומבה, אחד ספייר',20);txt(470,1540,'Q1 / Q2 — התאמת E/B/C',20)
txt(1100,1380,'עדיין מחוץ לחיווט: נתיך, נקודות הרומבה, EN ו־OI',24)
txt(1100,1430,'אין להוסיף C1–C4 או R8–R11 לפי התרשים הקודם',22)
txt(1100,1480,'תמונות דגמים; לא צילום של הרכבה שנבדקה',21)
S.append('</svg>');svg=''.join(S)
css=''' :root{--background:#fff;--foreground:#17314a;--card:#f2f6fa;--border:#bccbd7;--red:#c72d35;--ground:#27394c;--blue:#076db9;--green:#007b54;--purple:#8545a4}body{margin:0;font:18px/1.7 Arial,sans-serif;background:var(--background);color:var(--foreground)}header,footer{padding:16px 24px;max-width:1500px;margin:auto}a{color:#17649a}button{font:inherit;padding:6px 15px;cursor:pointer}nav{display:flex;flex-wrap:wrap;gap:10px;align-items:center}.canvas{overflow:auto}.canvas>svg{display:block;width:100%;min-width:1100px}.wire{cursor:pointer}.wire.selected polyline:last-child{stroke-width:10}#detail{min-height:2em}table{border-collapse:collapse;width:100%;font-size:16px}td,th{padding:8px;border-bottom:1px solid var(--border);text-align:right}td[dir=ltr]{text-align:left}tr.selected{background:#e5f0fc}@media print{nav{display:none}.canvas>svg{min-width:0}header,footer{font-size:12px}}'''
head='''<!doctype html><html lang="he" dir="rtl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>RooCore — כל החוטים יחד</title><link rel="icon" href="data:,"><style>'''+css+'''</style><header><h1>החוטים בין המודולים — מבט משולב</h1><p>זו צמת ההכנה בלבד. תנאי השלבים במדריך עדיין חלים; אין לחבר לסוללה. החוטים מסתיימים בפדים שזוהו, ובכבל הליידר המנותק לאחר מיפוי. תמונת XT30 היא לזיהוי המוצר; חיבורי LOAD דורשים אימות קוטביות.</p><nav><button id="zoom-in">הגדלה +</button><button id="zoom-out">הקטנה −</button><a href="RooCore-assembly.html">שלבי ההכנה והבדיקות</a><a href="ELECTRICAL_REVIEW.md">מפות הרגליים ותוצאות הביקורת</a><a href="electrical-schematic.svg">סכמה חשמלית</a><a href="combined-wiring.svg">תמונה וקטורית</a></nav><p id="detail" aria-live="polite">לחץ על חוט או על שורה בטבלה כדי להדגיש את המסלול שלו.</p></header><main class="canvas">'''
head=head.replace('<main class="canvas">','<p>'+html.escape(REVISION)+'</p><main class="canvas">')
body=head+svg+'</main><footer><table><thead><tr><th>חוט</th><th>מקור</th><th>יעד</th><th>תנאי</th></tr></thead><tbody>'
for i,r in enumerate(rows,1):body+=f'<tr data-wire="{i}"><td><button aria-label="הדגשת חוט {i}">{i}</button></td><td dir="ltr">{html.escape(r["first"])}</td><td dir="ltr">{html.escape(r["last"])}</td><td>{html.escape(r["condition"])}</td></tr>'
body+='</tbody></table><p>מקורות התמונות ומפות הפינים: <a href="MODULE_AUDIT.md">ביקורת המודולים</a>. בליידר נעשה שימוש בתצלום מעמוד השער של מסמך היצרן. הנורית בתמונת המפסק דולקת בתצלום המקור; המתג בהכנה שלנו צריך להיות OFF.</p></footer>'
body+='''<script>const svg=document.querySelector('svg'),detail=document.getElementById('detail');let zoom=100;document.getElementById('zoom-in').onclick=()=>{zoom=Math.min(220,zoom+20);svg.style.width=zoom+'%'};document.getElementById('zoom-out').onclick=()=>{zoom=Math.max(100,zoom-20);svg.style.width=zoom+'%'};function select(n){document.querySelectorAll('[data-wire]').forEach(e=>e.classList.toggle('selected',e.dataset.wire===n));detail.textContent=document.querySelector('.wire[data-wire="'+n+'"]').getAttribute('aria-label')}document.querySelectorAll('[data-wire]').forEach(e=>{e.onclick=()=>select(e.dataset.wire);if(e.classList.contains('wire'))e.onkeydown=k=>{if(k.key==='Enter'||k.key===' '){k.preventDefault();select(e.dataset.wire)}}});</script></html>'''
(O/'combined-wiring.html').write_text(body)
# Resolve palette in standalone SVG so it also opens in an ordinary image viewer.
svg=svg.replace('<svg xmlns=', '<svg xmlns=',1)
for k,v in {'background':'#fff','foreground':'#17314a','card':'#f2f6fa','border':'#bccbd7','red':'#c72d35','ground':'#27394c','blue':'#076db9','green':'#007b54','purple':'#8545a4'}.items():svg=svg.replace(f'var(--{k})',v)
(O/'combined-wiring.svg').write_text(svg)
print('Built combined photo diagram with all 13 ledger wires.')
