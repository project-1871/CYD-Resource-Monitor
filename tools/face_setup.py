#!/usr/bin/env python3
"""Use your own picture for the CYD's talking face.

    python tools/face_setup.py path/to/picture.png

Opens a page in your browser that shows the picture and asks you to click a
few spots: top of the head, chin, mouth corners, each eye's centre and
outline, and the cheeks. Then it saves tools/art/face.json, bakes
src/face_art.h (tools/make_face.py) and, if g++ is installed, shows a preview
of every mood. Flash the CYD afterwards (pio run -t upload).

Black-and-white line art on a white background, face looking at you, works
best. Run it again with no picture to adjust the points on the current one.
Needs Pillow (pip install pillow); nothing else.
"""
import http.server
import json
import mimetypes
import shutil
import socketserver
import subprocess
import sys
import threading
import webbrowser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "tools/art"
CFG = ART / "face.json"
PREVIEW = ART / "preview.png"

PAGE = r"""<!doctype html>
<html><head><meta charset="utf-8"><title>CYD face setup</title>
<style>
  body { margin: 0; font: 15px system-ui, sans-serif; background: #1d1d22; color: #eee; }
  header { padding: 10px 16px; background: #2b2b33; display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
  #step { font-weight: 600; font-size: 17px; flex: 1; min-width: 260px; }
  #hint { color: #aab; font-size: 13px; }
  button { font: inherit; padding: 6px 14px; border-radius: 6px; border: 0; background: #4a4a58; color: #fff; cursor: pointer; }
  button.go { background: #d0427a; }
  button:disabled { opacity: .4; cursor: default; }
  main { display: flex; gap: 16px; padding: 16px; flex-wrap: wrap; }
  canvas { background: #fff; cursor: crosshair; max-width: 100%; }
  #out { max-width: 100%; }
  #out img { image-rendering: pixelated; width: 100%; max-width: 960px; border: 1px solid #444; }
  #log { white-space: pre-wrap; color: #aab; font: 12px monospace; }
</style></head><body>
<header>
  <div><div id="step"></div><div id="hint"></div></div>
  <button id="undo">Undo (Backspace)</button>
  <button id="next" class="go">Next (Enter)</button>
  <button id="restart">Start over</button>
</header>
<main><canvas id="c"></canvas><div id="out"></div></main>
<script>
const STEPS = [
  {key: "top",   n: 1, text: "Click the very top of the head", hint: "Tips of the ears or hair. Everything above is cut off."},
  {key: "chin",  n: 1, text: "Click just under the chin", hint: "Everything below is cut off. Captions go under the face on the CYD."},
  {key: "mouthL", n: 1, text: "Click the LEFT corner of the mouth", hint: "The drawn mouth gets erased; the CYD draws a talking one there."},
  {key: "mouthR", n: 1, text: "Click the RIGHT corner of the mouth", hint: ""},
  {key: "eye0c", n: 1, text: "Click the middle of the eye on the LEFT", hint: "The pupil. Comic eyes (^^, O O) are drawn centred here."},
  {key: "eye0",  n: 0, text: "Click around the outline of the LEFT eye, then Next", hint: "Go around the lashes and iris (not long eyeliner wings). This part is erased to blink and for comic eyes. At least 3 points."},
  {key: "eye1c", n: 1, text: "Click the middle of the eye on the RIGHT", hint: ""},
  {key: "eye1",  n: 0, text: "Click around the outline of the RIGHT eye, then Next", hint: "Same as the left one."},
  {key: "cheek0", n: 1, text: "Click the LEFT cheek", hint: "Where the pink blush goes."},
  {key: "cheek1", n: 1, text: "Click the RIGHT cheek", hint: ""},
];
const c = document.getElementById("c"), g = c.getContext("2d");
const img = new Image();
let pts = {}, si = 0, scale = 1;

function fit() {
  const maxW = Math.min(window.innerWidth - 32, 900), maxH = window.innerHeight - 110;
  scale = Math.min(maxW / img.width, maxH / img.height, 1.5);
  c.width = img.width * scale; c.height = img.height * scale;
  draw();
}
function P(p) { return [p[0] * scale, p[1] * scale]; }
function dot(p, col) { const [x, y] = P(p); g.fillStyle = col; g.beginPath(); g.arc(x, y, 5, 0, 7); g.fill(); }
function poly(a, col, close) {
  if (!a || !a.length) return;
  g.strokeStyle = col; g.lineWidth = 2; g.beginPath();
  a.forEach((p, i) => { const [x, y] = P(p); i ? g.lineTo(x, y) : g.moveTo(x, y); });
  if (close && a.length > 2) g.closePath();
  g.stroke(); a.forEach(p => dot(p, col));
}
function draw() {
  g.drawImage(img, 0, 0, c.width, c.height);
  const hl = (y, col) => { if (y == null) return; g.fillStyle = col; g.fillRect(0, y * scale - 1, c.width, 3); };
  if (pts.top) { g.fillStyle = "rgba(0,0,0,.35)"; g.fillRect(0, 0, c.width, pts.top[0][1] * scale); hl(pts.top[0][1], "#d0427a"); }
  if (pts.chin) { g.fillStyle = "rgba(0,0,0,.35)"; g.fillRect(0, pts.chin[0][1] * scale, c.width, c.height); hl(pts.chin[0][1], "#d0427a"); }
  if (pts.mouthL && pts.mouthR) {
    const m = mouth(); g.strokeStyle = "#e33"; g.lineWidth = 2; g.beginPath();
    g.ellipse(m.c[0] * scale, m.c[1] * scale, m.r[0] * scale, m.r[1] * scale, 0, 0, 7); g.stroke();
  } else if (pts.mouthL) dot(pts.mouthL[0], "#e33");
  ["eye0", "eye1"].forEach(k => poly(pts[k], "#28f", STEPS[si] && STEPS[si].key !== k));
  ["eye0c", "eye1c"].forEach(k => pts[k] && dot(pts[k][0], "#0b4"));
  ["cheek0", "cheek1"].forEach(k => pts[k] && dot(pts[k][0], "#f6a"));
}
function mouth() {
  const a = pts.mouthL[0], b = pts.mouthR[0];
  const rx = Math.max(6, Math.hypot(b[0] - a[0], b[1] - a[1]) / 2 * 1.25);
  return {c: [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], r: [rx, rx * 0.62]};
}
function show() {
  const s = STEPS[si];
  document.getElementById("step").textContent = s ? `${si + 1}/${STEPS.length}: ${s.text}` : "All set: building...";
  document.getElementById("hint").textContent = s ? s.hint : "";
  document.getElementById("next").disabled = !s || (s.n === 0 ? (pts[s.key] || []).length < 3 : !pts[s.key]);
  draw();
}
function advance() {
  const s = STEPS[si];
  if (!s || document.getElementById("next").disabled) return;
  si++; show();
  if (si === STEPS.length) save();
}
c.addEventListener("click", e => {
  const s = STEPS[si]; if (!s) return;
  const r = c.getBoundingClientRect();
  const p = [Math.round((e.clientX - r.left) * (img.width / r.width)), Math.round((e.clientY - r.top) * (img.height / r.height))];
  if (s.n === 1) { pts[s.key] = [p]; show(); advance(); }
  else { (pts[s.key] = pts[s.key] || []).push(p); show(); }
});
function undo() {
  const s = STEPS[si];
  if (s && s.n === 0 && (pts[s.key] || []).length) pts[s.key].pop();
  else if (si > 0) { si--; delete pts[STEPS[si].key]; }
  show();
}
document.getElementById("undo").onclick = undo;
document.getElementById("next").onclick = advance;
document.getElementById("restart").onclick = () => { pts = {}; si = 0; document.getElementById("out").innerHTML = ""; show(); };
document.addEventListener("keydown", e => {
  if (e.key === "Backspace") { e.preventDefault(); undo(); }
  if (e.key === "Enter") advance();
});
async function save() {
  const m = mouth();
  const cfg = {
    crop: [Math.min(pts.top[0][1], pts.chin[0][1]), Math.max(pts.top[0][1], pts.chin[0][1])],
    mouth: m.c.map(Math.round), mouth_wipe: m.r.map(Math.round),
    eyes: [{center: pts.eye0c[0], wipe: pts.eye0}, {center: pts.eye1c[0], wipe: pts.eye1}],
    cheeks: [pts.cheek0[0], pts.cheek1[0]],
  };
  if (cfg.eyes[0].center[0] > cfg.eyes[1].center[0]) { cfg.eyes.reverse(); cfg.cheeks.sort((a, b) => a[0] - b[0]); }
  const out = document.getElementById("out");
  out.innerHTML = "<p>Building...</p>";
  const r = await (await fetch("/save", {method: "POST", body: JSON.stringify(cfg)})).json();
  document.getElementById("step").textContent = r.ok ? "Done! Now flash the CYD: pio run -t upload" : "Something went wrong";
  document.getElementById("hint").textContent = r.ok ? "Not happy? Press Start over (or Undo) and click again." : "";
  out.innerHTML = (r.preview ? `<p>Every mood, as the CYD will draw it:</p><img src="/preview.png?${Date.now()}">` : "")
    + `<div id="log"></div>`;
  document.getElementById("log").textContent = r.log;
}
fetch("/config").then(r => r.json()).then(cfg => {
  img.onload = () => {
    if (cfg && cfg.eyes) {                 // load the saved points so they can be adjusted
      const [a, b] = cfg.eyes, [mx, my] = cfg.mouth, rx = cfg.mouth_wipe[0] / 1.25;
      pts = {top: [[0, cfg.crop[0]]], chin: [[0, cfg.crop[1]]],
             mouthL: [[mx - rx, my]], mouthR: [[mx + rx, my]],
             eye0c: [a.center], eye0: a.wipe, eye1c: [b.center], eye1: b.wipe,
             cheek0: [cfg.cheeks[0]], cheek1: [cfg.cheeks[1]]};
      si = STEPS.length;
      document.getElementById("out").innerHTML = "<p>These are the saved points. Start over to click new ones.</p>";
    }
    fit(); show();
    if (si === STEPS.length) {
      document.getElementById("step").textContent = "Saved points shown";
      document.getElementById("hint").textContent = "Press Start over to redo them.";
    }
  };
  img.src = "/image";
});
window.addEventListener("resize", fit);
</script></body></html>
"""


def build(cfg):
    """Write face.json, bake the header and render the mood preview."""
    old = json.loads(CFG.read_text()) if CFG.exists() else {}
    cfg["image"] = state["image"]
    if old.get("image") == cfg["image"]:       # keep hand-made extras (e.g. lid lines)
        for i, e in enumerate(cfg["eyes"]):
            oe = (old.get("eyes") or [{}, {}])[i]
            if oe.get("lid") and oe.get("wipe") == e["wipe"]:
                e["lid"] = oe["lid"]
    CFG.write_text(json.dumps(cfg, indent=2) + "\n")
    log = []
    r = subprocess.run([sys.executable, str(ROOT / "tools/make_face.py")], capture_output=True, text=True)
    log.append((r.stdout + r.stderr).strip())
    if r.returncode:
        return {"ok": False, "log": "\n".join(log)}
    preview = False
    gxx = shutil.which("g++") or shutil.which("clang++")
    if gxx:
        exe = ART / "face_preview"
        ppm = ART / "preview.ppm"
        r = subprocess.run([gxx, "-O2", "-std=c++17", "-I", str(ROOT / "src"),
                            str(ROOT / "tools/face_preview.cpp"), "-o", str(exe)],
                           capture_output=True, text=True)
        if r.returncode == 0 and subprocess.run([str(exe), str(ppm)]).returncode == 0:
            from PIL import Image
            im = Image.open(ppm)
            im.resize((im.width * 3 // 2, im.height * 3 // 2), Image.NEAREST).save(PREVIEW)
            ppm.unlink()
            preview = True
        else:
            log.append("preview: " + r.stderr.strip()[-400:])
    else:
        log.append("(install g++ to see a preview of every mood here)")
    log.append(f"saved {CFG.relative_to(ROOT)} and src/face_art.h\n"
               "next: flash the CYD with  pio run -t upload")
    return {"ok": True, "preview": preview, "log": "\n".join(log)}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, body, ctype, code=200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            self.send(PAGE.encode(), "text/html; charset=utf-8")
        elif path == "/image":
            f = ART / state["image"]
            self.send(f.read_bytes(), mimetypes.guess_type(f.name)[0] or "application/octet-stream")
        elif path == "/config":
            cfg = json.loads(CFG.read_text()) if CFG.exists() else None
            if cfg and cfg.get("image") != state["image"]:
                cfg = None
            self.send(json.dumps(cfg).encode(), "application/json")
        elif path == "/preview.png" and PREVIEW.exists():
            self.send(PREVIEW.read_bytes(), "image/png")
        else:
            self.send(b"not found", "text/plain", 404)

    def do_POST(self):
        if self.path != "/save":
            return self.send(b"not found", "text/plain", 404)
        cfg = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        try:
            res = build(cfg)
        except Exception as e:  # show it on the page rather than hang
            res = {"ok": False, "log": repr(e)}
        print(res["log"])
        self.send(json.dumps(res).encode(), "application/json")


state = {}


def main():
    ART.mkdir(parents=True, exist_ok=True)
    if len(sys.argv) > 1:
        src = Path(sys.argv[1]).expanduser().resolve()
        if not src.exists():
            sys.exit(f"{src} not found")
        if src.parent != ART:                     # keep a copy next to face.json
            dst = ART / ("face" + src.suffix.lower())
            shutil.copyfile(src, dst)
            src = dst
        state["image"] = src.name
    elif CFG.exists():
        state["image"] = json.loads(CFG.read_text())["image"]
    else:
        sys.exit(__doc__)

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", 0), Handler) as srv:
        url = f"http://127.0.0.1:{srv.server_address[1]}/"
        print(f"Face setup is open in your browser: {url}\n(Ctrl+C here when you're done)")
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
