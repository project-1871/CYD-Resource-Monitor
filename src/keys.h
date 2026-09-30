#pragma once
#include "data.h"
#include "icons.h"

// Quick-launch page: swipe left from the dashboard. Each tap sends
// "L <n>" back up the USB serial; the agent runs that key's command.
namespace keys {

uint32_t flashUntil[N_KEYS] = {};

void update() {
  bgGrid(26);

  // header
  fb.fillRect(4, 5, 5, 14, th.accent);
  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(TL_DATUM);
  fb.setTextColor(th.accent);
  fb.drawString("QUICK LAUNCH", 14, 4);
  bool on = pc.valid && (millis() - pc.lastRx < 1200);
  fb.fillCircle(SCR_W - 44, 12, 4, pc.valid ? (on ? C_OK : C_WARN) : C_BAD);
  if (pageButton(1)) g_screen = SCR_BOOM;
  if (rgbOn) rgbStrip(23, 2);
  else       fb.drawFastHLine(0, 24, SCR_W, C_BORDER);

  // 3 x 2 grid of big buttons, colored with the theme's metric colors
  const int gap = 6, top = 30, cols = 3, rows = 2;
  int bw = (SCR_W - gap * (cols + 1)) / cols;
  int bh = (SCR_H - top - gap * (rows + 1)) / rows;
  uint32_t now = millis();

  for (int i = 0; i < N_KEYS; i++) {
    int x = gap + (i % cols) * (bw + gap);
    int y = top + gap / 2 + (i / cols) * (bh + gap);
    uint16_t col = metricCol(i);
    bool over = in.sx >= x && in.sx < x + bw && in.sy >= y && in.sy < y + bh;
    bool hot  = (in.down && over) || now < flashUntil[i];

    fb.fillRoundRect(x, y, bw, bh, 8, hot ? col : C_CARD);
    fb.drawRoundRect(x, y, bw, bh, 8, col);
    corners(x, y, bw, bh, th.accent);

    // icon (or the label's initial if the icon name is unknown) + label underneath
    fb.setTextDatum(MC_DATUM);
    const uint8_t *ico = findIcon(keyIcon[i]);
    if (ico) {
      fb.drawBitmap(x + (bw - ICON_SZ) / 2, y + 10, ico, ICON_SZ, ICON_SZ, hot ? C_BG : col);
    } else {
      fb.setTextFont(4); fb.setTextSize(2);
      fb.setTextColor(hot ? C_BG : col);
      char ini[2] = {keyLabel[i][0], 0};
      fb.drawString(ini, x + bw / 2, y + bh / 2 - 12);
      fb.setTextSize(1);
    }
    fb.setTextFont(2);
    fb.setTextColor(hot ? C_BG : C_TEXT);
    fb.drawString(keyLabel[i], x + bw / 2, y + bh - 16);

    if (tapUpIn(x, y, bw, bh)) {
      Serial.printf("L %d\n", i);
      flashUntil[i] = now + 350;
    }
  }

  if (!pc.valid) {
    fb.setTextDatum(MC_DATUM); fb.setTextFont(2); fb.setTextColor(C_WARN);
    fb.fillRoundRect(SCR_W / 2 - 70, SCR_H / 2 - 11, 140, 22, 6, C_BG);
    fb.drawString("PC NOT CONNECTED", SCR_W / 2, SCR_H / 2);
  }

  int sw = swipeDir();
  if (sw > 0) g_screen = SCR_DASH;
  else if (sw < 0) g_screen = SCR_BOOM;
}

} // namespace keys
