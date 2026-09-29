// Screen language. Every user-visible text is written as TR("suomeksi", "in English")
// right where it is used, so both versions stay side by side.
#pragma once
#include <stdint.h>
#include "config.h"

enum Lang : uint8_t { LANG_FI = 0, LANG_EN = 1 };
inline uint8_t language = DEFAULT_LANGUAGE;      // chosen in Settings, saved in flash

#define TR(fi, en) (language == LANG_EN ? (en) : (fi))

inline char decimalSep() { return language == LANG_EN ? '.' : ','; }
inline char thousandsSep() { return language == LANG_EN ? ',' : ' '; }
// Replace '.' with the local decimal separator (in place).
// The home marker's label. The default (empty, or the default word in either language)
// follows the language; a name of the user's own is shown as it is.
#include <string.h>
inline bool isDefaultHomeName(const char* n) { return !n[0] || !strcmp(n, "Koti") || !strcmp(n, "Home"); }
inline const char* homeLabel(const char* n) { return isDefaultHomeName(n) ? TR("Koti", "Home") : n; }
inline void localDecimal(char* s) {
  for (char sep = decimalSep(); *s; s++) if (*s == '.') *s = sep;
}
