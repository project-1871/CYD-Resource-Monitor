#pragma once
#include <math.h>
#include <stdint.h>
// The drawing itself isn't in the repo (it's someone else's art): run
// tools/face_setup.py <your picture> to click its eyes/mouth in a browser and
// bake src/face_art.h. Without it the mouth animates on blank paper.
#if __has_include("face_art.h")
#include "face_art.h"
#else
#define CAT_W 0
#define CAT_H 212
#define CAT_X 0
#define CAT_STRIDE 1
#define CAT_EYE_Y0 0
#define CAT_EYE_Y1 0
#define CAT_EYE0_X 120.0f
#define CAT_EYE0_Y 90.0f
#define CAT_EYE1_X 200.0f
#define CAT_EYE1_Y 90.0f
#define CAT_EYE_R 19.3f
#define CAT_MOUTH_X 160.0f
#define CAT_MOUTH_Y 120.0f
#define CAT_SCALE 0.4f
#define CAT_CHEEK0_X 118.0f
#define CAT_CHEEK0_Y 100.0f
#define CAT_CHEEK1_X 202.0f
#define CAT_CHEEK1_Y 100.0f
static const uint8_t catArt[1] = {0};
static const uint8_t catEyesShut[1] = {0};
static const uint8_t catEyesBlank[1] = {0};
#endif

// ── Talking face ────────────────────────────────────
// An anime cat girl in black-and-white line art (baked from a drawing by
// tools/make_face.py into face_art.h, see below). The drawn mouth was erased from
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

// Moods: picked per sentence on the PC (kokoro_synth.py) and sent as {"mood":..}.
// Each one gets comic eyes/brows and a manga symbol drawn over the art.
enum { MOOD_NEUTRAL = 0, MOOD_HAPPY, MOOD_ANGRY, MOOD_SAD, MOOD_SURPRISED,
       MOOD_SMUG, MOOD_CURIOUS, MOOD_COUNT };
static const char *const moodNames[MOOD_COUNT] =
    {"neutral", "happy", "angry", "sad", "surprised", "smug", "curious"};
static int faceMood = MOOD_NEUTRAL;
static uint32_t faceMoodMs = 0;   // ms since the mood started (pop-in animation)
static uint32_t faceNowMs  = 0;   // free-running clock for wobble/twinkle
// moods that wipe the drawn eyes and paint their own
static inline bool moodOwnEyes(int m) {
  return m == MOOD_HAPPY || m == MOOD_SURPRISED || m == MOOD_SMUG;
}

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
  const uint8_t *row = catArt + y * CAT_STRIDE;
  if (y >= CAT_EYE_Y0 && y < CAT_EYE_Y1) {
    if (moodOwnEyes(faceMood)) row = catEyesBlank + (y - CAT_EYE_Y0) * CAT_STRIDE;
    else if (faceBlink)        row = catEyesShut + (y - CAT_EYE_Y0) * CAT_STRIDE;
  }
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

// ── Mood overlays ───────────────────────────────────
// Small vector helpers that paint straight into the 8-bit buffer. Black strokes
// go through inkPx (anti-aliased, darkest wins); colour fills just overwrite.

static inline void paintPx(uint8_t *dst, int x, int y, uint8_t c) {
  if (x >= 0 && x < FACE_W && y >= 0 && y < FACE_H) dst[y * FACE_W + x] = c;
}

static inline float segDist(float px, float py, float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
  float t = l2 > 0 ? fclamp(((px - x0) * dx + (py - y0) * dy) / l2, 0, 1) : 0;
  return hypotf(px - (x0 + t * dx), py - (y0 + t * dy));
}

// stroke from (x0,y0) to (x1,y1), width w; c = 0 means black ink
static void stroke(uint8_t *dst, float x0, float y0, float x1, float y1, float w, uint8_t c = 0) {
  int xa = (int)(fminf(x0, x1) - w - 1), xb = (int)(fmaxf(x0, x1) + w + 1);
  int ya = (int)(fminf(y0, y1) - w - 1), yb = (int)(fmaxf(y0, y1) + w + 1);
  for (int y = ya; y <= yb; y++)
    for (int x = xa; x <= xb; x++) {
      float cov = w / 2 + 0.5f - segDist(x + 0.5f, y + 0.5f, x0, y0, x1, y1);
      if (cov <= 0) continue;
      if (c) { if (cov > 0.5f) paintPx(dst, x, y, c); }
      else inkPx(dst, x, y, fclamp(cov, 0, 1));
    }
}

// quadratic curve through (x0,y0) -> (x2,y2) bent toward control (x1,y1)
static void curve(uint8_t *dst, float x0, float y0, float x1, float y1, float x2, float y2,
                  float w, uint8_t c = 0) {
  const int N = 10;
  float px = x0, py = y0;
  for (int i = 1; i <= N; i++) {
    float t = i / (float)N, u = 1 - t;
    float x = u * u * x0 + 2 * u * t * x1 + t * t * x2, y = u * u * y0 + 2 * u * t * y1 + t * t * y2;
    stroke(dst, px, py, x, y, w, c);
    px = x; py = y;
  }
}

static void disc(uint8_t *dst, float cx, float cy, float r, uint8_t c) {
  for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++)
      if (hypotf(x + 0.5f - cx, y + 0.5f - cy) <= r) paintPx(dst, x, y, c);
}

static void ring(uint8_t *dst, float cx, float cy, float r, float w) {
  for (int y = (int)(cy - r - w); y <= (int)(cy + r + w); y++)
    for (int x = (int)(cx - r - w); x <= (int)(cx + r + w); x++)
      inkPx(dst, x, y, w / 2 + 0.5f - fabsf(hypotf(x + 0.5f - cx, y + 0.5f - cy) - r));
}

// pop-in: overshoots a little, settles at 1 after ~250 ms
static inline float popIn(uint32_t ms) {
  float t = fclamp(ms / 250.0f, 0, 1), u = t - 1;
  return 1 + 2.7f * u * u * u + 1.7f * u * u;
}

static inline uint8_t moodColor(uint8_t r, uint8_t g, uint8_t b, int grey) {
  return faceMono ? faceInk[grey] : RGB332(r, g, b);
}

// 4-point twinkle star
static void sparkle(uint8_t *dst, float cx, float cy, float r) {
  if (r < 1) return;
  const uint8_t gold = moodColor(7, 6, 0, 2);
  for (int y = (int)(cy - r); y <= (int)(cy + r); y++)
    for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
      float dx = fabsf(x + 0.5f - cx) / r, dy = fabsf(y + 0.5f - cy) / r;
      if (sqrtf(dx) + sqrtf(dy) <= 1) paintPx(dst, x, y, gold);
    }
  disc(dst, cx, cy, r * 0.14f + 0.6f, 0xFF);
}

// 💢 anger vein: four bent strokes around a centre, pulsing
static void angerVein(uint8_t *dst, float cx, float cy, float s) {
  const uint8_t red = moodColor(7, 0, 0, 2);
  float r = 7 * s, g = 2.5f * s;
  for (int q = 0; q < 4; q++) {
    float sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
    float ax = cx + sx * g, ay = cy + sy * (g + r), bx = cx + sx * (g + r), by = cy + sy * g;
    curve(dst, ax, ay, cx + sx * g, cy + sy * g, bx, by, 3.2f * s, red);
  }
}

// sweat / tear drop, point up
static void drop(uint8_t *dst, float cx, float cy, float r) {
  const uint8_t blue = moodColor(3, 6, 3, 1);
  for (int y = (int)(cy - 2.6f * r); y <= (int)(cy + r + 1); y++)
    for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
      float px = x + 0.5f - cx, py = y + 0.5f - cy;
      bool in = py >= 0 ? hypotf(px, py) <= r : fabsf(px) <= r * (1 + py / (2.6f * r));
      if (in) paintPx(dst, x, y, blue);
    }
  disc(dst, cx - r * 0.35f, cy - r * 0.1f, r * 0.25f, 0xFF);
}

// big bold "!" or "?" (comic lettering, black with the mood colour inside)
static void glyph(uint8_t *dst, char ch, float cx, float cy, float s, uint8_t fill) {
  for (int pass = 0; pass < 2; pass++) {
    float w = (pass ? 3.5f : 6.5f) * s;
    uint8_t c = pass ? fill : 0;
    if (ch == '!') {
      stroke(dst, cx, cy - 13 * s, cx, cy + 3 * s, w, c);
      disc(dst, cx, cy + 10 * s, w * 0.55f, pass ? fill : faceInk[3]);
    } else {
      curve(dst, cx - 7 * s, cy - 8 * s, cx - 6 * s, cy - 18 * s, cx + 2 * s, cy - 15 * s, w, c);
      curve(dst, cx + 2 * s, cy - 15 * s, cx + 10 * s, cy - 11 * s, cx + 1 * s, cy - 3 * s, w, c);
      stroke(dst, cx + 1 * s, cy - 3 * s, cx, cy + 2 * s, w, c);
      disc(dst, cx, cy + 10 * s, w * 0.55f, pass ? fill : faceInk[3]);
    }
  }
}

// Everything is placed from the eyes, so it fits whatever face was baked:
// FK scales strokes to the eye size (1 = the art this was tuned on), FD is the
// distance between the eyes and FMX/FMY the point between them.
static const float eyeX[2] = {CAT_EYE0_X, CAT_EYE1_X};
static const float eyeY[2] = {CAT_EYE0_Y, CAT_EYE1_Y};
#define FK  (CAT_EYE_R / 19.3f)
#define FD  (CAT_EYE1_X - CAT_EYE0_X)
#define FMX ((CAT_EYE0_X + CAT_EYE1_X) / 2)
#define FMY ((CAT_EYE0_Y + CAT_EYE1_Y) / 2)
// a spot around the head, in eye-distances from the middle of the eyes
static inline float spotX(float u) { return fclamp(FMX + u * FD, 16, FACE_W - 16); }
static inline float spotY(float v) { return fclamp(FMY + v * FD, 18, FACE_H - 18); }

static void drawMoodEyes(uint8_t *dst) {
  for (int i = 0; i < 2; i++) {
    float cx = eyeX[i], cy = eyeY[i], side = i ? 1 : -1;   // side: outer direction
    switch (faceMood) {
      case MOOD_HAPPY:            // ^ ^
        curve(dst, cx - 13 * FK, cy + 5 * FK, cx, cy - 13 * FK, cx + 13 * FK, cy + 5 * FK, 3.5f * FK);
        break;
      case MOOD_SURPRISED:        // wide round eyes, tiny pupils
        ring(dst, cx, cy, 12 * FK, 2.5f * FK);
        disc(dst, cx, cy + FK, 4.5f * FK, faceInk[3]);
        disc(dst, cx - 1.5f * FK, cy - FK, 1.3f * FK, 0xFF);
        break;
      case MOOD_SMUG: {           // half-lidded, iris peeking under a flat lid
        for (int y = (int)cy; y <= (int)(cy + 9 * FK); y++)
          for (int x = (int)(cx - 9 * FK); x <= (int)(cx + 9 * FK); x++)
            if (hypotf(x + 0.5f - (cx + side * 2 * FK), y + 0.5f - cy) <= 8.5f * FK) paintPx(dst, x, y, faceInk[3]);
        disc(dst, cx + (side * 2 - 3) * FK, cy + 3 * FK, 1.6f * FK, 0xFF);
        stroke(dst, cx - 14 * FK, cy - (1 + side * 1.5f) * FK, cx + 14 * FK, cy - (1 - side * 1.5f) * FK, 3.5f * FK);
        curve(dst, cx - 12 * FK, cy + 11 * FK, cx, cy + 14 * FK, cx + 12 * FK, cy + 11 * FK, 1.4f * FK);
        break;
      }
    }
  }
}

static void drawMoodBrows(uint8_t *dst) {
  for (int i = 0; i < 2; i++) {
    float cx = eyeX[i], cy = eyeY[i] - 19 * FK, in = i ? -FK : FK;   // in: toward the nose
    switch (faceMood) {
      case MOOD_ANGRY:    stroke(dst, cx - in * 13, cy - 6 * FK, cx + in * 11, cy + 4 * FK, 4.0f * FK); break;
      case MOOD_SAD:      stroke(dst, cx - in * 13, cy + 3 * FK, cx + in * 11, cy - 6 * FK, 3.5f * FK); break;
      case MOOD_SURPRISED:
        curve(dst, cx - 12 * FK, cy - 4 * FK, cx, cy - 14 * FK, cx + 12 * FK, cy - 4 * FK, 3.0f * FK);
        break;
      case MOOD_SMUG:
        if (i) curve(dst, cx - 12 * FK, cy - 2 * FK, cx, cy - 12 * FK, cx + 12 * FK, cy - 6 * FK, 3.0f * FK);
        else   stroke(dst, cx - 12 * FK, cy + FK, cx + 12 * FK, cy + 3 * FK, 3.0f * FK);
        break;
      case MOOD_CURIOUS:
        if (i) curve(dst, cx - 12 * FK, cy - 3 * FK, cx, cy - 13 * FK, cx + 12 * FK, cy - 5 * FK, 3.0f * FK);
        break;
    }
  }
}

static void drawMoodSymbols(uint8_t *dst) {
  float pop = popIn(faceMoodMs), tw = faceNowMs / 1000.0f;
  switch (faceMood) {
    case MOOD_HAPPY:
      sparkle(dst, spotX(-1.39f), spotY(-1.24f), (15 + 4 * sinf(tw * 6.0f)) * FK * pop);
      sparkle(dst, spotX(1.77f), spotY(-0.26f), (12 + 4 * sinf(tw * 6.0f + 2)) * FK * pop);
      sparkle(dst, spotX(-1.65f), spotY(0.05f), (10 + 3 * sinf(tw * 6.0f + 4)) * FK * pop);
      break;
    case MOOD_ANGRY:
      angerVein(dst, spotX(0.95f), spotY(-0.91f), 1.7f * FK * pop * (1.0f + 0.12f * sinf(tw * 14.0f)));
      break;
    case MOOD_SAD: {
      float fall = fmodf(faceMoodMs / 1400.0f, 1.0f);
      drop(dst, spotX(1.26f), spotY(-0.62f) + 18 * FK * fall, 8.0f * FK * pop);
      drop(dst, eyeX[0] - 4 * FK, eyeY[0] + (16 + 12 * fall) * FK, 4.0f * FK * pop);
      break;
    }
    case MOOD_SURPRISED:
      glyph(dst, '!', spotX(1.57f), spotY(-1.19f) - 3 * fabsf(sinf(tw * 9.0f)), 1.2f * FK * pop, moodColor(7, 1, 0, 1));
      glyph(dst, '!', spotX(1.91f), spotY(-1.02f) - 3 * fabsf(sinf(tw * 9.0f + 1)), 0.9f * FK * pop, moodColor(7, 1, 0, 1));
      break;
    case MOOD_CURIOUS:
      glyph(dst, '?', spotX(1.63f), spotY(-1.19f) + 3 * sinf(tw * 4.0f), 1.15f * FK * pop, moodColor(2, 5, 3, 1));
      break;
    case MOOD_SMUG:
      sparkle(dst, spotX(1.57f), spotY(-0.57f), (12 + 3 * sinf(tw * 5.0f)) * FK * pop);
      break;
  }
}

// closed mouth for each mood (the open, talking mouth is the same for all)
static void drawClosedMouth(uint8_t *dst) {
  float x = CAT_MOUTH_X, y = CAT_MOUTH_Y;
  switch (faceMood) {
    case MOOD_ANGRY: curve(dst, x - 7, y + 3, x, y - 4, x + 7, y + 3, 1.8f); break;      // pout
    case MOOD_SAD:   curve(dst, x - 7, y + 2, x - 3, y - 2, x, y + 1, 1.5f);
                     curve(dst, x, y + 1, x + 3, y - 2, x + 7, y + 2, 1.5f); break;      // wobbly
    case MOOD_SURPRISED: ring(dst, x, y, 3.5f, 1.8f); break;                             // o
    case MOOD_SMUG:  curve(dst, x - 6, y, x + 2, y + 4, x + 8, y - 3, 1.6f); break;      // smirk
    default:         drawCatMouth(dst); break;                                           // :3
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
  if (faceMood == MOOD_SMUG || faceMood == MOOD_HAPPY) {     // extra blush
    drawBlush(dst, CAT_CHEEK0_X + 9, CAT_CHEEK0_Y - 1);
    drawBlush(dst, CAT_CHEEK1_X - 9, CAT_CHEEK1_Y - 1);
  }
  drawMoodEyes(dst);
  drawMoodBrows(dst);
  if (o < 0.08f) drawClosedMouth(dst);
  else           drawOpenMouth(dst, (o - 0.08f) / 0.92f);
  drawMoodSymbols(dst);
}
