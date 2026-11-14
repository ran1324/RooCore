"use strict";
(() => {
  const el = (id) => document.getElementById(id),
    kinds = {
      room: "חדר",
      no_go: "אזור אסור",
      wall: "קיר וירטואלי",
      carpet: "שטיח",
    },
    colors = {
      room: "#245db0",
      no_go: "#b42318",
      wall: "#1f2937",
      carpet: "#997018",
    };
  let me,
    csrf = "",
    maps = [],
    map = null,
    dirty = false,
    drag = null,
    editing = null;
  async function api(path, body, method = "POST") {
    const r = await fetch(path, {
      method: body === undefined ? "GET" : method,
      headers:
        body === undefined
          ? {}
          : { "Content-Type": "application/json", "X-CSRF-Token": csrf },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    if (r.status === 401) {
      location.assign("/login");
      throw Error("יש להיכנס מחדש");
    }
    if (!r.ok) {
      if (r.status === 409)
        throw Error("המידע השתנה או שהשם כבר קיים. טענו מחדש לפני שמירה.");
      if (r.status === 403) throw Error("אין לחשבון הרשאה לפעולה הזאת.");
      if (r.status === 422)
        throw Error("בדקו את הפרטים, גבולות האזורים ותוקף המפה.");
      throw Error("הפעולה לא הושלמה. נסו שוב.");
    }
    return r.json();
  }
  const can = (robot, right) =>
    me.admin || (me.grants[robot] || []).includes(right);
  const option = (text, value) => {
    const n = document.createElement("option");
    n.textContent = text;
    n.value = value;
    return n;
  };
  function button(text, fn) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "button";
    b.textContent = text;
    b.onclick = fn;
    return b;
  }
  function message(id, text) {
    el(id).textContent = text;
  }
  function run(id, fn) {
    el(id).addEventListener("click", async () => {
      try {
        await fn();
      } catch (e) {
        message(
          id.startsWith("plan") ? "plan-message" : "map-notice",
          e.message,
        );
      }
    });
  }
  function grantsFields(parent, grants = {}) {
    const boxes = {};
    for (const [id, name] of [
      ["roomba-620", "Roomba 620"],
      ["sim-620", "סימולטור"],
    ]) {
      const field = document.createElement("fieldset");
      field.className = "grant-fields";
      const legend = document.createElement("legend");
      legend.textContent = name;
      field.append(legend);
      boxes[id] = {};
      for (const [right, title] of [
        ["view", "צפייה"],
        ["control", "תכנון הפעלה"],
        ["map_edit", "עריכת מפה"],
      ]) {
        const label = document.createElement("label"),
          input = document.createElement("input");
        input.type = "checkbox";
        input.checked = (grants[id] || []).includes(right);
        label.append(input, document.createTextNode(title));
        field.append(label);
        boxes[id][right] = input;
      }
      parent.append(field);
    }
    return () =>
      Object.fromEntries(
        Object.entries(boxes).map(([id, rights]) => [
          id,
          Object.entries(rights)
            .filter(([, input]) => input.checked)
            .map(([right]) => right),
        ]),
      );
  }
  let newGrants;
  function activation(url) {
    el("activation-link").href = url;
    el("activation-link").textContent =
      "קישור אישי לקביעת סיסמה (תקף לחצי שעה)";
    el("activation-link").hidden = false;
  }
  async function users() {
    if (!me.admin) return;
    const list = el("user-list");
    list.replaceChildren();
    for (const u of (await api("/api/users")).users) {
      const row = document.createElement("div");
      row.className = "user-row";
      const title = document.createElement("strong");
      title.textContent = `${u.name} (${u.username}) · ${u.enabled ? "פעיל" : "גישה מבוטלת"}${u.admin ? " · מנהל המערכת" : ""}`;
      row.append(title);
      if (!u.admin) {
        const readGrants = grantsFields(row, u.grants);
        const enabled = document.createElement("input");
        enabled.type = "checkbox";
        enabled.checked = u.enabled;
        const label = document.createElement("label");
        label.append(enabled, document.createTextNode("גישה פעילה"));
        row.append(
          label,
          button("שמירת הרשאות", async () => {
            try {
              await api(
                "/api/users/" + u.id,
                { enabled: enabled.checked, grants: readGrants() },
                "PATCH",
              );
              message(
                "user-message",
                "ההרשאות נשמרו. חיבורים קיימים של המשתמש נסגרו.",
              );
              await users();
            } catch (e) {
              message("user-message", e.message);
            }
          }),
          button("קישור חדש לקביעת סיסמה", async () => {
            try {
              activation(
                (await api("/api/users/" + u.id + "/invite", {}))
                  .activation_url,
              );
            } catch (e) {
              message("user-message", e.message);
            }
          }),
        );
      }
      list.append(row);
    }
  }
  function mapBody(m) {
    return {
      name: el("map-name").value || m.name,
      revision: m.revision,
      geometry: m.geometry,
      cells: m.cells,
      zones: m.zones,
      source: m.source,
      source_map_id: m.source_map_id || null,
    };
  }
  async function loadMaps() {
    maps = (await api("/api/robots/" + el("map-robot").value + "/maps")).maps;
    el("map-select").replaceChildren(
      option("בחרו מפה", ""),
      ...maps.map((m) => option(m.name, m.id)),
    );
    map = null;
    dirty = false;
    renderMap();
    el("map-new").disabled = el("map-import").disabled = !can(
      el("map-robot").value,
      "map_edit",
    );
    await plans();
  }
  async function plans() {
    const p = (await api("/api/robots/" + el("map-robot").value + "/plans"))
      .plans;
    el("plan-list").replaceChildren();
    for (const item of p) {
      const n = document.createElement("p");
      n.textContent = `טיוטה: ${item.action === "clean" ? "ניקוי חדר" : "הגעה לנקודה"} · גרסת מפה ${item.revision} · לא נשלחה לרובוט`;
      el("plan-list").append(n);
    }
  }
  function changed() {
    dirty = true;
    renderMap();
  }
  function renderMap() {
    el("map-workspace").hidden = !map;
    el("map-empty").hidden = maps.length > 0 || !!map;
    if (!map) return;
    el("map-name").value = map.name;
    el("map-revision").textContent =
      `${map.source === "planning" ? "מפת תכנון" : map.source === "simulation" ? "סימולציה" : "מפה מיובאת, לא מאומתת"} · גרסה ${map.revision}${dirty ? " · לא נשמר" : ""}`;
    const edit = can(el("map-robot").value, "map_edit");
    for (const id of [
      "map-name",
      "map-save",
      "zone-name",
      "zone-kind",
      "zone-x1",
      "zone-y1",
      "zone-x2",
      "zone-y2",
      "zone-add",
    ])
      el(id).disabled = !edit;
    el("plan-clean").disabled = el("plan-goto").disabled =
      !can(el("map-robot").value, "control") || dirty || !map.id;
    const list = el("zone-list");
    list.replaceChildren();
    el("plan-room").replaceChildren();
    for (const z of map.zones) {
      const row = document.createElement("div");
      row.className = "zone-row";
      const text = document.createElement("span");
      text.textContent = `${z.name} · ${kinds[z.kind]}`;
      row.append(text);
      if (edit)
        row.append(
          button("עריכה", () => {
            editing = z.id;
            el("zone-name").value = z.name;
            el("zone-kind").value = z.kind;
            for (const k of ["x1", "y1", "x2", "y2"])
              el("zone-" + k).value = z[k];
            el("zone-add").textContent = "עדכון האזור";
            el("zone-name").focus();
          }),
          button("מחיקה", () => {
            map.zones = map.zones.filter((item) => item.id !== z.id);
            changed();
          }),
        );
      list.append(row);
      if (z.kind === "room") el("plan-room").append(option(z.name, z.id));
    }
    draw();
  }
  function world(event) {
    const c = el("map-canvas"),
      rect = c.getBoundingClientRect(),
      g = map.geometry;
    return {
      x:
        g.origin_x +
        ((event.clientX - rect.left) / rect.width) * g.cols * g.resolution,
      y:
        g.origin_y +
        (1 - (event.clientY - rect.top) / rect.height) * g.rows * g.resolution,
    };
  }
  function draw() {
    if (!map) return;
    const c = el("map-canvas"),
      ctx = c.getContext("2d"),
      g = map.geometry,
      w = g.cols * g.resolution,
      h = g.rows * g.resolution;
    ctx.clearRect(0, 0, c.width, c.height);
    const px = (x) => ((x - g.origin_x) / w) * c.width,
      py = (y) => c.height - ((y - g.origin_y) / h) * c.height;
    map.cells.forEach((v, i) => {
      ctx.fillStyle = v === 1 ? "#58616d" : v === 0 ? "#dce1e8" : "#f8fafc";
      ctx.fillRect(
        ((i % g.cols) / g.cols) * c.width,
        ((g.rows - 1 - Math.floor(i / g.cols)) / g.rows) * c.height,
        c.width / g.cols + 0.5,
        c.height / g.rows + 0.5,
      );
    });
    ctx.strokeStyle = "#d9dee5";
    ctx.lineWidth = 1;
    for (let x = 0; x <= g.cols; x++) {
      ctx.beginPath();
      ctx.moveTo((x / g.cols) * c.width, 0);
      ctx.lineTo((x / g.cols) * c.width, c.height);
      ctx.stroke();
    }
    for (let y = 0; y <= g.rows; y++) {
      ctx.beginPath();
      ctx.moveTo(0, (y / g.rows) * c.height);
      ctx.lineTo(c.width, (y / g.rows) * c.height);
      ctx.stroke();
    }
    for (const z of map.zones) {
      ctx.strokeStyle = colors[z.kind];
      ctx.lineWidth = 3;
      ctx.beginPath();
      if (z.kind === "wall") {
        ctx.moveTo(px(z.x1), py(z.y1));
        ctx.lineTo(px(z.x2), py(z.y2));
        ctx.stroke();
      } else {
        ctx.fillStyle = colors[z.kind] + "22";
        ctx.fillRect(
          px(z.x1),
          py(z.y2),
          px(z.x2) - px(z.x1),
          py(z.y1) - py(z.y2),
        );
        ctx.strokeRect(
          px(z.x1),
          py(z.y2),
          px(z.x2) - px(z.x1),
          py(z.y1) - py(z.y2),
        );
      }
      ctx.font = "16px sans-serif";
      ctx.fillStyle = colors[z.kind];
      ctx.textAlign = "right";
      ctx.fillText(z.name, px(z.x2) - 4, py(z.y2) + 20);
    }
  }
  function addZone() {
    if (!map || !can(el("map-robot").value, "map_edit")) return;
    const previous = map.zones.find((z) => z.id === editing);
    const z = {
      ...previous,
      id: previous?.id || crypto.randomUUID(),
      name: el("zone-name").value.trim(),
      kind: el("zone-kind").value,
    };
    for (const k of ["x1", "y1", "x2", "y2"])
      z[k] = Number(el("zone-" + k).value);
    const g = map.geometry;
    if (
      !z.name ||
      !Object.values(z).every(
        (v) => typeof v !== "number" || Number.isFinite(v),
      ) ||
      [z.x1, z.x2].some(
        (x) => x < g.origin_x || x > g.origin_x + g.cols * g.resolution,
      ) ||
      [z.y1, z.y2].some(
        (y) => y < g.origin_y || y > g.origin_y + g.rows * g.resolution,
      ) ||
      (z.kind !== "wall" && (z.x2 <= z.x1 || z.y2 <= z.y1)) ||
      (z.kind === "wall" && Math.hypot(z.x2 - z.x1, z.y2 - z.y1) <= 0.001) ||
      (!previous && map.zones.length >= 32)
    )
      throw Error("יש לבחור שם ואזור בתוך גבולות המפה, עד 32 אזורים.");
    if (previous)
      map.zones = map.zones.map((item) => (item.id === z.id ? z : item));
    else map.zones.push(z);
    editing = null;
    el("zone-add").textContent = "הוספת אזור";
    changed();
  }
  async function saveMap() {
    if (!map) return;
    const path =
      "/api/robots/" +
      el("map-robot").value +
      "/maps" +
      (map.id ? "/" + map.id : "");
    map = await api(path, mapBody(map), map.id ? "PUT" : "POST");
    dirty = false;
    maps = (await api("/api/robots/" + el("map-robot").value + "/maps")).maps;
    el("map-select").replaceChildren(...maps.map((m) => option(m.name, m.id)));
    el("map-select").value = map.id;
    renderMap();
    message("map-notice", "המפה נשמרה בענן. טרם נשלחה או אושרה בבקר.");
  }
  async function plan(action) {
    const body = { action, map_id: map.id, revision: map.revision };
    if (action === "clean") body.zone_id = el("plan-room").value;
    else
      body.target = [
        Number(el("target-x").value),
        Number(el("target-y").value),
      ];
    await api("/api/robots/" + el("map-robot").value + "/plans", body);
    message(
      "plan-message",
      "התכנון נשמר כטיוטה. הפעלה פיזית עדיין אינה זמינה.",
    );
    await plans();
  }
  el("map-canvas").addEventListener("pointerdown", (e) => {
    if (!map) return;
    drag = world(e);
    el("target-x").value = drag.x.toFixed(2);
    el("target-y").value = drag.y.toFixed(2);
    e.target.setPointerCapture(e.pointerId);
  });
  el("map-canvas").addEventListener("pointerup", (e) => {
    if (!drag || !map) return;
    const end = world(e);
    if (
      can(el("map-robot").value, "map_edit") &&
      Math.hypot(end.x - drag.x, end.y - drag.y) > 0.05
    ) {
      for (const [key, value] of Object.entries(
        el("zone-kind").value === "wall"
          ? { x1: drag.x, x2: end.x, y1: drag.y, y2: end.y }
          : {
              x1: Math.min(drag.x, end.x),
              x2: Math.max(drag.x, end.x),
              y1: Math.min(drag.y, end.y),
              y2: Math.max(drag.y, end.y),
            },
      ))
        el("zone-" + key).value = value.toFixed(2);
      editing = null;
      try {
        addZone();
      } catch (e) {
        message("map-notice", e.message);
      }
    }
    drag = null;
  });
  el("map-select").onchange = () => {
    if (dirty && !confirm("לעבור למפה אחרת ולוותר על השינויים שלא נשמרו?"))
      return;
    map = structuredClone(
      maps.find((m) => m.id === el("map-select").value) || null,
    );
    dirty = false;
    renderMap();
  };
  el("map-robot").onchange = () => {
    if (dirty && !confirm("להחליף רובוט ולוותר על השינויים שלא נשמרו?")) return;
    loadMaps().catch((e) => message("map-notice", e.message));
  };
  el("map-name").oninput = () => {
    if (map) {
      map.name = el("map-name").value;
      dirty = true;
      el("map-revision").textContent = "שינויים שלא נשמרו";
      el("plan-clean").disabled = el("plan-goto").disabled = true;
    }
  };
  run("map-new", () => {
    if (dirty && !confirm("ליצור מפה חדשה ולוותר על השינויים?")) return;
    map = {
      name: "תכנון הבית",
      revision: 0,
      geometry: {
        cols: 40,
        rows: 30,
        resolution: 0.25,
        origin_x: 0,
        origin_y: 0,
      },
      cells: [],
      zones: [],
      source: "planning",
    };
    dirty = true;
    renderMap();
    message(
      "map-notice",
      "מפת תכנון 10 × 7.5 מטר. אינה מדידה של הבית. אפשר לייבא מפה שנמדדה.",
    );
  });
  run("map-save", saveMap);
  run("zone-add", addZone);
  run("plan-clean", () => plan("clean"));
  run("plan-goto", () => plan("goto"));
  el("map-import").onchange = async (e) => {
    try {
      const file = e.target.files[0];
      if (!file) return;
      if (file.size > 1024 * 1024) throw Error("גודל הקובץ המרבי הוא 1MB.");
      const doc = JSON.parse(await file.text());
      const robot = el("map-robot").value;
      if (
        (doc.robot_id && doc.robot_id !== robot) ||
        (doc.key?.robot_id && doc.key.robot_id !== robot)
      )
        throw Error("זהות הרובוט בקובץ אינה תואמת לרובוט שנבחר.");
      const g = doc.geometry || doc.key?.geometry;
      if (!g || !Array.isArray(doc.zones))
        throw Error("קובץ המפה אינו במבנה נתמך.");
      map = await api("/api/robots/" + robot + "/maps", {
        name: doc.name || "מפה מיובאת",
        revision: 0,
        geometry: g,
        cells: doc.cells || [],
        zones: doc.zones,
        source_map_id: doc.source_map_id || doc.key?.map_id || doc.id || null,
        source:
          doc.source === "simulation" || doc.key?.source === "simulation"
            ? "simulation"
            : "hardware_import",
      });
      dirty = false;
      maps.push(map);
      el("map-select").append(option(map.name, map.id));
      el("map-select").value = map.id;
      renderMap();
      message(
        "map-notice",
        "המפה יובאה ונשמרה בענן. היא עדיין לא מאומתת או פעילה בבקר.",
      );
    } catch (e) {
      message("map-notice", e.message);
    } finally {
      e.target.value = "";
    }
  };
  run("map-export", () => {
    if (!map) return;
    const url = URL.createObjectURL(
      new Blob([JSON.stringify(map, null, 2)], { type: "application/json" }),
    );
    const a = document.createElement("a");
    a.href = url;
    a.download = "roocore-map.json";
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  el("user-form").onsubmit = async (e) => {
    e.preventDefault();
    const b = e.target.querySelector("button");
    b.disabled = true;
    try {
      const r = await api("/api/users", {
        username: el("member-username").value,
        name: el("member-name").value,
        email: el("member-email").value,
        grants: newGrants(),
      });
      activation(r.activation_url);
      message(
        "user-message",
        "החשבון נוצר. אפשר לקבוע סיסמה דרך הקישור האישי.",
      );
      await users();
      e.target.reset();
    } catch (e) {
      message("user-message", e.message);
    } finally {
      b.disabled = false;
    }
  };
  el("logout").onclick = async () => {
    try {
      await api("/api/auth/logout", {});
      location.assign("/login");
    } catch (e) {
      alert(e.message);
    }
  };
  el("password-form").onsubmit = async (e) => {
    e.preventDefault();
    try {
      await api("/api/auth/password", {
        current_password: el("current-password").value,
        password: el("change-password").value,
      });
      location.assign("/login");
    } catch (e) {
      message("password-message", e.message);
    }
  };
  addEventListener("beforeunload", (e) => {
    if (dirty) {
      e.preventDefault();
      e.returnValue = "";
    }
  });
  (async () => {
    const result = await api("/api/me");
    me = result.user;
    csrf = result.csrf;
    el("signed-user").textContent = me.name;
    el("users-nav").hidden = !me.admin;
    newGrants = grantsFields(el("member-grants"));
    for (const [id, title] of [
      ["roomba-620", "Roomba 620"],
      ["sim-620", "סימולטור"],
    ])
      if (can(id, "view")) el("map-robot").append(option(title, id));
    if (el("map-robot").value) await loadMaps();
    else message("map-notice", "טרם ניתנה לחשבון הרשאת גישה לרובוט.");
    await users();
  })().catch((e) => message("map-notice", e.message));
})();
