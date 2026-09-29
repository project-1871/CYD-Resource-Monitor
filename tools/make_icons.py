#!/usr/bin/env python3
"""Render the quick-launch icon set to src/icons.h (48x48 1-bit, MSB-first).
Glyphs come from JetBrainsMono Nerd Font; add a (name, codepoint) pair to
ICONS and rerun, then reflash."""
import os
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf"
N = 48
ICONS = [
    ("steam", 0xF04D3), ("web", 0xF059F), ("firefox", 0xF0239), ("chrome", 0xF02AF),
    ("email", 0xF01EE), ("discord", 0xF066F), ("youtube", 0xF05C3), ("folder", 0xF024B),
    ("terminal", 0xF018D), ("music", 0xF075A), ("gamepad", 0xF0297), ("spotify", 0xF04C7),
    ("settings", 0xF0493), ("chat", 0xF0B79), ("camera", 0xF0100), ("power", 0xF0425),
    ("star", 0xF04CE), ("twitch", 0xF0543), ("whatsapp", 0xF05A3), ("robot", 0xF06A9),
]


def render(cp):
    for sz in range(80, 10, -1):  # largest size whose glyph fits the box
        f = ImageFont.truetype(FONT, sz)
        b = f.getbbox(chr(cp))
        if b[2] - b[0] <= N - 2 and b[3] - b[1] <= N - 2:
            break
    im = Image.new("1", (N, N), 0)
    ImageDraw.Draw(im).text(((N - (b[2] - b[0])) // 2 - b[0], (N - (b[3] - b[1])) // 2 - b[1]),
                            chr(cp), font=f, fill=1)
    px = im.load()
    out = []
    for y in range(N):
        for xb in range(0, N, 8):
            v = 0
            for k in range(8):
                v = (v << 1) | (1 if px[xb + k, y] else 0)
            out.append(v)
    return out


def main():
    out = ["#pragma once", "#include <Arduino.h>", "",
           "// 48x48 1-bit icons rendered from JetBrainsMono Nerd Font (MSB-first rows).",
           "// Regenerate: tools/make_icons.py. Name them in ~/.config/cyd-monitor/keys.json.",
           "#define ICON_SZ 48", ""]
    for n, cp in ICONS:
        by = render(cp)
        out.append(f"const uint8_t icon_{n}[] PROGMEM = {{")
        for i in range(0, len(by), 16):
            out.append("  " + ",".join(f"0x{v:02x}" for v in by[i:i + 16]) + ",")
        out.append("};")
    out += ["", "struct IconDef { const char *name; const uint8_t *bmp; };", "const IconDef iconTable[] = {"]
    out += [f'  {{"{n}", icon_{n}}},' for n, _ in ICONS]
    out += ["};", f"#define N_ICONS {len(ICONS)}", "",
            "const uint8_t *findIcon(const char *name) {",
            "  for (int i = 0; i < N_ICONS; i++)",
            "    if (!strcasecmp(name, iconTable[i].name)) return iconTable[i].bmp;",
            "  return nullptr;", "}", ""]
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "icons.h")
    with open(path, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    main()
