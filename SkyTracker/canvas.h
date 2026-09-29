// A 16-bit drawing surface. Same as Adafruit's GFXcanvas16, but it can draw into a
// buffer owned by someone else (the LCD frame buffers), and it fills rectangles row
// by row. The library fills them column by column, which is several times slower on
// a frame buffer in PSRAM because every pixel lands on a different cache line.
#pragma once
#include <Adafruit_GFX.h>

class Canvas : public GFXcanvas16 {
 public:
  Canvas(uint16_t w, uint16_t h, bool allocate = true) : GFXcanvas16(w, h, allocate) {}
  void use(uint16_t* buf) { buffer = buf; }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w <= 0 || h == 0) return;
    if (h < 0) { h = -h; y -= h - 1; }            // as the library treats negative heights
    for (int16_t i = 0; i < h; i++) drawFastHLine(x, y + i, w, color);
  }
};
