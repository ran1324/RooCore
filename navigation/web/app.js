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
  COMPLETE: "הניקיון הושלם",
};
const explanations = {
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
  render(data);
  return data;
}
function clock(t) {
  return `${Math.floor(t / 60)
    .toString()
    .padStart(2, "0")}:${Math.floor(t % 60)
    .toString()
    .padStart(2, "0")}`;
}
function render(s) {
  state = s;
  $("phase").textContent = phases[s.state];
  $("coverage").innerHTML = `${s.coverage}<span>%</span>`;
  $("battery").innerHTML = `${s.battery}<span>%</span>`;
  $("pass").textContent = `${s.pass} / ${s.passes}`;
  $("clock").textContent = clock(s.time);
  $("stats").textContent =
    `מרחק ${s.distance} מ׳ · התנגשויות ${s.collisions} · כיסוי אמת בסימולטור ${s.truth_coverage}%`;
  $("reason").textContent =
    explanations[s.reason] ||
    s.reason ||
    "המפה נבנית מהסריקות. תאי מפה שלא נצפו אינם נחשבים פנויים.";
  $("run").textContent = running
    ? "השהיה"
    : s.state === "PAUSED" || s.state === "FAULT"
      ? "אישור והמשך"
      : "התחלת ניקיון";
  $("run").disabled = s.state === "COMPLETE";
  $("home").disabled = !["CLEANING", "RESUMING"].includes(s.state);
  $("profile").disabled = running;
  $("scenario").disabled = running;
  $("events").replaceChildren(
    ...s.events
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
    if (f in s.flags)
      b.setAttribute(
        "aria-pressed",
        String(f === "cliff" ? s.flags[f] : !s.flags[f]),
      );
  }
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
  const p = (x, y) => [margin + x * scale, c.height - margin - y * scale];
  ctx.clearRect(0, 0, c.width, c.height);
  ctx.fillStyle = "#e4eae8";
  ctx.fillRect(0, 0, c.width, c.height);
  const covered = new Set(s.covered);
  s.cells.forEach((v, i) => {
    const [x, y] = p(
      (i % s.cols) * s.resolution,
      (Math.floor(i / s.cols) + 1) * s.resolution,
    );
    ctx.fillStyle =
      v === 1
        ? "#687f81"
        : v === 0
          ? "#bdc9c7"
          : covered.has(i)
            ? "#b7ddc6"
            : "#f8faf7";
    ctx.fillRect(x, y, scale * s.resolution + 0.3, scale * s.resolution + 0.3);
  });
  if ($("truth").checked) {
    ctx.strokeStyle = "#c76832";
    ctx.lineWidth = 2;
    ctx.setLineDash([5, 4]);
    for (const [a, b, d, e] of s.obstacles) {
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
  line(s.trace, "#7fbaa2", 2);
  line([[s.pose.x, s.pose.y], ...s.path], "#00a6ac", 3, [6, 4]);
  if (s.dirt) {
    const [x, y] = p(...s.dirt);
    ctx.strokeStyle = "#dca658";
    ctx.setLineDash([3, 5]);
    ctx.beginPath();
    ctx.arc(x, y, s.dirt[2] * scale, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);
  }
  const [dx, dy] = p(s.dock.x, s.dock.y);
  ctx.fillStyle = "#00786d";
  ctx.fillRect(dx - 14, dy - 14, 28, 28);
  ctx.fillStyle = "white";
  ctx.font = "20px Arial";
  ctx.textAlign = "center";
  ctx.fillText("ϟ", dx, dy + 7);
  if (s.checkpoint) {
    const [x, y] = p(...s.checkpoint);
    ctx.strokeStyle = "#c77735";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(x, y, 9, 0, Math.PI * 2);
    ctx.stroke();
  }
  const [rx, ry] = p(s.pose.x, s.pose.y);
  ctx.save();
  ctx.translate(rx, ry);
  ctx.rotate(-s.pose.yaw);
  ctx.shadowColor = "#203a4533";
  ctx.shadowBlur = 12;
  ctx.fillStyle = "#7753c3";
  ctx.beginPath();
  ctx.arc(0, 0, 0.18 * scale, 0, Math.PI * 2);
  ctx.fill();
  ctx.shadowBlur = 0;
  ctx.strokeStyle = "white";
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.moveTo(0, 0);
  ctx.lineTo(0.15 * scale, 0);
  ctx.stroke();
  ctx.restore();
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
          : "start",
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
      : "תנועה רגילה וניקוי נוסף באזורים מלוכלכים.";
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
fetch("/api/state")
  .then((r) => r.json())
  .then(render)
  .catch((e) => ($("reason").textContent = e.message));
