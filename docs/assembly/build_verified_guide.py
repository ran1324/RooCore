# coding: utf-8
"""Build all active assembly artifacts from the same module-specific wire list."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import base64, csv, hashlib, html, json, re, runpy, xml.etree.ElementTree as ET
from guide_content import REVISION, SOURCES, STEPS, PENDING, MODULES, ENDPOINTS, NC, UV_SECTION, SETUP_SECTION
from schematic import build_schematic
from build_uv_divider import build as build_uv_divider

OUT=Path(__file__).resolve().parent
build_uv_divider()
E=html.escape
COLORS={'red':'#c82e30','black':'#26384a','blue':'#006fc4','green':'#007b54','purple':'#8545a4'}
PHOTO_URLS={
 'assets/d36v28-pinout.jpg':'https://a.pololu-files.com/picture/0J10059.1200.jpg?7e3be4e50163b4241112416a22f4f3d9',
 'assets/switch-2810-original.jpg':'https://a.pololu-files.com/picture/0J6787.1200.jpg?d04456a91c8ade58351c82c74b33ce89',
 'assets/xiao-product.jpg':'https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/img/xiaoesp32s3.jpg',
 'assets/xiao-official-pinout.png':'https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/img/XIAO_ESP32-S3_front_pinout.png'}

class Drawing:
    def __init__(self,title,height=760):
        self.svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="{height}" viewBox="0 0 1200 {height}"><rect width="100%" height="100%" fill="#f8fafc"/>']
        self.model=ET.Element('mxGraphModel',adaptiveColors='auto',page='1',pageWidth='1200',pageHeight=str(height))
        self.root=ET.SubElement(self.model,'root')
        ET.SubElement(self.root,'mxCell',id='0');ET.SubElement(self.root,'mxCell',id='1',parent='0')
        self.box('title',20,15,1160,52,title,'#e7eef6',size=24)
        self.box('mode',20,78,1160,42,'הכנת צמה ללא מתח · אין חיבור לסוללה · תנאי כל שלב בחוברת','#fff0d6',size=18)
    def cell(self,id,x,y,w,h,value,style):
        c=ET.SubElement(self.root,'mxCell',id=id,value=value,style=style+';html=1;',vertex='1',parent='1')
        ET.SubElement(c,'mxGeometry',x=str(x),y=str(y),width=str(w),height=str(h),**{'as':'geometry'})
    def box(self,id,x,y,w,h,label,fill='#fff',stroke='#aebdcb',size=18):
        rtl=bool(re.search('[\u0590-\u05ff]',label))
        # Isolate Latin pin names so Hebrew prefixes cannot split 3V3 or reverse pad labels.
        shown=re.sub(r'[A-Za-z0-9]+(?:[A-Za-z0-9 /._+−→–-]*[A-Za-z0-9+−])?',lambda m:'\u2066'+m[0]+'\u2069',label) if rtl else label
        direction='rtl' if rtl else 'ltr'
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill}" stroke="{stroke}" stroke-width="2"/><text x="{x+w/2}" y="{y+h/2+6}" text-anchor="middle" direction="{direction}" font-family="Arial,sans-serif" font-size="{size}" fill="#17314a">{E(shown)}</text>')
        value=f'<div dir="{direction}">{E(shown)}</div>'
        self.cell(id,x,y,w,h,value,f'rounded=1;whiteSpace=wrap;fontSize={size};fillColor={fill};strokeColor={stroke};fontColor=#17314a')
    def photo(self,id,file,x,y,w,h):
        mime='image/png' if file.endswith('.png') else 'image/jpeg'
        data='data:'+mime+';base64,'+base64.b64encode((OUT/file).read_bytes()).decode()
        self.svg.append(f'<image href="{data}" x="{x}" y="{y}" width="{w}" height="{h}" preserveAspectRatio="none"/>')
        self.cell(id,x,y,w,h,'','shape=image;imageAspect=0;image='+PHOTO_URLS[file])
    def pad(self,id,x,y,color):
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="9" fill="white" stroke="{color}" stroke-width="4"/>')
        self.cell(id,x-9,y-9,18,18,'',f'ellipse;fillColor=#ffffff;strokeColor={color};strokeWidth=4')
    def line(self,id,a,b,x1,y1,x2,y2,color):
        self.svg.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="white" stroke-width="8"/><line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="4"/>')
        c=ET.SubElement(self.root,'mxCell',id=id,source=a,target=b,edge='1',parent='1',style=f'html=1;endArrow=none;startArrow=none;strokeColor={color};strokeWidth=4;')
        ET.SubElement(c,'mxGeometry',relative='1',**{'as':'geometry'})
    def save(self,name):
        svg=''.join(self.svg)+'</svg>'
        (OUT/(name+'.svg')).write_text(svg)
        ET.ElementTree(self.model).write(OUT/(name+'.drawio'),encoding='utf-8',xml_declaration=True)
        return svg

def row_diagram(s):
    d=Drawing(s['title'],190+96*len(s['rows']))
    for i,r in enumerate(s['rows']):
        y=145+i*96;c=COLORS[r['color']]
        d.box('a'+str(i),70,y,400,55,r['first']);d.box('b'+str(i),730,y,400,55,r['last'])
        d.line('wire'+str(i),'a'+str(i),'b'+str(i),470,y+27.5,730,y+27.5,c)
        d.box('n'+str(i),555,y+4,90,45,str(r['id']),stroke=c)
    return d,d.save('connect-'+s['id'])

def photo_diagram(s):
    if s['id'] not in {'power','switch','xiao','control','lidar','uart'}:return None
    d=Drawing(s['title']+' — החוטים על תמונת המודול')
    if s['id']=='power':
        d.photo('board','assets/d36v28-pinout.jpg',440,155,720,468.6)
        points=[(355,390),(355,500),(355,725),(355,610)]
        for i,(r,(px,py),by) in enumerate(zip(s['rows'],points,[260,355,555,455])):
            label=r['first'] if i<2 else r['last'];x,y=440+px*.6,155+py*.6;c=COLORS[r['color']]
            d.box('label'+str(i),25,by,340,52,f'{r["id"]} · {label}',stroke=c)
            d.pad('pad'+str(i),x,y,c);d.line('wire'+str(i),'label'+str(i),'pad'+str(i),365,by+26,x,y,c)
        footer='EN ו־PG נשארים פנויים · אין כאן חיבור לסוללה או ללוח הרומבה'
    elif s['id']=='switch':
        d.photo('board','assets/switch-2810-original.jpg',460,145,700,539.6)
        for i,(r,(px,py),by) in enumerate(zip(s['rows'],[(134,428),(318,567)],[315,460])):
            x,y=460+px*700/1200,145+py*700/1200;c=COLORS[r['color']]
            d.box('label'+str(i),25,by,330,52,f'{r["id"]} · {r["first"]} → {r["last"]}',stroke=c)
            d.pad('pad'+str(i),x,y,c);d.line('wire'+str(i),'label'+str(i),'pad'+str(i),355,by+26,x,y,c)
        footer='ON / VOUT / SW עדיין פנויים · המתג המכני OFF · אין נגד נוסף'
    elif s['id']=='xiao':
        # Original Seeed product photograph, 1400x1050. Pad identities cross-checked with full pinout.
        d.photo('board','assets/xiao-product.jpg',150,145,700,525)
        for i,(r,(px,py),by) in enumerate(zip(s['rows'],[(1006,562),(1068,515)],[510,345])):
            x,y=150+px*.5,145+py*.5;c=COLORS[r['color']]
            label='GND BUS → GND' if i==0 else '5V BUS → 5V / VBUS'
            d.box('label'+str(i),875,by,290,52,f'{r["id"]} · {label}',stroke=c)
            d.pad('pad'+str(i),x,y,c);d.line('wire'+str(i),'pad'+str(i),'label'+str(i),x,y,875,by+26,c)
        footer='USB מנותק · 3V3 ו־BAT פנויים · רק שני חוטי ההזנה מחוברים'
    elif s['id']=='control':
        d.photo('xiao','assets/xiao-product.jpg',20,155,550,412.5)
        d.photo('switch','assets/switch-2810-original.jpg',620,160,550,423.958)
        x1,y1=20+517*550/1400,155+381*550/1400
        x2,y2=620+508*550/1200,160+709*550/1200;c=COLORS['blue']
        d.box('wire-label',355,590,470,50,'9 · D3 / GPIO4  →  ON · חוט יחיד',stroke=c)
        d.pad('d3',x1,y1,c);d.pad('on',x2,y2,c)
        d.line('wire1','d3','wire-label',x1,y1,355,615,c)
        d.line('wire2','wire-label','on',825,615,x2,y2,c)
        footer='אותו חוט מתחיל ב־D3 ומסתיים ב־ON · אין נגד באמצע · לא SW'
    elif s['id']=='uart':
        d.photo('xiao','assets/xiao-product.jpg',20,155,700,525)
        for i,(r,px,py,by) in enumerate(zip(s['rows'],[452,388],[426,472],[340,490])):
            x,y=20+px*.5,155+py*.5;c=COLORS[r['color']]
            label='12 · D4 / GPIO5 → LiDAR RX' if i==0 else '13 · LiDAR TX → D5 / GPIO6'
            d.box('cable'+str(i),790,by,380,55,label,stroke=c,size=19)
            d.pad('pad'+str(i),x,y,c)
            d.line('wire'+str(i),'pad'+str(i),'cable'+str(i),x,y,790,by+27.5,c)
        footer='3.3V UART · בלי מתאם או נגד נוסף · כיבוי בתוכנה, החוטים נשארים מחוברים'
    else:
        d.photo('switch','assets/switch-2810-original.jpg',460,145,700,539.6)
        x,y=460+595*700/1200,145+100*700/1200;c=COLORS['red']
        d.box('vcc',25,170,330,52,'10 · LiDAR cable VCC',stroke=c)
        d.pad('vout',x,y,c);d.line('wire1','vout','vcc',x,y,355,196,c)
        d.box('vout-label',570,130,220,40,'S1 VOUT',size=17)
        c=COLORS['black']
        d.box('gnd-bus',25,370,330,52,'11 · GND BUS',stroke=c)
        d.box('gnd-cable',25,550,330,52,'LiDAR cable GND',stroke=c)
        d.line('wire2','gnd-bus','gnd-cable',190,422,190,550,c)
        footer='קצות VCC / GND מזוהים במיפוי המחבר · התקע מנותק מהליידר · TX / RX מבודדים'
    d.box('footer',35,690,1130,42,footer,'#fff0d6',size=17)
    return d,d.save('photo-'+s['id'])

def links(keys):return ' · '.join(f'<a href="{E(SOURCES[k][1])}">{E(SOURCES[k][0])}</a>' for k in keys)
CSS='''.setup{overflow-wrap:anywhere}.setup-step{padding:18px 0;border-top:1px solid #d9e2ea}.setup pre{max-width:100%;overflow:auto;white-space:pre;overflow-wrap:normal;background:#edf2f6;padding:14px;border-radius:8px;font:14px/1.6 monospace}.setup-table table{min-width:650px}.green{color:#007b54}.purple{color:#8545a4}*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#edf2f6;color:#182f45;font:18px/1.75 Arial,sans-serif}main{max-width:1210px;margin:auto;padding:26px}h1{font-size:42px;line-height:1.2}h2{font-size:27px;line-height:1.4}section{background:white;border:1px solid #ccd9e5;border-radius:14px;margin:22px 0;padding:28px;scroll-margin-top:85px}a{color:#145b92}nav{position:sticky;top:0;background:#edf2f6f5;z-index:5;padding:10px 0;display:flex;flex-wrap:wrap;gap:8px}nav a,button{background:white;border:1px solid #a8bbcc;border-radius:7px;padding:6px 12px;font:inherit;text-decoration:none;color:#193147}button{cursor:pointer}.stop{background:#fff0ee;border:2px solid #b53b32;border-radius:10px;padding:18px}.gate{background:#fff4df;border-right:5px solid #b98320;padding:15px;margin:18px 0}.tag{background:#e5edf5;border-radius:5px;padding:4px 10px;font-size:14px;font-weight:bold}.small,.sources{font-size:14px;color:#50667b}.drawing{direction:ltr;border:1px solid #c7d4df;border-radius:9px;overflow:auto;margin:20px 0}.drawing svg{width:100%;height:auto;display:block;min-width:650px}.ref{max-width:100%;height:auto;max-height:580px;object-fit:contain;display:block;margin:15px auto}.tablewrap{overflow:auto}.tablewrap table{min-width:900px}table{width:100%;border-collapse:collapse;font-size:15px}td,th{padding:10px;border-bottom:1px solid #d9e2ea;text-align:right;vertical-align:top}th{background:#eaf0f6}li{margin:10px 0}input{font:inherit;padding:8px;border:1px solid #adbfce;border-radius:5px;width:100%;min-width:130px}.hidden{display:none}.links,.legend{display:flex;gap:18px;flex-wrap:wrap}.red{color:#c82e30}.blue{color:#006fc4}.black{color:#26384a}details{border:1px solid #cbd6e0;border-radius:8px;padding:12px;margin-top:16px}summary{cursor:pointer;font-weight:bold}.pending{border-right:5px solid #b53b32;padding:8px 18px;margin:18px 0}@media(max-width:700px){main{padding:12px}section{padding:18px 12px}h1{font-size:32px}h2{font-size:24px}nav{position:static}table{font-size:13px}}@media print{nav,button,#search,.links{display:none}body{background:white;font-size:13px}main{padding:0}section{break-before:page;padding:12px;border:0}.drawing svg{min-width:0}details{display:none}h1{font-size:30px}}'''

intro=f'''<header><span class="tag">{E(REVISION)}</span><h1>הכנת החיבורים לפי המודולים שקנית</h1><p>תמונות היצרן, חוטים מסומנים ושלב אחד בכל פעם.</p><div class="stop"><strong>אין עדיין אישור לחבר את ההרכבה לסוללת הרומבה.</strong><br>אפשר להכין את החלקים המפורטים כאן ללא מתח. מיפוי NOYITO של הרומבה, מעגלי הטרנזיסטורים, בחירת ערך הנתיך שכבר קיים ונקודות ההספק ברובוט עדיין דורשים השלמה. אין לדלג על התנאי שבתחילת כל שלב.</div><p>הגרסה הזאת נבדקה עם electronics ו־esp32-development. מפות הממיר, המפסק, הבקר ושקע הליידר מאומתות במסמכים; פירוט מה שנותר והבדיקות שמכריעות אותו נמצא בביקורת החשמלית. מספרי החוטים 1–13 אחידים בכל התוצרים. הגרסה הזאת מחליפה את המדריך הקודם. נוספו שני חוטי UART קבועים בשלב 7, עם רצף תוכנה מחייב ותנאי בדיקה לפני הפעלה; חיבורים שמפתם לא זוהתה נשארו מחוץ לצמה.</p><div class="links"><a href="combined-wiring.html">כל התמונות והחוטים יחד</a><a href="MODULE_AUDIT.md">ביקורת כל המודולים</a><a href="LIDAR_POWER_UART.md">כיבוי הליידר בלי לנתק חוטים</a><a href="FIRMWARE_INTEGRATION.md">השילוב בקושחה המלאה</a><a href="ELECTRICAL_REVIEW.md">מפות רגליים וחישובים</a><a href="electrical-schematic.svg">סכמה חשמלית</a><a href="../../HARDWARE_INVENTORY.md">קובץ הפריטים שלנו</a><a href="RooCore-assembly.drawio">תרשימים לעריכה</a><a href="connections.csv">טבלת החוטים</a><a href="RooCore-assembly-kit.zip">כל הערכה</a></div></header>'''
intro=intro.replace('<div class="links">','<div class="links"><a href="en/RooCore-assembly.html">English</a>')
sections=[];ledger=[];mxfile=ET.Element('mxfile',host='app.diagrams.net')
for s in STEPS:
    actions='<ol>'+''.join('<li>'+E(a)+'</li>' for a in s['actions'])+'</ol>';visual='';table=''
    if s['rows']:
        drawing,svg=row_diagram(s)
        ET.SubElement(mxfile,'diagram',id='connect-'+s['id'],name=s['title']).append(drawing.model)
        photo=photo_diagram(s)
        if photo:
            d,pic=photo;ET.SubElement(mxfile,'diagram',id='photo-'+s['id'],name=s['title']+' — תמונה').append(d.model)
            visual='<h3>החוטים על תמונת המודול</h3><div class="drawing">'+pic+'</div>'
        visual+='<details><summary>אותם חיבורים בשורות פשוטות</summary><div class="drawing">'+svg+'</div></details>'
        table='<div class="tablewrap"><table><tr><th>חוט</th><th>קצה ראשון</th><th>קצה שני</th><th>רשת</th><th>חוט ובדיקה</th></tr>'
        for i,r in enumerate(s['rows']):
            table+=f'<tr><td class="{r["color"]}">{r["id"]}</td><td><bdi dir="ltr">{E(r["first"])}</bdi></td><td><bdi dir="ltr">{E(r["last"])}</bdi></td><td><bdi dir="ltr">{E(r["net"])}</bdi></td><td>{E(r["gauge"])} — {E(r["check"])}</td></tr>'
            ledger.append(dict(step=s['id'],connection=r['id'],**r,condition=s['gate'],status='unpowered_preparation_only',source='; '.join(SOURCES[k][1] for k in s['sources'])))
        table+='</table></div>'
    ref=''
    if s.get('image'):
        opened=' open' if s['id']=='lidar' else ''
        ref=f'<details{opened}><summary>תמונת היצרן וכיוון המבט</summary><p>{E(s["image_note"])}</p><a href="{E(s["image"])}"><img class="ref" src="{E(s["image"])}" alt="{E(s["image_note"])}"/></a></details>'
    sections.append(f'<section id="s-{s["id"]}" class="assembly-step"><span class="tag">{E(s["kind"])}</span><h2>{E(s["title"])}</h2><p><strong>קח:</strong> {E(s["take"])}</p><div class="gate"><strong>לפני שמתחילים:</strong> {E(s["gate"])}</div>{actions}{visual}{table}{ref}<p><strong>בסוף השלב:</strong> {E(s["after"])}</p><p class="sources">{links(s["sources"])}</p></section>')

schematic_svg,schematic_model=build_schematic(ledger,OUT)
ET.SubElement(mxfile,'diagram',id='electrical-schematic',name='סכמת חיבורי המודולים').append(schematic_model)
pins='<section id="pins"><h2>מיקום הפדים והרשתות</h2><p>שמות הפדים הם לפי המודול. מספרי מיקום מקומיים אינם מספור של רגלי שבב. J1 הוא חצי XT30; J2 הוא כבל הליידר; U4 הוא XIAO.</p><details><summary>סכמה חשמלית של 13 החוטים</summary><div class="drawing">'+schematic_svg+'</div></details><div class="tablewrap"><table><tr><th>קצה חוט</th><th>רכיב / פד</th><th>מיקום</th><th>רשת</th></tr>'+''.join(f'<tr><td><bdi dir="ltr">{E(name)}</bdi></td><td><bdi dir="ltr">{E(a)} / {E(b)}</bdi></td><td>{E(c)}</td><td><bdi dir="ltr">{E(net)}</bdi></td></tr>' for name,(a,b,c,net) in ENDPOINTS.items())+'</table></div><h3>נשארים בלי חוט — NC</h3><ul>'+''.join(f'<li><bdi dir="ltr">{E(a)}: {E(b)}</bdi> — {E(c)}</li>' for a,b,c in NC)+'</ul></section>'
u = UV_SECTION
uv_section = '<section id="hardware-cutoff" class="assembly-step"><h2>'+E(u['title'])+'</h2><div class="gate">'+E(u['gate'])+'</div><p>'+E(u['control'])+'</p><p>'+E(u['take'])+'</p><p><bdi dir="ltr">'+E(u['route'])+'</bdi></p><img class="ref" src="uv-divider.svg" alt="'+E(u['title'])+'"/><p>'+E(u['orientation'])+'</p><img class="ref" src="assets/d36v28-pinout.jpg" alt="'+E(u['orientation'])+'"/><div class="tablewrap"><table><tr>'+''.join('<th>'+E(h)+'</th>' for h in u['table_head'])+'</tr>'+''.join('<tr>'+''.join('<td><bdi dir="ltr">'+E(v)+'</bdi></td>' for v in row)+'</tr>' for row in u['rows'])+'</table></div><ol>'+''.join('<li>'+E(a)+'</li>' for a in u['actions'])+'</ol><p>'+E(u['after'])+'</p><a href="POWER_DOCKING_PLAN.md">'+E(u['detail'])+'</a></section>'
def setup_table(head, rows):
    return '<div class="tablewrap setup-table"><table><thead><tr>'+''.join('<th scope="col">'+E(v)+'</th>' for v in head)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+E(v)+'</td>' for v in row)+'</tr>' for row in rows)+'</tbody></table></div>'

setup = SETUP_SECTION
setup_section = '<section id="initial-setup" class="assembly-step setup"><h2>'+E(setup['title'])+'</h2><p class="small">'+E(setup['status'])+'</p><div class="gate">'+E(setup['gate'])+'</div><h3>'+E(setup['config_title'])+'</h3>'+setup_table(setup['config_head'],setup['config_rows'])
for step in setup['steps']:
    setup_section += '<article id="setup-'+E(step['id'])+'" class="setup-step"><h3>'+E(step['title'])+'</h3><ol>'+''.join('<li>'+E(a)+'</li>' for a in step['actions'])+'</ol>'
    if step['code']:
        setup_section += '<pre dir="ltr"><code>'+E(step['code'])+'</code></pre>'
    setup_section += '<p><strong>'+E(setup['result_label'])+':</strong> '+E(step['result'])+'</p></article>'
setup_section += '<h3>'+E(setup['trouble_title'])+'</h3>'+setup_table(setup['trouble_head'],setup['trouble_rows'])+'<p class="gate">'+E(setup['closing'])+'</p></section>'

pending='<section id="pending"><h2>8 · מה נסגר ומה נשאר להשלמה</h2><p>מפות U3/S1/XIAO והליידר אומתו. הבדיקות שנותרו מפורטות ב־<a href="ELECTRICAL_REVIEW.md">ביקורת החשמלית</a>, במקום הסימון הכללי “לא מאומת”. אין כאן שורות חיבור לביצוע עבור הרכיבים הבאים.</p><details><summary>NOYITO: אחד לרומבה לאחר מיפוי; השני ספייר ואינו נדרש לליידר</summary><img class="ref" src="assets/noyito-product-photo.jpg" alt="צילום מוצר NOYITO עם שבב 12 מגעים ורכיבים מולחמים"/><p>מודול אחד מיועד להתאמת הרמות של הרומבה; מיפוי ההזנה שלו עדיין נדרש. הליידר כבר 3.3V, ולכן אינו משתמש במודול השני. VCC ו־3V3 אינם GND; אין לחבר אותם לאדמה ואין להוסיף C1–C4. בתקריב שבב 12 מגעים; מפת RUT ובדיקות הניתוב מופיעות בביקורת.</p></details>'+''.join(f'<div class="pending"><strong>{E(a)}</strong><p>{E(b)}</p></div>' for a,b in PENDING)+'<p><strong>המידע המדויק להשלמה:</strong> מיפוי המסילות ו־OE של NOYITO, התאמת סימון 2N3904 למפרט, סדרת הנתיך ומפרט DC שלה, וגרסת לוח הרומבה עם נקודות ההספק. תמונות הדגמים כבר נבדקו; אין צורך לשלוח שוב את אותם קישורי המוצרים. בדיקות רציפות ומתח עדיין מבוצעות על החלק שמרכיבים בפועל.</p></section>'
tests='''<section id="tests"><h2>בדיקות — מתחילים בממיר בלבד</h2><p>ספק מעבדה מוגבל זרם לא נמצא ברכש שנבדק. אם אין ספק כזה זמין, נשארים בבדיקות ללא מתח; הסוללה אינה תחליף לבדיקה הראשונה. נקודות המדידה TP1–TP4 והגבולות מפורטים בביקורת החשמלית.</p><ol><li><strong>ללא מתח:</strong> בדוק כל חוט מול הטבלה, משיכה עדינה, בידוד ורסיסי בדיל. מדוד רציפות של חוטים מנותקים והשווה להתנגדות הגששים. צפצוף אינו אישור כולל להיעדר קצר; קבלים עשויים לגרום לקריאה משתנה.</li>
<li><strong>U3 לבדו:</strong> נתק את כל העומסים. EN ו־PG פנויים. כוון ספק כבוי ל־12V ומגבלת זרם 0.1A, חבר פלוס ל־VIN ומינוס ל־GND. אלה תנאי בדיקה זמניים, לא ספי הסוללה. אין רובוט מחובר.</li>
<li><strong>TP1 — יציאת הממיר:</strong> שחור ב־COM ואדום ב־V/Ω, מצב מתח DC. חבר גששים מבודדים בין VOUT ל־GND כשהספק כבוי, ורק אז הפעל. הצפי סביב 5V בתחום 4.8–5.2V, בהתחשב בדיוק המכשיר. אם הספק נשאר בהגבלת זרם, המתח שגוי או יש חימום חריג — כבה ובדוק. הגבלה רגעית בעת טעינת קבלים אינה לבדה קצר; אין להעלות זרם כדי לעקוף תקלה לא מוסברת.</li>
<li>כבה ונתק את הספק לפני שינוי חוטים. בדיקה ללא עומס אינה מאשרת מתח תקין בהתנעת הליידר. טווח 4% של הממיר משאיר אפס מרווח לנפילת מתח כאשר יציאתו 4.8V.</li>
<li><strong>TP4 — בהמשך, בקצה כבל הליידר:</strong> נדרשים 4.8–5.2V במגעי VCC/GND תחת עומס, כולל התנעה ופעילות הבקר. מולטימטר בודק DC; כדי לאמת שקיעות ונחשולים קצרים צריך מדידת צורת גל. תוצאה שחוסר הוודאות שלה חוצה את הגבול אינה מעבר. זו דרישת שחרור, לא הוראה לחבר את הליידר כעת.</li>
<li><strong>TP5 — UART בכיבוי:</strong> בצע את בדיקות D4/D5, סדר כיבוי S1 וזמן עליית ההספק המתוארות ב־LIDAR_POWER_UART.md לפני שימוש. בכיבוי רגיל החוטים נשארים מחוברים. מדידות רצף וצורת גל דורשות אוסצילוסקופ; הן לא בוצעו.</li>
<li>בדיקת USB או צריבת XIAO נעשות כשהוא מנותק מכל הצמה, גם מהאותות. לפני בקרת S1 דרושה קושחה שנבדקה עם D3/GPIO4 נמוך באתחול וב־reset. הרצף משולב כעת בקושחת RooCoreXiao המלאה, כולל מסלול כיבוי הסוללה. היא נבנתה בשתי תצורות; פרטי הקוד ב־FIRMWARE_INTEGRATION.md והתוצאות ב־VALIDATION.md. הגדרות ההפעלה נשארות false/0 עד בדיקת ההרכבה. לא בוצעה צריבה או בדיקת חומרה.</li></ol><div class="gate">השלמת מיפוי NOYITO, ההגנות ונקודות הרובוט עדיין נדרשת למערכת מלאה. מדידות עומס, ספי EN והתאוששות סוללה לא בוצעו. אין בחוברת הוראה לחבר את ההרכבה לסוללה.</div><p>אין לשים מולטימטר במצב אמפר במקביל לסוללה. אין לבצע בדיקת התנגדות או רציפות על מעגל מוזן. <a href="ELECTRICAL_REVIEW.md">הפירוט והבדיקות המכריעות</a>.</p></section>'''
bom='<section id="bom"><h2>הפריטים שלנו ומצבם</h2><p>רכישה אינה קבלה או בדיקת חומרה. אין כאן הוראת קנייה חדשה.</p><div class="tablewrap"><table><tr><th>פריט</th><th>דגם שנרכש</th><th>מצב החיבור</th></tr>'+''.join(f'<tr><td>{E(a)}</td><td>{E(b)}</td><td>{E(c)}</td></tr>' for a,b,c in MODULES)+'</table></div><p><strong>אין קבלים חיצוניים ואין R8/R9/R10/R11 בשלבי ההכנה.</strong> זוג נגדי EN מופיע בסעיף הנפרד. רשימת R1–R7 הישנה ו־Q1 אינם הוראת הרכבה; Q2/BRC עדיין דורש אימות.</p></section>'
names=['דגם ותווית הסוללה','U3 ללא עומס — VIN / VOUT / זרם','XIAO — 3V3, בבדיקה מבודדת','מיפוי NOYITO — עדיין פתוח','Q1/Q2 — התאמת הסימון למפת E/B/C','דגם ודירוג נתיך — עדיין פתוח','ליידר — מתח בהתנעה ובעבודה, לבדיקה בהמשך','ליידר — D4/D5 ו־VCC בכיבוי, זמן עליית ההספק ו־reset','מתח סוללה והתאוששות וספי EN — לבדיקה בהמשך']
measure='<section id="measure"><h2>רישום תוצאות אמיתיות</h2><p>לא הוזנו תוצאות בדיקה. השדות אינם משחררים חיבור או הפעלה.</p><table>'+''.join(f'<tr><th>{E(n)}</th><td><input aria-label="{E(n)}" placeholder="טרם נבדק"/></td></tr>' for n in names)+'</table><button id="save-measurements">שמירת התוצאות כ־JSON</button></section>'
src='<section id="sources"><h2>מקורות הבדיקה</h2><ul>'+''.join(f'<li><a href="{E(u)}">{E(t)}</a></li>' for t,u in SOURCES.values())+'</ul><p>התמונות הן תמונות הדגמים במקורות. הקווים הצבעוניים הם תרשים, ולא צילום של הרכבה שנבדקה. <a href="MODULE_AUDIT.md">הביקורת המלאה</a>.</p></section>'
nav='<nav><a href="#initial-setup">'+E(setup['nav'])+'</a><a href="#bom">פריטים</a>'+''.join(f'<a href="#s-{s["id"]}">{i+1}</a>' for i,s in enumerate(STEPS))+f'<a href="#hardware-cutoff">{E(UV_SECTION["title"])}</a><a href="#pins">פדים וסכמה</a><a href="#pending">מה חסר</a><a href="#tests">בדיקות</a><a href="#measure">תוצאות</a></nav>'
script='''document.querySelector('#search').addEventListener('input',e=>{const q=e.target.value.toLowerCase().trim();document.querySelectorAll('.assembly-step').forEach(s=>s.classList.toggle('hidden',q&&!s.textContent.toLowerCase().includes(q)))});document.querySelector('#save-measurements').onclick=()=>{const data={date:new Date().toISOString(),status:'user-entered, not verified',measurements:Object.fromEntries([...document.querySelectorAll('#measure input')].map(x=>[x.getAttribute('aria-label'),x.value]))};const a=document.createElement('a');const u=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));a.href=u;a.download='RooCore-measurements.json';a.click();setTimeout(()=>URL.revokeObjectURL(u),1000)};'''
page=f'<!doctype html><html lang="he" dir="rtl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="data:,"><title>RooCore — מדריך לאחר ביקורת מודולים</title><style>{CSS}</style><main>{intro}{nav}<p class="legend"><span class="red">אדום: הזנה חיובית</span><span class="black">שחור: GND</span><span class="blue">כחול: אות בקרה</span><span class="green">ירוק: D4 אל RX</span><span class="purple">סגול: TX אל D5</span></p><p class="small">הצבעים הם סימון מוצע; מאמתים תמיד שמות פדים. לא מחליפים רכיב שבוטל בחוט.</p><input id="search" aria-label="חיפוש בשלבים" placeholder="חיפוש בשלבי ההכנה"/>{setup_section}{"".join(sections)}{pins}{uv_section}{pending}{tests}{bom}{measure}{src}</main><script>{script}</script></html>'
(OUT/'RooCore-assembly.html').write_text(page)
ET.ElementTree(mxfile).write(OUT/'RooCore-assembly.drawio',encoding='utf-8',xml_declaration=True)
(OUT/'connections.json').write_text(json.dumps(ledger,ensure_ascii=False,indent=2))
with (OUT/'connections.csv').open('w',encoding='utf-8-sig',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(ledger[0]));w.writeheader();w.writerows(ledger)
md=['# RooCore — הוראות לאחר ביקורת מודולים',REVISION,'','[החוברת עם תמונות וחוטים](RooCore-assembly.html) · [כל החוטים בתמונה משולבת](combined-wiring.html) · [ביקורת המודולים](MODULE_AUDIT.md) · [הביקורת החשמלית ומפות הרגליים](ELECTRICAL_REVIEW.md) · [סכמת המודולים](electrical-schematic.svg)','','**אין עדיין אישור לחיבור לסוללת הרומבה.** תנאי כל שלב מחייבים.','']
md+=['## '+setup['title'],'',setup['status'],'','**'+setup['gate']+'**','','### '+setup['config_title'],'','| '+' | '.join(setup['config_head'])+' |','|---|---|---|']+['| '+' | '.join(row)+' |' for row in setup['config_rows']]+['']
for step in setup['steps']:
    md+=['### '+step['title'],'']+[f'{i+1}. {a}' for i,a in enumerate(step['actions'])]+['']
    if step['code']:
        md+=['```sh',step['code'],'```','']
    md+=['**'+setup['result_label']+':** '+step['result'],'']
md+=['### '+setup['trouble_title'],'','| '+' | '.join(setup['trouble_head'])+' |','|---|---|---|']+['| '+' | '.join(row)+' |' for row in setup['trouble_rows']]+['',setup['closing'],'']
for s in STEPS:
    md+=['## '+s['title'],'','**קח:** '+s['take'],'','**תנאי:** '+s['gate'],'']+[f'{i+1}. {a}' for i,a in enumerate(s['actions'])]
    if s['rows']:
        md+=['','| חוט | קצה ראשון | קצה שני | רשת | חוט ובדיקה |','|---|---|---|---|---|']+[f'| {r["id"]} | {r["first"]} | {r["last"]} | {r["net"]} | {r["gauge"]}; {r["check"]} |' for i,r in enumerate(s['rows'])]
        md+=['',f'[תמונת החיווט לשלב זה](photo-{s["id"]}.svg)']
    md+=['','**בסיום:** '+s['after'],'']
md+=['## '+u['title'],'',u['gate'],'',u['control'],'',u['take'],'',u['route'],'','![EN](uv-divider.svg)','','![Pololu](assets/d36v28-pinout.jpg)','',u['orientation'],'','| '+' | '.join(u['table_head'])+' |','|---|---|---|']+['| '+' | '.join(row)+' |' for row in u['rows']]+['']+[f'{i+1}. {a}' for i,a in enumerate(u['actions'])]+['',u['after'],'','['+u['detail']+'](POWER_DOCKING_PLAN.md)','']
md+=['## מפת קצות החוטים','', 'J1 הוא חצי XT30, ‏J2 הוא כבל הליידר ו־U4 הוא XIAO. שמות הפדים במודולים אינם מספרי רגליים בשבב. מספרי החוטים 1–13 אחידים בכל התרשימים.', '', '| קצה חוט | רכיב / פד | מיקום | רשת |','|---|---|---|---|']+[f'| {name} | {a} / {b} | {c} | {net} |' for name,(a,b,c,net) in ENDPOINTS.items()]+['','## פינים שנשארים ללא חוט — NC','']+[f'- **{a}:** {b} — {c}' for a,b,c in NC]+['']
plain=lambda value:html.unescape(re.sub('<[^>]+>','',value))
test_paragraphs=re.findall(r'<p>(.*?)</p>',tests)
md+=['## בדיקות — מתחילים בממיר בלבד','',plain(test_paragraphs[0]),'']+[f'{i+1}. {plain(item)}' for i,item in enumerate(re.findall(r'<li>(.*?)</li>',tests))]+['','**כאן עוצרים:** '+plain(re.search(r'<div class="gate">(.*?)</div>',tests)[1]),'',plain(test_paragraphs[-1]),'']
md+=['## מה עדיין אינו מאושר','']+[f'- **{a}:** {b}' for a,b in PENDING]+['','## מקורות','']+[f'- [{a}]({u})' for a,u in SOURCES.values()]
md+=['','בדיקות ונקודות מדידה מפורטות: [ELECTRICAL_REVIEW.md](ELECTRICAL_REVIEW.md). תוצאות בדיקת הקבצים: [VALIDATION.md](VALIDATION.md).','']
md[2:2]=['[English version](en/README.md)', '']
(OUT/'README.md').write_text('\n'.join(md)+'\n')
withdraw=Drawing('התרשים הקודם בוטל — אין להרכיב לפיו',310)
withdraw.box('replacement',35,150,1130,50,'פתח את RooCore-assembly.html לגרסה לאחר ביקורת המודולים','#fff0ee',size=22)
withdraw.box('reason',35,220,1130,50,'הוסרו פינים ורכיבים שלא אומתו למודולים שנרכשו','#fff0ee',size=21)
for name in ['step-01','step-02','step-03','step-04','step-05','step-06','step-07a','step-07b','step-08','step-09','XIAO-pinmap']:withdraw.save(name)
for p in list(OUT.glob('*.drawio'))+list(OUT.glob('*.svg')):
    if not p.name.startswith('._'):ET.parse(p)
assert len(ledger)==13 and all(r['component']=='WIRE' for r in ledger)
assert not any(any(x in r['first']+' '+r['last'] for x in ['U1','U2','VCCA','VCCB','OE','J14','BAT','OI','EN','PG']) for r in ledger)
# Refresh the combined view before packaging, so no manual second build can be forgotten.
runpy.run_path(str(OUT/'build_overview.py'))
# Source snapshots for offline review, not a standalone flashable kit.
(OUT/'firmware-source').mkdir(exist_ok=True)
for name in ['RooCoreXiao.ino', 'config.example.h']:
    (OUT/'firmware-source'/name).write_bytes((OUT.parents[1]/'firmware/xiao/RooCoreXiao'/name).read_bytes())
for name in ['PowerManagement.h', 'PowerShutdownOI.h', 'NativeCapture.h', 'BatteryLearning.h', 'BatteryLearningStore.h', 'CloudPolicy.h', 'CloudTrust.h', 'WifiSetup.h', 'WifiSetupPage.h', 'WifiProvisioning.h']:
    (OUT/'firmware-source'/name).write_bytes((OUT.parents[1]/'lib/RooNavigation/src'/name).read_bytes())

from build_english import build as build_english
build_english()
guide_inputs = ['HARDWARE_INVENTORY.md','AMAZON_ORDER.md',
    'docs/assembly/guide_content.py','docs/assembly/guide_english.py','docs/assembly/ui.en.json',
    'docs/assembly/build_verified_guide.py','docs/assembly/build_english.py','docs/assembly/build_uv_divider.py',
    'docs/assembly/POWER_DOCKING_PLAN.md','docs/assembly/en/POWER_DOCKING_PLAN.md',
    'docs/assembly/LIDAR_POWER_UART.md','docs/assembly/en/LIDAR_POWER_UART.md',
    'docs/assembly/firmware-integration.json','docs/assembly/en/HARDWARE_INVENTORY.md',
    'docs/APPLE_HOME-he.md','docs/APPLE_HOME.md','server/control/README-he.md','server/control/README.md']
guide_outputs = ['RooCore-assembly.html','en/RooCore-assembly.html','README.md','en/README.md',
    'uv-divider.svg','uv-divider.drawio','uv-divider.csv','uv-divider.json']
(OUT/'guide-build.json').write_text(json.dumps(dict(
    sources={name:hashlib.sha256((OUT.parents[1]/name).read_bytes()).hexdigest() for name in guide_inputs},
    outputs={name:hashlib.sha256((OUT/name).read_bytes()).hexdigest() for name in guide_outputs},
    physical_validation=False),indent=2)+'\n')
with ZipFile(OUT/'RooCore-assembly-kit.zip','w',ZIP_DEFLATED) as z:
    for p in sorted(OUT.rglob('*')):
        if p.is_file() and p.suffix in {'.html','.md','.csv','.json','.svg','.drawio','.pdf','.jpg','.png','.h','.ino','.cpp','.properties','.txt'} and not any(n.startswith('.') for n in p.relative_to(OUT).parts):
            if p.suffix=='.html':
                z.writestr(p.relative_to(OUT.parents[1]).as_posix(),p.read_text().replace('<a href="RooCore-assembly-kit.zip">כל הערכה</a>','').replace('<a href="../RooCore-assembly-kit.zip">Complete kit</a>',''))
            else:z.write(p,p.relative_to(OUT.parents[1]))
    for name in ['HARDWARE_INVENTORY.md','AMAZON_ORDER.md','WIRING_GUIDE.md','POWER_DESIGN.md','POWER_SHUTDOWN.md',
                 'docs/POWER_SHUTDOWN.md','docs/POWER_SHUTDOWN-he.md',
                 'docs/POWER_MANAGEMENT.md','docs/POWER_MANAGEMENT-he.md',
                 'docs/BATTERY_LEARNING.md','docs/BATTERY_LEARNING-he.md',
                 'docs/APPLE_HOME.md','docs/APPLE_HOME-he.md','docs/APPLE_HOME_VALIDATION.md',
                 'tests/test_matter_sim.sh','tests/build_matter_sim.sh','tests/matter_sim_test.cpp',
                 'lib/RooNavigation/src/BatteryLearning.h','lib/RooNavigation/src/BatteryLearningStore.h',
                 'lib/RooNavigation/src/PowerManagement.h','lib/RooNavigation/src/PowerShutdownOI.h',
                 'tools/xiao_capture.py','tools/build_xiao_ota.sh','docs/oi-map.json',
                 'lib/RooNavigation/src/NativeCapture.h',
                 'lib/RooNavigation/src/CloudPolicy.h','lib/RooNavigation/src/CloudTrust.h',
                 'docs/CLOUD_STREAM.md','docs/CLOUD_STREAM-he.md',
                 'docs/WIFI_SETUP.md','docs/WIFI_SETUP-he.md',
                 'lib/RooNavigation/src/WifiSetup.h','lib/RooNavigation/src/WifiSetupPage.h','lib/RooNavigation/src/WifiProvisioning.h',
                 'firmware/xiao/RooCoreXiao/RooCoreXiao.ino','firmware/xiao/RooCoreXiao/config.example.h',
                 'docs/diagrams/wiring-xiao.svg',
                 'hardware/README.md','hardware/check_shutdown.py']:
        content=(OUT.parents[1]/name).read_text()
        content=re.sub(r'\[([^\]]+)\]\([^)]*2026-11-09-withdrawn-before-module-audit\.zip\)',r'\1 (ארכיון נפרד בסביבת העבודה, אינו בערכה הזאת)',content)
        z.writestr(name,content)
    for folder in ['server/control','firmware/xiao/RooCoreMatterSim']:
        for p in sorted((OUT.parents[1]/folder).rglob('*')):
            if p.is_file() and p.suffix in {'.md','.ino','.h'} and not any(n.startswith('.') for n in p.relative_to(OUT.parents[1]/folder).parts):
                z.write(p,p.relative_to(OUT.parents[1]))
    for p in sorted((OUT.parents[1]/'lib/RooCoreLidarPower').rglob('*')):
        if p.is_file() and not p.name.startswith('.'):
            z.write(p,p.relative_to(OUT.parents[1]))
    z.writestr('OPEN-FIRST.html','<!doctype html><html lang="he" dir="rtl"><meta charset="utf-8"><title>RooCore</title><h1>RooCore assembly / מדריך ההכנה</h1><p><a href="docs/assembly/en/RooCore-assembly.html">English illustrated guide</a></p><p><a href="docs/assembly/RooCore-assembly.html">פתיחת המדריך עם תמונות וחוטים</a></p><p>יש לחלץ את כל התיקייה לפני פתיחה. אין עדיין הוראות לחיבור לסוללה. הארכיון שבוטל אינו חלק מהערכה.</p></html>')
print(f'Built {len(STEPS)} preparation stages, {len(ledger)} wire records and 6 photo diagrams; robot/battery wiring withheld; UART requires ordered software shutdown.')
