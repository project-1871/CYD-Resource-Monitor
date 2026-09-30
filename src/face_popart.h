#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ── Talking face (v6.1 pop-art lips, retired: kept for reference, not included) ──
// A pop-art close-up of a mouth: cel-shaded magenta lips with pale shine
// streaks, solid black shadow shapes, and Ben-Day halftone dots where one
// tone fades into the next, on pale peach "paper". The MONO theme prints it
// as plain black-and-white halftone. The resting face is shaded once
// (faceInit) into an RGB332 buffer; each frame (faceDraw) re-samples it with
// the upper lip lifted and the jaw dropped, and paints the open mouth (teeth,
// tongue) into the gap. Plain C so it can be previewed on a PC
// (tools/face_preview.cpp).
//
// The face is modelled in "face units" and drawn zoomed in: face point
// (FACE_CX + mx, FACE_CY + my) lands on screen at (FACE_CX + mx*ZOOM, SEAM_Y + my*ZOOM).

#define FACE_W   320
#define FACE_H   178
#define FACE_CX  160.0f
#define FACE_CY   80.0f   // lip seam at rest, face units
#define SEAM_Y    92.0f   // ...and on screen
#define ZOOM       2.1f
#define LIP_W     66.0f   // mouth half-width, face units
#define OPEN_MAX  44.0f   // lip gap at full openness, screen px
#define DOT_CELL   4.0f   // halftone screen pitch, px

static uint8_t *faceRest = nullptr;
static bool     faceMono = false;

static inline float fsq(float v) { return v * v; }
static inline float fclamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float fsmooth(float a, float b, float v) {
  float t = fclamp((v - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}
static inline float gauss(float d, float s) { return expf(-fsq(d / s)); }
static inline void  fmix(float *c, float r, float g, float b, float k) {
  c[0] += (r - c[0]) * k; c[1] += (g - c[1]) * k; c[2] += (b - c[2]) * k;
}

// Each part of the face has four inks from light to dark. A tone T in 0..3
// picks two neighbouring inks and prints the darker one as halftone dots
// whose size is the fraction between them, so flat cel-shaded bands get a
// dotted edge where they meet.
enum { R_SKIN, R_LIP, R_TEETH, R_TONGUE, R_N };
#define RGB332(r, g, b) (uint8_t)(((r) << 5) | ((g) << 2) | (b))
static const uint8_t palColor[R_N][4] = {
  {RGB332(7, 6, 2), RGB332(7, 4, 2), RGB332(4, 1, 1), 0},   // peach paper, pink dots, shadow
  {RGB332(7, 4, 3), RGB332(6, 0, 2), RGB332(3, 0, 1), 0},   // shine, magenta, deep plum
  {RGB332(7, 7, 3), RGB332(5, 7, 3), RGB332(2, 4, 2), 0},   // white, mint, teal
  {RGB332(7, 3, 2), RGB332(5, 0, 1), RGB332(2, 0, 0), 0},   // tongue
};
static const uint8_t palMono[4] = {0xFF, RGB332(5, 5, 2), RGB332(2, 2, 1), 0};

// squared distance to the nearest halftone dot centre on a 45-degree screen,
// in cell units (0 at a centre, 0.5 at a cell corner)
static inline float dotDist2(int x, int y) {
  float a = (x + y + 400) * (0.70711f / DOT_CELL), b = (x - y + 400) * (0.70711f / DOT_CELL);
  a -= (int)a + 0.5f; b -= (int)b + 0.5f;
  return a * a + b * b;
}

static inline uint8_t faceInk(int region, float T, int x, int y) {
  T = fclamp(T, 0, 2.999f);
  if (faceMono) {                                   // black dots on white, sized by darkness
    float f = powf(T / 3, 1.5f);
    return dotDist2(x, y) < 0.6084f * f ? 0 : 0xFF;
  }
  int i = (int)T;
  float f = T - i;
  const uint8_t *p = palColor[region];
  return dotDist2(x, y) < 0.6084f * f ? p[i + 1] : p[i];
}

// lip geometry at rest (face units), u = -1..1 across the mouth
static inline float lipSeam(float u) {          // corners up in a smile, the
  return FACE_CY - 5 * u * u + 1.4f * gauss(u, 0.14f);   // upper lip's bead dips in the middle
}
static inline float lipUpper(float u) {        // upper lip height: two rounded humps
  float bow = 1 + 0.22f * gauss(fabsf(u) - 0.30f, 0.22f) - 0.24f * gauss(u, 0.13f);
  return 17.0f * powf(1 - u * u, 0.5f) * bow;
}
static inline float lipLower(float u) { return 22.0f * powf(1 - u * u, 0.45f); }

// per-column lip geometry for faceShade (face units)
struct LipCol { float dx, au, ys, hu, hl, top, bot, nTop, nBot, shadowY, shadowK, cornerX; };
static LipCol lipCol(float x) {
  LipCol c;
  c.dx = x - FACE_CX; c.au = fabsf(c.dx / LIP_W);
  float uu = fminf(c.au, 0.999f);
  c.ys = lipSeam(uu); c.hu = lipUpper(uu); c.hl = lipLower(uu);
  c.top = c.ys - c.hu; c.bot = c.ys + c.hl;
  // outline distances get divided by these to correct for the slope of the edge
  float e = 0.02f, u2 = fminf(uu + e, 0.999f), u1 = fmaxf(uu - e, 0);
  float sTop = ((lipSeam(u2) - lipUpper(u2)) - (lipSeam(u1) - lipUpper(u1))) / ((u2 - u1) * LIP_W);
  float sBot = ((lipSeam(u2) + lipLower(u2)) - (lipSeam(u1) + lipLower(u1))) / ((u2 - u1) * LIP_W);
  c.nTop = sqrtf(1 + sTop * sTop); c.nBot = sqrtf(1 + sBot * sBot);
  c.shadowY = c.bot + 6;
  c.shadowK = c.au < 1.1f ? fsmooth(1.05f, 0.35f, c.au) * (c.dx < 0 ? 1.0f : 0.8f) : 0;
  c.cornerX = fminf(fabsf(c.dx - (LIP_W + 0.5f)), fabsf(c.dx + (LIP_W + 0.5f)));
  return c;
}

// tone of the resting face at row y of a column: sets *region, returns T 0..3
static float faceShade(const LipCol &c, float y, int *region) {
  float dx = c.dx, adx = fabsf(dx);
  *region = R_SKIN;

  // skin: a light even dot screen that thickens into shadow
  float T = 0.14f;
  T += 0.9f * gauss(y - 18, 6) * fsmooth(34, 12, adx);            // under the nose
  float upTop = FACE_CY + 1.4f - lipUpper(0);
  T += 0.35f * gauss(dx, 5) * fsmooth(22, 32, y) * fsmooth(upTop, upTop - 6, y);   // philtrum
  float t = (y - 16) / 86.0f;                                     // smile lines
  if (t > 0 && t < 1) T += 0.55f * gauss(adx - (40 + 50 * powf(t, 1.2f)), 6) * sinf(t * 3.14159f);

  // cel shadows: a black crescent under the lower lip and pools at the corners
  if (c.shadowK > 0) T += 2.1f * gauss(y - c.shadowY, 5.5f) * c.shadowK;
  if (c.cornerX < 12) T += 2.4f * gauss(sqrtf(fsq(c.cornerX) + fsq((y - lipSeam(1)) * 1.2f)), 4.0f);

  if (c.au < 1) {
    float dTop = (y - c.top) / c.nTop, dBot = (c.bot - y) / c.nBot;
    if (dTop > -0.45f && dBot > -0.45f) {
      *region = R_LIP;
      if (dTop < 0.35f || dBot < 0.35f) return 2.3f;   // soft plum outline
      if (fabsf(y - c.ys) < 0.45f + 0.4f * (1 - c.au)) return 2.7f;   // where the lips meet
      if (y < c.ys) {                                // upper lip, turned away from the light
        float k = (c.ys - y) / fmaxf(c.hu, 1);       // 0 at the seam .. 1 at the top
        T = 1.9f - 0.9f * k;
        T -= 1.2f * gauss(k - 0.55f, 0.22f) * gauss(dx + 18, 18);   // a small shine streak
      } else {                                       // lower lip: full, glossy
        float k = (y - c.ys) / fmaxf(c.hl, 1);
        T = 1.0f + 2.86f * fsq(k - 0.35f) + 0.9f * gauss(k, 0.18f);
        // two shine streaks across the fullest part
        T -= 1.3f * gauss(k - 0.40f, 0.15f) * gauss(dx + 14, 20);
        T -= 0.9f * gauss(k - 0.52f, 0.12f) * gauss(dx - 22, 12);
      }
      T += 0.9f * fsmooth(0.75f, 1.0f, c.au);        // darker toward the corners
      return T;
    }
  }
  return T;
}

// shade the resting face once; returns false if out of memory
static bool faceInit(bool mono) {
  if (!faceRest) faceRest = (uint8_t *)malloc(FACE_W * FACE_H);
  if (!faceRest) return false;
  faceMono = mono;
  for (int x = 0; x < FACE_W; x++) {
    LipCol c = lipCol(FACE_CX + (x + 0.5f - FACE_CX) / ZOOM);
    for (int y = 0; y < FACE_H; y++) {
      int region;
      float T = faceShade(c, FACE_CY + (y + 0.5f - SEAM_Y) / ZOOM, &region);
      faceRest[y * FACE_W + x] = faceInk(region, T, x, y);
    }
  }
  return true;
}

// upper teeth, from the middle out: edges in |u| (perspective squeezes them
// toward the corners) and relative length
static const float toothEdge[] = {0, 0.19f, 0.34f, 0.48f, 0.60f, 0.70f, 0.79f, 0.87f};
static const float toothLen[]  = {1.0f, 0.86f, 0.94f, 0.80f, 0.72f, 0.62f, 0.55f};
#define N_TOOTH 7

// one column of a tooth row: which tooth, its length, how far to the
// nearest gap and how lit it is (rounded across); computed once per column
struct ToothCol { float h, gapPx, s; };
static ToothCol toothCol(float au, float len, bool lower) {
  ToothCol t = {-1, 0, 0};
  if (au >= toothEdge[N_TOOTH]) return t;
  int k = 0;
  while (k < N_TOOTH - 1 && au >= toothEdge[k + 1]) k++;
  float at = (au - toothEdge[k]) / (toothEdge[k + 1] - toothEdge[k]);   // 0..1 across the tooth
  float c  = 2 * at - 1;
  t.h = len * (lower ? 0.9f : toothLen[k]) * (1 - 0.28f * c * c * c * c);   // rounded tips
  if (k == 2 && !lower) t.h *= 1 - 0.18f * fsq(c);   // canine comes to a soft point
  t.gapPx = fminf(at, 1 - at) * (toothEdge[k + 1] - toothEdge[k]) * LIP_W * ZOOM;
  t.s = (1 - c * c) * (1 - fsq(au / toothEdge[N_TOOTH]));   // 1 = full light
  return t;
}

// tone of a tooth pixel v px from the gum end, or -1 for none
static inline float toothTone(const ToothCol &t, float v) {
  if (v > t.h) return -1;
  if (t.gapPx < 0.9f || v > t.h - 1.3f) return 2.3f;   // soft gaps and tips
  return 1.6f * (1 - t.s) + 0.9f * fsmooth(4, 0, v);   // shadow up under the lip
}

// draw the face into an 8-bit (RGB332) frame buffer, stride FACE_W, mouth open 0..1
static void faceDraw(uint8_t *dst, float o) {
  const float J = 0.7f * OPEN_MAX * o;               // jaw drop
  const float LW = LIP_W * ZOOM;
  for (int x = 0; x < FACE_W; x++) {
    float dx = x + 0.5f - FACE_CX, u = dx / LW, au = fabsf(u);
    bool  in = au < 1;
    float ys  = SEAM_Y + ((in ? lipSeam(u) : lipSeam(1)) - FACE_CY) * ZOOM;
    float gap = in ? OPEN_MAX * o * powf(1 - u * u, 0.45f) : 0;
    float yU = ys - 0.3f * gap, yL = ys + 0.7f * gap;
    float jaw = J * gauss(dx, 105 * ZOOM);
    float tr = LW * 0.62f;
    float tongueH  = gap * 0.40f * (fabsf(dx) < tr ? sqrtf(1 - fsq(dx / tr)) : 0);
    float teethLen = fminf(gap * 0.55f, 4 + 18 * sqrtf(fmaxf(0, 1 - fsq(u / 0.9f))));
    float lowLen   = fminf(gap * 0.18f, 10 * sqrtf(fmaxf(0, 1 - fsq(u / 0.62f))));
    ToothCol up = toothCol(au, teethLen, false), lo = toothCol(au, lowLen, true);

    // rows well above the lip are a straight copy, rows well below it a constant
    // jaw shift; only the bands next to the opening need per-pixel math
    int yA = (int)(yU - 40), yB = (int)(yL + 36) + 1;
    if (yA < 0) yA = 0;
    if (yB > FACE_H) yB = FACE_H;
    const uint8_t *src = faceRest + x;
    uint8_t *p = dst + x;
    int y = 0;
    for (; y < yA; y++, p += FACE_W) *p = src[y * FACE_W];
    for (; y < yB; y++, p += FACE_W) {
      float fy = y + 0.5f;
      if (fy <= yU) {                                 // upper face, lip lifted near the mouth
        float fu = 1 - (yU - fy) * (1.0f / 40);
        int sy = (int)(fy + 0.3f * gap * (fu > 0 ? fu : 0));
        *p = src[(sy < FACE_H ? sy : FACE_H - 1) * FACE_W];
      } else if (fy >= yL) {                          // lower lip and jaw, dropped
        float t = fclamp((fy - yL) * (1.0f / 36), 0, 1);
        int sy = (int)(fy - (0.7f * gap + (jaw - 0.7f * gap) * t));
        if (sy < ys) sy = (int)ys;
        *p = src[(sy < 0 ? 0 : sy) * FACE_W];
      } else {                                        // inside the open mouth: black
        float v = fy - yU, tv = yL - fy;
        uint8_t c = 0;
        if (tv < tongueH && tongueH - tv > 1.2f)
          c = faceInk(R_TONGUE, 1.3f + 1.2f * fsmooth(tongueH, 0, tongueH - tv), x, y);
        float T = toothTone(up, v);
        if (T < 0 && gap > 16 && au < 0.62f) T = toothTone(lo, tv);
        if (T >= 0) c = faceInk(R_TEETH, T, x, y);
        *p = c;
      }
    }
    for (; y < FACE_H; y++, p += FACE_W) {
      int sy = (int)(y + 0.5f - jaw);
      *p = src[(sy < 0 ? 0 : sy) * FACE_W];
    }
  }
}
