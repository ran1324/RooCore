const $ = (id) => document.getElementById(id);
const phases = {
  READY: "מוכן להפעלה",
  CLEANING: "מנקה",
  RETURNING: "חוזר לטעינה",
  DOCKING: "מתחבר לעמדה",
  CHARGING: "בטעינה",
  RESUMING: "חוזר לנקודת ההמשך",
  PAUSED: "מושהה",
  FAULT: "עצירה לבדיקה",
  COMPLETE: "התוכנית הסתיימה",
  EDGING: "ניקוי קירות ופינות",
  TOUCHUP: "השלמת אזורים קטנים",
  YIELDING: "ממתין למעבר פנוי",
  SETTLING: "עצירה לבדיקת חיישנים",
  RECOVERING: "נסיגה מוגבלת",
};
const explanations = {
  "telemetry stale or invalid": "נתוני חישה חסרים או לא עדכניים. התנועה נעצרה.",
  "localization lost": "המיקום המקומי אינו אמין. התנועה נעצרה.",
  "no progress": "לא הייתה התקדמות בזמן המותר. הניסיון הסתיים.",
  "recovery exhausted": "ניסיון ההיחלצות הסתיים; אין ניסיון חוזר אוטומטי.",
  "dock timeout": "העמדה לא נמצאה בזמן שהוקצה. הרובוט עצר.",
  cliff: "חיישן מצוק הופעל. התנועה נעצרה.",
  "stale or missing LiDAR": "אין סריקת ליידר עדכנית. התנועה נעצרה.",
  "localization lost / map frame mismatch":
    "המיקום אינו מזוהה. יש לשחזר מיקום לפני המשך.",
  "cliff / wheel drop / bumper": "חיישן בטיחות הופעל. התנועה והניקוי נעצרו.",
  operator: "המשימה מושהית; השטח שנוקה נשמר.",
  "resumed checkpoint": "הטעינה הושלמה וחזרנו לנקודת ההמשך.",
  "no safe route": "לא נמצא מסלול פנוי מספיק עבור גוף הרובוט.",
  "obstacle inside stopping margin": "מכשול קרוב מדי. התנועה נעצרה.",
  "battery / operator": "חזרה לעמדת הטעינה תוך שמירת נקודת ההמשך.",
  finished: "השטח הנגיש שנצפה נוקה. חוזרים לעמדה.",
};
let state,
  running = false,
  pending = false,
  generation = 0;
async function api(action, body = {}) {
  const r = await fetch("/api/" + action, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await r.json();
  if (!r.ok) throw Error(data.error);
  if (data.state) render(data);
  return data;
}
function clock(t) {
  return `${Math.floor(t / 60)
    .toString()
    .padStart(2, "0")}:${Math.floor(t % 60)
    .toString()
    .padStart(2, "0")}`;
}
const healthLabels = {unconfigured: "ממתין להגדרת מדיניות השוואה", insufficient_data: "עדיין אין מספיק משימות להשוואה", suspected_degradation: "נמצאה ירידה מתמשכת בזמן העבודה", needs_measurement: "נמצא שינוי שדורש בדיקה", no_decline_detected: "לא זוהתה ירידה במשימות שנבדקו"};
const frequencyLabels = {after_use: "אחרי כל שימוש", weekly: "פעם בשבוע", twice_weekly: "פעמיים בשבוע", fortnightly: "כל שבועיים", monthly: "פעם בחודש"};
function textNode(tag, content) { const node = document.createElement(tag); node.textContent = content; return node; }
function renderInsights(s) {
  const health = s.battery_health || {classification: "insufficient_data"};
  $("health-status").textContent = healthLabels[health.classification] || "ממתינים לנתונים";
  $("health-status").classList.toggle("warning", ["suspected_degradation", "needs_measurement"].includes(health.classification));
  $("health-detail").textContent = s.source === "simulation" || !s.source ? "נתונים מדומים אינם יוצרים אבחון או התראה על הסוללה האמיתית." :
    health.classification === "unconfigured" ? "נדרשים זיהוי סוללה ומדיניות השוואה לפני הסקת מסקנות." : "חזרה ידנית לעמדה, פערי תקשורת ופרופילים שונים נשמרים בנפרד מהשוואת זמן העבודה.";
  $("health-evidence").replaceChildren(...(health.comparisons || []).slice(-3).map(c => textNode("li",
    Number.isFinite(c.latest_runtime_s) ? `זמן עבודה אחרון: ${clock(c.latest_runtime_s)} · בסיס השוואה: ${clock(c.baseline_runtime_s)} · ${Math.round(c.latest_to_baseline_ratio * 100)}% מזמן הבסיס` : `${c.comparable_missions || 0} משימות מתאימות להשוואה`)));
  const missions = s.history?.missions || [];
  $("history").replaceChildren(...(missions.length ? missions.slice(0, 8).map(m => {
    const day = new Date(m.last_seen || m.first_seen);
    const date = Number.isNaN(day.getTime()) ? "" : day.toLocaleString("he-IL", {timeZone: "Asia/Jerusalem", day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit"});
    return textNode("li", `${date} · ${phases[m.state] || m.state || "דיווח משימה"}${m.coverage != null ? ` · כיסוי מוכר ${m.coverage}%` : ""}`);
  }) : [textNode("li", "עדיין אין משימות שמורות ממקור זה")]));
  const care = s.maintenance;
  if (!care) return;
  $("pets").checked = !!care.settings?.pets;
  $("usage").textContent = `זמן מברשות מתועד ${clock(care.usage?.brush_s || 0)} · ${care.usage?.gaps || 0} פערים בדיווח. לא כולל שימוש שלא תועד.`;
  $("parts").replaceChildren(...care.parts.map(part => {
    const row = document.createElement("div"); row.className = "part";
    row.append(textNode("strong", part.label));
    const content = document.createElement("div");
    content.append(textNode("p", `התקנה: ${part.installed || "לא ידוע"} · ניקוי אחרון: ${part.last_cleaned || "לא ידוע"}`));
    content.append(textNode("p", `ניקוי ${frequencyLabels[part.clean_frequency] || part.clean_frequency}${part.clean_due ? ` · הבא: ${part.clean_due}` : ""}`));
    content.append(textNode("p", part.replacement_window ? `חלון החלפה לפי גיל: ${[...new Set(part.replacement_window)].join(" עד ")}` : "מועד החלפה: אין תאריך התקנה או מרווח יצרן קבוע"));
    const reminder = part.snoozed ? `התזכורת נדחתה עד ${part.snoozed_until}` : part.replacement_review_due ? "הגיע מועד לבדוק החלפה לפי גיל החלק" : part.cleaning_due ? "הגיע מועד הניקוי" : "";
    if (reminder) { const note = textNode("p", reminder); note.className = "warning"; content.append(note); }
    if (part.note) content.append(textNode("p", part.note));
    row.append(content); return row;
  }));
  if (!$("service-part").options.length) $("service-part").replaceChildren(...care.parts.map(part => { const option = textNode("option", part.label); option.value = part.part; return option; }));
}
function render(s) {
  state = s;
  const simulated = (s.source || "simulation") === "simulation";
  const readonly = s.read_only || !simulated;
  $("simulation-controls").hidden = readonly;
  $("readonly-note").hidden = !readonly;
  $("truth").closest("label").hidden = !simulated;
  if (!simulated) $("truth").checked = false;
  $("source-tag").textContent = { simulation: "סימולציה בלבד", robot: "דיווחי הרובוט · צפייה בלבד", replay: "הקלטה · אינה בזמן אמת" }[s.source || "simulation"];
  $("battery-label").textContent = simulated ? "סוללה מדומה" : "סוללה מדווחת";
  $("map-note").textContent = simulated ? "ליבת XIAO ב־C++ · גאומטריה וטעינה מדומות" : "מפה מדיווחים · כיסוי אינו אישור לאיסוף לכלוך";
  const fresh = s.freshness || {};
  $("freshness").textContent = simulated ? "נתוני הסימולציה נשמרים בנפרד מנתוני הרובוט." :
    ({fresh: "דיווח עדכני", stale: "הדיווח ישן — מוצג המצב האחרון", disconnected: "החיבור נותק — מוצג המצב האחרון", waiting: "ממתינים לדיווח ראשון", replay: "שחזור הקלטה"}[fresh.status] || "דיווח מוקלט") +
    (Number.isFinite(fresh.age_s) ? ` · התקבל לפני ${Math.round(fresh.age_s)} שניות` : "") +
    (s.pose?.valid === false ? " · המיקום אינו מאומת" : "");
  $("freshness").classList.toggle("warning", !simulated && fresh.status !== "fresh");
  $("limits").textContent = simulated ? "הדמיית תנועה וחיישנים עם ליבת הניווט של XIAO. מידות המברשות והעמדה הן נתוני בדיקה, והטעינה מואצת. אין כאן מודל פיזיקלי לאבק או לזרימת אוויר." : "השרת אוסף דיווחים בלבד. תאים שלא נצפו אינם נחשבים נקיים. תנועה, טעינה ובטיחות מנוהלות מקומית ברובוט; ניתוק האתר אינו אישור לתנועה.";
  renderInsights(s);
  $("profile").value = s.profile;
  $("scenario").value = s.scenario;
  $("phase").textContent = phases[s.state] || "ממתינים לדיווח";
  $("coverage").innerHTML = `${s.coverage ?? "—"}<span>${s.coverage == null ? "" : "%"}</span>`;
  $("battery").innerHTML = `${s.battery ?? "—"}<span>${s.battery == null ? "" : "%"}</span>`;
  $("pass").textContent =
    `${s.pass > (s.base_passes || s.passes) ? "שטיח " : ""}${s.pass} / ${s.passes} ${s.pass_axis === "horizontal" ? "↔" : "↕"}`;
  $("clock").textContent = clock(s.time);
  $("stats").textContent = simulated ?
    `מרחק ${s.distance} מ׳ · התנגשויות ${s.collisions} · מברשות מרכזיות ${s.truth_coverage}% מהרצפה · כולל צד ${s.truth_combined}%` : `זמן ניקיון שדווח ${clock(s.progress?.reported_cleaning_s || 0)} · פערים בדיווח ${s.progress?.gaps || 0}`;
  $("reason").textContent =
    (s.dock_guard?.anchor_valid === false ? "מיקום העמדה אינו מאומת או זוהה מגע לא צפוי. התנועה עצרה ונדרשת בדיקה." : "") ||
    (s.state === "COMPLETE"
      ? `התוכנית הסתיימה. ${s.pending_main_m2} מ״ר מהרצפה המוכרת לא קיבלו מעבר מרכזי במעבר הזה; צהוב מציין מברשת צד בלבד.`
      : "") ||
    explanations[s.reason] ||
    s.reason ||
    (s.edge_incomplete
      ? `נותרו ${s.edge_incomplete} מעברי היקף שלא הושלמו; השטח נשאר גלוי כלא נקי.`
      : "") ||
    (s.state === "YIELDING"
      ? "ממתין עד 8 שניות לפינוי המעבר, ואז בודק עקיפה בטוחה. סימן × מציין מגע בפגוש."
      : "") ||
    "המפה נבנית מהסריקות. תאי מפה שלא נצפו אינם נחשבים פנויים.";
  $("run").textContent = running
    ? "השהיה"
    : s.state === "PAUSED" || s.state === "FAULT"
      ? "אישור והמשך"
      : s.state === "READY"
        ? "התחלת ניקיון"
        : "המשך הרצה";
  $("run").disabled = s.state === "COMPLETE";
  $("home").disabled = ![
    "CLEANING",
    "RESUMING",
    "EDGING",
    "TOUCHUP",
    "YIELDING",
  ].includes(s.state);
  $("profile").disabled = running;
  $("scenario").disabled = running;
  $("events").replaceChildren(
    ...(s.events || [])
      .slice()
      .reverse()
      .map((e) => {
        const li = document.createElement("li"),
          t = document.createElement("time");
        t.textContent = clock(e.time);
        li.append(
          t,
          document.createTextNode(
            e.message.startsWith("DIRT")
              ? "זוהה לכלוך · מעבר מקומי נוסף"
              : e.message.startsWith("PASS")
                ? "התחלת מעבר נוסף"
                : phases[e.message.split(":")[0]] || e.message,
          ),
        );
        return li;
      }),
  );
  for (const b of document.querySelectorAll("[data-fault]")) {
    const f = b.dataset.fault;
    if (f in (s.flags || {}))
      b.setAttribute(
        "aria-pressed",
        String(f === "cliff" ? s.flags[f] : !s.flags[f]),
      );
  }
  window.RooZones?.state(s);
  draw();
}
function draw() {
  if (!state) return;
  const s = state,
    c = $("map"),
    ctx = c.getContext("2d"),
    margin = 45,
    W = 870,
    H = 630,
    scale = Math.min(W / (s.cols * s.resolution), H / (s.rows * s.resolution));
  const ox = s.map_origin?.x || 0, oy = s.map_origin?.y || 0;
  const gridPoint = (x, y) => [margin + x * scale, c.height - margin - y * scale];
  const p = (x, y) => gridPoint(x - ox, y - oy);
  ctx.clearRect(0, 0, c.width, c.height);
  ctx.fillStyle = "#e4eae8";
  ctx.fillRect(0, 0, c.width, c.height);

  s.cells.forEach((v, i) => {
    const [x, y] = gridPoint(
      (i % s.cols) * s.resolution,
      (Math.floor(i / s.cols) + 1) * s.resolution,
    );
    ctx.fillStyle = v === 1 ? "#687f81" : v === 0 ? "#bdc9c7" : "#f8faf7";
    ctx.fillRect(x, y, scale * s.resolution + 0.3, scale * s.resolution + 0.3);
  });
  window.RooZones?.background(ctx, p, scale, s);
  function paintSamples(samples, color) {
    ctx.fillStyle = color;
    for (const index of samples || []) {
      const cell = Math.floor(index / 4),
        sub = index % 4;
      const x = ((cell % s.cols) + (sub & 1) / 2) * s.resolution;
      const y =
        (Math.floor(cell / s.cols) + (sub >> 1) / 2 + 0.5) * s.resolution;
      const point = gridPoint(x, y);
      ctx.fillRect(
        ...point,
        (scale * s.resolution) / 2 + 0.1,
        (scale * s.resolution) / 2 + 0.1,
      );
    }
  }
  if (!window.RooZones?.planning()) {
    paintSamples(s.side_covered, "#f2ce8c");
    paintSamples(s.fine_covered, "#b7ddc6");
  }
  window.RooZones?.draw(ctx, p, scale, s);
  ctx.strokeStyle = "#b4462b";
  ctx.lineWidth = 2;
  for (const cell of s.contacts || []) {
    const [x, y] = gridPoint(
      ((cell % s.cols) + 0.5) * s.resolution,
      (Math.floor(cell / s.cols) + 0.5) * s.resolution,
    );
    ctx.beginPath();
    ctx.moveTo(x - 4, y - 4);
    ctx.lineTo(x + 4, y + 4);
    ctx.moveTo(x + 4, y - 4);
    ctx.lineTo(x - 4, y + 4);
    ctx.stroke();
  }
  if ($("truth").checked) {
    ctx.strokeStyle = "#c76832";
    ctx.lineWidth = 2;
    ctx.setLineDash([5, 4]);
    for (const [a, b, d, e] of s.obstacles || []) {
      const [x, y] = p(a, e);
      ctx.strokeRect(x, y, (d - a) * scale, (e - b) * scale);
    }
    ctx.setLineDash([]);
  }
  function line(points, color, width, dash = []) {
    if (!points.length) return;
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.setLineDash(dash);
    points.forEach(([x, y], i) => {
      const q = p(x, y);
      i ? ctx.lineTo(...q) : ctx.moveTo(...q);
    });
    ctx.stroke();
    ctx.setLineDash([]);
  }
  if (!window.RooZones?.planning()) {
  for (const segment of s.trace_segments || [s.trace || []]) line(segment, "#7fbaa2", 2);
  if (s.pose && s.pose.valid !== false) line([[s.pose.x, s.pose.y], ...(s.path || [])], "#00a6ac", 3, [6, 4]);
  }
  if (s.dirt) {
    const [x, y] = p(...s.dirt);
    ctx.strokeStyle = "#dca658";
    ctx.setLineDash([3, 5]);
    ctx.beginPath();
    ctx.arc(x, y, s.dirt[2] * scale, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);
  }
  ctx.fillStyle = "#d98b4e55";
  for (const cell of s.excluded || []) {
    const [x, y] = gridPoint((cell % s.cols) * s.resolution, (Math.floor(cell / s.cols) + 1) * s.resolution);
    ctx.fillRect(x, y, scale * s.resolution, scale * s.resolution);
  }
  ctx.fillStyle = "#526d69";
  for (const [a,b,d,e] of s.dock_footprint || []) {
    const [x,y] = p(a,e); ctx.fillRect(x,y,(d-a)*scale,(e-b)*scale);
  }
  if (s.dock && s.dock.valid !== false) {
  const [dx, dy] = p(s.dock.x, s.dock.y);
  ctx.fillStyle = "#00786d";
  ctx.fillRect(dx - 14, dy - 14, 28, 28);
  ctx.fillStyle = "white";
  ctx.font = "20px Arial";
  ctx.textAlign = "center";
  ctx.fillText("ϟ", dx, dy + 7);
  }
  if (s.checkpoint) {
    const [x, y] = p(...s.checkpoint);
    ctx.strokeStyle = "#c77735";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(x, y, 9, 0, Math.PI * 2);
    ctx.stroke();
  }
  if (s.pose && Number.isFinite(s.pose.x)) {
  const [rx, ry] = p(s.pose.x, s.pose.y);
  ctx.save();
  ctx.globalAlpha = s.pose.valid === false ? 0.3 : 1;
  ctx.translate(rx, ry);
  ctx.rotate(-s.pose.yaw);
  ctx.shadowColor = "#203a4533";
  ctx.shadowBlur = 12;
  ctx.fillStyle = "#7753c3";
  ctx.beginPath();
  ctx.arc(0, 0, 0.17 * scale, 0, Math.PI * 2);
  ctx.fill();
  ctx.shadowBlur = 0;
  // Draw the actual reported configuration; older telemetry may omit geometry.
  const g = s.geometry || {};
  if ([g.brush_width, g.brush_depth, g.brush_offset].every(Number.isFinite)) {
    ctx.fillStyle = "#e7fff0";
    ctx.fillRect((g.brush_offset - g.brush_depth / 2) * scale,
      -g.brush_width / 2 * scale, g.brush_depth * scale, g.brush_width * scale);
  }
  if ([g.side_x, g.side_y, g.side_radius].every(Number.isFinite) && g.side_radius > 0) {
    ctx.strokeStyle = "#e6a33e";
    ctx.beginPath();
    ctx.arc(g.side_x * scale, -g.side_y * scale, g.side_radius * scale, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.strokeStyle = "white";
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.moveTo(0, 0);
  ctx.lineTo(0.15 * scale, 0);
  ctx.stroke();
  ctx.restore();
  }
  ctx.strokeStyle = "#203a45";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(c.width - margin - scale, c.height - 18);
  ctx.lineTo(c.width - margin, c.height - 18);
  ctx.stroke();
  ctx.fillStyle = "#203a45";
  ctx.font = "12px Arial";
  ctx.fillText("1 מטר", c.width - margin - scale / 2, c.height - 25);
}
async function loop() {
  if (!running || pending) return;
  pending = true;
  const g = generation;
  try {
    const s = await api("step", { count: Number($("speed").value) });
    if (
      g === generation &&
      ["FAULT", "COMPLETE", "PAUSED", "READY"].includes(s.state)
    ) {
      running = false;
      render(s);
    }
  } catch (e) {
    running = false;
    $("reason").textContent = e.message;
  } finally {
    pending = false;
    if (running) setTimeout(loop, 30);
  }
}
async function stopAndWait() {
  running = false;
  generation++;
  while (pending) await new Promise((r) => setTimeout(r, 25));
}
$("run").onclick = async () => {
  try {
    if (running) {
      await stopAndWait();
      await api("pause");
    } else {
      await api(
        state.state === "PAUSED" || state.state === "FAULT"
          ? "resume"
          : state.state === "READY"
            ? "start"
            : "step",
        { count: 1 },
      );
      running = !["FAULT", "PAUSED", "COMPLETE"].includes(state.state);
      render(state);
      loop();
    }
  } catch (e) {
    $("reason").textContent = e.message;
  }
};
async function reset() {
  await stopAndWait();
  await api("reset", {
    profile: $("profile").value,
    scenario: $("scenario").value,
  });
  $("profile-description").textContent =
    $("profile").value === "deep"
      ? "תנועה איטית, פקודת ניקוי גבוהה ושני מעברים."
      : "מעבר שיטתי אחד, עם השלמת קצוות בשטח שנצפה.";
}
$("reset").onclick = reset;
$("profile").onchange = reset;
$("scenario").onchange = reset;
$("home").onclick = () =>
  api("home").catch((e) => ($("reason").textContent = e.message));
$("truth").onchange = draw;
for (const b of document.querySelectorAll("[data-fault]"))
  b.onclick = () =>
    api("inject", { fault: b.dataset.fault }).catch(
      (e) => ($("reason").textContent = e.message),
    );
$("export").onclick = async () => {
  const data = await (await fetch("/api/checkpoint")).json();
  const url = URL.createObjectURL(
    new Blob([JSON.stringify(data, null, 2)], { type: "application/json" }),
  );
  const a = document.createElement("a");
  a.href = url;
  a.download = "roocore-mission.json";
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};
$("service-date").value = new Date().toLocaleDateString("sv-SE", {timeZone: "Asia/Jerusalem"});
$("service-form").onsubmit = async (e) => {
  e.preventDefault();
  try {
    await api("maintenance/event", {part: $("service-part").value, action: $("service-action").value, date: $("service-date").value});
    $("service-result").textContent = "הדיווח נשמר בהיסטוריה של מקור הנתונים הנוכחי.";
    await refreshState();
  } catch (error) { $("service-result").textContent = error.message; }
};
$("pets").onchange = async () => {
  try { await api("maintenance/settings", {pets: $("pets").checked}); await refreshState(); }
  catch (error) { $("service-result").textContent = error.message; }
};
async function refreshState() {
  try {
    const response = await fetch("/api/state", {cache: "no-store"});
    if (!response.ok) throw Error("לא ניתן לקבל דיווח מהשרת");
    render(await response.json());
  } catch (e) { $("freshness").textContent = "אין חיבור לשרת · " + e.message; $("freshness").classList.add("warning"); }
}
refreshState();
setInterval(() => { if (!running && !pending) refreshState(); }, 1000);
window.RooZonesBeforeEdit = async () => {
  running = false;
  generation++;
  if (state && !state.read_only && !["READY", "PAUSED", "FAULT", "COMPLETE"].includes(state.state))
    await api("pause");
};
