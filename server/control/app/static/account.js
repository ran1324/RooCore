'use strict';
const byId = id => document.getElementById(id);
let resetToken = location.pathname === '/reset' ? location.hash.slice(1) : '';
let resendAt = 0;
function screen(form, title, note) {
  for (const id of ['login-form', 'code-form', 'forgot-form', 'reset-form']) byId(id).hidden = id !== form;
  byId('auth-title').textContent = title;
  byId('auth-note').textContent = note;
  byId('forgot-toggle').hidden = form !== 'login-form';
  byId('back-login').hidden = form === 'login-form';
  byId('auth-message').textContent = '';
  byId('auth-title').focus();
}
function showCode(data) {
  screen('code-form', 'בדקו את המייל', 'שלחנו קוד אימות ל־' + data.email);
  byId('password').value = '';
  byId('login-code').value = '';
  resendAt = Date.now() + data.resend_after * 1000;
  byId('login-code').focus(); countdown();
}
function countdown() {
  const seconds = Math.max(0, Math.ceil((resendAt - Date.now()) / 1000));
  byId('resend-code').disabled = seconds > 0;
  byId('resend-code').textContent = seconds > 0 ? 'אפשר לשלוח קוד חדש בעוד ' + seconds + ' שניות' : 'שליחת קוד חדש';
}
setInterval(countdown, 1000);
async function submit(path, body) {
  const r = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)});
  if (!r.ok) {
    if (r.status === 429) throw Error('יותר מדי ניסיונות. המתינו לפני ניסיון נוסף.');
    if (r.status === 503) throw Error('לא הצלחנו לשלוח את המייל. נסו להיכנס שוב בעוד כמה דקות.');
    if (r.status === 401) throw Error(path.endsWith('/verify') || path.endsWith('/resend') ? 'הקוד שגוי או שפג תוקפו. בדקו את הקוד האחרון שנשלח, או חזרו לכניסה.' : 'שם המשתמש או הסיסמה אינם נכונים.');
    throw Error('הבקשה לא הושלמה. בדקו את הפרטים ואת תוקף הקישור.');
  }
  return r.json();
}
function feedback(message) { byId('auth-message').textContent = message; byId('auth-message').focus(); }
function bind(id, action) {
  byId(id).addEventListener('submit', async e => {
    e.preventDefault(); const button = e.target.querySelector('button[type="submit"], button');
    button.disabled = true; button.setAttribute('aria-busy', 'true'); byId('auth-message').textContent = '';
    try { await action(); } catch (error) { feedback(error instanceof TypeError ? 'החיבור נקטע. נסו שוב.' : error.message); }
    finally { button.disabled = false; button.removeAttribute('aria-busy'); }
  });
}
bind('login-form', async () => {
  const data = await submit('/api/auth/login', {username: byId('username').value, password: byId('password').value});
  if (data.mfa_required) showCode(data);
});
bind('code-form', async () => {
  const code = byId('login-code').value.replace(/\s/g, '');
  if (!/^[0-9]{8}$/.test(code)) throw Error('יש להזין קוד בן 8 ספרות.');
  await submit('/api/auth/verify', {code}); byId('login-code').value = ''; location.assign('/');
});
byId('resend-code').addEventListener('click', async () => {
  byId('resend-code').disabled = true;
  try { showCode(await submit('/api/auth/resend', {})); feedback('שלחנו קוד חדש. הקוד הקודם אינו תקף יותר.'); }
  catch (error) { resendAt = Date.now() + 60000; feedback(error.message); }
});
bind('forgot-form', async () => {
  await submit('/api/auth/forgot', {email: byId('email').value});
  feedback('אם קיים חשבון עם הכתובת הזאת, נשלח אליו קישור לשחזור. בדקו גם את תיקיית הספאם.');
});
bind('reset-form', async () => {
  if (byId('new-password').value !== byId('confirm-password').value) throw Error('הסיסמאות אינן תואמות.');
  await submit('/api/auth/reset', {token: resetToken, password: byId('new-password').value}); resetToken = '';
  byId('new-password').value = ''; byId('confirm-password').value = ''; byId('reset-form').hidden = true;
  feedback('הסיסמה נשמרה וכל החיבורים הקודמים נותקו. אפשר להיכנס עם הסיסמה החדשה וקוד מהמייל.');
});
byId('forgot-toggle').onclick = () => screen('forgot-form', 'שחזור סיסמה', 'נשלח קישור אישי לכתובת הדוא״ל של החשבון.');
if (resetToken) {
  history.replaceState(null, '', '/reset'); screen('reset-form', 'בחירת סיסמה חדשה', 'בחרו סיסמה אישית באורך 15 תווים לפחות. הקישור תקף לחצי שעה.');
} else if (location.pathname === '/reset') {
  screen('forgot-form', 'שחזור סיסמה', 'נדרש קישור אישי מהמייל. אפשר לבקש קישור חדש כאן.');
} else {
  fetch('/api/auth/challenge').then(r => r.ok ? r.json() : {}).then(data => { if (data.mfa_required) showCode(data); }).catch(() => {});
}
