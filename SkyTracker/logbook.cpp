// The spotter's logbook (see logbook.h).
#include "logbook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "render.h"            // haversineKm, renderAlloc

namespace {

const uint32_t PASS_GAP = 600;          // a pass ends after 10 minutes out of sight

// Every aircraft ever logged: ICAO address -> number of passes. Open addressing, in PSRAM.
struct HexSlot { uint32_t key, count; };          // key = address + 1 (0: empty)
const uint32_t HEX_SLOTS = 65536, HEX_MAX = 60000;
HexSlot* hexes = nullptr;

// Every aircraft type: "A20N" -> passes.
struct TypeSlot { uint32_t key, count; char name[6]; };
const uint32_t TYPE_SLOTS = 1024;
TypeSlot* types = nullptr;

// Planes inside the circle right now.
struct Open { LogPass p; uint32_t lastSeen; };
const int MAX_OPEN = 256;
Open* opened = nullptr;
int nOpen = 0;

// Finished passes waiting to be written to the card.
const int MAX_PENDING = 96;
LogPass* pending = nullptr;
int pendHead = 0, nPending = 0;

LogStats st;
int todayKey = 0;                       // 20261007
const int MAX_TODAY_TYPES = 128;
uint32_t todayTypes[MAX_TODAY_TYPES];
int nTodayTypes = 0;

uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; return x ^ (x >> 16); }

HexSlot* hexSlot(uint32_t hex, bool add) {
  if (!hexes) return nullptr;
  uint32_t key = hex + 1;
  for (uint32_t i = mix(key) & (HEX_SLOTS - 1);; i = (i + 1) & (HEX_SLOTS - 1)) {
    HexSlot& s = hexes[i];
    if (s.key == key) return &s;
    if (!s.key) {
      if (!add || st.aircraft >= HEX_MAX) return nullptr;
      s.key = key;
      s.count = 0;
      st.aircraft++;
      return &s;
    }
  }
}
uint32_t typeKey(const char* t) {
  uint32_t k = 0;
  for (int i = 0; i < 4 && t[i]; i++) k = k << 8 | (uint8_t)t[i];
  return k;
}
TypeSlot* typeSlot(const char* name, bool add) {
  uint32_t key = typeKey(name);
  if (!types || !key) return nullptr;
  for (uint32_t i = mix(key) & (TYPE_SLOTS - 1), n = 0; n < TYPE_SLOTS; i = (i + 1) & (TYPE_SLOTS - 1), n++) {
    TypeSlot& s = types[i];
    if (s.key == key) return &s;
    if (!s.key) {
      if (!add || st.types >= TYPE_SLOTS * 3 / 4) return nullptr;
      s.key = key;
      s.count = 0;
      snprintf(s.name, sizeof s.name, "%.4s", name);
      st.types++;
      return &s;
    }
  }
  return nullptr;
}

bool parseHex(const char* s, uint32_t* out) {
  uint32_t v = 0;
  int n = 0;
  for (; *s; s++, n++) {
    char c = *s;
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0 || n >= 6) return false;      // '~' marks addresses that aren't real ICAO ones
    v = v << 4 | d;
  }
  if (n != 6) return false;
  *out = v;
  return true;
}
// Letters, digits, spaces and dashes only (the values go into CSV files).
void clean(char* dst, size_t n, const char* src) {
  size_t o = 0;
  for (; *src && o + 1 < n; src++) {
    char c = *src;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == ' ') dst[o++] = c;
  }
  while (o && dst[o - 1] == ' ') o--;
  dst[o] = 0;
}

int dayKey(uint32_t t) {
  time_t tt = t;
  struct tm lt;
  localtime_r(&tt, &lt);
  return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}
int hourOf(uint32_t t) {
  time_t tt = t;
  struct tm lt;
  localtime_r(&tt, &lt);
  return lt.tm_hour;
}

void resetToday(int key) {
  todayKey = key;
  st.today = st.todayNew = 0;
  memset(st.hours, 0, sizeof st.hours);
  memset(&st.closest, 0, sizeof st.closest);
  st.closest.km = -1;
  nTodayTypes = 0;
}

void countToday(const LogPass& p) {
  st.today++;
  if (p.first) st.todayNew++;
  st.hours[hourOf(p.when)]++;
  if (st.closest.km < 0 || p.km < st.closest.km) st.closest = p;
  uint32_t k = typeKey(p.type);
  if (!k) return;
  for (int i = 0; i < nTodayTypes; i++) if (todayTypes[i] == k) return;
  if (nTodayTypes < MAX_TODAY_TYPES) todayTypes[nTodayTypes++] = k;
}

void record(LogPass p, bool live) {
  HexSlot* h = hexSlot(p.hex, true);
  p.first = h && h->count == 0;
  if (h) h->count++;
  st.passes++;
  if (TypeSlot* t = typeSlot(p.type, true)) t->count++;
  if (!st.since || p.when < st.since) st.since = p.when;
  if (!todayKey) resetToday(dayKey(st.now ? st.now : p.when));
  if (dayKey(p.when) == todayKey) countToday(p);
  if (st.closestEver.km < 0 || p.km < st.closestEver.km) st.closestEver = p;
  if (h && (h->count > st.regularN || st.regular.hex == p.hex)) { st.regular = p; st.regularN = h->count; }
  if (p.first) {                                       // newest first
    int i = st.nRecentNew < 4 ? st.nRecentNew++ : 3;
    if (i < 3 || p.when > st.recentNew[3].when) {
      while (i > 0 && st.recentNew[i - 1].when < p.when) { st.recentNew[i] = st.recentNew[i - 1]; i--; }
      st.recentNew[i] = p;
    }
  }
  if (!live || !pending) return;
  if (nPending == MAX_PENDING) { pendHead = (pendHead + 1) % MAX_PENDING; nPending--; }   // card gone: drop the oldest
  pending[(pendHead + nPending++) % MAX_PENDING] = p;
}

Open* findOpen(uint32_t hex) {
  for (int i = 0; i < nOpen; i++) if (opened[i].p.hex == hex) return &opened[i];
  return nullptr;
}

const char* field(char*& s, char sep) {      // next field of a CSV line (cut in place)
  char* f = s;
  char* e = strchr(s, sep);
  if (e) { *e = 0; s = e + 1; } else s += strlen(s);
  return f;
}

}  // namespace

const char* LogPass::label(char* buf, size_t n) const {
  if (cs[0]) snprintf(buf, n, "%s", cs);
  else if (reg[0]) snprintf(buf, n, "%s", reg);
  else snprintf(buf, n, "%06X", (unsigned)hex);
  return buf;
}

void logbookInit() {
  if (!hexes) hexes = (HexSlot*)renderAlloc(sizeof(HexSlot) * HEX_SLOTS);
  if (!types) types = (TypeSlot*)renderAlloc(sizeof(TypeSlot) * TYPE_SLOTS);
  if (!opened) opened = (Open*)renderAlloc(sizeof(Open) * MAX_OPEN);
  if (!pending) pending = (LogPass*)renderAlloc(sizeof(LogPass) * MAX_PENDING);
  if (hexes) memset(hexes, 0, sizeof(HexSlot) * HEX_SLOTS);
  if (types) memset(types, 0, sizeof(TypeSlot) * TYPE_SLOTS);
  memset(&st, 0, sizeof st);
  st.closest.km = st.closestEver.km = -1;
  nOpen = nPending = pendHead = 0;
  todayKey = 0;
}

double logbookReachKm(double lat, double lon) {
  return haversineKm(lat, lon, cfg.homeLat, cfg.homeLon) + cfg.logKm;
}

void logbookObserve(const Plane* planes, int n, double lat, double lon, double radiusKm, uint32_t epoch) {
  if (!hexes || !opened || epoch < 1600000000) return;           // (the clock isn't set yet)
  st.now = epoch;
  int key = dayKey(epoch);
  if (key != todayKey) resetToday(key);
  if (logbookReachKm(lat, lon) > radiusKm + 0.5) return;       // the report doesn't cover the circle
  for (int i = 0; i < n; i++) {
    const Plane& p = planes[i];
    uint32_t hex;
    if (p.category[0] == 'C' || !parseHex(p.hex, &hex)) continue;   // ground vehicles, non-ICAO
    float km = (float)haversineKm(cfg.homeLat, cfg.homeLon, p.lat, p.lon);
    if (km > cfg.logKm) continue;
    Open* o = findOpen(hex);
    if (!o) {
      if (nOpen == MAX_OPEN) continue;
      o = &opened[nOpen++];
      memset(o, 0, sizeof *o);
      o->p.hex = hex;
      o->p.km = 1e9f;
    }
    o->lastSeen = epoch;
    if (km < o->p.km) {
      o->p.km = km;
      o->p.when = epoch;
      o->p.altFt = p.hasAlt ? p.alt : -1;
    }
    if (p.cs[0]) clean(o->p.cs, sizeof o->p.cs, p.cs);          // (may only arrive later)
    if (p.reg[0]) clean(o->p.reg, sizeof o->p.reg, p.reg);
    if (p.type[0]) clean(o->p.type, sizeof o->p.type, p.type);
  }
  // Passes that have ended. Not before the history is back: they would count as first visits.
  if (st.ready)
    for (int i = 0; i < nOpen;) {
      if (epoch - opened[i].lastSeen < PASS_GAP) { i++; continue; }
      record(opened[i].p, true);
      opened[i] = opened[--nOpen];
    }
  st.open = nOpen;
}

bool logbookIsNew(const char* hexStr) {
  uint32_t hex;
  if (!st.ready || !st.since || st.now - st.since < 86400 || !parseHex(hexStr, &hex) || !findOpen(hex)) return false;
  HexSlot* h = hexSlot(hex, false);
  return !h || h->count == 0;
}

int logbookSeen(const char* hexStr) {
  uint32_t hex;
  if (!parseHex(hexStr, &hex)) return 0;
  HexSlot* h = hexSlot(hex, false);
  return h ? (int)h->count : 0;
}

const LogStats& logbookStats() {
  // Most common types
  memset(st.topType, 0, sizeof st.topType);
  memset(st.topTypeN, 0, sizeof st.topTypeN);
  if (types)
    for (uint32_t i = 0; i < TYPE_SLOTS; i++) {
      const TypeSlot& t = types[i];
      if (!t.key) continue;
      for (int k = 0; k < 3; k++)
        if (t.count > st.topTypeN[k]) {
          for (int j = 2; j > k; j--) { st.topTypeN[j] = st.topTypeN[j - 1]; memcpy(st.topType[j], st.topType[j - 1], 6); }
          st.topTypeN[k] = t.count;
          memcpy(st.topType[k], t.name, 6);
          break;
        }
    }
  // Today's rarest type
  st.rareType[0] = 0;
  st.rareCount = 0;
  for (int i = 0; i < nTodayTypes; i++) {
    char name[6];
    uint32_t k = todayTypes[i];
    int n = 0;
    for (int b = 3; b >= 0; b--) if ((k >> (b * 8)) & 0xFF) name[n++] = (char)((k >> (b * 8)) & 0xFF);
    name[n] = 0;
    TypeSlot* t = typeSlot(name, false);
    if (t && (!st.rareType[0] || t->count < st.rareCount)) { st.rareCount = t->count; snprintf(st.rareType, sizeof st.rareType, "%s", t->name); }
  }
  return st;
}

// ---- saving --------------------------------------------------------------------------
int logbookPending(LogPass* out, int max) {
  int n = nPending < max ? nPending : max;
  for (int i = 0; i < n; i++) out[i] = pending[(pendHead + i) % MAX_PENDING];
  return n;
}
void logbookPop(int n) {
  if (n > nPending) n = nPending;
  pendHead = (pendHead + n) % MAX_PENDING;
  nPending -= n;
}

void logbookPath(const LogPass& p, char* out, size_t n) {
  time_t t = p.when;
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(out, n, "/skytracker/%04d-%02d.csv", lt.tm_year + 1900, lt.tm_mon + 1);
}

void logbookHeader(char* out, size_t n) {
  // The BOM tells spreadsheets the file is UTF-8 (for the ä and ö in the Finnish header).
  snprintf(out, n, "\xEF\xBB\xBF%s\n", TR("päivä;aika;hex;tunnus;rekisteri;tyyppi;lähin etäisyys km;korkeus ft;ensikäynti",
                                           "date,time,hex,callsign,registration,type,closest km,altitude ft,first visit"));
}

void logbookLine(const LogPass& p, char sep, char* out, size_t n) {
  time_t t = p.when;
  struct tm lt;
  localtime_r(&t, &lt);
  char km[16], alt[16] = "";
  snprintf(km, sizeof km, "%.1f", p.km);
  if (sep == ';') for (char* c = km; *c; c++) if (*c == '.') *c = ',';
  if (p.altFt >= 0) snprintf(alt, sizeof alt, "%ld", (long)p.altFt);
  snprintf(out, n, "%04d-%02d-%02d%c%02d:%02d:%02d%c%06x%c%s%c%s%c%s%c%s%c%s%c%d\n",
           lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, sep, lt.tm_hour, lt.tm_min, lt.tm_sec, sep,
           (unsigned)p.hex, sep, p.cs, sep, p.reg, sep, p.type, sep, km, sep, alt, sep, p.first ? 1 : 0);
}

// ---- start-up ----------------------------------------------------------------------------
void logbookSetClock(uint32_t epoch) {
  st.now = epoch;
  resetToday(dayKey(epoch));
}

void logbookReplay(const char* in) {
  char line[160];
  snprintf(line, sizeof line, "%s", in);
  if (line[0] < '0' || line[0] > '9') return;          // the header (or an empty line)
  char sep = strchr(line, ';') ? ';' : ',';
  char* s = line;
  const char* date = field(s, sep);
  const char* clock = field(s, sep);
  const char* hex = field(s, sep);
  const char* cs = field(s, sep);
  const char* reg = field(s, sep);
  const char* type = field(s, sep);
  const char* km = field(s, sep);
  const char* alt = field(s, sep);
  struct tm tm = {};
  int Y, M, D, h, m, sec;
  if (sscanf(date, "%d-%d-%d", &Y, &M, &D) != 3 || sscanf(clock, "%d:%d:%d", &h, &m, &sec) != 3) return;
  tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D;
  tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
  tm.tm_isdst = -1;
  LogPass p = {};
  if (!parseHex(hex, &p.hex)) return;
  p.when = (uint32_t)mktime(&tm);
  clean(p.cs, sizeof p.cs, cs);
  clean(p.reg, sizeof p.reg, reg);
  clean(p.type, sizeof p.type, type);
  char k[16];
  snprintf(k, sizeof k, "%s", km);
  for (char* c = k; *c; c++) if (*c == ',') *c = '.';
  p.km = (float)atof(k);
  p.altFt = alt[0] ? atol(alt) : -1;
  record(p, false);
}

void logbookReplayDone(bool saving) {
  st.ready = true;
  st.saving = saving;
}

// ---- the simulator's made-up history ------------------------------------------------------
void logbookDemoSeed(const AppState& s, uint32_t epoch) {
  uint32_t seed = 12345;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFFFF; };
  static const char* TYPES[] = {"A20N", "A321", "A320", "E190", "AT76", "B738", "A359", "BCS3", "B789", "A333",
                                "C172", "EC35", "PC12", "B38M", "E195", "CRJ9", "A388", "B77W", "C68A", "DH8D"};
  static const uint8_t WEIGHT[] = {30, 20, 14, 14, 12, 12, 6, 6, 5, 4, 3, 3, 2, 6, 5, 3, 1, 2, 2, 3};
  static const char* AIRLINES[] = {"FIN", "SAS", "NOZ", "DLH", "RYR", "BAW", "KLM", "QTR"};
  st.now = epoch;
  resetToday(dayKey(epoch));
  const int DAYS = 40;
  uint32_t dayStart = epoch - (epoch % 86400) - DAYS * 86400UL;
  for (int d = 0; d <= DAYS; d++) {
    uint32_t t0 = dayStart + d * 86400UL;
    uint32_t times[160];
    int n = 50 + rnd() % 90;
    if (n > 160) n = 160;
    for (int i = 0; i < n; i++) times[i] = t0 + 6 * 3600 + rnd() % (17 * 3600);   // mostly daytime
    for (int i = 1; i < n; i++)
      for (int j = i; j > 0 && times[j] < times[j - 1]; j--) { uint32_t x = times[j]; times[j] = times[j - 1]; times[j - 1] = x; }
    for (int i = 0; i < n; i++) {
      if (times[i] >= epoch) break;
      LogPass p = {};
      p.when = times[i];
      // Some aircraft come by often (the same few airliners), most only now and then.
      uint32_t r = rnd() % 1000;
      int idx = (int)(r * r / 1000 * 3);                  // 0..2997, crowded at the low end
      p.hex = 0x400000 + idx * 37;
      int w = idx % 140, k = 0;
      while (k < 19 && w >= WEIGHT[k]) { w -= WEIGHT[k]; k++; }
      snprintf(p.type, sizeof p.type, "%s", TYPES[k]);
      snprintf(p.cs, sizeof p.cs, "%s%d", AIRLINES[idx % 8], 100 + (idx * 7) % 2800);
      snprintf(p.reg, sizeof p.reg, "OH-%c%c%c", 'A' + idx % 26, 'A' + (idx / 26) % 26, 'A' + (idx / 676) % 26);
      p.km = 0.5f + (rnd() % 995) / 10.0f;
      p.altFt = (int32_t)(1000 + rnd() % 380) * 100;
      record(p, false);
    }
    // Most of the demo planes have been by before; the rest show up as new.
    for (int i = 0; i < 20 && i < s.nPlanes; i++) {
      const Plane& q = s.planes[i];
      if (rnd() % DAYS >= (uint32_t)(1 + (i * 7) % 18)) continue;
      LogPass p = {};
      if (!parseHex(q.hex, &p.hex)) continue;
      p.when = t0 + 8 * 3600 + rnd() % (12 * 3600);
      if (p.when >= epoch) continue;
      clean(p.cs, sizeof p.cs, q.cs);
      clean(p.reg, sizeof p.reg, q.reg);
      clean(p.type, sizeof p.type, q.type);
      p.km = 2.0f + (rnd() % 900) / 10.0f;
      p.altFt = (int32_t)(30 + rnd() % 350) * 100;
      record(p, false);
    }
  }
  logbookReplayDone(true);               // (as if there were a card, like on the board)
}
