#pragma once
#include <ArduinoJson.h>
#include "ui.h"
#include "face.h"

// ── Talking mouth ──────────────────────────────────
// While Alice (the PC's local TTS voice) speaks, the agent relays her
// kokoro_synth.py stream: {"talk":1} / {"talk":0} around each utterance,
// {"m":0..1} mouth openness every 40 ms, {"w":"word"} as each word is heard
// and {"p":","} for punctuation. The screen switches to a full-screen mouth
// with the words captioned underneath, then drops back when she's done.

enum { TALK_OFF = 0, TALK_ON, TALK_CLOSING };
int      talkState  = TALK_OFF;
uint32_t talkRx     = 0;     // last mouth message
uint32_t talkEndAt  = 0;
float    mouthTarget = 0, mouthOpen = 0;
uint32_t wordAt     = 0;
uint32_t mouthRx = 0, mouthFrames = 0;   // debug counters     // when the newest word arrived (caption flash)

#define TALK_HOLD_MS    700   // closed mouth stays up this long after she stops
#define TALK_LOST_MS   2500   // no frames for this long = missed the "talk":0

// captions: the finished line above (small, dim), the line being spoken (big)
char capPrev[64] = "", capCur[64] = "";
int  capLastWord = 0;        // index in capCur where the newest word starts

void captionReset() { capPrev[0] = capCur[0] = 0; capLastWord = 0; }

void captionWord(const char *w, bool glue) {
  fb.setTextFont(4); fb.setTextSize(1);
  char next[64];
  if (glue || !capCur[0]) snprintf(next, sizeof(next), "%s%s", capCur, w);
  else                    snprintf(next, sizeof(next), "%s %s", capCur, w);
  if (!glue && capCur[0] && (fb.textWidth(next) > SCR_W - 24 || strlen(next) > 60)) {
    strlcpy(capPrev, capCur, sizeof(capPrev));   // wrap: current line moves up
    strlcpy(capCur, w, sizeof(capCur));
    capLastWord = 0;
  } else {
    if (!glue) capLastWord = capCur[0] ? strlen(capCur) + 1 : 0;
    strlcpy(capCur, next, sizeof(capCur));
  }
}

// returns true if the line was a mouth message
bool mouthMsg(JsonDocument &doc) {
  uint32_t now = millis();
  if (doc["m"].is<float>()) {
    mouthTarget = constrain(doc["m"].as<float>(), 0.0f, 1.0f);
    if (talkState != TALK_ON) { talkState = TALK_ON; captionReset(); }
  } else if (doc["talk"].is<int>()) {
    if (doc["talk"].as<int>()) { talkState = TALK_ON; captionReset(); mouthTarget = 0; }
    else if (talkState == TALK_ON) { talkState = TALK_CLOSING; talkEndAt = now; mouthTarget = 0; }
  } else if (const char *w = doc["w"]) {
    captionWord(w, false); wordAt = now;
  } else if (const char *p = doc["p"]) {
    captionWord(p, true);
  } else {
    return false;
  }
  talkRx = now;
  mouthRx++;
  return true;
}

bool mouthActive() {
  uint32_t now = millis();
  if (talkState == TALK_ON && now - talkRx > TALK_LOST_MS) {
    talkState = TALK_CLOSING; talkEndAt = now; mouthTarget = 0;
  }
  if (talkState == TALK_CLOSING && now - talkEndAt > TALK_HOLD_MS) {
    talkState = TALK_OFF;
    Serial.printf("mouth rx=%u frames=%u\n", mouthRx, mouthFrames);
    mouthRx = mouthFrames = 0;
  }
  return talkState != TALK_OFF;
}

// ── Drawing ─────────────────────────────────────────
// The face itself is in face.h (the cat girl); it writes straight into the
// 8-bit frame buffer.
bool faceReady() {
  bool mono = th.strip == monoStrip;               // MONO theme: grey instead of pink/red
  if (faceOk && faceMono == mono) return true;
  return faceInit(mono);
}

// blinks every 2-5 s, sometimes twice in a row
uint32_t blinkAt = 0;
void blinkUpdate(uint32_t now) {
  const uint32_t SHUT_MS = 120;
  if (!blinkAt) blinkAt = now + 1500;
  faceBlink = now >= blinkAt && now < blinkAt + SHUT_MS;
  if (now >= blinkAt + SHUT_MS)
    blinkAt = now + (random(4) == 0 ? 180 : 2000 + random(3000));
}

void drawMouth() {
  mouthFrames++;
  mouthOpen += (mouthTarget - mouthOpen) * 0.55f;
  if (!faceReady()) return;
  blinkUpdate(millis());
  faceDraw((uint8_t *)fb.getPointer(), powf(mouthOpen, 0.8f));

  // caption: the line being spoken, under the picture (FACE_H = 212)
  fb.setTextFont(4);
  int wAll = fb.textWidth(capCur), x0 = SCR_W / 2 - wAll / 2;
  char head[64];
  strlcpy(head, capCur, min((int)sizeof(head), capLastWord + 1));
  fb.setTextDatum(TL_DATUM);
  fb.setTextColor(C_TEXT);
  fb.drawString(head, x0, FACE_H + 2);
  fb.setTextColor(th.accent);                      // the word being said
  fb.drawString(capCur + capLastWord, x0 + fb.textWidth(head), FACE_H + 2);
}
