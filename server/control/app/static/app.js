"use strict";
const $ = (id) => document.getElementById(id);
const labels = {
  idle: "בהמתנה",
  charging: "בטעינה",
  held: "עצור",
  fault: "תקלה",
  unknown: "לא ידוע",
};
const views = {
  maps: ["מפת הבית", "מפות שמורות, חדרים ותכנון ניקוי."],
  users: ["משתמשים והרשאות", "גישה אישית לכל אחד מבני הבית."],
  account: ["החשבון שלי", "הגדרות הכניסה האישיות."],
  overview: ["סקירה", "מצב הרובוט והחיבור לשרת."],
  events: ["אירועים", "דיווחים שהתקבלו מהרובוט ומבדיקות השרת."],
  system: ["מערכת", "פרטי השרת ומצב ההכנה לחיבור הבקר."],
};
let data = null,
  events = [],
  source = "hardware",
  busy = false,
  failed = false,
  lastRefresh = null;
const time = (v) => new Date(v).toLocaleTimeString("he-IL", { hour12: false });
const date = (v) => new Date(v).toLocaleDateString("he-IL");
const full = (v) => (v ? `${date(v)}, ${time(v)}` : "—");
function status(id, text, tone = "neutral") {
  const el = $(id);
  if (el.textContent !== text) el.textContent = text;
  el.className = `status ${tone}`;
}
function service(id, text, ready = false) {
  $(id).textContent = text;
  $(id).classList.toggle("ready", ready);
}
function setView(focus = false) {
  const name = location.hash.slice(1);
  const view = Object.hasOwn(views, name) ? name : "overview";
  for (const el of document.querySelectorAll("[data-panel]"))
    el.hidden = el.dataset.panel !== view;
  for (const el of document.querySelectorAll("[data-view]"))
    el.dataset.view === view
      ? el.setAttribute("aria-current", "page")
      : el.removeAttribute("aria-current");
  $("page-title").textContent = views[view][0];
  $("page-description").textContent = views[view][1];
  document.title = `RooCore · ${views[view][0]}`;
  if (focus) {
    $("page-title").tabIndex = -1;
    $("page-title").focus({ preventScroll: true });
  }
}
function renderStatus() {
  if (!data) return;
  const r = data.robots.find((r) => r.id === "roomba-620") || {
      online: false,
      telemetry: null,
    },
    s = data.robots.find((r) => r.id === "sim-620") || {
      online: false,
      telemetry: null,
    };
  const current =
    r.online &&
    Date.now() - Date.parse(r.last_seen) < data.stale_after_seconds * 1000;
  status(
    "connection",
    failed
      ? "אין קשר לשרת"
      : !$("auto-refresh").checked
        ? "עדכון ידני"
        : data.mqtt_connected
          ? "השרת זמין"
          : "תקשורת הבקר מנותקת",
    failed
      ? "bad"
      : data.mqtt_connected && $("auto-refresh").checked
        ? "good"
        : "neutral",
  );
  status(
    "robot-status",
    failed
      ? "מצב החיבור לא ידוע"
      : current
        ? "מחובר"
        : r.telemetry
          ? "אין דיווח עדכני"
          : "לא מחובר",
    !failed && current ? "good" : "neutral",
  );
  $("robot-empty").hidden = !!r.telemetry;
  $("battery").textContent =
    r.telemetry?.battery_percent == null
      ? "—"
      : `${r.telemetry.battery_percent}%`;
  $("battery-note").dir = r.telemetry?.battery_mv != null ? "ltr" : "rtl";
  $("battery-note").textContent =
    r.telemetry?.battery_mv != null
      ? `${(r.telemetry.battery_mv / 1000).toFixed(2)} V`
      : r.telemetry
        ? "דיווח הבקר"
        : "טרם התקבל דיווח";
  $("robot-state").textContent = r.telemetry
    ? labels[r.telemetry.state] || "לא ידוע"
    : "לא ידוע";
  $("last-seen").textContent = r.last_seen ? time(r.last_seen) : "—";
  $("last-seen-note").textContent = r.last_seen
    ? date(r.last_seen)
    : "טרם התקבל דיווח";
  $("robot-note").hidden = !r.telemetry;
  $("robot-note").textContent = failed
    ? "המידע האחרון מוצג כפי שנשמר. כרגע אין קשר לשרת."
    : current
      ? "הנתונים התקבלו מהבקר. בטיחות הפעולה טרם נבדקה על הרובוט."
      : "לא התקבל דיווח עדכני. מצב הרובוט כרגע אינו ידוע.";
  service("api-state", failed ? "לא ידוע" : "זמין", !failed);
  service(
    "mqtt-state",
    failed ? "לא ידוע" : data.mqtt_connected ? "מוכן" : "מנותק",
    !failed && data.mqtt_connected,
  );
  service("db-state", failed ? "לא ידוע" : "זמין", !failed);
  $("os-version").textContent = data.runtime?.os || "—";
  $("python-version").textContent = data.runtime?.python || "—";
  $("started-at").textContent = full(data.runtime?.started_at);
  status(
    "sim-status",
    failed
      ? "מצב לא ידוע"
      : s.online && Date.now() - Date.parse(s.last_seen) < 15000
        ? "בדיקה פעילה"
        : s.telemetry
          ? "הבדיקה הסתיימה"
          : "טרם התקבלו נתונים",
  );
  $("sim-last-seen").textContent = full(s.last_seen);
  $("rejected-messages").textContent = String(data.rejected_messages);
  $("updated").textContent = lastRefresh
    ? `נבדק לאחרונה ב־${time(lastRefresh)}${$("auto-refresh").checked ? "" : " · עדכון ידני"}`
    : "ממתין לעדכון";
}
function empty(target, title, description) {
  const wrap = document.createElement("div");
  wrap.className = "empty-events";
  const h = document.createElement(target.id === "event-list" ? "h2" : "h3"),
    p = document.createElement("p");
  h.textContent = title;
  p.textContent = description;
  wrap.append(h, p);
  target.append(wrap);
}
function eventRow(e) {
  const row = document.createElement("div");
  row.className = "event-row";
  const dot = document.createElement("span");
  dot.className = "event-dot";
  dot.setAttribute("aria-hidden", "true");
  const info = document.createElement("div"),
    title = document.createElement("strong"),
    detail = document.createElement("p");
  title.textContent = e.kind === "offline" ? "החיבור נותק" : "התקבל דיווח";
  const description = [];
  if (e.payload.state)
    description.push(labels[e.payload.state] || "מצב לא ידוע");
  if (e.payload.battery_percent != null)
    description.push(`סוללה ${e.payload.battery_percent}%`);
  detail.textContent = description.join(" · ");
  info.append(title);
  if (description.length) info.append(detail);
  const identity = document.createElement("span");
  identity.className = "event-source";
  identity.textContent =
    e.source === "simulation" ? "בדיקת שרת · מידע מדומה" : "Roomba 620";
  const stamp = document.createElement("div");
  stamp.className = "event-time";
  const t = document.createElement("time"),
    day = document.createElement("small");
  t.dateTime = e.received_at;
  t.textContent = time(e.received_at);
  day.textContent = date(e.received_at);
  stamp.append(t, day);
  row.append(dot, info, identity, stamp);
  return row;
}
function renderEvents() {
  const recent = $("recent-events"),
    list = $("event-list");
  recent.replaceChildren();
  list.replaceChildren();
  const physical = events.filter((e) => e.source === "hardware");
  for (const e of physical.slice(0, 4)) recent.append(eventRow(e));
  if (!physical.length)
    empty(
      recent,
      "אין עדיין פעילות להצגה",
      "כאן יופיעו הדיווחים שיגיעו מהרומבה.",
    );
  const selected = events.filter(
    (e) => source === "all" || e.source === source,
  );
  for (const e of selected) list.append(eventRow(e));
  if (!selected.length)
    empty(
      list,
      source === "simulation" ? "אין אירועי בדיקה" : "אין עדיין אירועים",
      source === "hardware"
        ? "עדיין לא התקבלו דיווחים מהרומבה."
        : "אירועים יופיעו כאן לאחר קבלת דיווח.",
    );
  $("event-count").textContent = `${selected.length} אירועים`;
  $("source-description").textContent =
    source === "hardware"
      ? "דיווחים מהרומבה בלבד."
      : source === "simulation"
        ? "מידע מדומה לבדיקת השרת. אינו משקף את מצב הרובוט."
        : "דיווחים מהרומבה ומבדיקות השרת. המקור מצוין בכל שורה.";
  for (const b of document.querySelectorAll("[data-source]"))
    b.setAttribute("aria-pressed", String(b.dataset.source === source));
}
async function read(path) {
  const res = await fetch(path, {
    cache: "no-store",
    signal: AbortSignal.timeout(7000),
  });
  if (res.status === 401) {
    location.replace("/login");
    throw new Error("signin");
  }
  if (!res.ok) throw new Error("unavailable");
  return res.json();
}
async function refresh() {
  if (busy) return;
  busy = true;
  $("refresh").disabled = true;
  $("refresh").querySelector("span").textContent = "מרענן";
  try {
    const [state, history] = await Promise.all([
      read("/api/status"),
      read("/api/events?limit=200"),
    ]);
    data = state;
    events = history.events;
    failed = false;
    lastRefresh = new Date().toISOString();
    $("error").hidden = true;
    renderStatus();
    renderEvents();
  } catch (e) {
    failed = true;
    $("error").hidden = false;
    if (data) renderStatus();
    else {
      status("connection", "אין קשר לשרת", "bad");
      status("robot-status", "מצב החיבור לא ידוע");
      for (const id of ["api-state", "mqtt-state", "db-state"])
        service(id, "לא ידוע");
    }
  } finally {
    busy = false;
    $("refresh").disabled = false;
    $("refresh").querySelector("span").textContent = "רענון";
  }
}
$("refresh").addEventListener("click", refresh);
$("auto-refresh").addEventListener("change", () => {
  $("auto-refresh").checked ? refresh() : renderStatus();
});
for (const b of document.querySelectorAll("[data-source]"))
  b.addEventListener("click", () => {
    source = b.dataset.source;
    renderEvents();
  });
$("show-test-events").addEventListener("click", () => {
  source = "simulation";
  renderEvents();
});
addEventListener("hashchange", () => setView(true));
document.addEventListener("visibilitychange", () => {
  if (!document.hidden && $("auto-refresh").checked) refresh();
});
setInterval(() => {
  if (!document.hidden) {
    renderStatus();
    if ($("auto-refresh").checked) refresh();
  }
}, 5000);
setView();
renderEvents();
refresh();
