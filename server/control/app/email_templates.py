"""RTL transactional messages. Authentication links always use the fixed public origin."""
from dataclasses import dataclass
from html import escape
import re

ORIGIN = 'https://roocore.saymour.co.il'


@dataclass(frozen=True)
class Email:
    subject: str
    text: str
    html: str


def render(kind, *, code=None, token=None):
    action_url, action_label = None, None
    if kind == 'login':
        if not isinstance(code, str) or not re.fullmatch(r'[0-9]{8}', code):
            raise ValueError('Expected an eight-digit login code')
        title = 'קוד הכניסה שלך'
        intro = 'נשאר עוד שלב אחד כדי להיכנס ל־RooCore. הזינו את הקוד במסך הכניסה.'
        note = 'הקוד תקף ל־10 דקות ולשימוש אחד בלבד. אל תעבירו אותו לאף אחד.'
        footer = 'לא ניסיתם להיכנס? אפשר להתעלם מההודעה. ללא הקוד לא תושלם הכניסה.'
    elif kind in ('recovery', 'invitation'):
        if not isinstance(token, str) or not re.fullmatch(r'[A-Za-z0-9_-]{40,128}', token):
            raise ValueError('Invalid account token')
        title = 'בחירת סיסמה חדשה' if kind == 'recovery' else 'החשבון שלך מוכן'
        intro = ('קיבלנו בקשה לשחזור הסיסמה לחשבון שלך. לחצו על הכפתור לבחירת סיסמה חדשה.'
                 if kind == 'recovery' else 'צורפתם למערכת RooCore. בחרו סיסמה אישית כדי להתחיל.')
        note = 'הקישור אישי, תקף ל־30 דקות ולשימוש אחד בלבד.'
        footer = ('לא ביקשתם לשחזר סיסמה? אפשר להתעלם מההודעה. הסיסמה הקיימת תישאר בתוקף.'
                  if kind == 'recovery' else 'הכניסה לחשבון כוללת גם קוד אימות שיישלח לכתובת הדוא״ל הזאת.')
        action_url = ORIGIN + '/reset#' + token
        action_label = 'בחירת סיסמה'
    elif kind == 'password_changed':
        title = 'הסיסמה עודכנה'
        intro = 'הסיסמה לחשבון RooCore שלך שונתה. כל החיבורים הקודמים נותקו.'
        note = 'בכניסה הבאה תידרשו לסיסמה החדשה ולקוד אימות מהמייל.'
        footer = 'לא ביצעתם את השינוי? פנו למנהל המערכת ובדקו גם את אבטחת תיבת הדוא״ל שלכם.'
        action_url, action_label = ORIGIN + '/login', 'כניסה למערכת'
    elif kind == 'test':
        title = 'הדואר מחובר'
        intro = 'זוהי הודעת בדיקה ממערכת RooCore. החיבור דרך Mailgun בדומיין saymour.co.il פועל.'
        note = 'מכאן יגיעו קודי כניסה, קישורי שחזור סיסמה והודעות מערכת.'
        footer = 'זו בדיקת דואר בלבד. לא שונתה סיסמה ולא בוצעה פעולה ברובוט.'
        action_url, action_label = ORIGIN, 'פתיחת RooCore'
    else:
        raise ValueError('Unknown email template')
    return layout(title, intro, note, footer, action_url, action_label, code)


def notice(title, body):
    return layout(title, body, 'הודעת מערכת אוטומטית מ־RooCore.',
                  'הודעה זו מתייחסת לבדיקות שרת בלבד.', ORIGIN, 'פתיחת המערכת')


def layout(title, intro, note, footer, action_url=None, action_label=None, code=None):
    text = '\n\n'.join(x for x in (title, intro, code, action_url, note, footer) if x)
    intro_html = escape(intro).replace('\n', '<br>')
    content = ''
    if code:
        content = f'''<p dir="ltr" style="margin:28px 0;padding:20px 12px;background:#f0f5fb;border:1px solid #dae5f1;border-radius:10px;text-align:center;font-family:Consolas,monospace;font-size:34px;font-weight:700;letter-spacing:6px;color:#153b66">{escape(code)}</p>'''
    if action_url:
        content += f'''<table role="presentation" cellpadding="0" cellspacing="0" style="margin:28px 0"><tr><td style="background:#245db0;border-radius:8px"><a href="{escape(action_url, quote=True)}" style="display:inline-block;padding:14px 26px;color:#ffffff;text-decoration:none;font-size:16px;font-weight:700">{escape(action_label)}</a></td></tr></table>'''
    fallback = (f'<p style="margin:22px 0 0;font-size:12px;color:#566477">אם הכפתור אינו נפתח, העתיקו את הקישור לדפדפן:</p><p dir="ltr" style="font-size:12px;word-break:break-all"><a href="{escape(action_url, quote=True)}" style="color:#245db0">{escape(action_url)}</a></p>' if action_url else '')
    html = f'''<!doctype html><html lang="he" dir="rtl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{escape(title)}</title></head>
<body style="margin:0;padding:0;background:#eef2f6;font-family:Arial,Helvetica,sans-serif;color:#202c3c">
<div style="display:none;max-height:0;overflow:hidden;opacity:0">{escape(intro)}</div>
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" dir="rtl" style="background:#eef2f6"><tr><td align="center" style="padding:36px 16px">
<table role="presentation" width="560" cellpadding="0" cellspacing="0" style="width:100%;max-width:560px"><tr><td dir="ltr" align="left" style="padding:0 4px 20px;font-size:24px;font-weight:700;letter-spacing:-1px;color:#153b66">RooCore<span style="color:#7c8c9f"> / </span></td></tr>
<tr><td style="background:#ffffff;border:1px solid #dce3ec;border-top:4px solid #245db0;border-radius:12px;padding:32px 28px;text-align:right">
<p style="margin:0 0 14px;color:#60748c;font-size:12px;letter-spacing:1px">החשבון שלך · RooCore</p>
<h1 style="margin:0 0 18px;font-size:27px;line-height:1.35;color:#172c46">{escape(title)}</h1>
<p style="margin:0;font-size:16px;line-height:1.8;color:#405169">{intro_html}</p>{content}
<p style="margin:22px 0 0;font-size:14px;line-height:1.8;color:#405169">{escape(note)}</p>
<table role="presentation" width="100%" style="margin-top:26px;border-top:1px solid #e5eaf0"><tr><td style="padding-top:20px;font-size:13px;line-height:1.8;color:#66768b">{escape(footer)}</td></tr></table>{fallback}
</td></tr><tr><td style="padding:20px 8px;text-align:center;font-size:12px;line-height:1.8;color:#6d7b8d">RooCore · מערכת הבית שלך<br>הודעה אוטומטית. אין להשיב לכתובת זו.</td></tr></table>
</td></tr></table></body></html>'''
    return Email('RooCore · ' + title, text, html)
