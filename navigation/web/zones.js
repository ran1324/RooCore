/* Direct map manipulation. Coordinates stay inside the model, never in the form. */
(() => {
  const node = (tag, text = '', attrs = {}) => {
    const n = document.createElement(tag); n.textContent = text; Object.assign(n, attrs); return n;
  };
  const button = (text, action, cls = '') => {
    const b = node('button', text, {type: 'button', className: cls}); b.onclick = action; return b;
  };
  const canvas = document.getElementById('map'), mapWrap = canvas.parentElement;
  const host = node('section', '', {id: 'zone-editor'});
  const top = node('div', '', {className: 'zone-top'});
  const heading = node('div');
  heading.append(node('h2', 'הבית שלי'), node('p', 'חדרים, שטיחים וגבולות — ישירות על המפה', {className: 'zone-hint'}));
  const open = button('תכנון הבית', toggleEditor, 'zone-primary');
  top.append(heading, open); host.append(top); mapWrap.before(host);
  const tools = node('div', '', {className: 'zone-tools', hidden: true});
  const toolButtons = {};
  for (const [kind, label] of [['select','בחירה והזזה'], ['room','חדר'], ['carpet','שטיח'], ['no_go','אזור אסור'], ['wall','קיר וירטואלי']]) {
    const b = button(label, () => chooseTool(kind)); b.dataset.kind = kind; toolButtons[kind] = b; tools.append(b);
  }
  const undo = button('ביטול פעולה', undoEdit), save = button('שמירת המפה', saveMap, 'zone-primary');
  tools.append(undo, save); host.append(tools);
  const instruction = node('p', '', {className: 'zone-instruction', hidden: true}); host.append(instruction);
  const workbench = node('div', '', {className: 'zone-workbench'});
  mapWrap.before(workbench); workbench.append(mapWrap);
  const inspector = node('section', '', {className: 'zone-inspector', hidden: true});
  inspector.setAttribute('aria-label', 'הגדרות האזור הנבחר'); workbench.append(inspector);
  const mapLabel = node('label', 'שם המפה'), mapName = node('input', '', {maxLength: 80});
  mapLabel.append(mapName); inspector.append(mapLabel);
  const mapBadge = node('p', '', {className: 'zone-map-badge'}); inspector.append(mapBadge);
  const list = node('div', '', {className: 'zone-list'}); inspector.append(list);
  const empty = node('div', '', {className: 'zone-empty'});
  empty.append(node('h3', 'מה יש בבית שלכם?'), node('p', 'בחרו חדר, שטיח או אזור אסור, וגררו מלבן סביבו במפה. אפשר גם להקיש על שתי הפינות.', {className:'zone-hint'}));
  inspector.append(empty);
  const properties = node('div', '', {className:'zone-properties', hidden:true}); inspector.append(properties);
  const fields = {};
  function field(key, title, options) {
    const label = node('label', title), input = options ? node('select') : node('input');
    input.id = 'zone-' + key;
    if (options) for (const [value, text] of options) input.append(node('option',text,{value}));
    fields[key] = input; label.append(input); properties.append(label); return input;
  }
  field('name', 'שם האזור').maxLength=80;
  field('room_type','איזה חדר זה?', [['living_room','סלון'],['bedroom','חדר שינה'],['kitchen','מטבח'],['bathroom','חדר רחצה'],['hallway','מסדרון'],['other','אחר']]);
  field('reason','מה נמצא כאן?', [['general','אזור שלא מנקים'],['pet_bowls','קערות ואוכל לחיות'],['bathroom','חדר רחצה'],['low_clearance','רהיט נמוך / סכנת פגיעה']]);
  for (const [key,title] of [['present','השטיח נמצא כרגע'],['boost','ניקוי בעוצמה מרבית'],['enabled','האזור פעיל']]) {
    const input = field(key,title); input.type='checkbox'; input.parentElement.className='zone-switch';
  }
  field('extra_passes','מעברים נוספים על השטיח', [['0','ללא תוספת'],['1','מעבר נוסף אחד'],['2','שני מעברים נוספים']]);
  const policy = node('p','',{className:'zone-policy'}); properties.append(policy);
  const moveTools = node('details','',{className:'zone-adjust'});
  moveTools.append(node('summary','הזזה ושינוי גודל בכפתורים'));
  const moveRow = node('div','',{className:'zone-adjust-buttons'});
  for (const [label,dx,dy] of [['ימינה',1,0],['שמאלה',-1,0],['למעלה',0,1],['למטה',0,-1]]) moveRow.append(button(label,()=>adjust(dx,dy,false)));
  for (const [label,dx,dy] of [['הרחבה',1,0],['הצרה',-1,0],['הגבהה',0,1],['הנמכה',0,-1]]) moveRow.append(button(label,()=>adjust(dx,dy,true)));
  moveTools.append(moveRow); properties.append(moveTools);
  properties.append(button('מחיקת האזור', removeSelected, 'zone-delete'));
  const bottom = node('div','',{className:'zone-bottom'}), status = node('p','טוען את המפה…',{className:'zone-status'});
  status.setAttribute('role','status');
  const discard = button('ביטול כל השינויים',loadMap,'zone-link');
  bottom.append(status,discard); workbench.after(bottom);
  let doc, latest, mapKey, selected=null, editing=false, tool='select', dirty=false, busy=false;
  let transform, gesture=null, first=null, preview=null, history=[], generation=0;
  const kinds={room:'חדר',no_go:'אזור אסור',carpet:'שטיח',wall:'קיר וירטואלי'};
  const colors={room:'#00786d',no_go:'#aa392b',carpet:'#805d00',wall:'#aa392b'};
  const clone = value => JSON.parse(JSON.stringify(value));
  const current = () => doc?.zones.find(z=>z.id===selected);
  const redraw = () => { if (typeof window.draw==='function') window.draw(); };
  const message = (text,error=false) => {status.textContent=text; status.classList.toggle('zone-error',error);};
  function remember() {history.push(clone({zones:doc.zones,name:doc.name})); if(history.length>40)history.shift();}
  function changed() {dirty=true; mapBadge.textContent='שינויים שלא נשמרו'; updateControls(); redraw();}
  function updateControls() {
    const stopped = latest && ['READY','PAUSED','COMPLETE'].includes(latest.state);
    open.disabled = !doc || busy;
    save.disabled = !doc || !stopped || busy;
    undo.disabled = !history.length || busy;
    discard.hidden = !dirty;
    tools.hidden = inspector.hidden = instruction.hidden = !editing;
    workbench.classList.toggle('is-editing',editing);
    canvas.classList.toggle('zone-drawing',editing);
    canvas.style.touchAction = editing ? 'none' : '';
    for(const [k,b] of Object.entries(toolButtons)) b.setAttribute('aria-pressed',String(tool===k));
    open.textContent = editing ? 'חזרה לניקיון' : 'תכנון הבית';
    instruction.textContent = tool==='select' ? 'לחצו על אזור כדי לערוך אותו. גררו להזזה; גררו פינה לשינוי הגודל.' : tool==='wall' ? 'גררו קו על המפה כדי לחסום מעבר. אפשר גם להקיש על שני קצותיו.' : 'גררו מלבן על המפה. בטלפון אפשר גם להקיש על שתי פינות נגדיות.';
  }
  function chooseTool(kind) {tool=kind; first=preview=null; selected=null; renderProperties(); updateControls(); redraw(); canvas.focus({preventScroll:true});}
  async function toggleEditor() {
    if(!editing && window.RooZonesBeforeEdit) await window.RooZonesBeforeEdit();
    editing=!editing; document.body.classList.toggle('zone-editing',editing);
    first=preview=null; updateControls(); redraw();
    if(editing) host.scrollIntoView({block:'start',behavior:'instant'});
  }
  function renderProperties() {
    const z=current(); properties.hidden=!z; empty.hidden=!!z;
    if(!z)return;
    for(const [k,f] of Object.entries(fields)) {
      if(f.type==='checkbox') f.checked=z[k]; else f.value=z[k];
      f.parentElement.hidden = ['present','boost','extra_passes'].includes(k) ? z.kind!=='carpet' : k==='room_type' ? z.kind!=='room' : k==='reason' ? z.kind!=='no_go' : false;
    }
    const base=latest?.base_passes || (latest?.profile==='deep'?2:1);
    const baseText=base===1?'מעבר רגיל אחד':'שני מעברים רגילים';
    const extraText=z.extra_passes===0?'ללא מעבר נוסף':z.extra_passes===1?'ועוד מעבר אחד על השטיח':'ועוד שני מעברים על השטיח';
    policy.textContent=z.kind==='carpet' ? (!z.present ? 'הסימון נשמר. כשהשטיח אינו כאן, מנקים לפי התוכנית הרגילה.' : `${baseText} ${extraText}.${z.boost?' השאיבה והמברשות פועלות בעוצמה המרבית הנתמכת.':''}`) : z.kind==='no_go'||z.kind==='wall' ? 'הרובוט יישאר מחוץ לגבול הזה, עם מרווח לגוף שלו.' : 'שם החדר יופיע על המפה כדי לעזור לכם להתמצא בבית.';
  }
  function renderList() {
    list.replaceChildren();
    if(doc) for(const z of doc.zones) {
      const b=button(z.name,()=>{selected=z.id;tool='select';renderProperties();renderList();updateControls();redraw();});
      const small=node('small',kinds[z.kind]+(!z.enabled?' · כבוי':z.kind==='carpet'&&!z.present?' · לא נמצא':'')); b.append(small);
      b.style.setProperty('--zone-color',colors[z.kind]); b.classList.toggle('selected',z.id===selected); list.append(b);
    }
    mapBadge.textContent=dirty?'שינויים שלא נשמרו':doc?.cells?.some(v=>v!==0)?'מפה שמורה · '+doc.zones.length+' אזורים':'מפה נוכחית · שמרו כדי לחזור אליה בהמשך';
    renderProperties();updateControls();
  }
  for(const [k,f] of Object.entries(fields)) f.addEventListener('change',()=>{
    const z=current();if(!z)return;remember();
    z[k]=f.type==='checkbox'?f.checked:k==='extra_passes'?Number(f.value):f.value.trim();
    if(k==='room_type')z.name=f.selectedOptions[0].textContent;
    if(k==='reason' && z.name==='אזור אסור')z.name=f.selectedOptions[0].textContent;
    changed();renderList();
  });
  mapName.onchange=()=>{remember();doc.name=mapName.value;changed();};
  function undoEdit(){const value=history.pop();if(!value)return;doc.zones=value.zones;mapName.value=doc.name=value.name;selected=null;changed();renderList();message('הפעולה האחרונה בוטלה.');}
  function removeSelected(){if(!current())return;remember();doc.zones=doc.zones.filter(z=>z.id!==selected);selected=null;changed();renderList();message('האזור הוסר מהטיוטה. אפשר לבטל את הפעולה.');}
  function bounds(){const g=doc.key.geometry;return [g.origin_x,g.origin_y,g.origin_x+g.cols*g.resolution,g.origin_y+g.rows*g.resolution];}
  function fit(z){const [l,b,r,t]=bounds(),w=z.x2-z.x1,h=z.y2-z.y1;if(w>r-l||h>t-b)return false;const dx=Math.max(l-z.x1,Math.min(0,r-z.x2)),dy=Math.max(b-z.y1,Math.min(0,t-z.y2));z.x1+=dx;z.x2+=dx;z.y1+=dy;z.y2+=dy;return true;}
  function adjust(dx,dy,resize){const z=current();if(!z)return;const before=clone(z),step=doc.key.geometry.resolution;remember();if(resize){z.x2+=dx*step;z.y2+=dy*step;}else{z.x1+=dx*step;z.x2+=dx*step;z.y1+=dy*step;z.y2+=dy*step;}if(z.kind!=='wall'&&(z.x2-z.x1<step||z.y2-z.y1<step)||!fit(z)){Object.assign(z,before);history.pop();return;}changed();}
  function toPoint(e){
    if(!transform||!doc)return null;
    const rect=canvas.getBoundingClientRect(),factor=Math.min(rect.width/canvas.width,rect.height/canvas.height);
    const x=(e.clientX-rect.left-(rect.width-canvas.width*factor)/2)/factor;
    const y=(e.clientY-rect.top-(rect.height-canvas.height*factor)/2)/factor;
    const [a,b]=transform.p(0,0),[l,bottom,r,t]=bounds();
    return [Math.max(l,Math.min(r,(x-a)/transform.scale)),Math.max(bottom,Math.min(t,(b-y)/transform.scale))];
  }
  function tolerance(){const rect=canvas.getBoundingClientRect();return 14/Math.min(rect.width/canvas.width,rect.height/canvas.height)/transform.scale;}
  function corners(z){return z.kind==='wall'?[[z.x1,z.y1],[z.x2,z.y2]]:[[z.x1,z.y1],[z.x2,z.y1],[z.x2,z.y2],[z.x1,z.y2]];}
  function hit(p){
    const z=current(),tol=tolerance();
    if(z){const index=corners(z).findIndex(c=>Math.hypot(c[0]-p[0],c[1]-p[1])<tol);if(index>=0)return {zone:z,corner:index};}
    for(const v of [...doc.zones].reverse()){
      if(v.kind==='wall'){const dx=v.x2-v.x1,dy=v.y2-v.y1,t=Math.max(0,Math.min(1,((p[0]-v.x1)*dx+(p[1]-v.y1)*dy)/(dx*dx+dy*dy)));if(Math.hypot(p[0]-v.x1-t*dx,p[1]-v.y1-t*dy)<tol)return {zone:v};}
      else if(p[0]>=v.x1&&p[0]<=v.x2&&p[1]>=v.y1&&p[1]<=v.y2)return {zone:v};
    }return null;
  }
  function rectangle(a,b,kind){return {x1:kind==='wall'?a[0]:Math.min(a[0],b[0]),y1:kind==='wall'?a[1]:Math.min(a[1],b[1]),x2:kind==='wall'?b[0]:Math.max(a[0],b[0]),y2:kind==='wall'?b[1]:Math.max(a[1],b[1])};}
  canvas.tabIndex=0;
  canvas.addEventListener('pointerdown',e=>{
    if(!editing||busy||!doc)return;const p=toPoint(e);if(!p)return;e.preventDefault();canvas.setPointerCapture(e.pointerId);
    if(tool==='select'){
      const found=hit(p);selected=found?.zone.id||null;renderList();redraw();
      if(found)gesture={type:found.corner===undefined?'move':'resize',corner:found.corner,start:p,original:clone(found.zone),pixel:[e.clientX,e.clientY],moved:false};
    }else{gesture={type:'new',start:first||p,pixel:[e.clientX,e.clientY],second:!!first,moved:false};}
  });
  canvas.addEventListener('pointermove',e=>{
    if(!gesture)return;const p=toPoint(e),g=gesture;
    if(Math.hypot(e.clientX-g.pixel[0],e.clientY-g.pixel[1])>4)g.moved=true;
    if(!g.moved)return;
    if(g.type==='new')preview={kind:tool,name:kinds[tool],enabled:true,present:true,...rectangle(g.start,p,tool)};
    else{
      const z=current();if(!z)return;
      if(g.type==='move'){const dx=p[0]-g.start[0],dy=p[1]-g.start[1];Object.assign(z,g.original,{x1:g.original.x1+dx,x2:g.original.x2+dx,y1:g.original.y1+dy,y2:g.original.y2+dy});fit(z);}
      else{const cs=corners(g.original),opposite=z.kind==='wall'?cs[1-g.corner]:cs[(g.corner+2)%4];Object.assign(z,rectangle(opposite,p,z.kind));}
    }redraw();
  });
  canvas.addEventListener('pointerup',e=>{
    if(!gesture)return;const g=gesture,p=toPoint(e);gesture=null;
    if(g.type==='new'){
      if(!g.moved&&!g.second){first=p;message('בחרו את הפינה הנגדית כדי להשלים את המלבן.');return;}
      first=null;preview=null;
      const rect=rectangle(g.start,p,tool);
      if(Math.hypot(rect.x2-rect.x1,rect.y2-rect.y1)<.05 || (tool!=='wall'&&(rect.x2-rect.x1<.05||rect.y2-rect.y1<.05))){message('סמנו אזור גדול יותר במפה.',true);redraw();return;}
      if(doc.zones.length>=32){message('אפשר לשמור עד 32 אזורים.',true);return;}
      remember();const z={id:crypto.randomUUID(),name:kinds[tool],kind:tool,...rect,enabled:true,present:true,boost:true,extra_passes:tool==='carpet'?1:0,room_type:'other',reason:'general'};
      doc.zones.push(z);selected=z.id;tool='select';changed();renderList();message('האזור נוסף. תנו לו שם ושמרו את המפה.');fields.name.focus({preventScroll:true});fields.name.select();
    }else if(g.moved){
      const z=current();if(!z)return;const after=clone(z);Object.assign(z,g.original);remember();
      if(after.kind==='wall'||(after.x2-after.x1>=.05&&after.y2-after.y1>=.05))Object.assign(z,after);
      changed();message('הגבולות עודכנו בטיוטה.');
    }redraw();
  });
  canvas.addEventListener('pointercancel',()=>{if(gesture?.original&&current())Object.assign(current(),gesture.original);gesture=preview=first=null;redraw();});
  canvas.addEventListener('keydown',e=>{
    if(!editing)return;
    const move={ArrowRight:[1,0],ArrowLeft:[-1,0],ArrowUp:[0,1],ArrowDown:[0,-1]}[e.key];
    if(move&&current()){e.preventDefault();adjust(...move,e.shiftKey);}
    if(e.key==='Escape'){gesture=preview=first=null;tool='select';updateControls();redraw();}
    if(e.key==='Delete'&&current()){e.preventDefault();removeSelected();}
  });
  async function loadMap(){
    const id=++generation;busy=true;updateControls();
    try{const r=await fetch('/api/zones'),data=await r.json();if(id!==generation)return;if(!r.ok)throw Error(data.error);doc=data;dirty=false;history=[];selected=null;mapName.value=doc.name;renderList();redraw();message(doc.application==='simulation'?'הסימונים נשמרים במפה ומופעלים בסימולציה.':'מפת תצפית: הסימונים נשמרים, אך אינם מועברים לרובוט.');}
    catch(e){message('לא ניתן לטעון את המפה: '+e.message,true);}finally{if(id===generation){busy=false;updateControls();}}
  }
  async function saveMap(){
    busy=true;updateControls();
    try{const r=await fetch('/api/zones',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({key:doc.key,revision:doc.revision,name:mapName.value,zones:doc.zones})}),data=await r.json();if(!r.ok)throw Error(data.error);doc=data;dirty=false;history=[];renderList();redraw();message(doc.application==='simulation'?'המפה נשמרה. ההגדרות פעילות בסימולציה.':'המפה והסימונים נשמרו כתכנון בלבד.');const s=await fetch('/api/state');if(s.ok&&window.render)window.render(await s.json());}
    catch(e){const msg=e.message.includes('overlap')?'הגבול קרוב מדי לרובוט או לעמדה. הזיזו אותו מעט.':e.message.includes('elsewhere')?'המפה עודכנה בחלון אחר. לחצו ביטול כל השינויים כדי לטעון אותה מחדש.':e.message;message('השמירה לא בוצעה: '+msg,true);}finally{busy=false;updateControls();}
  }
  window.RooZones={
    planning:()=>editing,
    state(s){latest=s;const key=JSON.stringify([s.source||'simulation',s.identity?.robot_id,s.identity?.map_id||s.scenario,s.cols,s.rows,s.resolution,s.map_origin]);if(key!==mapKey){mapKey=key;doc=null;selected=null;loadMap();}updateControls();},
    background(ctx,p,scale,s){
      if(!editing||!doc?.cells?.some(v=>v!==0))return;
      const g=doc.key.geometry;ctx.save();
      doc.cells.forEach((v,c)=>{const[x,y]=p(g.origin_x+(c%g.cols)*g.resolution,g.origin_y+(Math.floor(c/g.cols)+1)*g.resolution);ctx.fillStyle=v===1?'#687f81':v===0?'#bdc9c7':'#f8faf7';ctx.fillRect(x,y,scale*g.resolution+.3,scale*g.resolution+.3);});ctx.restore();
    },
    draw(ctx,p,scale,s){
      transform={p,scale};ctx.save();
      const rect=canvas.getBoundingClientRect(),factor=Math.min(rect.width/canvas.width,rect.height/canvas.height);
      const fontSize=13/Math.max(.2,factor),pad=5/Math.max(.2,factor),handle=6/Math.max(.2,factor);
      const shown=editing?(doc?.zones||s.zones||[]):(s.zones??doc?.zones??[]);
      for(const z of [...shown,...(preview?[preview]:[])]){
        const[x,y]=p(z.x1,z.y1),[x2,y2]=p(z.x2,z.y2),chosen=z.id===selected;
        const inactive=!z.enabled||(z.kind==='carpet'&&!z.present);ctx.globalAlpha=inactive?.4:1;
        ctx.strokeStyle=colors[z.kind];ctx.fillStyle=colors[z.kind]+(chosen?'30':'1d');ctx.lineWidth=chosen?3:2;ctx.setLineDash(inactive?[5,5]:z.kind==='carpet'?[8,4]:[]);
        if(z.kind==='wall'){ctx.lineWidth=4;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x2,y2);ctx.stroke();}
        else{ctx.fillRect(x,y2,x2-x,y-y2);ctx.strokeRect(x,y2,x2-x,y-y2);}
        if(z.kind==='no_go'){ctx.save();ctx.beginPath();ctx.rect(x,y2,x2-x,y-y2);ctx.clip();ctx.globalAlpha=.18;ctx.lineWidth=1;for(let a=x-(y-y2);a<x2;a+=12){ctx.beginPath();ctx.moveTo(a,y);ctx.lineTo(a+(y-y2),y2);ctx.stroke();}ctx.restore();}
        ctx.setLineDash([]);ctx.font=`bold ${fontSize}px Arial`;ctx.textAlign='center';
        const label=z.name+(z.kind==='carpet'&&!z.present?' · לא נמצא':''),width=ctx.measureText(label).width,tx=(x+x2)/2,ty=Math.min(y,y2)+fontSize+pad;
        ctx.fillStyle='#ffffffed';ctx.fillRect(tx-width/2-pad,ty-fontSize-pad,width+pad*2,fontSize+pad*2);ctx.fillStyle=colors[z.kind];ctx.fillText(label,tx,ty);
        if(chosen&&editing){ctx.fillStyle='white';ctx.lineWidth=2;for(const pt of corners(z)){const[a,b]=p(...pt);ctx.beginPath();ctx.rect(a-handle,b-handle,handle*2,handle*2);ctx.fill();ctx.stroke();}}
      }
      if(first){ctx.fillStyle=colors[tool]||'#00786d';ctx.beginPath();ctx.arc(...p(...first),6,0,Math.PI*2);ctx.fill();}
      ctx.restore();
    }
  };
})();
