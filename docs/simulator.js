// The firmware build (simulator/build.py writes it next to this file).
const WASM_URL = "sim.wasm?v=17";

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

  let lastDraw = 0;
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
      ex.sim_clock(d.getTime() / 1000);
      const ptr = ex.sim_frame(now(), d.getHours(), d.getMinutes(), d.getSeconds());
      const px = new Uint16Array(memory.buffer, ptr, 800 * 480);
      for (let i = 0; i < px.length; i++) out[i] = lut[px[i]];
      ctx.putImageData(image, 0, 0);
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
})();
