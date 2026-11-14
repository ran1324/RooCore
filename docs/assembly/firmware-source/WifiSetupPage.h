#pragma once
// Served only inside the private, temporary setup AP. No remote assets.
static const char RooWifiSetupPage[] PROGMEM = R"ROOHTML(<!doctype html>
<html lang="he" dir="rtl"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>חיבור RooCore לרשת</title><style>
*{box-sizing:border-box}body{margin:0;background:#f3f5f6;color:#192b35;font:18px system-ui,sans-serif}main{max-width:520px;margin:7vh auto;padding:28px;background:white;border-radius:18px}h1{font-size:28px;margin-top:0}p{line-height:1.6}label{display:block;margin:22px 0 8px}input,select,button{width:100%;padding:13px;font:inherit;border:1px solid #b6c1c6;border-radius:8px}button{background:#174a59;color:white;cursor:pointer;margin-top:16px}button:disabled{opacity:.5}#status{min-height:3em}small{display:block;line-height:1.6;color:#445b66}input{direction:ltr;text-align:left}@media(max-width:560px){main{margin:16px;padding:22px}}
</style><main><h1>חיבור RooCore לרשת</h1><p>בחרו את רשת הבית בתדר 2.4GHz. פרטי הרשת נשמרים בבקר אחרי חיבור מוצלח.</p>
<form id="form"><label for="networks">רשתות בסביבה</label><select id="networks"><option value="">הזנת שם רשת ידנית</option></select><button type="button" id="scan">סריקת רשתות</button>
<label for="ssid">שם הרשת</label><input id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
<label for="password">סיסמת הרשת</label><input id="password" name="password" type="password" maxlength="63" autocomplete="new-password"><small>ברשת פתוחה אפשר להשאיר ריק.</small>
<button id="connect">חיבור ושמירה</button></form><p id="status" role="status" aria-live="polite"></p><small>הרובוט מושהה במהלך ההגדרה. אחרי החיבור חזרו לרשת הבית ופתחו את roocore.saymour.co.il. החלון נסגר אוטומטית אחרי חמש דקות.</small></main>
<script src="/setup/app.js"></script></html>)ROOHTML";
static const char RooWifiSetupJs[] PROGMEM = R"ROOJS('use strict';
const el=id=>document.getElementById(id);let nonce='',busy=false,done=false;
const messages={ready:'אפשר לבחור רשת ולהתחבר.',connecting:'מתחבר לרשת…',failed:'החיבור לא הצליח. בדקו את הסיסמה ונסו שוב.',storage_failed:'החיבור הצליח אך השמירה נכשלה. נסו שוב.',saved:'הרשת נשמרה. חזרו לרשת הבית ופתחו את אתר RooCore.'};
async function request(path,options={}){const r=await fetch(path,{cache:'no-store',...options});if(!r.ok)throw Error('request');return r.json();}
async function status(){try{const s=await request('/setup/state');nonce=s.nonce;busy=s.state==='connecting';done=s.state==='saved';el('connect').disabled=busy||done;el('scan').disabled=busy||done;el('status').textContent=messages[s.state]||messages.ready;if(done){el('password').value='';return;}}catch{el('status').textContent='אין קשר לבקר. התחברו שוב לרשת ההגדרה או לרשת הבית אם החיבור הושלם.';}if(!done)setTimeout(status,1500);}
el('networks').addEventListener('change',()=>{if(el('networks').value)el('ssid').value=el('networks').value;});
el('scan').addEventListener('click',async()=>{el('scan').disabled=true;el('status').textContent='סורק רשתות…';try{let s,first=true;do{s=await request(first?'/setup/scan?refresh=1':'/setup/scan');first=false;if(s.scanning)await new Promise(r=>setTimeout(r,900));}while(s.scanning);const list=el('networks');list.replaceChildren(new Option('הזנת שם רשת ידנית',''));const seen=new Set();for(const n of s.networks){if(!n.ssid||seen.has(n.ssid))continue;seen.add(n.ssid);const option=new Option(n.ssid+' ('+n.rssi+' dBm)'+(n.secure?' 🔒':''),n.ssid);list.add(option);}el('status').textContent='בחרו רשת מהרשימה או הזינו שם ידנית.';}catch{el('status').textContent='הסריקה נכשלה. אפשר להזין שם רשת ידנית.';}el('scan').disabled=busy||done;});
el('form').addEventListener('submit',async e=>{e.preventDefault();if(busy||done||!nonce)return;busy=true;el('connect').disabled=true;try{const body=new URLSearchParams({ssid:el('ssid').value,password:el('password').value,nonce});await request('/setup/connect',{method:'POST',body});el('password').value='';el('status').textContent=messages.connecting;}catch{busy=false;el('connect').disabled=false;el('status').textContent='לא ניתן להתחבר. בדקו שם רשת וסיסמה (8–63 תווים, או ריק לרשת פתוחה).';}});status();
)ROOJS";
