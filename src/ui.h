#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <Preferences.h>
#include "config.h"

TFT_eSPI    tft;
TFT_eSprite fb(&tft);   // full-frame 8-bit back buffer
TFT_Touch   touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);
Preferences prefs;

// ── Screens ────────────────────────────────────────
enum { SCR_DASH = 0, SCR_DETAIL, SCR_SETTINGS, SCR_KEYS };
int g_screen = SCR_DASH;

// ── Themes (colors chosen to survive 8-bit RRRGGGBB) ─
struct Theme {
  const char *name;
  uint16_t bg, card, border, grid, text, muted, accent;
  uint16_t cpu, gpu, ram, temp, disk, net;
  uint16_t ok, warn, bad;
  bool light;
  // optional (0 / nullptr = defaults): gauge track, RGB-strip colors, glitch tearing
  uint16_t track;
  const uint16_t *strip;
  bool glitch;
  bool scan;      // CRT scanlines + rolling bright band
};

// 8-bit greys: the frame buffer has 2-bit blue, so only these stay close to neutral
const uint16_t monoStrip[6] = {TFT_WHITE, 0xB5B5, 0x6B6B, 0x4A4B, 0x6B6B, 0xB5B5};
// japglitch (Omarchy): RGB-split fringe colors + border accents
const uint16_t glitchStrip[6] = {0xB13F, 0x249F, 0xFB60, 0x2375, 0x9135, 0xFFF5};

// red-on-black hacker terminal: red/white fringes for the glitch tears
const uint16_t redrootStrip[6] = {0xF800, TFT_WHITE, 0xA000, 0x4000, 0xA000, 0xF800};

const Theme themes[] = {
  {"CYBER",     TFT_BLACK, 0x0008, 0x4208, 0x0841, TFT_WHITE, 0x8410, TFT_CYAN,
   TFT_CYAN, TFT_MAGENTA, TFT_YELLOW, TFT_ORANGE, TFT_GREEN, 0x255F,
   TFT_GREEN, TFT_YELLOW, TFT_RED, false},
  {"SYNTHWAVE", TFT_BLACK, 0x2008, 0x8010, 0x2004, TFT_WHITE, 0x8410, 0xF81F,
   0xFD20, 0xF81F, 0xFFE0, 0xFB2C, 0x07FF, 0xAD5F,
   TFT_GREEN, TFT_YELLOW, TFT_RED, false},
  {"MATRIX",    TFT_BLACK, 0x0120, 0x0480, 0x0100, 0x9FE6, 0x0480, 0x07E0,
   0x07E0, 0x9FE6, 0xAFE0, 0xFFE0, 0x04A0, 0x07EF,
   0x07E0, TFT_YELLOW, TFT_RED, false},
  {"LIGHT",     0xEF7D, TFT_WHITE, 0x8410, 0xC618, TFT_BLACK, 0x630C, 0x001F,
   0x019F, 0xA017, 0xBC40, 0xCB20, 0x0460, 0x001F,
   0x0460, 0xBC40, TFT_RED, false},
  {"MONO",      TFT_BLACK, TFT_BLACK, 0x6B6B, 0x4A4B, TFT_WHITE, 0xB5B5, TFT_WHITE,
   TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE,
   0xB5B5, TFT_WHITE, TFT_WHITE, false, 0x4A4B, monoStrip},
  {"JAPGLITCH", 0x200B, 0x212B, 0x4A4B, 0x492B, 0xFFF5, 0x95AB, 0xB13F,
   0xB13F, 0x249F, 0xDFE0, 0xFB60, 0x25AB, 0xB12B,
   0x25AB, 0xFB60, 0xF920, false, 0x492B, glitchStrip, true},
  {"REDROOT",   TFT_BLACK, 0x2000, 0x6000, 0x2000, TFT_WHITE, 0xB5B5, 0xF800,
   0xF800, TFT_WHITE, 0xB5B5, 0xFB00, 0xA000, TFT_WHITE,
   TFT_WHITE, 0xFB00, 0xF800, false, 0x4000, redrootStrip, true, true},
  {"MONOROOT",  TFT_BLACK, TFT_BLACK, 0x6B6B, 0x4A4B, TFT_WHITE, 0xB5B5, TFT_WHITE,
   TFT_WHITE, TFT_WHITE, 0xB5B5, TFT_WHITE, 0xB5B5, TFT_WHITE,
   0xB5B5, TFT_WHITE, TFT_WHITE, false, 0x4A4B, monoStrip, true, true},
};
#define N_THEMES 8
int themeIdx = 0;
#define th (themes[themeIdx])

#define C_BG     th.bg
#define C_CARD   th.card
#define C_BORDER th.border
#define C_MUTED  th.muted
#define C_TEXT   th.text
#define C_OK     th.ok
#define C_WARN   th.warn
#define C_BAD    th.bad

const uint16_t rgbCycle[6] = {
  TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_MAGENTA
};

uint16_t dimc(uint16_t c) { return (c >> 1) & 0x7BEF; }

// ── Input with edge detection ──────────────────────
// Resistive touch drops out for a frame or two mid-drag, so a release only
// counts after RELEASE_MS of no pressure (keeps a swipe in one piece).
#define RELEASE_MS 45
struct Input {
  bool down = false, justDown = false, justUp = false;
  int  x = 0, y = 0;     // current (or last) touch point
  int  sx = 0, sy = 0;   // where this touch started
} in;

void inputUpdate() {
  static uint32_t lastPress = 0;
  bool raw = touch.Pressed();
  uint32_t now = millis();
  if (raw) { in.x = touch.X(); in.y = touch.Y(); lastPress = now; }
  bool p = raw || (in.down && now - lastPress < RELEASE_MS);
  in.justDown = p && !in.down;
  in.justUp   = !p && in.down;
  in.down     = p;
  if (in.justDown) { in.sx = in.x; in.sy = in.y; }
}

// on release: -1 = swiped left, +1 = swiped right, 0 = no swipe
int swipeDir() {
  if (!in.justUp) return 0;
  int dx = in.x - in.sx, dy = in.y - in.sy;
  if (abs(dx) < 60 || abs(dx) < 2 * abs(dy)) return 0;
  return dx < 0 ? -1 : 1;
}

// tap that fires on release, and only if the finger didn't travel (not a swipe)
bool tapUpIn(int x, int y, int w, int h) {
  return in.justUp && abs(in.x - in.sx) < 20 && abs(in.y - in.sy) < 20 &&
         in.sx >= x && in.sx < x + w && in.sy >= y && in.sy < y + h;
}

// page dots in the header: which of the two swipe pages is showing
void pageDots(int x, int y, int page, uint16_t on, uint16_t off) {
  for (int i = 0; i < 2; i++) {
    if (i == page) fb.fillCircle(x + i * 10, y, 3, on);
    else           fb.drawCircle(x + i * 10, y, 3, off);
  }
}

bool tapIn(int x, int y, int w, int h) {
  return in.justDown && in.x >= x && in.x < x + w && in.y >= y && in.y < y + h;
}

// ── Widgets ────────────────────────────────────────
bool uiButton(int x, int y, int w, int h, const char *label, uint16_t col) {
  bool over = in.x >= x && in.x < x + w && in.y >= y && in.y < y + h;
  bool hot  = in.down && over;
  fb.fillRoundRect(x, y, w, h, 6, hot ? col : C_CARD);
  fb.drawRoundRect(x, y, w, h, 6, col);
  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(MC_DATUM);
  fb.setTextColor(hot ? C_BG : col);
  fb.drawString(label, x + w / 2, y + h / 2);
  return in.justDown && over;
}

uint16_t loadColor(float pct) {
  if (pct < 60) return C_OK;
  if (pct < 85) return C_WARN;
  return C_BAD;
}

// animated RGB accent strip — the PC-modder signature
void rgbStrip(int y, int hgt = 2) {
  int t = millis() / 120;
  for (int x = 0; x < SCR_W; x += 4)
    fb.fillRect(x, y, 4, hgt, (th.strip ? th.strip : rgbCycle)[(x / 4 + t) % 6]);
}

// glitch tearing: every few seconds, shift a few rows of the frame sideways
// and streak fringe colors through them, for ~0.1-0.25 s
void glitchFx() {
  static uint32_t next = 0, until = 0;
  uint32_t now = millis();
  if (now >= next) { until = now + random(90, 250); next = now + random(1800, 5000); }
  if (now > until) return;
  uint8_t *buf = (uint8_t *)fb.getPointer();
  const uint16_t *fr = th.strip ? th.strip : rgbCycle;
  for (int n = random(2, 6); n > 0; n--) {
    int y = random(0, SCR_H - 12), h = random(2, 12), dx = random(-18, 19);
    for (int row = y; row < y + h; row++) {
      uint8_t *r = buf + row * SCR_W;
      if (dx > 0) memmove(r + dx, r, SCR_W - dx);
      else if (dx < 0) memmove(r, r - dx, SCR_W + dx);
    }
    if (random(3) == 0) fb.drawFastHLine(random(0, SCR_W / 2), y, random(40, SCR_W), fr[random(0, 2)]);
  }
}

// CRT scanlines: darken every other row of the 8-bit (RRRGGGBB) frame,
// except inside a slow rolling band that sweeps down the screen
void scanFx() {
  static uint8_t lut[256];
  static bool init = false;
  if (!init) {
    for (int c = 0; c < 256; c++)
      lut[c] = (((c >> 5) >> 1) << 5) | ((((c >> 2) & 7) >> 1) << 2) | ((c & 3) >> 1);
    init = true;
  }
  uint8_t *buf = (uint8_t *)fb.getPointer();
  int band = (millis() / 25) % (SCR_H + 60) - 30;   // 30-row roll bar
  for (int row = 1; row < SCR_H; row += 2) {
    if (row >= band && row < band + 30) continue;
    uint8_t *r = buf + row * SCR_W;
    for (int x = 0; x < SCR_W; x++) r[x] = lut[r[x]];
  }
}

// dotted background grid
void bgGrid(int top) {
  for (int y = top + 8; y < SCR_H; y += 16)
    for (int x = 8; x < SCR_W; x += 16)
      fb.drawPixel(x, y, th.grid);
}

// cyber corner brackets on a rect
void corners(int x, int y, int w, int h, uint16_t col) {
  fb.drawFastHLine(x + 3, y, 10, col);      fb.drawFastVLine(x, y + 3, 10, col);
  fb.drawFastHLine(x + w - 13, y + h - 1, 10, col);
  fb.drawFastVLine(x + w - 1, y + h - 13, 10, col);
}

// 300-degree gauge arc, gap at the bottom
void gauge(int cx, int cy, int r, float frac, uint16_t col) {
  frac = constrain(frac, 0.0f, 1.0f);
  int ir = r - 5;
  fb.drawArc(cx, cy, r, ir, 30, 330, th.track ? th.track : (th.light ? 0xC618 : 0x2104), C_BG, false);
  if (frac > 0.01f)
    fb.drawArc(cx, cy, r, ir, 30, 30 + (int)(frac * 300), col, C_BG, false);
  fb.drawCircle(cx, cy, ir - 3, th.grid);   // techy inner ring
}

// sparkline; optional area fill below the line
void sparkline(int x, int y, int w, int h, const float *ring, int head,
               int total, int n, float vmax, uint16_t col, bool fill = false) {
  if (total < 2 || vmax <= 0) return;
  n = min(n, total);
  uint16_t dc = dimc(dimc(col));
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int idx = (head - n + i + 240) % 120;        // ring length 120
    float v = constrain(ring[idx] / vmax, 0.0f, 1.0f);
    int gx = x + i * w / (n - 1);
    int gy = y + h - 1 - (int)(v * (h - 1));
    if (fill && (i & 1)) fb.drawFastVLine(gx, gy, y + h - gy, dc);
    if (px >= 0) fb.drawLine(px, py, gx, gy, col);
    px = gx; py = gy;
  }
}
