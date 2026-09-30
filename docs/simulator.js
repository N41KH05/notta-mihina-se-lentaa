// The firmware build (simulator/build.py writes it next to this file).
const WASM_URL = "sim.wasm?v=12";

// Optional: a CORS proxy for the live data (see proxy/README.md). It is called as
// PROXY?url=<encoded service URL>. Can also be given as ?proxy=... in the page address.
const LIVE_PROXY = "";

(async function () {
  const statusEl = document.getElementById("status");
  const canvas = document.getElementById("screen");
  const ctx = canvas.getContext("2d");
  const fail = (msg) => { statusEl.hidden = false; statusEl.textContent = msg; };

  if (!("WebAssembly" in window)) {
    fail("This browser is too old to run the simulator. Try a current Chrome, Edge, Firefox or Safari.");
    return;
  }

  // Load the firmware build.
  let bytes;
  try {
    const res = await fetch(WASM_URL);
    if (!res.ok) throw new Error("HTTP " + res.status);
    bytes = await res.arrayBuffer();
  } catch (e) {
    fail("Couldn't load the simulator (" + e.message + "). If you opened the file straight from disk, " +
         "serve the folder instead: python -m http.server -d docs");
    return;
  }

  // The few system calls the C code expects; output just goes to the console.
  let memory;
  const wasi = {
    fd_write(fd, iovs, iovsLen, nwritten) {
      const v = new DataView(memory.buffer);
      let n = 0, text = "";
      for (let i = 0; i < iovsLen; i++) {
        const p = v.getUint32(iovs + i * 8, true), l = v.getUint32(iovs + i * 8 + 4, true);
        text += new TextDecoder().decode(new Uint8Array(memory.buffer, p, l));
        n += l;
      }
      if (text.trim()) console.log(text.trim());
      v.setUint32(nwritten, n, true);
      return 0;
    },
    fd_close() { return 0; },
    fd_seek() { return 0; },
  };

  let ex;
  try {
    const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi });
    ex = instance.exports;
    memory = ex.memory;
    if (ex._initialize) ex._initialize();
  } catch (e) {
    fail("The simulator failed to start (" + e.message + ").");
    return;
  }

  const t0 = performance.now();
  const now = () => Math.floor(performance.now() - t0) + 1000;
  ex.sim_init(now());
  statusEl.hidden = true;

  // RGB565 -> RGBA lookup table (little-endian canvas pixels).
  const lut = new Uint32Array(65536);
  for (let v = 0; v < 65536; v++) {
    const r = ((v >> 11) & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = (v & 31) * 255 / 31;
    lut[v] = 0xff000000 | (Math.round(b) << 16) | (Math.round(g) << 8) | Math.round(r);
  }
  const image = ctx.createImageData(800, 480);
  const out = new Uint32Array(image.data.buffer);

  // Pointers (mouse or fingers) -> screen pixels, fed to the firmware's gesture code.
  const pointers = new Map();
  const toScreen = (e) => {
    const r = canvas.getBoundingClientRect();
    return [Math.round((e.clientX - r.left) * 800 / r.width), Math.round((e.clientY - r.top) * 480 / r.height)];
  };
  canvas.addEventListener("pointerdown", (e) => { canvas.setPointerCapture(e.pointerId); pointers.set(e.pointerId, toScreen(e)); e.preventDefault(); });
  canvas.addEventListener("pointermove", (e) => { if (pointers.has(e.pointerId) && !lifted.has(e.pointerId)) pointers.set(e.pointerId, toScreen(e)); });
  // A lifted finger is removed only after the firmware has seen it at least once,
  // so very quick taps (a mouse click, a trackpad tap) still count.
  const lifted = new Set();
  const up = (e) => { if (pointers.has(e.pointerId)) lifted.add(e.pointerId); };
  canvas.addEventListener("pointerup", up);
  canvas.addEventListener("pointercancel", up);
  let lastWheel = 0;
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    const t = performance.now();
    if (t - lastWheel < 180 || Math.abs(e.deltaY) < 2) return;
    lastWheel = t;
    ex.sim_zoom(e.deltaY < 0 ? 1 : -1);
  }, { passive: false });
  canvas.addEventListener("keydown", (e) => {
    if (e.key === "+" || e.key === "=") ex.sim_zoom(1);
    else if (e.key === "-") ex.sim_zoom(-1);
  });

  document.getElementById("firstRun").addEventListener("click", () => { ex.sim_first_run(); canvas.focus(); });

  // ---- Live data ------------------------------------------------------------------------
  const params = new URLSearchParams(location.search);
  const PROXY = params.get("proxy") || LIVE_PROXY;
  const SOURCES = [
    { name: "adsb.lol", url: (la, lo, r) => `https://api.adsb.lol/v2/lat/${la}/lon/${lo}/dist/${r}` },
    { name: "adsb.fi", url: (la, lo, r) => `https://opendata.adsb.fi/api/v3/lat/${la}/lon/${lo}/dist/${r}` },
    { name: "airplanes.live", url: (la, lo, r) => `https://api.airplanes.live/v2/point/${la}/${lo}/${r}` },
  ];
  const via = (u) => PROXY ? PROXY + (PROXY.includes("?") ? "&" : "?") + "url=" + encodeURIComponent(u) : u;
  async function getText(url, ms = 8000) {
    const c = new AbortController();
    const timer = setTimeout(() => c.abort(), ms);
    try {
      const r = await fetch(via(url), { signal: c.signal, cache: "no-store" });
      if (!r.ok) { const e = new Error("HTTP " + r.status); e.status = r.status; throw e; }
      return await r.text();
    } finally { clearTimeout(timer); }
  }
  const enc = new TextEncoder(), dec = new TextDecoder();
  // Write strings into the firmware's text buffer, NUL-separated. Returns their offsets.
  function put(...parts) {
    const bytes = parts.map((p) => enc.encode(p));
    const total = bytes.reduce((n, b) => n + b.length + 1, 0);
    const ptr = ex.sim_buf(total);
    const mem = new Uint8Array(memory.buffer);
    const offs = [];
    let o = 0;
    for (const b of bytes) { offs.push(o); mem.set(b, ptr + o); mem[ptr + o + b.length] = 0; o += b.length + 1; }
    return { offs, lens: bytes.map((b) => b.length) };
  }
  const cstr = (p) => { const m = new Uint8Array(memory.buffer); let e = p; while (m[e]) e++; return dec.decode(m.subarray(p, e)); };

  const modeLabel = document.getElementById("modeLabel"), liveNote = document.getElementById("liveNote");
  const btnLive = document.getElementById("modeLive"), btnSim = document.getElementById("modeSim");
  let liveOn = false, busy = false, everOk = false, failRounds = 0, srcIdx = 0, lastFetch = -1e9, lastKey = "";
  function setLive(on, note) {
    liveOn = on;
    ex.sim_live(on ? 1 : 0);
    btnLive.setAttribute("aria-pressed", on); btnSim.setAttribute("aria-pressed", !on);
    modeLabel.textContent = on ? "LIVE DATA" : "SIMULATED TRAFFIC";
    liveNote.textContent = note || (on ? "Real aircraft from public ADS-B feeds, fetched by your browser." : "Made-up traffic around home, as in the board's demo mode.");
    failRounds = 0; everOk = false; lastFetch = -1e9;
    if (on) poll();
  }
  btnLive.addEventListener("click", () => setLive(true));
  btnSim.addEventListener("click", () => setLive(false));

  async function poll() {
    if (!liveOn || busy || document.hidden) return;
    busy = true;
    lastFetch = performance.now();
    const lat = ex.sim_query_lat().toFixed(4), lon = ex.sim_query_lon().toFixed(4), r = ex.sim_query_radius();
    lastKey = lat + lon + r;
    const errors = [];
    let ok = false;
    for (let i = 0; i < SOURCES.length && !ok && liveOn; i++) {
      const k = (srcIdx + i) % SOURCES.length, src = SOURCES[k];
      try {
        const text = await getText(src.url(lat, lon, r));
        if (!liveOn) break;
        const { offs, lens } = put(text, src.name);
        if (ex.sim_live_planes(lens[0], offs[1]) >= 0) { ok = true; srcIdx = k; }
        else errors.push(src.name + ": virheellinen vastaus");
      } catch (e) {
        errors.push(src.name + ": " + (e.name === "AbortError" ? "ei vastausta" : e.status ? "HTTP " + e.status : "estetty selaimessa"));
      }
    }
    if (liveOn) {
      if (ok) {
        everOk = true; failRounds = 0;
        modeLabel.textContent = "LIVE DATA · " + SOURCES[srcIdx].name.toUpperCase();
      } else {
        put(errors.join("; ")); ex.sim_live_error();
        if (++failRounds >= 1 && !everOk)          // never worked here: show simulated traffic right away
          setLive(false, PROXY ? "The live-data proxy isn't answering, so simulated traffic is shown."
                               : "The flight-data services don't allow direct browser access, so simulated traffic is shown.");
      }
    }
    busy = false;
  }
  setInterval(poll, 4000);
  document.addEventListener("visibilitychange", () => { if (!document.hidden) poll(); });

  // Routes, photos and address searches the firmware asks for.
  let routeBusy = false, photoBusy = "", searchBusy = false;
  async function serviceRequests() {
    if (!liveOn) return;
    const rp = ex.sim_route_pending();
    if (rp && !routeBusy) {
      routeBusy = true;
      const cs = cstr(rp);
      let len = -1, text = "";
      try { text = await getText("https://api.adsbdb.com/v0/callsign/" + encodeURIComponent(cs)); len = 1; } catch (e) {}
      const { offs, lens } = put(len > 0 ? text : "", cs);
      const bufPtr = ex.sim_buf(0);
      ex.sim_route_result(bufPtr + offs[1], len > 0 ? lens[0] : -1);
      routeBusy = false;
    }
    const hp = ex.sim_photo_wanted();
    if (hp && !photoBusy) {
      const hex = cstr(hp), reg = cstr(ex.sim_photo_reg());
      photoBusy = hex;
      await fetchPhoto(hex, reg);
      photoBusy = "";
    }
    const sq = ex.sim_search_wanted();
    if (sq && !searchBusy) {
      searchBusy = true;
      const q = cstr(sq);
      try {
        const text = await getText("https://nominatim.openstreetmap.org/search?format=jsonv2&limit=6&accept-language=fi&q=" + encodeURIComponent(q));
        const { lens } = put(text);
        ex.sim_search_result(lens[0]);
      } catch (e) { ex.sim_search_result(-1); }
      searchBusy = false;
    }
  }
  setInterval(serviceRequests, 300);

  const PHOTO_W = 272, PHOTO_H = 122;
  const photoCanvas = document.createElement("canvas");
  photoCanvas.width = PHOTO_W; photoCanvas.height = PHOTO_H;
  const pctx = photoCanvas.getContext("2d", { willReadFrequently: true });
  async function fetchPhoto(hex, reg) {
    const done = (state, who = "", link = "") => { const { offs } = put(hex, who, link); ex.sim_photo_done(state, offs[1], offs[2]); };
    try {
      let info = JSON.parse(await getText("https://api.planespotters.net/pub/photos/hex/" + hex));
      if (!(info.photos && info.photos.length) && reg)
        info = JSON.parse(await getText("https://api.planespotters.net/pub/photos/reg/" + encodeURIComponent(reg)));
      const ph = info.photos && info.photos[0];
      if (!ph || !ph.thumbnail_large) { done(3); return; }                 // no photo of this aircraft
      const img = new Image();
      img.crossOrigin = "anonymous";
      await new Promise((ok, bad) => { img.onload = ok; img.onerror = bad; img.src = via(ph.thumbnail_large.src); });
      // Fill the card, cropping the edges (like the board does).
      const s = Math.max(PHOTO_W / img.naturalWidth, PHOTO_H / img.naturalHeight);
      const w = img.naturalWidth * s, h = img.naturalHeight * s;
      pctx.drawImage(img, (PHOTO_W - w) / 2, (PHOTO_H - h) / 2, w, h);
      const d = pctx.getImageData(0, 0, PHOTO_W, PHOTO_H).data;           // throws if the image isn't shareable
      const px = new Uint16Array(memory.buffer, ex.sim_photo_pixels(), PHOTO_W * PHOTO_H);
      for (let i = 0; i < px.length; i++)
        px[i] = ((d[i * 4] >> 3) << 11) | ((d[i * 4 + 1] >> 2) << 5) | (d[i * 4 + 2] >> 3);
      let link = (ph.link || "").split("?")[0];
      if (link.length > 78) { const m = link.match(/^https:\/\/www\.planespotters\.net\/photo\/\d+/); if (m) link = m[0]; }
      done(2, ph.photographer || "", link);
    } catch (e) {
      done(0);                                                             // not reachable: no card
    }
  }

  // Live data needs a proxy: the public flight-data services don't allow direct requests
  // from web pages. Without one, the demo shows simulated traffic.
  if (PROXY) {
    document.getElementById("modes").hidden = false;
    setLive(!params.has("demo"));
  } else {
    setLive(false, "Simulated traffic around home, as in the board's demo mode.");
  }

  let lastDraw = 0, lastCheck = 0;
  function tick(t) {
    const pts = [...pointers.values()];
    const [a = [0, 0], b = [0, 0]] = pts;
    ex.sim_touch(Math.min(pts.length, 2), a[0], a[1], b[0], b[1], now());
    for (const id of lifted) pointers.delete(id);
    lifted.clear();
    // Redraw 5 times a second like the board, or every frame while touching.
    if (pts.length || t - lastDraw > 190) {
      lastDraw = t;
      const d = new Date();
      const ptr = ex.sim_frame(now(), d.getHours(), d.getMinutes(), d.getSeconds());
      const px = new Uint16Array(memory.buffer, ptr, 800 * 480);
      for (let i = 0; i < px.length; i++) out[i] = lut[px[i]];
      ctx.putImageData(image, 0, 0);
    }
    // After the map is moved or zoomed, fetch the new area soon.
    if (liveOn && !busy && !pts.length && t - lastCheck > 500) {
      lastCheck = t;
      if (performance.now() - lastFetch > 1500) {
        const key = ex.sim_query_lat().toFixed(4) + ex.sim_query_lon().toFixed(4) + ex.sim_query_radius();
        if (key !== lastKey) poll();
      }
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})();
