// UTF-8 text -> the single-byte codes our fonts use (ASCII plus a few extras).
// Screen text is written as normal UTF-8 ("Jyväskylä", "↑ 1 200 ft/min").
#pragma once
#include <stdint.h>
#include <stddef.h>

// Font codes above ASCII (see tools/make_fonts.py).
//   0x80 up arrow   0x81 down arrow   0x82 degree   0x83 middle dot   0x84 ellipsis
//   0x85 ä  0x86 ö  0x87 å  0x88 Ä  0x89 Ö  0x8A Å  0x8B š  0x8C Š  0x8D ž  0x8E Ž
static inline const char* utf8Fold(uint32_t cp) {
  switch (cp) {
    case 0x2191: return "\x80"; case 0x2193: return "\x81"; case 0xB0: return "\x82";
    case 0xB7: return "\x83";   case 0x2022: return "\x83"; case 0x2026: return "\x84";
    case 0xE4: return "\x85";   case 0xF6: return "\x86";   case 0xE5: return "\x87";
    case 0xC4: return "\x88";   case 0xD6: return "\x89";   case 0xC5: return "\x8A";
    case 0x161: return "\x8B";  case 0x160: return "\x8C";  case 0x17E: return "\x8D";
    case 0x17D: return "\x8E";
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0x101: case 0x103: case 0x105: return "a";
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0x100: case 0x104: return "A";
    case 0xE7: case 0x107: case 0x10D: return "c";   case 0xC7: case 0x106: case 0x10C: return "C";
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0x119: case 0x11B: case 0x113: return "e";
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0x118: case 0x11A: return "E";
    case 0xEC: case 0xED: case 0xEE: case 0xEF: case 0x131: return "i";
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0x130: return "I";
    case 0xF1: case 0x144: case 0x148: return "n";   case 0xD1: case 0x143: return "N";
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF8: case 0x151: return "o";
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD8: case 0x150: return "O";
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0x16B: case 0x16F: return "u";
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0x16E: return "U";
    case 0xFD: case 0xFF: return "y";                case 0xDD: return "Y";
    case 0xDF: return "ss"; case 0xE6: return "ae"; case 0xC6: return "Ae";
    case 0x142: return "l"; case 0x141: return "L";
    case 0x15B: case 0x15F: return "s"; case 0x15A: case 0x15E: return "S";
    case 0x17A: case 0x17C: return "z"; case 0x179: case 0x17B: return "Z";
    case 0x159: return "r"; case 0x158: return "R"; case 0x11F: return "g"; case 0x11E: return "G";
    case 0x10F: return "d"; case 0x165: return "t"; case 0xA0: return " ";
    case 0x2013: case 0x2014: return "-"; case 0x2018: case 0x2019: return "'";
    case 0x201C: case 0x201D: return "\"";
    default: return "?";
  }
}

// Convert in -> out (out may be the same buffer: the result is never longer...
// except "ss"/"ae", so give out at least strlen(in) + 8 bytes).
static inline void utf8ToFont(const char* in, char* out, size_t n) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 1 < n) {
    uint8_t c = *p;
    uint32_t cp = 0;
    int len = 0;
    if (c < 0x80) { out[o++] = (char)c; p++; continue; }
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 3; }
    else { out[o++] = (char)c; p++; continue; }       // already a font code (0x80-0xBF)
    int k = 1;
    for (; k <= len; k++) {
      if ((p[k] & 0xC0) != 0x80) break;
      cp = (cp << 6) | (p[k] & 0x3F);
    }
    if (k <= len) { p++; continue; }                  // broken sequence: skip the lead byte
    p += len + 1;
    for (const char* f = utf8Fold(cp); *f && o + 1 < n; f++) out[o++] = *f;
  }
  out[o] = 0;
}

// Number of characters (not bytes) in a UTF-8 string.
static inline int utf8Len(const char* s) {
  int n = 0;
  for (; *s; s++) if (((uint8_t)*s & 0xC0) != 0x80) n++;
  return n;
}
// Remove the last character of a UTF-8 string.
static inline void utf8Pop(char* s) {
  int n = 0;
  while (s[n]) n++;
  while (n > 0) {
    n--;
    uint8_t c = (uint8_t)s[n];
    s[n] = 0;
    if ((c & 0xC0) != 0x80) break;
  }
}
