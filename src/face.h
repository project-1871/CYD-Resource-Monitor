#pragma once
#include <math.h>
#include <stdint.h>
// The drawing itself isn't in the repo (it's someone else's art): put a
// line-art PNG at tools/art/catgirl.png and run tools/make_catgirl.py to bake
// src/catgirl_art.h. Without it the mouth animates on blank paper.
#if __has_include("catgirl_art.h")
#include "catgirl_art.h"
#else
#define CAT_W 0
#define CAT_H 212
#define CAT_X 0
#define CAT_STRIDE 1
#define CAT_EYE_Y0 0
#define CAT_EYE_Y1 0
#define CAT_MOUTH_X 160.0f
#define CAT_MOUTH_Y 120.0f
#define CAT_SCALE 0.4f
#define CAT_CHEEK0_X 118.0f
#define CAT_CHEEK0_Y 100.0f
#define CAT_CHEEK1_X 202.0f
#define CAT_CHEEK1_Y 100.0f
static const uint8_t catArt[1] = {0};
static const uint8_t catEyesShut[1] = {0};
#endif

// ── Talking face ────────────────────────────────────
// An anime cat girl in black-and-white line art (baked from a drawing by
// tools/make_catgirl.py into catgirl_art.h, see below). The drawn mouth was erased from
// the art; faceDraw paints a live one in its place: a little cat ":3" when
// closed, opening into a rounded anime mouth with a fang and a tongue. The
// eyes swap to a shut copy while faceBlink is set. Pink blush and the mouth
// are the only colour; the MONO theme prints those in grey. Plain C so it can
// be previewed on a PC (tools/face_preview.cpp).

#define FACE_W 320
#define FACE_H CAT_H

static bool faceOk   = false;
static bool faceMono = false;
static bool faceBlink = false;   // set by the caller while the eyes are shut

#define RGB332(r, g, b) (uint8_t)(((r) << 5) | ((g) << 2) | (b))
static const uint8_t faceInk[4] = {0xFF, RGB332(5, 5, 2), RGB332(2, 2, 1), 0};

static inline float fclamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

static bool faceInit(bool mono) {
  faceMono = mono;
  faceOk = true;
  return true;
}

// ink level 0..3 of the art at screen (x, y)
static inline int catLevel(int x, int y) {
  int ax = x - CAT_X;
  if (ax < 0 || ax >= CAT_W) return 0;
  const uint8_t *row = (faceBlink && y >= CAT_EYE_Y0 && y < CAT_EYE_Y1)
                           ? catEyesShut + (y - CAT_EYE_Y0) * CAT_STRIDE
                           : catArt + y * CAT_STRIDE;
  return (row[ax >> 2] >> (6 - 2 * (ax & 3))) & 3;
}

// darkest-wins: only ever darken a pixel with line ink
static inline void inkPx(uint8_t *dst, int x, int y, float cover) {
  if (x < 0 || x >= FACE_W || y < 0 || y >= FACE_H || cover < 0.2f) return;
  int lv = cover > 0.75f ? 3 : cover > 0.45f ? 2 : 1;
  uint8_t *p = dst + y * FACE_W + x;
  for (int k = 0; k < 4; k++) if (*p == faceInk[k] && k >= lv) return;
  *p = faceInk[lv];
}

// blush: three short pink hatches on each cheek
static void drawBlush(uint8_t *dst, float cx, float cy) {
  const uint8_t pink = faceMono ? faceInk[1] : RGB332(7, 4, 2);
  for (int h = -1; h <= 1; h++)
    for (int t = 0; t < 8; t++) {
      int x = (int)(cx + h * 4 + 3 - t * 0.5f), y = (int)(cy - 4 + t);
      if (x >= 0 && x < FACE_W && y >= 0 && y < FACE_H && dst[y * FACE_W + x] == 0xFF)
        dst[y * FACE_W + x] = pink;
    }
}

// closed: a little cat mouth, two lower half-circles side by side
static void drawCatMouth(uint8_t *dst) {
  const float r = 3.0f, cy = CAT_MOUTH_Y - 1;
  for (int y = (int)cy - 1; y <= (int)(cy + r) + 2; y++)
    for (int x = (int)(CAT_MOUTH_X - 2 * r) - 2; x <= (int)(CAT_MOUTH_X + 2 * r) + 2; x++) {
      float px = x + 0.5f, py = y + 0.5f;
      if (py < cy) continue;
      float best = 9;
      for (int s = -1; s <= 1; s += 2) {
        float d = fabsf(hypotf(px - (CAT_MOUTH_X + s * r), py - cy) - r);
        if (d < best) best = d;
      }
      inkPx(dst, x, y, 1.1f - best);
    }
}

// open mouth outline: half-width W, top edge (with the fang notch) and bottom
struct MouthShape { float W, top, bot; };
static inline float mouthTop(const MouthShape &m, float u) {
  float y = CAT_MOUTH_Y - m.top * powf(1 - u * u, 0.5f);
  float n = fabsf(u - 0.38f);                       // the little dip over the fang
  if (n < 0.14f) y += (0.14f - n) * m.top * 3.0f;
  return y;
}
static inline float mouthBot(const MouthShape &m, float u) {
  return CAT_MOUTH_Y + m.bot * powf(1 - u * u, 0.55f);
}
// is (x, y) inside the shape grown by g px?
static inline bool inMouth(const MouthShape &m, float x, float y, float g) {
  float u = (x - CAT_MOUTH_X) / (m.W + g);
  if (fabsf(u) >= 1) return false;
  return y > mouthTop(m, u) - g && y < mouthBot(m, u) + g;
}

static void drawOpenMouth(uint8_t *dst, float o) {
  MouthShape m = {9.5f + 5.0f * o, 1.5f + 5.0f * o, 1.5f + 9.5f * o};
  const float LINE = 1.4f;
  const uint8_t cIn  = faceMono ? faceInk[2] : RGB332(3, 0, 0);    // dark red inside
  const uint8_t cTng = faceMono ? faceInk[1] : RGB332(7, 3, 2);    // pink tongue
  const float fangX = CAT_MOUTH_X + 0.38f * m.W, fangLen = 1.5f + 3.0f * o;
  const float tongueTop = CAT_MOUTH_Y + m.bot * (0.9f - 0.45f * o);
  int x0 = (int)(CAT_MOUTH_X - m.W - 3), x1 = (int)(CAT_MOUTH_X + m.W + 3);
  int y0 = (int)(CAT_MOUTH_Y - m.top - 3), y1 = (int)(CAT_MOUTH_Y + m.bot + 3);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      int nIn = 0, nLine = 0, nTng = 0, nFang = 0;
      for (int sy = 0; sy < 4; sy++)                 // 4x4 supersampling
        for (int sx = 0; sx < 4; sx++) {
          float px = x + (sx + 0.5f) * 0.25f, py = y + (sy + 0.5f) * 0.25f;
          if (inMouth(m, px, py, 0)) {
            nIn++;
            float u = (px - CAT_MOUTH_X) / m.W;
            float ft = mouthTop(m, fclamp(u, -0.99f, 0.99f));
            if (fabsf(px - fangX) < 2.0f * (1 - (py - ft) / fangLen) && py - ft < fangLen) nFang++;
            else if (py > tongueTop + 1.5f * (u * u)) nTng++;
          } else if (inMouth(m, px, py, LINE)) {
            nLine++;
          }
        }
      if (x < 0 || x >= FACE_W || y < 0 || y >= FACE_H) continue;
      uint8_t *p = dst + y * FACE_W + x;
      if (nLine >= 3)            { inkPx(dst, x, y, (nLine + nIn * 0.6f) / 16.0f); continue; }
      if (nIn >= 8)              *p = nFang * 2 > nIn ? 0xFF : nTng * 2 > nIn ? cTng : cIn;
      else if (nIn + nLine >= 3) inkPx(dst, x, y, (nIn + nLine) / 16.0f);
    }
}

// o: mouth openness 0..1
static void faceDraw(uint8_t *dst, float o) {
  for (int y = 0; y < FACE_H; y++) {
    uint8_t *p = dst + y * FACE_W;
    for (int x = 0; x < FACE_W; x++) p[x] = faceInk[catLevel(x, y)];
  }
  drawBlush(dst, CAT_CHEEK0_X, CAT_CHEEK0_Y);
  drawBlush(dst, CAT_CHEEK1_X, CAT_CHEEK1_Y);
  if (o < 0.08f) drawCatMouth(dst);
  else           drawOpenMouth(dst, (o - 0.08f) / 0.92f);
}
