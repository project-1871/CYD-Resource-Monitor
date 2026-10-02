// Preview the talking face on a PC: g++ -O2 -I src tools/face_preview.cpp -o /tmp/fp && /tmp/fp out.ppm [mono]
// Writes a 4x2 sheet: every mood with the mouth closed, plus one talking,
// blinking frame (RGB332 expanded to RGB888).
#include <cstdio>
#include "face.h"

int main(int argc, char **argv) {
  const int n = 8, cols = 4;
  static uint8_t fb[FACE_W * FACE_H];
  faceInit(argc > 2);
  FILE *f = fopen(argc > 1 ? argv[1] : "face.ppm", "wb");
  fprintf(f, "P6 %d %d 255\n", FACE_W * cols, FACE_H * (n / cols));
  static uint8_t img[FACE_H * 2][FACE_W * 4][3];
  for (int i = 0; i < n; i++) {
    faceMood = i < MOOD_COUNT ? i : MOOD_NEUTRAL;
    faceBlink = i >= MOOD_COUNT;
    faceMoodMs = 1000; faceNowMs = 400;
    faceDraw(fb, i < MOOD_COUNT ? 0 : 0.6f);
    int ox = (i % cols) * FACE_W, oy = (i / cols) * FACE_H;
    for (int y = 0; y < FACE_H; y++)
      for (int x = 0; x < FACE_W; x++) {
        uint8_t c = fb[y * FACE_W + x];
        img[oy + y][ox + x][0] = (c >> 5) * 255 / 7;
        img[oy + y][ox + x][1] = ((c >> 2) & 7) * 255 / 7;
        img[oy + y][ox + x][2] = (c & 3) * 255 / 3;
      }
  }
  fwrite(img, 1, sizeof(img), f);
  fclose(f);
}
