#include "places.h"
#include <stdio.h>
#include <string.h>
#include "lang.h"
#include "mapdata.h"
#include "utf8.h"

namespace {
// Lower-case one font-coded character (see utf8.h: 0x88-0x8A are Ä, Ö, Å; 0x8C Š; 0x8E Ž).
char lowerFont(char c) {
  uint8_t u = (uint8_t)c;
  if (u >= 'A' && u <= 'Z') return (char)(u + 32);
  if (u == 0x88 || u == 0x89 || u == 0x8A) return (char)(u - 3);
  if (u == 0x8C || u == 0x8E) return (char)(u - 1);
  return c;
}
bool startsWith(const char* s, const char* q) {
  for (; *q; s++, q++)
    if (!*s || lowerFont(*s) != lowerFont(*q)) return false;
  return true;
}
// Match at the start of the name or of any word in it ("Kristiinankaupunki" is found
// with "kris", "Staraja Russa" with "russa").
bool matches(const char* name, const char* q) {
  if (startsWith(name, q)) return true;
  for (const char* p = name; *p; p++)
    if ((*p == ' ' || *p == '-') && startsWith(p + 1, q)) return true;
  return false;
}
}  // namespace

int placeSearchOffline(const char* query, Place* out, int max) {
  char q[64];
  utf8ToFont(query, q, sizeof q);
  // trim spaces and anything after a comma ("Helsinki, Suomi" -> "Helsinki")
  char* c = strchr(q, ',');
  if (c) *c = 0;
  char* s = q;
  while (*s == ' ') s++;
  for (int i = (int)strlen(s) - 1; i >= 0 && s[i] == ' '; i--) s[i] = 0;
  if (!*s) return 0;
  int n = 0;
  // Two passes: exact-start matches of big towns first, then the rest.
  for (int pass = 0; pass < 2 && n < max; pass++)
    for (uint32_t i = 0; i < PLACES_N && n < max; i++) {
      const MapPlace& p = PLACES[i];
      // Match either language's name; show the one for the chosen language.
      const char* fi = mapNameFi(p.name);
      const char* en = mapNameEn(p.name);
      bool first = (startsWith(fi, s) || startsWith(en, s)) && p.big;
      if (pass == 0 ? !first : (first || !(matches(fi, s) || matches(en, s)))) continue;
      const char* name = mapName(p.name, language == LANG_EN);
      Place& o = out[n++];
      snprintf(o.name, sizeof o.name, "%s", name);
      snprintf(o.detail, sizeof o.detail, "%s", TR("kaupunki kartalta (ilman nettiä)", "town from the map (offline)"));
      o.lat = latFromY((float)p.y);
      o.lon = lonFromX((float)p.x);
    }
  return n;
}
