// Preview the talking face on a PC: g++ -O2 -I src tools/face_preview.cpp -o /tmp/fp && /tmp/fp out.ppm [mono]
// Writes a 3x2 sheet of the face at a few mouth openings, the last one blinking
// (RGB332 expanded to RGB888).
#include <cstdio>
#include "face.h"

int main(int argc, char **argv) {
  const float opens[] = {0, 0.15f, 0.35f, 0.6f, 1.0f, 0.5f};
  const int n = 6, cols = 3;
  static uint8_t fb[FACE_W * FACE_H];
  faceInit(argc > 2);
  FILE *f = fopen(argc > 1 ? argv[1] : "face.ppm", "wb");
  fprintf(f, "P6 %d %d 255\n", FACE_W * cols, FACE_H * (n / cols));
  static uint8_t img[FACE_H * 2][FACE_W * 3][3];
  for (int i = 0; i < n; i++) {
    faceBlink = i == n - 1;
    faceDraw(fb, opens[i]);
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
