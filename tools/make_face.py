#!/usr/bin/env python3
"""Bake your face drawing into src/face_art.h for the talking face.

Reads tools/art/face.json (made by tools/face_setup.py, which lets you click
the points on your picture in a browser) and the image it names. The head is
cropped, the drawn mouth is erased (face.h animates its own), and two copies
of the eye rows are made: eyes shut (blinking) and eyes wiped (moods that
draw their own eyes). Pixels are stored as 2-bit ink levels (0 = paper ..
3 = black), four per byte. Black-and-white line art on white works best.

    python tools/make_face.py [debug.png]

face.json, all in source-image pixels:
  image       file name, next to face.json
  crop        [top, bottom]: top of the head (ears) to just under the chin
  mouth       [x, y] centre of the drawn mouth; mouth_wipe [rx, ry] its half-size
  eyes        two of {center: [x, y] pupil, wipe: [[x, y], ...] outline of the
              part to erase, lid: [[x, y], ...] optional closed-eye line,
              inner corner first}; the first is the eye on the left
  cheeks      [[x, y], [x, y]] where the blush goes
"""
import json
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parent.parent
CFG = ROOT / "tools/art/face.json"
OUT = ROOT / "src/face_art.h"
FACE_H = 212                    # screen rows for the art (captions go below)
SCR_W = 320


def auto_lid(wipe, center):
    """A closed-eye line when face.json doesn't give one: a lash bowed down
    across the eye, a little below the pupil."""
    xs = [p[0] for p in wipe]
    ys = [p[1] for p in wipe]
    h = max(ys) - min(ys)
    y = center[1] + 0.15 * h
    x0, x1 = min(xs) + 0.05 * (max(xs) - min(xs)), max(xs) - 0.05 * (max(xs) - min(xs))
    return [(x0 + (x1 - x0) * t, y + 0.18 * h * (1 - (2 * t - 1) ** 2)) for t in (0, .25, .5, .75, 1)]


def main():
    if not CFG.exists():
        sys.exit(f"{CFG} not found: run tools/face_setup.py <your picture> first")
    cfg = json.loads(CFG.read_text())
    art = Image.open(CFG.parent / cfg["image"])
    if art.mode in ("RGBA", "LA", "P"):        # transparent background -> white paper
        art = art.convert("RGBA")
        bg = Image.new("RGBA", art.size, "white")
        bg.alpha_composite(art)
        art = bg
    art = ImageOps.autocontrast(art.convert("L"))

    crop_y0, crop_y1 = cfg["crop"]
    mx, my = cfg["mouth"]
    rx, ry = cfg.get("mouth_wipe", [0.08 * (crop_y1 - crop_y0), 0.05 * (crop_y1 - crop_y0)])
    eyes = cfg["eyes"]

    d = ImageDraw.Draw(art)
    d.ellipse((mx - rx, my - ry, mx + rx, my + ry), fill=255)

    blank = art.copy()
    db = ImageDraw.Draw(blank)
    for e in eyes:
        db.polygon([tuple(p) for p in e["wipe"]], fill=255)

    s = FACE_H / (crop_y1 - crop_y0)
    shut = art.copy()
    ds = ImageDraw.Draw(shut)
    for e in eyes:
        ds.polygon([tuple(p) for p in e["wipe"]], fill=255)
        lid = [tuple(p) for p in (e.get("lid") or auto_lid(e["wipe"], e["center"]))]
        scale = 0.3926 / s                         # line widths were tuned at this scale
        widths = [round(w * scale) for w in (3, 6, 8, 9, 9, 9)]
        for i in range(len(lid) - 1):              # thick in the middle, thin at the inner corner
            ds.line(lid[i:i + 2], fill=0, width=max(1, widths[min(i, len(widths) - 1)]), joint="curve")

    # horizontal: keep the whole width if it fits, else a window centred on the mouth
    full_w = round(art.width * s)
    if full_w <= SCR_W:
        cx0, cx1 = 0, art.width
    else:
        half = SCR_W / s / 2
        cx0 = int(max(0, min(art.width - 2 * half, mx - half)))
        cx1 = int(cx0 + 2 * half)
    w = min(SCR_W, round((cx1 - cx0) * s))

    def bake(im):
        im = im.crop((cx0, crop_y0, cx1, crop_y1)).resize((w, FACE_H), Image.LANCZOS)
        lv = []
        data = im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()
        for v in data:
            ink = min(1.0, (1 - v / 255) * 1.7)      # thin lines fade when shrunk: boost
            lv.append(0 if ink < .16 else 1 if ink < .42 else 2 if ink < .72 else 3)
        return lv

    open_, shut_, blank_ = bake(art), bake(shut), bake(blank)
    rows = [y for y in range(FACE_H) if open_[y * w:(y + 1) * w] != shut_[y * w:(y + 1) * w]
            or open_[y * w:(y + 1) * w] != blank_[y * w:(y + 1) * w]]
    ey0, ey1 = (rows[0], rows[-1] + 1) if rows else (0, 1)

    def pack(lv):
        out = []
        for i in range(0, len(lv), 4):
            b = 0
            for k, v in enumerate(lv[i:i + 4]): b |= v << (6 - 2 * k)
            out.append(b)
        return out

    stride = (w + 3) // 4
    def rowpack(lv, y0, y1):
        out = []
        for y in range(y0, y1): out += pack(lv[y * w:(y + 1) * w] + [0] * (stride * 4 - w))
        return out

    def carr(name, data):
        lines = [", ".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
        return f"static const uint8_t {name}[{len(data)}] PROGMEM = {{\n  " + ",\n  ".join(lines) + "\n};\n"

    sx = lambda x: (SCR_W - w) / 2 + (x - cx0) * s
    sy = lambda y: (y - crop_y0) * s
    eye_r = sum((max(p[0] for p in e["wipe"]) - min(p[0] for p in e["wipe"])) / 2 for e in eyes) / len(eyes) * s
    OUT.write_text(
        "#pragma once\n// Generated by tools/make_face.py from tools/art/face.json. Do not edit.\n"
        "// 2-bit ink levels (0 paper .. 3 black), 4 px per byte, MSB first.\n"
        "#include <stdint.h>\n#ifndef PROGMEM\n#define PROGMEM\n#endif\n\n"
        f"#define CAT_W {w}\n#define CAT_H {FACE_H}\n#define CAT_X {(SCR_W - w) // 2}\n#define CAT_STRIDE {stride}\n"
        f"#define CAT_EYE_Y0 {ey0}\n#define CAT_EYE_Y1 {ey1}\n#define CAT_EYE_R {eye_r:.1f}f\n"
        + "".join(f"#define CAT_EYE{i}_X {sx(e['center'][0]):.1f}f\n#define CAT_EYE{i}_Y {sy(e['center'][1]):.1f}f\n"
                  for i, e in enumerate(eyes))
        + f"#define CAT_MOUTH_X {sx(mx):.1f}f\n#define CAT_MOUTH_Y {sy(my):.1f}f\n#define CAT_SCALE {s:.4f}f\n"
        + "".join(f"#define CAT_CHEEK{i}_X {sx(x):.1f}f\n#define CAT_CHEEK{i}_Y {sy(y):.1f}f\n"
                  for i, (x, y) in enumerate(cfg["cheeks"]))
        + "\n" + carr("catArt", rowpack(open_, 0, FACE_H)) + "\n" + carr("catEyesShut", rowpack(shut_, ey0, ey1))
        + "\n" + carr("catEyesBlank", rowpack(blank_, ey0, ey1)))
    print(f"{OUT.name}: {w}x{FACE_H}, eye rows {ey0}-{ey1}, "
          f"{stride * FACE_H + 2 * stride * (ey1 - ey0)} bytes")

    if len(sys.argv) > 1:                          # debug: the source edits, full size
        both = Image.new("L", (art.width * 2, art.height), 255)
        both.paste(art, (0, 0)); both.paste(shut, (art.width, 0))
        both.save(sys.argv[1])


if __name__ == "__main__":
    main()
