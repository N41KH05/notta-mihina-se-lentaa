// Text drawing. Produces exactly the same pixels as Adafruit_GFX's print() with a
// custom font, but draws each glyph row as horizontal runs instead of pixel by pixel,
// and draws outlined text in one pass instead of printing the string 13 times.
#include "text.h"
#include <string.h>
#include "fonts.h"
#include "utf8.h"
#include "render.h"                              // renderAlloc

const Fnt R11 = {&F_R11, F_R11_ASC, 11}, R12 = {&F_R12, F_R12_ASC, 12}, R13 = {&F_R13, F_R13_ASC, 13},
          R14 = {&F_R14, F_R14_ASC, 14}, C12 = {&F_C12, F_C12_ASC, 12}, B12 = {&F_B12, F_B12_ASC, 12},
          B13 = {&F_B13, F_B13_ASC, 13}, B14 = {&F_B14, F_B14_ASC, 14}, B15 = {&F_B15, F_B15_ASC, 15},
          B16 = {&F_B16, F_B16_ASC, 16}, B18 = {&F_B18, F_B18_ASC, 18}, B22 = {&F_B22, F_B22_ASC, 22},
          B26 = {&F_B26, F_B26_ASC, 26}, B30 = {&F_B30, F_B30_ASC, 30}, B34 = {&F_B34, F_B34_ASC, 34};

namespace {

inline const GFXglyph* glyphOf(const GFXfont* f, uint8_t c) {
  return c >= f->first && c <= f->last ? &f->glyph[c - f->first] : nullptr;
}
inline bool bitAt(const uint8_t* bm, uint32_t i) { return bm[i >> 3] & (0x80 >> (i & 7)); }

// Calls run(x, y, w) for each horizontal run of set pixels in the string's glyphs.
// (x, y) is the baseline start, as for Adafruit_GFX::setCursor.
template <typename Run>
void forEachRun(const GFXfont* f, int x, int y, const char* s, Run run) {
  int cx = x, cy = y;
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    if (*p == '\n') { cx = 0; cy += f->yAdvance; continue; }   // (as Adafruit_GFX::write)
    if (*p == '\r') continue;
    const GFXglyph* gl = glyphOf(f, *p);
    if (!gl) continue;
    const uint8_t* bm = f->bitmap + gl->bitmapOffset;
    int w = gl->width, h = gl->height, x0 = cx + gl->xOffset, y0 = cy + gl->yOffset;
    uint32_t bit = 0;
    for (int yy = 0; yy < h; yy++) {
      int start = -1;
      for (int xx = 0; xx < w; xx++, bit++) {
        if (bitAt(bm, bit)) { if (start < 0) start = xx; }
        else if (start >= 0) { run(x0 + start, y0 + yy, xx - start); start = -1; }
      }
      if (start >= 0) run(x0 + start, y0 + yy, w - start);
    }
    cx += gl->xAdvance;
  }
}

// Scratch mask for outlined text: bit 0 = letter, bit 1 = outline (in PSRAM on the board).
const int MASK_BYTES = 12000;
uint8_t* mask = nullptr;

}  // namespace

int textW(const Fnt& fn, const char* in) {
  char buf[160];
  utf8ToFont(in, buf, sizeof buf);
  int w = 0;
  for (const uint8_t* s = (const uint8_t*)buf; *s; s++)
    if (const GFXglyph* gl = glyphOf(fn.f, *s)) w += gl->xAdvance;
  return w;
}

void text(Adafruit_GFX& g, int x, int y, const char* in, const Fnt& fn, uint16_t color) {
  char s[160];
  utf8ToFont(in, s, sizeof s);
  forEachRun(fn.f, x, y + fn.asc, s, [&](int rx, int ry, int rw) { g.drawFastHLine(rx, ry, rw, color); });
}
void textR(Adafruit_GFX& g, int xr, int y, const char* s, const Fnt& fn, uint16_t c) {
  text(g, xr - textW(fn, s), y, s, fn, c);
}
void textC(Adafruit_GFX& g, int xm, int y, const char* s, const Fnt& fn, uint16_t c) {
  text(g, xm - textW(fn, s) / 2, y, s, fn, c);
}

// The outline is every pixel within these offsets of a letter pixel.
static const int8_t HALO[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
                                 {-2, 0}, {2, 0}, {0, -2}, {0, 2}};

void haloText(Adafruit_GFX& g, int x, int y, const char* in, const Fnt& fn, uint16_t c, uint16_t halo) {
  char s[160];
  utf8ToFont(in, s, sizeof s);
  int by = y + fn.asc;
  // Bounding box of the letters.
  int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
  bool simple = true;
  forEachRun(fn.f, x, by, s, [&](int rx, int ry, int rw) {
    if (rx < x0) x0 = rx;
    if (rx + rw > x1) x1 = rx + rw;
    if (ry < y0) y0 = ry;
    if (ry + 1 > y1) y1 = ry + 1;
  });
  if (x1 <= x0) return;                              // nothing to draw
  for (const char* p = s; *p; p++) if (*p == '\n') simple = false;
  int mw = x1 - x0 + 4, mh = y1 - y0 + 4, ox = x0 - 2, oy = y0 - 2;
  if (!mask) mask = (uint8_t*)renderAlloc(MASK_BYTES);
  if (!simple || !mask || mw * mh > MASK_BYTES) {    // unusual: draw it the slow way
    for (auto& o : HALO) text(g, x + o[0], y + o[1], in, fn, halo);
    text(g, x, y, in, fn, c);
    return;
  }
  memset(mask, 0, mw * mh);
  forEachRun(fn.f, x, by, s, [&](int rx, int ry, int rw) {
    memset(mask + (ry - oy) * mw + (rx - ox), 1, rw);
  });
  for (int j = 2; j < mh - 2; j++)
    for (int i = 2; i < mw - 2; i++)
      if (mask[j * mw + i] & 1)
        for (auto& o : HALO) mask[(j + o[1]) * mw + i + o[0]] |= 2;
  // Outline pixels first, then the letters: the same result as drawing the string at
  // each offset in the outline colour and then once on top.
  for (int j = 0; j < mh; j++) {
    const uint8_t* row = mask + j * mw;
    for (int pass = 0; pass < 2; pass++) {
      int start = -1;
      for (int i = 0; i <= mw; i++) {
        bool on = i < mw && (pass == 0 ? row[i] == 2 : (row[i] & 1));
        if (on) { if (start < 0) start = i; }
        else if (start >= 0) {
          g.drawFastHLine(ox + start, oy + j, i - start, pass == 0 ? halo : c);
          start = -1;
        }
      }
    }
  }
}
