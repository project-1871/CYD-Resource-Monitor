// ── CYD Resource Monitor ───────────────────────────
// PC/Mac resource dashboard on an ESP32-2432S028R-
// compatible 2.8" TFT board. Data arrives as JSON
// lines over USB serial from agent/monitor_agent.py.

#include <Arduino.h>
#include "config.h"
#include "ui.h"
#include "data.h"
#include "dash.h"
#include "detail.h"
#include "settings.h"
#include "keys.h"

// Full-screen HELLO / GOODBYE, typed out letter by letter in the theme accent
void drawMsg() {
  bool bye = msgKind == MSG_BYE;
  const char *word = bye ? "GOODBYE" : "HELLO";
  uint32_t t = millis() - msgAt;
  char shown[8];
  int n = min((int)strlen(word), (int)(t / 90) + 1);
  strlcpy(shown, word, n + 1);

  fb.fillSprite(C_BG);
  bgGrid(0);
  corners(12, 12, SCR_W - 24, SCR_H - 24, th.accent);
  if (rgbOn) { rgbStrip(60, 2); rgbStrip(SCR_H - 62, 2); }

  fb.setTextDatum(MC_DATUM); fb.setTextFont(4); fb.setTextSize(2);
  fb.setTextColor(th.accent);
  fb.drawString(shown, SCR_W / 2, SCR_H / 2 - 14);
  if (n < (int)strlen(word) || (t / 400) % 2) {   // cursor block
    int cx = SCR_W / 2 + fb.textWidth(shown) / 2 + 6;
    fb.fillRect(cx, SCR_H / 2 - 36, 12, 42, th.accent);
  }
  fb.setTextSize(1); fb.setTextFont(2); fb.setTextColor(C_TEXT);
  char sub[48];
  snprintf(sub, sizeof(sub), bye ? "%s signing off" : "%s is online",
           pc.host[0] ? pc.host : "PC");
  fb.drawString(sub, SCR_W / 2, SCR_H / 2 + 34);
}

void setup() {
  Serial.begin(115200);
  Serial.setRxBufferSize(2048);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touch.setCal(526, 3443, 750, 3377, 320, 240, 1);

  fb.setColorDepth(8);
  if (!fb.createSprite(SCR_W, SCR_H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc failed!", 20, 100, 2);
    while (true) delay(1000);
  }

  loadSettings();
}

void loop() {
  inputUpdate();
  serialPoll();

  fb.fillSprite(C_BG);
  switch (g_screen) {
    case SCR_DASH:     dash::update();     break;
    case SCR_DETAIL:   dpage::update();   break;
    case SCR_SETTINGS: settings::update(); break;
    case SCR_KEYS:     keys::update();     break;
  }
  if (msgKind == MSG_HELLO && millis() - msgAt > HELLO_MS) msgKind = MSG_NONE;
  if (msgKind != MSG_NONE) drawMsg();
  if (th.glitch) glitchFx();
  if (th.scan)   scanFx();
  fb.pushSprite(0, 0);
}
