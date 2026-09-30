#pragma once
#include "data.h"

// Boombox page: swipe left from quick launch. Shows what the PC's media
// player is doing (np, from the agent), pulses the speakers while it plays,
// and sends "M play|next|prev|volup|voldn" / "M goto <n>" back up the serial.
namespace boom {

const int BTN_N = 5;
uint32_t flashUntil[BTN_N] = {};

// a speaker: grille ring, cone that pumps with k (0..1), dust cap
void speaker(int cx, int cy, int r, float k, uint16_t col) {
  fb.fillCircle(cx, cy, r, C_CARD);
  fb.drawCircle(cx, cy, r, col);
  fb.drawCircle(cx, cy, r - 3, C_BORDER);
  for (int a = 0; a < 360; a += 30) {                    // grille bolts
    float rad = a * DEG_TO_RAD;
    fb.fillCircle(cx + cosf(rad) * (r - 7), cy + sinf(rad) * (r - 7), 1, C_MUTED);
  }
  int rc = r * 0.62f + k * 3;
  fb.fillCircle(cx, cy, rc, C_BG);
  fb.drawCircle(cx, cy, rc, col);
  fb.drawCircle(cx, cy, rc - 5 - (int)(k * 2), col);
  fb.fillCircle(cx, cy, r * 0.2f + k * 3, col);
}

// playback symbols drawn as shapes (the fonts have no media glyphs)
void glyph(int i, int cx, int cy, uint16_t c) {
  switch (i) {
    case 1:  // previous: bar + triangle
      fb.fillRect(cx - 9, cy - 7, 3, 14, c);
      fb.fillTriangle(cx + 7, cy - 7, cx + 7, cy + 7, cx - 5, cy, c);
      break;
    case 2:  // play / pause
      if (np.ok && np.state == 1) { fb.fillRect(cx - 7, cy - 8, 5, 16, c); fb.fillRect(cx + 2, cy - 8, 5, 16, c); }
      else fb.fillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 9, cy, c);
      break;
    case 3:  // next: triangle + bar
      fb.fillTriangle(cx - 7, cy - 7, cx - 7, cy + 7, cx + 5, cy, c);
      fb.fillRect(cx + 6, cy - 7, 3, 14, c);
      break;
    default: {  // volume down / up
      fb.setTextFont(2); fb.setTextDatum(MC_DATUM); fb.setTextColor(c);
      fb.drawString(i == 0 ? "VOL -" : "VOL +", cx, cy);
    }
  }
}

void fmtTime(char *out, size_t n, float s) {
  int t = s < 0 ? 0 : (int)s;
  snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

// text that scrolls sideways when it doesn't fit in w
void marquee(const char *s, int x, int y, int w, uint16_t col) {
  int tw = fb.textWidth(s);
  fb.setTextColor(col);
  fb.setTextDatum(TL_DATUM);
  if (tw <= w) { fb.drawString(s, x + (w - tw) / 2, y); return; }
  int span = tw + 30;
  int off = (millis() / 40) % span;
  fb.setViewport(x, y, w, 18);
  fb.drawString(s, -off, 0);
  fb.drawString(s, -off + span, 0);
  fb.resetViewport();
}

void update() {
  uint32_t now = millis();
  bool live = np.ok && now - np.rx < 4000;
  bool playing = live && np.state == 1;
  float t = now / 1000.0f;

  bgGrid(26);

  // header
  fb.fillRect(4, 5, 5, 14, th.accent);
  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(TL_DATUM);
  fb.setTextColor(th.accent);
  fb.drawString("BOOMBOX", 14, 4);
  if (live && np.player[0]) {
    char h[32];
    if (np.tlTotal > 0) snprintf(h, sizeof(h), "%s  %d", np.player, np.tlTotal);
    else strlcpy(h, np.player, sizeof(h));
    fb.setTextColor(C_MUTED);
    fb.drawString(h, 82, 4);
  }
  bool on = pc.valid && (now - pc.lastRx < 1200);
  fb.fillCircle(SCR_W - 44, 12, 4, pc.valid ? (on ? C_OK : C_WARN) : C_BAD);
  if (pageButton(2)) g_screen = SCR_RADIO;
  if (rgbOn) rgbStrip(23, 2);
  else       fb.drawFastHLine(0, 24, SCR_W, C_BORDER);

  // body + speakers (a fake beat: two sines, pumps only while playing)
  fb.fillRoundRect(2, 28, SCR_W - 4, 90, 10, C_CARD);
  fb.drawRoundRect(2, 28, SCR_W - 4, 90, 10, C_BORDER);
  float kL = playing ? fabsf(sinf(t * 7.3f)) * (0.6f + 0.4f * fabsf(sinf(t * 1.7f))) : 0;
  float kR = playing ? fabsf(sinf(t * 7.3f + 0.6f)) * (0.6f + 0.4f * fabsf(sinf(t * 2.1f))) : 0;
  speaker(42, 73, 38, kL, metricCol(M_CPU));
  speaker(SCR_W - 42, 73, 38, kR, metricCol(M_GPU));

  // LCD
  const int lx = 86, ly = 33, lw = SCR_W - 2 * lx, lh = 80;
  fb.fillRoundRect(lx, ly, lw, lh, 5, C_BG);
  fb.drawRoundRect(lx, ly, lw, lh, 5, th.accent);
  fb.setTextFont(2);
  if (!live) {
    fb.setTextDatum(MC_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString("NOTHING PLAYING", lx + lw / 2, ly + 28);
    fb.setTextColor(th.accent);
    fb.drawString("tap PLAY", lx + lw / 2, ly + 50);
  } else {
    marquee(np.title[0] ? np.title : "(untitled)", lx + 4, ly + 3, lw - 8, C_TEXT);
    marquee(np.artist, lx + 4, ly + 20, lw - 8, C_MUTED);

    // equalizer bars
    const int bars = 14, bw = (lw - 16) / bars;
    for (int i = 0; i < bars; i++) {
      float v = playing ? fabsf(sinf(t * (3.1f + i * 0.37f) + i)) * (0.35f + 0.65f * fabsf(sinf(t * 0.9f + i * 0.5f))) : 0.05f;
      int h = 2 + (int)(v * 16);
      fb.fillRect(lx + 8 + i * bw, ly + 56 - h, bw - 2, h, i < bars / 2 ? metricCol(M_CPU) : metricCol(M_GPU));
    }

    // progress (runs on locally between the agent's once-a-second updates)
    float pos = np.pos + (playing ? (now - np.rx) / 1000.0f : 0);
    if (np.len > 0 && pos > np.len) pos = np.len;
    int pw = lw - 16;
    fb.fillRect(lx + 8, ly + 60, pw, 3, C_BORDER);
    if (np.len > 0) fb.fillRect(lx + 8, ly + 60, (int)(pw * pos / np.len), 3, th.accent);
    char a[12], b[12], tt[28];
    fmtTime(a, sizeof(a), pos);
    fmtTime(b, sizeof(b), np.len);
    if (np.len > 0) snprintf(tt, sizeof(tt), "%s / %s", a, b); else strlcpy(tt, a, sizeof(tt));
    fb.setTextFont(1); fb.setTextDatum(TL_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString(np.state == 2 ? "PAUSED" : tt, lx + 8, ly + 67);
  }
  if (np.vol >= 0) {                                     // volume, bottom right of the LCD
    char v[10]; snprintf(v, sizeof(v), "VOL %d", np.vol);
    fb.setTextFont(1); fb.setTextDatum(TR_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString(v, lx + lw - 8, ly + 67);
  }

  // buttons: VOL-  prev  play/pause  next  VOL+
  const char *cmd[BTN_N] = {"voldn", "prev", "play", "next", "volup"};
  const int by = 122, bh = 26, gap = 4;
  int bwid = (SCR_W - gap * (BTN_N + 1)) / BTN_N;
  for (int i = 0; i < BTN_N; i++) {
    int x = gap + i * (bwid + gap);
    uint16_t col = i == 2 ? th.accent : metricCol((i + 2) % M_COUNT);
    bool over = in.sx >= x && in.sx < x + bwid && in.sy >= by && in.sy < by + bh;
    bool hot = (in.down && over) || now < flashUntil[i];
    fb.fillRoundRect(x, by, bwid, bh, 6, hot ? col : C_CARD);
    fb.drawRoundRect(x, by, bwid, bh, 6, col);
    glyph(i, x + bwid / 2, by + bh / 2, hot ? C_BG : col);
    if (tapUpIn(x, by, bwid, bh)) {
      Serial.printf("M %s\n", cmd[i]);
      flashUntil[i] = now + 300;
    }
  }

  // track list: the song before, the current one (highlighted), what's next
  const int ty = 152, rh = 17, rows = 5;
  fb.drawFastHLine(0, ty - 2, SCR_W, C_BORDER);
  fb.setTextFont(2);
  if (live && np.tlN > 0) {
    for (int r = 0; r < rows && r < np.tlN; r++) {
      int idx = np.tlStart + r;
      int y = ty + r * rh;
      bool cur = idx == np.tlCur;
      if (cur) fb.fillRoundRect(2, y, SCR_W - 4, rh - 1, 3, th.accent);
      char num[8]; snprintf(num, sizeof(num), "%d", idx + 1);
      fb.setTextDatum(TR_DATUM);
      fb.setTextColor(cur ? C_BG : C_MUTED);
      fb.drawString(num, 38, y);
      fb.setTextDatum(TL_DATUM);
      fb.setTextColor(cur ? C_BG : C_TEXT);
      char line[40]; strlcpy(line, np.tl[r], sizeof(line));
      while (fb.textWidth(line) > SCR_W - 52 && strlen(line) > 3) line[strlen(line) - 1] = 0;
      fb.drawString(line, 44, y);
      if (!cur && tapUpIn(0, y, SCR_W, rh)) Serial.printf("M goto %d\n", idx);
    }
  } else {
    fb.setTextDatum(MC_DATUM); fb.setTextColor(C_MUTED);
    fb.drawString(live ? "no track list from this player" : "your music library opens in VLC",
                  SCR_W / 2, ty + 2 * rh);
  }

  if (!pc.valid) {
    fb.setTextDatum(MC_DATUM); fb.setTextFont(2); fb.setTextColor(C_WARN);
    fb.fillRoundRect(SCR_W / 2 - 70, SCR_H / 2 - 11, 140, 22, 6, C_BG);
    fb.drawString("PC NOT CONNECTED", SCR_W / 2, SCR_H / 2);
  }

  int sw = swipeDir();
  if (sw > 0) g_screen = SCR_KEYS;
  else if (sw < 0) g_screen = SCR_RADIO;
}

} // namespace boom
