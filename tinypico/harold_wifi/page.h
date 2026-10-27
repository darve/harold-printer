// Web pages served by harold_wifi.ino. Kept in flash as raw string literals.

#pragma once

static const char MAIN_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>harold printer</title>
<style>
  :root { --bg:#f4f1ea; --card:#fff; --ink:#1d1b18; --muted:#6d675e; --accent:#1d1b18; --line:#ddd6ca; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#161513; --card:#22201d; --ink:#f1ede6; --muted:#a39c90; --accent:#f1ede6; --line:#3a3631; }
  }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--ink); font:16px/1.4 system-ui, sans-serif; }
  main { max-width:520px; margin:0 auto; padding:16px; }
  h1 { font-size:22px; margin:8px 0 4px; }
  #status { color:var(--muted); font-size:14px; margin-bottom:16px; }
  section { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:16px; margin-bottom:16px; }
  h2 { font-size:16px; margin:0 0 12px; }
  button, .pick { display:inline-block; font:inherit; padding:12px 16px; border-radius:10px; border:1px solid var(--accent);
    background:var(--accent); color:var(--bg); cursor:pointer; text-align:center; }
  button.ghost, .pick { background:transparent; color:var(--ink); }
  button:disabled { opacity:.4; cursor:default; }
  .row { display:flex; gap:8px; flex-wrap:wrap; }
  .row > * { flex:1; }
  input[type=file] { display:none; }
  input[type=text] { width:100%; font:inherit; padding:12px; border-radius:10px; border:1px solid var(--line);
    background:transparent; color:var(--ink); margin-bottom:8px; }
  canvas { display:block; width:100%; height:auto; margin:12px 0; background:#fff; image-rendering:pixelated; border-radius:4px; }
  label.slider { display:block; font-size:14px; color:var(--muted); margin-bottom:12px; }
  label.slider input { width:100%; }
  #msg { min-height:1.4em; font-size:14px; color:var(--muted); }
</style>
</head>
<body>
<main>
  <h1>harold printer</h1>
  <div id="status">Checking the printer...</div>

  <section>
    <h2>Photo</h2>
    <label class="pick" style="width:100%">Choose a photo<input id="file" type="file" accept="image/*"></label>
    <canvas id="preview" width="1" height="1" hidden></canvas>
    <div id="photoControls" hidden>
      <label class="slider">Brightness <input id="gamma" type="range" min="0.8" max="2.4" step="0.1" value="1.4"></label>
      <button id="printPhoto" style="width:100%">Print photo</button>
    </div>
  </section>

  <section>
    <h2>Text</h2>
    <input id="text" type="text" maxlength="190" placeholder="Type something">
    <button id="printText" style="width:100%">Print text</button>
  </section>

  <section>
    <div class="row">
      <button class="ghost" id="randomPhoto">Random photo</button>
      <button class="ghost" id="feed">Feed</button>
      <button class="ghost" id="test">Test page</button>
    </div>
  </section>

  <div id="msg"></div>
</main>

<script>
const $ = id => document.getElementById(id);
let printer = null;   // {ready, mode, width, height}
let source = null;    // the loaded <img>
let packed = null;    // {bits, w, h}

const MAX_BYTES = 128 * 1024;
const ROLL_MAX_ROWS = 1600;

function say(text) { $('msg').textContent = text; }

async function refresh() {
  try {
    const r = await fetch('/api/status');
    const t = await r.text();
    if (!t.startsWith('OK')) throw new Error(t);
    printer = {};
    for (const kv of t.split(' ').slice(1)) {
      const [k, v] = kv.split('=');
      printer[k] = isNaN(v) ? v : Number(v);
    }
    $('status').textContent = printer.ready
      ? (printer.mode === 'tspl'
          ? `Label printer ready (${printer.width} x ${printer.height} dots)`
          : `Receipt printer ready (${printer.width} dots wide)`)
      : 'No printer plugged into the Pico';
  } catch (e) {
    printer = null;
    $('status').textContent = 'Can\'t reach the Pico: ' + e.message;
  }
  if (source) render();
}

// Work out the output size: full width on a roll, fit-inside on a label,
// rotating landscape photos onto portrait labels.
function layout(img) {
  const W = printer ? printer.width : 384;
  const H = printer ? printer.height : 0;
  let iw = img.naturalWidth, ih = img.naturalHeight, rotate = false;
  if (H > 0 && (H > W) !== (ih > iw) && iw !== ih) { rotate = true; [iw, ih] = [ih, iw]; }
  let w, h;
  if (H > 0) {
    const s = Math.min(W / iw, H / ih);
    w = Math.max(1, Math.floor(iw * s));
    h = Math.max(1, Math.floor(ih * s));
  } else {
    w = W;
    h = Math.min(Math.round(ih * W / iw), ROLL_MAX_ROWS);
  }
  h = Math.min(h, Math.floor(MAX_BYTES / Math.ceil(w / 8)));
  return {w, h, rotate};
}

function render() {
  const {w, h, rotate} = layout(source);
  const c = $('preview');
  c.width = w; c.height = h;
  const ctx = c.getContext('2d', {willReadFrequently: true});
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, w, h);
  ctx.save();
  if (rotate) { ctx.translate(w, 0); ctx.rotate(Math.PI / 2); ctx.drawImage(source, 0, 0, h, w); }
  else {
    // On a roll, crop the middle if the photo is very tall.
    const sh = Math.min(source.naturalHeight, source.naturalWidth * h / w);
    const sy = (source.naturalHeight - sh) / 2;
    ctx.drawImage(source, 0, sy, source.naturalWidth, sh, 0, 0, w, h);
  }
  ctx.restore();

  const data = ctx.getImageData(0, 0, w, h);
  const bits = dither(data.data, w, h, Number($('gamma').value));
  // Show the dithered result.
  const px = data.data;
  for (let y = 0, i = 0; y < h; y++) {
    for (let x = 0; x < w; x++, i += 4) {
      const black = bits[y * Math.ceil(w / 8) + (x >> 3)] & (0x80 >> (x & 7));
      px[i] = px[i + 1] = px[i + 2] = black ? 0 : 255;
      px[i + 3] = 255;
    }
  }
  ctx.putImageData(data, 0, 0);
  packed = {bits, w, h};
  c.hidden = false;
  $('photoControls').hidden = false;
}

// Grey levels, stretch, gamma, then Floyd-Steinberg to 1 bpp, MSB first,
// 1 = black. Same recipe as tools/convert_photos.py.
function dither(rgba, w, h, gamma) {
  const n = w * h, g = new Float32Array(n);
  const hist = new Uint32Array(256);
  for (let i = 0; i < n; i++) {
    const v = 0.299 * rgba[i * 4] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2];
    g[i] = v;
    hist[v | 0]++;
  }
  let lo = 0, hi = 255, acc = 0;
  while (lo < 255 && (acc += hist[lo]) < n * 0.01) lo++;
  acc = 0;
  while (hi > 0 && (acc += hist[hi]) < n * 0.01) hi--;
  const span = Math.max(1, hi - lo);
  for (let i = 0; i < n; i++) {
    const t = Math.min(1, Math.max(0, (g[i] - lo) / span));
    g[i] = 255 * Math.pow(t, 1 / gamma);
  }
  const rowBytes = Math.ceil(w / 8);
  const out = new Uint8Array(rowBytes * h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x, old = g[i], nv = old < 128 ? 0 : 255, err = old - nv;
      if (nv === 0) out[y * rowBytes + (x >> 3)] |= 0x80 >> (x & 7);
      if (x + 1 < w) g[i + 1] += err * 7 / 16;
      if (y + 1 < h) {
        if (x > 0) g[i + w - 1] += err * 3 / 16;
        g[i + w] += err * 5 / 16;
        if (x + 1 < w) g[i + w + 1] += err * 1 / 16;
      }
    }
  }
  return out;
}

async function post(url, body, label) {
  const buttons = document.querySelectorAll('button');
  buttons.forEach(b => b.disabled = true);
  say(label + '...');
  try {
    const r = await fetch(url, {method: 'POST', body});
    const t = await r.text();
    say(t.startsWith('OK') ? 'Printed.' : t.replace(/^ERR /, 'Problem: '));
  } catch (e) {
    say('Problem: ' + e.message);
  } finally {
    buttons.forEach(b => b.disabled = false);
  }
}

$('file').onchange = e => {
  const f = e.target.files[0];
  if (!f) return;
  const img = new Image();
  img.onload = () => { source = img; render(); say(''); };
  img.onerror = () => say('Couldn\'t open that image.');
  img.src = URL.createObjectURL(f);
};
$('gamma').oninput = () => source && render();
$('printPhoto').onclick = () => {
  if (!packed) return;
  const fd = new FormData();
  fd.append('file', new Blob([packed.bits]), 'image.bin');
  post(`/api/image?w=${packed.w}&h=${packed.h}`, fd, 'Printing photo');
};
$('printText').onclick = () => {
  const t = $('text').value.trim();
  if (t) post('/api/text', new URLSearchParams({text: t}), 'Printing text');
};
$('randomPhoto').onclick = () => post('/api/photo', null, 'Printing a random photo');
$('feed').onclick = () => post('/api/feed', null, 'Feeding');
$('test').onclick = () => post('/api/test', null, 'Printing test page');

refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>
)HTML";

static const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>harold printer setup</title>
<style>
  body { margin:0; font:16px/1.4 system-ui, sans-serif; background:#f4f1ea; color:#1d1b18; }
  main { max-width:420px; margin:0 auto; padding:16px; }
  input, button { display:block; width:100%; font:inherit; padding:12px; margin:8px 0 16px; border-radius:10px; border:1px solid #bbb; box-sizing:border-box; }
  button { background:#1d1b18; color:#fff; border-color:#1d1b18; }
</style>
</head>
<body>
<main>
  <h1>Wi-Fi setup</h1>
  <p>Enter your home Wi-Fi so the printer page can join it.</p>
  <form method="post" action="/setup">
    <label>Network name<input name="ssid" required autocapitalize="none" autocorrect="off"></label>
    <label>Password<input name="pass" type="password"></label>
    <button>Save and restart</button>
  </form>
</main>
</body>
</html>
)HTML";
