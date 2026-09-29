// Text in the screen fonts (fonts.h). Strings are UTF-8; ä, ö, å and a few other
// letters are mapped to the fonts' extra glyphs (utf8.h).
#pragma once
#include <Adafruit_GFX.h>

struct Fnt { const GFXfont* f; int asc; int size; };
extern const Fnt R11, R12, R13, R14, C12, B12, B13, B14, B15, B16, B18, B22, B26, B30, B34;

int textW(const Fnt& fn, const char* s);                 // width in pixels
// Top-left at (x, y).
void text(Adafruit_GFX& g, int x, int y, const char* s, const Fnt& fn, uint16_t color);
void textR(Adafruit_GFX& g, int xr, int y, const char* s, const Fnt& fn, uint16_t color);   // right edge at xr
void textC(Adafruit_GFX& g, int xm, int y, const char* s, const Fnt& fn, uint16_t color);   // centred on xm
// Text with a 2-pixel outline in `halo` (map labels).
void haloText(Adafruit_GFX& g, int x, int y, const char* s, const Fnt& fn, uint16_t color, uint16_t halo);
