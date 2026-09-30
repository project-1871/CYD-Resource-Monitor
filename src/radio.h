#pragma once
#include "data.h"
#include "boom.h"

// Radio page: drives the Radio Atlas Omarchy plugin through the agent. A retro
// tuner dial (needle = position in the station list), what's on air, playback
// buttons, genre buttons that load stations, and a station list you can swipe
// up/down and tap. Sends "R toggle|next|prev|random|volup|voldn",
// "R genre <n>" and "R play <n>" back up the serial.
namespace radio {

const char *GENRES[] = {"ROCK", "JAZZ", "HIPHOP", "LOFI", "NEWS", "RECENT"};
const char *GENRE_MATCH[] = {"ROCK", "JAZZ", "HIP HOP", "LOFI", "NEWS", "RECENT"};  // list labels from the agent
const int GENRE_N = 6, BTN_N = 6, ROWS = 5;
uint32_t flashBtn[BTN_N] = {}, flashGenre[GENRE_N] = {};
float needle = 14;
int scroll = 0;             // first station row shown
int lastCur = -2;           // follow the playing station when it changes

void glyph(int i, int cx, int cy, uint16_t c) {
  switch (i) {
    case 0:  // previous station
      fb.fillRect(cx - 9, cy - 6, 3, 12, c);
      fb.fillTriangle(cx + 6, cy - 6, cx + 6, cy + 6, cx - 5, cy, c);
      break;
    case 1:  // play / pause
      if (rad.on && !rad.paused) { fb.fillRect(cx - 6, cy - 7, 4, 14, c); fb.fillRect(cx + 2, cy - 7, 4, 14, c); }
      else fb.fillTriangle(cx - 5, cy - 8, cx - 5, cy + 8, cx + 8, cy, c);
      break;
    case 2:  // next station
      fb.fillTriangle(cx - 6, cy - 6, cx - 6, cy + 6, cx + 5, cy, c);
      fb.fillRect(cx + 6, cy - 6, 3, 12, c);
      break;
    case 3:  // random: a die
      fb.drawRoundRect(cx - 8, cy - 8, 16, 16, 3, c);
      fb.fillCircle(cx - 4, cy - 4, 1, c); fb.fillCircle(cx, cy, 1, c); fb.fillCircle(cx + 4, cy + 4, 1, c);
      break;
    default: {
      fb.setTextFont(2); fb.setTextDatum(MC_DATUM); fb.setTextColor(c);
      fb.drawString(i == 4 ? "VOL-" : "VOL+", cx, cy);
    }
  }
}

void update() {
  uint32_t now = millis();
  bool live = rad.ok && now - rad.rx < 5000;
  bool onAir = live && rad.on && !rad.paused;

  bgGrid(26);

  // header
  fb.fillRect(4, 5, 5, 14, th.accent);
  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(TL_DATUM);
  fb.setTextColor(th.accent);
  fb.drawString("RADIO", 14, 4);
  if (rad.listLabel[0]) { fb.setTextColor(C_MUTED); fb.drawString(rad.listLabel, 62, 4); }
  bool on = pc.valid && (now - pc.lastRx < 1200);
  fb.fillCircle(SCR_W - 44, 12, 4, pc.valid ? (on ? C_OK : C_WARN) : C_BAD);
  if (pageButton(3)) g_screen = SCR_DASH;
  if (rgbOn) rgbStrip(23, 2);
  else       fb.drawFastHLine(0, 24, SCR_W, C_BORDER);

  // tuner panel
  const int px = 3, py = 27, pw = SCR_W - 6, ph = 68;
  fb.fillRoundRect(px, py, pw, ph, 6, C_CARD);
  fb.drawRoundRect(px, py, pw, ph, 6, C_BORDER);
  // dial scale: FM-style numbers and ticks
  const int dx0 = 14, dx1 = SCR_W - 14, dy = py + 4;
  fb.setTextFont(1); fb.setTextDatum(TC_DATUM);
  for (int i = 0; i <= 40; i++) {
    int x = dx0 + (dx1 - dx0) * i / 40;
    bool major = i % 8 == 0;
    fb.drawFastVLine(x, dy + (major ? 8 : 11), major ? 6 : 3, major ? C_TEXT : C_MUTED);
    if (major) {
      char f[6]; snprintf(f, sizeof(f), "%d", 88 + i / 2);
      fb.setTextColor(C_MUTED);
      fb.drawString(f, x, dy);
    }
  }
  float target = (live && rad.count > 0 && rad.pos >= 0)
                     ? dx0 + (dx1 - dx0) * (rad.pos + 0.5f) / rad.count : dx0;
  if (onAir) target += sinf(now * 0.004f) * 1.2f;           // a little drift while tuned in
  needle += (target - needle) * 0.15f;
  fb.fillRect((int)needle - 1, dy + 5, 3, 14, TFT_RED);

  fb.setTextFont(2);
  if (!live) {
    fb.setTextDatum(MC_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString(rad.ok ? "RADIO OFF" : "Radio Atlas not found", SCR_W / 2, py + 42);
  } else {
    boom::marquee(rad.station[0] ? rad.station : "pick a genre or tap the die", px + 6, py + 22, pw - 12,
                  rad.station[0] ? th.accent : C_MUTED);
    const char *line = rad.busy[0] ? rad.busy : rad.title;
    boom::marquee(line, px + 6, py + 38, pw - 12, rad.busy[0] ? C_WARN : C_TEXT);
    fb.setTextFont(1); fb.setTextDatum(TL_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString(rad.country, px + 8, py + 57);
    char r[24];
    snprintf(r, sizeof(r), "%s  VOL %d", !rad.on ? "OFF" : rad.paused ? "PAUSED" : "LIVE", rad.vol);
    fb.setTextDatum(TR_DATUM);
    fb.drawString(r, px + pw - 8, py + 57);
    if (onAir && (now / 600) % 2) fb.fillCircle(px + pw - 8 - fb.textWidth(r) - 6, py + 60, 2, TFT_RED);
  }

  // buttons
  const char *cmd[BTN_N] = {"prev", "toggle", "next", "random", "voldn", "volup"};
  const int by = 98, bh = 24, gap = 4;
  int bw = (SCR_W - gap * (BTN_N + 1)) / BTN_N;
  for (int i = 0; i < BTN_N; i++) {
    int x = gap + i * (bw + gap);
    uint16_t col = i == 1 ? th.accent : metricCol(i % M_COUNT);
    bool over = in.sx >= x && in.sx < x + bw && in.sy >= by && in.sy < by + bh;
    bool hot = (in.down && over) || now < flashBtn[i];
    fb.fillRoundRect(x, by, bw, bh, 6, hot ? col : C_CARD);
    fb.drawRoundRect(x, by, bw, bh, 6, col);
    glyph(i, x + bw / 2, by + bh / 2, hot ? C_BG : col);
    if (tapUpIn(x, by, bw, bh)) { Serial.printf("R %s\n", cmd[i]); flashBtn[i] = now + 300; }
  }

  // genre buttons
  const int gy = 126, gh = 19;
  fb.setTextFont(2); fb.setTextDatum(MC_DATUM);
  for (int i = 0; i < GENRE_N; i++) {
    int x = gap + i * (bw + gap);
    bool sel = !strcmp(rad.listLabel, GENRE_MATCH[i]);
    bool over = in.sx >= x && in.sx < x + bw && in.sy >= gy && in.sy < gy + gh;
    bool hot = (in.down && over) || now < flashGenre[i] || sel;
    fb.fillRoundRect(x, gy, bw, gh, 9, hot ? th.accent : C_BG);
    fb.drawRoundRect(x, gy, bw, gh, 9, th.accent);
    fb.setTextColor(hot ? C_BG : th.accent);
    fb.drawString(GENRES[i], x + bw / 2, gy + gh / 2);
    if (tapUpIn(x, gy, bw, gh)) {
      Serial.printf("R genre %d\n", i);
      flashGenre[i] = now + 400;
      scroll = 0;
    }
  }

  // station list (swipe up/down to scroll)
  const int ly = 149, rh = 18;
  if (rad.listCur != lastCur) {                             // keep the playing station in view
    lastCur = rad.listCur;
    if (rad.listCur >= 0 && (rad.listCur < scroll || rad.listCur >= scroll + ROWS))
      scroll = max(0, rad.listCur - 1);
  }
  if (in.justUp && in.sy >= ly) {
    int ddy = in.y - in.sy, ddx = in.x - in.sx;
    if (abs(ddy) > 25 && abs(ddy) > abs(ddx)) scroll += ddy < 0 ? 3 : -3;
  }
  scroll = constrain(scroll, 0, max(0, rad.listN - ROWS));

  fb.setTextFont(2);
  if (rad.listN == 0) {
    fb.setTextDatum(MC_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString("tap a genre to load stations", SCR_W / 2, ly + 2 * rh);
  }
  for (int r = 0; r < ROWS && scroll + r < rad.listN; r++) {
    int idx = scroll + r, y = ly + r * rh;
    bool cur = idx == rad.listCur && live && rad.on;
    if (cur) fb.fillRoundRect(2, y, SCR_W - 16, rh - 1, 3, th.accent);
    char num[6]; snprintf(num, sizeof(num), "%d", idx + 1);
    fb.setTextDatum(TR_DATUM); fb.setTextColor(cur ? C_BG : C_MUTED);
    fb.drawString(num, 26, y);
    fb.setTextDatum(TL_DATUM); fb.setTextColor(cur ? C_BG : C_TEXT);
    fb.drawString(rad.list[idx], 32, y);
    if (!cur && tapUpIn(0, y, SCR_W - 14, rh)) Serial.printf("R play %d\n", idx);
  }
  if (rad.listN > ROWS) {                                    // scroll bar
    int th_ = ROWS * rh, bar = max(10, th_ * ROWS / rad.listN);
    int by2 = ly + (th_ - bar) * scroll / max(1, rad.listN - ROWS);
    fb.drawFastVLine(SCR_W - 6, ly, th_, C_BORDER);
    fb.fillRect(SCR_W - 8, by2, 5, bar, th.accent);
  }

  if (!pc.valid) {
    fb.setTextDatum(MC_DATUM); fb.setTextFont(2); fb.setTextColor(C_WARN);
    fb.fillRoundRect(SCR_W / 2 - 70, SCR_H / 2 - 11, 140, 22, 6, C_BG);
    fb.drawString("PC NOT CONNECTED", SCR_W / 2, SCR_H / 2);
  }

  if (swipeDir() > 0) g_screen = SCR_BOOM;
}

} // namespace radio
