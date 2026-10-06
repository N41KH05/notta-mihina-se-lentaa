// SkyTracker renderer (colour LCD): chart-style map on the left, flight panel on the right.
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "fonts.h"
#include "mapdata.h"
#include "qr.h"

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
static const uint16_t C_WHITE = 0xFFFF, C_BLACK = 0x0000, C_EMERG = RGB(214, 30, 30);

const Theme THEME_LIGHT = {
  RGB(186, 212, 234), RGB(247, 244, 236), RGB(96, 128, 160), RGB(150, 138, 160),     // sea, land, coast, border
  RGB(90, 120, 158), RGB(62, 64, 76), RGB(78, 78, 90), RGB(128, 112, 140), RGB(58, 96, 138),
  0xFFFF, RGB(243, 245, 249), RGB(238, 241, 246), RGB(226, 232, 240), RGB(206, 212, 224),
  RGB(22, 36, 66), 0xFFFF, RGB(200, 210, 230),                                        // bar
  RGB(28, 32, 44), RGB(104, 110, 124), RGB(22, 36, 66), RGB(214, 218, 226),          // ink...
  RGB(170, 178, 194), RGB(160, 168, 184),
  RGB(226, 58, 94), RGB(255, 236, 242), RGB(120, 130, 150), RGB(30, 36, 52), RGB(200, 110, 0),
};
const Theme THEME_DARK = {
  RGB(16, 27, 38), RGB(36, 40, 42), RGB(74, 106, 128), RGB(110, 100, 124),
  RGB(86, 128, 156), RGB(206, 194, 170), RGB(150, 150, 160), RGB(156, 136, 176), RGB(96, 146, 186),
  RGB(28, 32, 34), RGB(34, 38, 40), RGB(16, 18, 20), RGB(44, 50, 52), RGB(44, 50, 52),
  RGB(10, 12, 13), RGB(104, 226, 136), RGB(150, 210, 160),
  RGB(255, 176, 60), RGB(176, 144, 92), RGB(104, 226, 136), RGB(54, 60, 62),
  RGB(84, 92, 94), RGB(84, 92, 94),
  RGB(104, 226, 136), RGB(30, 62, 44), RGB(0, 0, 0), RGB(0, 0, 0), RGB(240, 120, 60),
};
const Theme* theme = &THEME_LIGHT;
bool useDarkTheme(bool dark) {
  const Theme* t = dark ? &THEME_DARK : &THEME_LIGHT;
  if (t == theme) return false;
  theme = t;
  return true;
}
#define C_SEA      (theme->sea)
#define C_LAND     (theme->land)
#define C_COAST    (theme->coast)
#define C_BORDER   (theme->border)
#define C_RING     (theme->ring)
#define C_PLACE    (theme->place)
#define C_RUNWAY   (theme->runway)
#define C_COUNTRY  (theme->country)
#define C_WATER    (theme->water)
#define C_SURFACE  (theme->surface)
#define C_BAR      (theme->bar)
#define C_ON_BAR   (theme->onBar)
#define C_TEXT     (theme->ink)
#define C_TEXT2    (theme->ink2)
#define C_PRIMARY  (theme->primary)
#define C_LINE     (theme->line)
#define C_BTN_EDGE (theme->btnEdge)
#define C_ACCENT   (theme->accent)
#define C_OUTLINE  (theme->outline)
#define C_SHADOW   (theme->shadow)

static const int W = SCREEN_W, H = SCREEN_H, PANEL_X = MAP_W;

// ---------------------------------------------------------------------------
//  Text (drawn by the functions at the end of this file)
// ---------------------------------------------------------------------------
static void fit(char* s, const Fnt& fn, int maxW) {
  utf8ToFont(s, s, strlen(s) + 1);      // one byte per character from here on
  if (textW(fn, s) <= maxW) return;
  int n = strlen(s);
  while (n > 1) {
    s[--n] = 0;
    s[n - 1] = '\x84';
    if (textW(fn, s) <= maxW) return;
  }
}

// ---------------------------------------------------------------------------
//  Formatting
// ---------------------------------------------------------------------------
static void thousands(char* out, size_t n, long v) {
  char sep = thousandsSep();
  char tmp[24];
  snprintf(tmp, sizeof tmp, "%ld", v < 0 ? -v : v);
  int len = strlen(tmp), o = 0;
  if (v < 0 && o < (int)n - 1) out[o++] = '-';
  for (int i = 0; i < len && o < (int)n - 1; i++) {
    out[o++] = tmp[i];
    int left = len - i - 1;
    if (left > 0 && left % 3 == 0 && o < (int)n - 1) out[o++] = sep;
  }
  out[o] = 0;
}
static void decimalComma(char* s) { localDecimal(s); }
static void fmtAlt(char* out, size_t n, const Plane& p, bool shortForm) {
  if (!p.hasAlt) { snprintf(out, n, "--"); return; }
  char t[16];
  if (!units.altM) {
    long r = lroundf(p.alt / 100.0f);
    if (shortForm && p.alt >= 10000) { snprintf(out, n, "FL%03ld", r); return; }
    thousands(t, sizeof t, r * 100);
    snprintf(out, n, "%s ft", t);
  } else {
    float m = p.alt * 0.3048f;
    thousands(t, sizeof t, shortForm ? lroundf(m / 100) * 100 : lroundf(m / 10) * 10);
    snprintf(out, n, "%s m", t);
  }
}
static void fmtSpeed(char* out, size_t n, const Plane& p) {
  if (!p.hasGs) snprintf(out, n, "--");
  else if (!units.speedKmh) snprintf(out, n, "%ld kt", lroundf(p.gs));
  else snprintf(out, n, "%ld km/h", lroundf(p.gs * 1.852f));
}
static void fmtDist(char* out, size_t n, double km) {
  double v = units.distKm ? km : km / 1.852;
  const char* u = units.distKm ? "km" : "nm";
  if (v < 10) snprintf(out, n, "%.1f %s", v, u);
  else snprintf(out, n, "%ld %s", lround(v), u);
  decimalComma(out);
}
static void fmtVrate(char* out, size_t n, const Plane& p) {
  if (!p.hasVrate || abs(p.vrate) < 300) { snprintf(out, n, "%s", TR("vaakalento", "level")); return; }
  const char* arrow = p.vrate > 0 ? "\x80" : "\x81";
  if (!units.altM) {
    char t[16];
    thousands(t, sizeof t, lroundf(abs(p.vrate) / 100.0f) * 100);
    snprintf(out, n, "%s %s ft/min", arrow, t);
  } else {
    snprintf(out, n, "%s %.1f m/s", arrow, abs(p.vrate) * 0.00508f);
    decimalComma(out);
  }
}
static void fmtClock(char* out, size_t n, const struct tm* t, bool secs) {
  if (!t) { snprintf(out, n, "--:--"); return; }
  int h = t->tm_hour;
  if (!TIME_24H) { h %= 12; if (!h) h = 12; }
  if (secs) snprintf(out, n, "%d:%02d:%02d", h, t->tm_min, t->tm_sec);
  else snprintf(out, n, "%d:%02d", h, t->tm_min);
}
static const char* compass(float deg) {
  static const char* fi[] = {"P", "KO", "I", "KA", "E", "LO", "L", "LU"};
  static const char* en[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int i = (int)floorf((deg + 22.5f) / 45.0f);
  return TR(fi, en)[((i % 8) + 8) % 8];
}
double haversineKm(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * M_PI / 180, p2 = lat2 * M_PI / 180;
  double dp = p2 - p1, dl = (lon2 - lon1) * M_PI / 180;
  double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
}
static double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * M_PI / 180, p2 = lat2 * M_PI / 180, dl = (lon2 - lon1) * M_PI / 180;
  double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  return fmod(atan2(y, x) * 180 / M_PI + 360, 360);
}
const char* typeName(const char* t) {
  static const char* map[][2] = {
    {"A20N", "Airbus A320neo"}, {"A21N", "Airbus A321neo"}, {"A319", "Airbus A319"},
    {"A320", "Airbus A320"}, {"A321", "Airbus A321"}, {"BCS1", "Airbus A220-100"},
    {"BCS3", "Airbus A220-300"}, {"A332", "Airbus A330-200"}, {"A333", "Airbus A330-300"},
    {"A339", "Airbus A330neo"}, {"A359", "Airbus A350-900"}, {"A35K", "Airbus A350-1000"},
    {"A388", "Airbus A380"}, {"B737", "Boeing 737-700"}, {"B738", "Boeing 737-800"},
    {"B739", "Boeing 737-900"}, {"B38M", "Boeing 737 MAX 8"}, {"B39M", "Boeing 737 MAX 9"},
    {"B752", "Boeing 757-200"}, {"B763", "Boeing 767-300"}, {"B772", "Boeing 777-200"},
    {"B77W", "Boeing 777-300ER"}, {"B77L", "Boeing 777-200LR"}, {"B788", "Boeing 787-8"},
    {"B789", "Boeing 787-9"}, {"B78X", "Boeing 787-10"}, {"B744", "Boeing 747-400"},
    {"B748", "Boeing 747-8"}, {"E170", "Embraer 170"}, {"E75L", "Embraer 175"},
    {"E190", "Embraer 190"}, {"E195", "Embraer 195"}, {"E290", "Embraer E190-E2"},
    {"E295", "Embraer E195-E2"}, {"AT75", "ATR 72-500"}, {"AT76", "ATR 72-600"},
    {"AT45", "ATR 42-500"}, {"DH8D", "Dash 8-400"}, {"CRJ9", "Bombardier CRJ900"},
    {"CRJ7", "Bombardier CRJ700"}, {"SF34", "Saab 340"}, {"C172", "Cessna 172"},
    {"PC12", "Pilatus PC-12"}, {"EC35", "Airbus H135"}, {"EC45", "Airbus H145"},
    {"C68A", "Cessna Citation Latitude"}, {"GLF6", "Gulfstream G650"}, {"A400", "Airbus A400M"},
    {"C130", "Lockheed C-130 Hercules"}, {"F18S", "F/A-18 Hornet"}, {"H60", "Black Hawk"},
    {"JS32", "Jetstream 32"}, {"B350", "King Air 350"},
  };
  for (auto& m : map)
    if (!strcmp(t, m[0])) return m[1];
  return t;
}

// Plane colour by altitude, like the big flight-tracking sites.
static uint16_t altColorFt(float a) {
  static const struct { float ft; uint8_t r, g, b; } stops[] = {
    {0, 226, 76, 40}, {2000, 242, 138, 30}, {6000, 232, 190, 30}, {12000, 110, 180, 56},
    {20000, 32, 160, 140}, {30000, 40, 112, 214}, {40000, 118, 66, 206}};
  if (a <= stops[0].ft) return RGB(stops[0].r, stops[0].g, stops[0].b);
  for (int i = 1; i < 7; i++)
    if (a <= stops[i].ft) {
      float t = (a - stops[i - 1].ft) / (stops[i].ft - stops[i - 1].ft);
      return RGB((int)(stops[i - 1].r + (stops[i].r - stops[i - 1].r) * t),
                 (int)(stops[i - 1].g + (stops[i].g - stops[i - 1].g) * t),
                 (int)(stops[i - 1].b + (stops[i].b - stops[i - 1].b) * t));
    }
  return RGB(stops[6].r, stops[6].g, stops[6].b);
}
static uint16_t altColor(const Plane& p) { return p.hasAlt ? altColorFt(p.alt) : RGB(120, 120, 130); }

// Same result as lroundf (halves away from zero), but much cheaper than the C
// library's version on the ESP32. Only for values well inside the int range.
static inline int rnd(float v) {
  int i = (int)v;                      // towards zero
  float f = v - (float)i;              // exact
  return f >= 0.5f ? i + 1 : f <= -0.5f ? i - 1 : i;
}

// ---------------------------------------------------------------------------
//  View transform
// ---------------------------------------------------------------------------
static float V_CX, V_CY, V_MPP;
static int V_ZOOM;
static void setView(float cx, float cy, int zoom) {
  V_CX = cx; V_CY = cy; V_ZOOM = zoom; V_MPP = metresPerPx(zoom);
}
static inline float sx(float x) { return (x - V_CX) / V_MPP + MAP_W / 2.0f; }
static inline float sy(float y) { return (V_CY - y) / V_MPP + H / 2.0f; }

// ---------------------------------------------------------------------------
//  Land: one even-odd scanline pass over every visible ring
// ---------------------------------------------------------------------------
// Each row is sampled at its centre (row + 0.5). An edge only matters for the rows
// whose centre it spans, so edges that don't cross any row centre on screen (most of
// them when zoomed out) are dropped straight away, and the rest are bucketed by their
// first row instead of being sorted.
struct Edge { float yTop, yBot, x, dx; int16_t row0; };
static Edge* edges = nullptr;
static int nEdges = 0;
static const int MAX_EDGES = 60000, MAX_ACTIVE = 4096;

static void addEdge(float x0, float y0, float x1, float y1) {
  if (y0 == y1 || nEdges >= MAX_EDGES) return;
  if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
  if (y1 < 0 || y0 >= H) return;
  // Rows r with y0 <= r + 0.5 < y1.
  int r0 = (int)ceilf(y0 - 0.5f), r1 = (int)ceilf(y1 - 0.5f) - 1;
  if (r0 < 0) r0 = 0;
  if (r1 > H - 1) r1 = H - 1;
  if (r0 > r1) return;
  Edge& e = edges[nEdges++];
  e.yTop = y0; e.yBot = y1; e.dx = (x1 - x0) / (y1 - y0); e.x = x0; e.row0 = r0;
}
struct Layer { const uint8_t* pts; float ox, oy, unit; };
// Screen position of layer point (x, y) is (x * k + bx, by - y * k): one multiply and
// add per coordinate instead of a division.
struct LayerView {
  float k, bx, by;
  explicit LayerView(const Layer& L)
      : k(L.unit / V_MPP), bx((L.ox - V_CX) / V_MPP + MAP_W / 2.0f), by((V_CY - L.oy) / V_MPP + H / 2.0f) {}
};

static bool shapeVisible(const MapShape& sh, const Layer& L, float margin) {
  float m = margin * V_MPP;
  float vx0 = V_CX - MAP_W / 2 * V_MPP - m, vx1 = V_CX + MAP_W / 2 * V_MPP + m;
  float vy0 = V_CY - H / 2 * V_MPP - m, vy1 = V_CY + H / 2 * V_MPP + m;
  float x0 = L.ox + sh.x0 * L.unit, x1 = L.ox + sh.x1 * L.unit;
  float y0 = L.oy + sh.y0 * L.unit, y1 = L.oy + sh.y1 * L.unit;
  return x1 >= vx0 && x0 <= vx1 && y1 >= vy0 && y0 <= vy1;
}
static void collectRings(const Layer& L, const MapShape* shapes, uint32_t n) {
  const LayerView v(L);
  for (uint32_t i = 0; i < n; i++) {
    const MapShape& sh = shapes[i];
    if (!shapeVisible(sh, L, 4)) continue;
    MapPoints pt(L.pts + sh.start);
    float px = pt.x * v.k + v.bx, py = v.by - pt.y * v.k;
    float fx = px, fy = py;
    for (uint32_t k = 1; k < sh.count; k++) {
      pt.next();
      float qx = pt.x * v.k + v.bx, qy = v.by - pt.y * v.k;
      addEdge(px, py, qx, qy);
      px = qx; py = qy;
    }
    addEdge(px, py, fx, fy);
  }
}
static void fillLand(Adafruit_GFX& g) {
  static uint16_t end[H];                          // edges by starting row (counting sort)
  static uint16_t* order = (uint16_t*)renderAlloc(sizeof(uint16_t) * MAX_EDGES);
  static uint16_t active[MAX_ACTIVE];
  static float xs[MAX_ACTIVE];
  memset(end, 0, sizeof end);
  for (int i = 0; i < nEdges; i++) end[edges[i].row0]++;
  for (int r = 0, sum = 0; r < H; r++) { int c = end[r]; end[r] = sum; sum += c; }   // starts
  for (int i = 0; i < nEdges; i++) order[end[edges[i].row0]++] = i;               // now ends
  int nAct = 0;
  for (int row = 0; row < H; row++) {
    float yc = row + 0.5f;
    for (int k = row ? end[row - 1] : 0; k < end[row] && nAct < MAX_ACTIVE; k++) active[nAct++] = order[k];
    int nx = 0;
    for (int i = 0; i < nAct;) {
      Edge& e = edges[active[i]];
      if (e.yBot <= yc) { active[i] = active[--nAct]; continue; }
      xs[nx++] = e.x + (yc - e.yTop) * e.dx;
      i++;
    }
    for (int i = 1; i < nx; i++) {
      float v = xs[i];
      int j = i - 1;
      while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
      xs[j + 1] = v;
    }
    // Sea and land in one pass: each pixel of the row is written once.
    int cur = 0;
    for (int i = 0; i + 1 < nx; i += 2) {
      int a = (int)ceilf(xs[i] - 0.5f), b = (int)floorf(xs[i + 1] - 0.5f);
      if (a < 0) a = 0;
      if (b > MAP_W - 1) b = MAP_W - 1;
      if (b < a) continue;
      if (a > cur) g.drawFastHLine(cur, row, a - cur, C_SEA);
      g.drawFastHLine(a, row, b - a + 1, C_LAND);
      if (b + 1 > cur) cur = b + 1;
    }
    if (cur < MAP_W) g.drawFastHLine(cur, row, MAP_W - cur, C_SEA);
  }
}
static void drawLines(Adafruit_GFX& g, const Layer& L, const MapShape* shapes, uint32_t n,
                      bool dashed, uint16_t color, uint8_t onlyFlag = 0) {
  const LayerView v(L);
  for (uint32_t i = 0; i < n; i++) {
    const MapShape& sh = shapes[i];
    if (onlyFlag && !(sh.flags & onlyFlag)) continue;
    if (!shapeVisible(sh, L, 2)) continue;
    MapPoints pt(L.pts + sh.start);
    float px = pt.x * v.k + v.bx, py = v.by - pt.y * v.k;
    const float firstX = px, firstY = py;
    float run = 0;
    // Solid lines: a segment whose ends land on the same pixel only draws that pixel,
    // which the previous segment has already drawn if it was drawn (the usual case
    // when zoomed out, where many points fall on one pixel).
    int ipx = rnd(px), ipy = rnd(py);
    bool prevDrawn = false;
    for (uint32_t k = 1; k < sh.count + (onlyFlag ? 1 : 0); k++) {
      float qx = firstX, qy = firstY;          // (closing a lake outline)
      if (k < sh.count) {
        pt.next();
        qx = pt.x * v.k + v.bx;
        qy = v.by - pt.y * v.k;
      }
      bool on = !((px < -50 && qx < -50) || (px > MAP_W + 50 && qx > MAP_W + 50) ||
                  (py < -50 && qy < -50) || (py > H + 50 && qy > H + 50));
      if (!dashed) {
        int iqx = rnd(qx), iqy = rnd(qy);
        if (on && (iqx != ipx || iqy != ipy)) g.drawLine(ipx, ipy, iqx, iqy, color);
        else if (on && !prevDrawn) g.drawPixel(ipx, ipy, color);
        prevDrawn = on;
        ipx = iqx; ipy = iqy;
      }
      if (on && dashed) {
        float len = hypotf(qx - px, qy - py);
        for (float t = 0; t < len;) {
          float phase = fmodf(run + t, 11.0f);
          float step = phase < 7 ? 7 - phase : 11 - phase;
          if (step > len - t) step = len - t;
          if (phase < 7) {
            float a = t / len, b = (t + step) / len;
            g.drawLine(rnd(px + (qx - px) * a), rnd(py + (qy - py) * a),
                       rnd(px + (qx - px) * b), rnd(py + (qy - py) * b), color);
          }
          t += step < 0.01f ? 0.01f : step;
        }
        run += len;
      }
      px = qx; py = qy;
    }
  }
}

// ---------------------------------------------------------------------------
//  Labels that don't overlap
// ---------------------------------------------------------------------------
struct Box { int16_t x0, y0, x1, y1; };
static Box taken[400];
static int nTaken = 0;
static void take(float x0, float y0, float x1, float y1) {
  if (nTaken < 400) taken[nTaken++] = {(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}
static bool isFree(float x0, float y0, float x1, float y1) {
  if (x0 < 2 || y0 < 2 || x1 > MAP_W - 2 || y1 > H - 2) return false;
  for (int i = 0; i < nTaken; i++) {
    const Box& b = taken[i];
    if (x0 < b.x1 && x1 > b.x0 && y0 < b.y1 && y1 > b.y0) return false;
  }
  return true;
}
// Map labels (towns, airports): text with a halo.
static bool mapLabel(Adafruit_GFX& g, float x, float y, const char* s, const Fnt& f, int gap,
                     uint16_t color) {
  int w = textW(f, s) + 4, h = f.size + 3;
  const float cand[4][2] = {{x + gap, y - h / 2.0f}, {x - gap - w, y - h / 2.0f},
                            {x - w / 2.0f, y - gap - h}, {x - w / 2.0f, y + gap}};
  for (auto& c : cand) {
    if (!isFree(c[0], c[1], c[0] + w, c[1] + h)) continue;
    take(c[0], c[1], c[0] + w, c[1] + h);
    haloText(g, rnd(c[0]) + 2, rnd(c[1]) + 1, s, f, color, C_LAND);
    return true;
  }
  return false;
}
// Country / sea names: centred on their point, only if the space is free.
static bool areaLabel(Adafruit_GFX& g, float x, float y, const char* s, const Fnt& f, uint16_t color,
                      uint16_t halo) {
  int w = textW(f, s) + 4, h = f.size + 3;
  static const int8_t shift[][2] = {{0, 0}, {0, -20}, {0, 20}, {-40, 0}, {40, 0}, {0, -40}, {0, 40}};
  for (auto& d : shift) {                 // nudge it a little if something is in the way
    float x0 = x - w / 2.0f + d[0], y0 = y - h / 2.0f + d[1];
    if (!isFree(x0, y0, x0 + w, y0 + h)) continue;
    take(x0, y0, x0 + w, y0 + h);
    haloText(g, rnd(x0) + 2, rnd(y0) + 1, s, f, color, halo);
    return true;
  }
  return false;
}

// Plane tags: a small card with the callsign and altitude.
static bool planeTag(Adafruit_GFX& g, float x, float y, const char* l1, const Fnt& f1,
                     const char* l2, uint16_t bg, uint16_t fg, uint16_t fg2, uint16_t edge, bool force) {
  int w = textW(f1, l1), w2 = textW(C12, l2);
  if (w2 > w) w = w2;
  w += 8;
  int h = f1.size + C12.size + 6, gap = 13;
  const float cand[4][2] = {{x + gap, y - h / 2.0f}, {x - gap - w, y - h / 2.0f},
                            {x - w / 2.0f, y - gap - h}, {x - w / 2.0f, y + gap}};
  for (auto& c : cand) {
    if (!force && !isFree(c[0], c[1], c[0] + w, c[1] + h)) continue;
    take(c[0], c[1], c[0] + w, c[1] + h);
    int ix = rnd(c[0]), iy = rnd(c[1]);
    g.fillRoundRect(ix, iy, w, h, 4, bg);
    if (edge != bg) g.drawRoundRect(ix, iy, w, h, 4, edge);
    text(g, ix + 4, iy + 2, l1, f1, fg);
    text(g, ix + 4, iy + 3 + f1.size, l2, C12, fg2);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Background map (cached by the caller; redrawn only when the view moves)
// ---------------------------------------------------------------------------
static float homeX, homeY;

// Plane icons: what kind of aircraft it is, from its ICAO type code (A320, AT76, EC35...)
// or, failing that, the size category its transponder sends.
enum IconKind : uint8_t { ICON_JET, ICON_WIDE, ICON_BIZJET, ICON_PROP, ICON_LIGHT, ICON_HELI, ICON_GLIDER };
static bool typeIn(const char* t, const char* const* list) {
  for (; *list; list++)
    if (!strncmp(t, *list, strlen(*list))) return true;
  return false;
}
static IconKind iconKind(const Plane& p) {
  static const char* const HELI[] = {"EC", "AS3", "AS5", "AS6", "A10", "A11", "A13", "A16", "A18", "B06", "B40",
    "B41", "B42", "B50", "B21", "R22", "R44", "R66", "S76", "S92", "S61", "H16", "H50", "MI8", "MI17", "NH90",
    "H60", "UH60", "H47", "MD52", "MD60", "EXPL", "GAZL", "LYNX", "PUMA", "KA32", "TIGR", "BK17", "AW", nullptr};
  static const char* const GLIDER[] = {"GLID", "ASK", "ASW", "ARCP", "DUO", "NIMB", "VENT", nullptr};
  static const char* const PROP[] = {"AT4", "AT5", "AT7", "ATP", "DH8", "DHC", "SF34", "SB20", "JS3", "JS4", "D328",
    "F50", "F27", "E120", "B190", "BE20", "BE30", "B350", "BE9", "PC12", "PC6", "C208", "C212", "AN2", "AN3",
    "L410", "D228", "SW4", "MA60", "Y12", "P180", "C130", "C30J", "CN35", "C295", "A400", "TBM", "BN2", "PA31",
    "PA34", "C340", "C40", "C41", "C421", "BE55", "BE58", "DA42", "DA62", "AC6", "AC9", "P68", "SC7", nullptr};
  static const char* const LIGHT[] = {"C15", "C170", "C172", "C175", "C177", "C18", "C20", "C21", "PA", "P28", "P32", "SR2", "DA20", "DA40",
    "BE33", "BE35", "BE36", "M20", "RV", "TOBA", "TB", "AA5", "C42", "CTSW", "DR40", "DV20", "G115", "EV97", "PIVI",
    "WT9", "ULAC", "Z42", "Z43", "Z24", "SIRA", "S22T", nullptr};
  static const char* const BIZJET[] = {"C25", "C50", "C51", "C52", "C55", "C56", "C65", "C68", "C70", "C75", "CL30",
    "CL35", "CL60", "GLF", "GL5", "GL6", "GL7", "GLEX", "E50P", "E55P", "E35L", "E545", "E550", "LJ", "F2TH",
    "F900", "FA", "PC24", "HDJT", "H25", "BE40", "PRM1", "SF50", "G150", "G280", "ASTR", "GALX", "EA50", nullptr};
  static const char* const WIDE[] = {"A30", "A31", "A33", "A34", "A35", "A38", "B74", "B76", "B77", "B78", "IL96",
    "IL86", "MD11", "DC10", "L101", "A3ST", "BLCF", "C17", "C5", "A124", "A225", nullptr};
  const char* t = p.type;
  if (t[0]) {
    if (typeIn(t, HELI)) return ICON_HELI;
    if (typeIn(t, GLIDER)) return ICON_GLIDER;
    if (typeIn(t, PROP)) return ICON_PROP;
    if (typeIn(t, LIGHT)) return ICON_LIGHT;
    if (typeIn(t, BIZJET)) return ICON_BIZJET;
    if (typeIn(t, WIDE)) return ICON_WIDE;
  }
  const char* c = p.category;                         // ADS-B emitter category
  if (!strcmp(c, "A7")) return ICON_HELI;
  if (!strcmp(c, "B1")) return ICON_GLIDER;
  if (!strcmp(c, "A1") || c[0] == 'B' || c[0] == 'C') return ICON_LIGHT;
  if (!strcmp(c, "A2")) return ICON_BIZJET;
  if (!strcmp(c, "A5")) return ICON_WIDE;
  return ICON_JET;
}
// Relative size of each kind (a jumbo is drawn bigger still).
static float iconScale(const Plane& p, IconKind k) {
  static const float S[] = {1.0f, 1.25f, 0.82f, 0.9f, 0.7f, 0.85f, 0.95f};
  if (k == ICON_WIDE && (!strncmp(p.type, "A38", 3) || !strncmp(p.type, "B74", 3))) return 1.4f;
  return S[k];
}

// Each icon is a few triangles in a unit box: nose at y = -1, tail at y = +1.
struct Tri { float v[6]; };
static const Tri SHAPE_JET[] = {
  {{-0.13f, -1.0f, 0.13f, -1.0f, 0.13f, 0.88f}}, {{-0.13f, -1.0f, 0.13f, 0.88f, -0.13f, 0.88f}},
  {{0.0f, -0.38f, -1.0f, 0.30f, 1.0f, 0.30f}}, {{0.0f, 0.52f, -0.42f, 0.97f, 0.42f, 0.97f}}};
static const Tri SHAPE_WIDE[] = {
  {{-0.17f, -1.0f, 0.17f, -1.0f, 0.17f, 0.9f}}, {{-0.17f, -1.0f, 0.17f, 0.9f, -0.17f, 0.9f}},
  {{0.0f, -0.45f, -1.0f, 0.32f, 1.0f, 0.32f}}, {{0.0f, 0.5f, -0.46f, 0.98f, 0.46f, 0.98f}}};
static const Tri SHAPE_BIZJET[] = {
  {{-0.11f, -1.0f, 0.11f, -1.0f, 0.11f, 0.9f}}, {{-0.11f, -1.0f, 0.11f, 0.9f, -0.11f, 0.9f}},
  {{0.0f, -0.12f, -0.82f, 0.32f, 0.82f, 0.32f}}, {{0.0f, 0.6f, -0.4f, 0.98f, 0.4f, 0.98f}}};
static const Tri SHAPE_PROP[] = {
  {{-0.12f, -1.0f, 0.12f, -1.0f, 0.12f, 0.9f}}, {{-0.12f, -1.0f, 0.12f, 0.9f, -0.12f, 0.9f}},
  {{-1.0f, -0.4f, 1.0f, -0.4f, 1.0f, -0.18f}}, {{-1.0f, -0.4f, 1.0f, -0.18f, -1.0f, -0.18f}},
  {{-0.4f, 0.72f, 0.4f, 0.72f, 0.4f, 0.92f}}, {{-0.4f, 0.72f, 0.4f, 0.92f, -0.4f, 0.92f}}};
static const Tri SHAPE_LIGHT[] = {
  {{-0.13f, -1.0f, 0.13f, -1.0f, 0.1f, 0.9f}}, {{-0.13f, -1.0f, 0.1f, 0.9f, -0.1f, 0.9f}},
  {{-1.0f, -0.5f, 1.0f, -0.5f, 1.0f, -0.24f}}, {{-1.0f, -0.5f, 1.0f, -0.24f, -1.0f, -0.24f}},
  {{-0.38f, 0.72f, 0.38f, 0.72f, 0.38f, 0.94f}}, {{-0.38f, 0.72f, 0.38f, 0.94f, -0.38f, 0.94f}}};
static const Tri SHAPE_GLIDER[] = {
  {{-0.08f, -1.0f, 0.08f, -1.0f, 0.06f, 0.95f}}, {{-0.08f, -1.0f, 0.06f, 0.95f, -0.06f, 0.95f}},
  {{-1.25f, -0.36f, 1.25f, -0.36f, 1.25f, -0.24f}}, {{-1.25f, -0.36f, 1.25f, -0.24f, -1.25f, -0.24f}},
  {{-0.32f, 0.84f, 0.32f, 0.84f, 0.32f, 0.96f}}, {{-0.32f, 0.84f, 0.32f, 0.96f, -0.32f, 0.96f}}};
static const Tri SHAPE_HELI[] = {                    // cabin, tail boom, tail rotor (main rotor: below)
  {{-0.32f, -0.6f, 0.32f, -0.6f, 0.32f, 0.2f}}, {{-0.32f, -0.6f, 0.32f, 0.2f, -0.32f, 0.2f}},
  {{-0.32f, -0.6f, 0.32f, -0.6f, 0.0f, -0.9f}}, {{-0.32f, 0.2f, 0.32f, 0.2f, 0.0f, 0.4f}},
  {{-0.07f, 0.2f, 0.07f, 0.2f, 0.07f, 0.98f}}, {{-0.07f, 0.2f, 0.07f, 0.98f, -0.07f, 0.98f}},
  {{-0.26f, 0.82f, 0.26f, 0.82f, 0.26f, 0.96f}}, {{-0.26f, 0.82f, 0.26f, 0.96f, -0.26f, 0.96f}}};
struct Shape { const Tri* tris; int n; };
#define SHAPE(a) {a, sizeof a / sizeof a[0]}
static const Shape SHAPES[] = {SHAPE(SHAPE_JET), SHAPE(SHAPE_WIDE), SHAPE(SHAPE_BIZJET), SHAPE(SHAPE_PROP),
                               SHAPE(SHAPE_LIGHT), SHAPE(SHAPE_HELI), SHAPE(SHAPE_GLIDER)};
#undef SHAPE

// rotorDeg: angle of a helicopter's main rotor (it turns a little every frame).
static void planeShape(Adafruit_GFX& g, float x, float y, float heading, float size, uint16_t c,
                       IconKind kind = ICON_JET, float rotorDeg = 45) {
  float a = heading * 0.0174533f, sn = sinf(a), cs = cosf(a);
  const Shape& sh = SHAPES[kind];
  for (int k = 0; k < sh.n; k++) {
    const float* t = sh.tris[k].v;
    int p[6];
    for (int i = 0; i < 3; i++) {
      float px = t[i * 2] * size, py = t[i * 2 + 1] * size;
      p[i * 2] = rnd(x + px * cs - py * sn);
      p[i * 2 + 1] = rnd(y + px * sn + py * cs);
    }
    g.fillTriangle(p[0], p[1], p[2], p[3], p[4], p[5], c);
  }
  if (kind == ICON_HELI)                              // two crossed blades over the cabin
    for (int b = 0; b < 2; b++) {
      float r = (rotorDeg + b * 90) * 0.0174533f, dx = cosf(r) * size, dy = sinf(r) * size;
      float w = 0.075f * size, wx = -sinf(r) * w, wy = cosf(r) * w;
      float cx = x - 0.2f * size * -sn, cy = y - 0.2f * size * cs;   // rotor hub, a bit forward
      g.fillTriangle(rnd(cx + dx + wx), rnd(cy + dy + wy), rnd(cx + dx - wx), rnd(cy + dy - wy),
                     rnd(cx - dx - wx), rnd(cy - dy - wy), c);
      g.fillTriangle(rnd(cx + dx + wx), rnd(cy + dy + wy), rnd(cx - dx - wx), rnd(cy - dy - wy),
                     rnd(cx - dx + wx), rnd(cy - dy + wy), c);
    }
}
static void ring(Adafruit_GFX& g, int x, int y, int r, int width, uint16_t c) {
  for (int i = 0; i < width; i++) g.drawCircle(x, y, r + i, c);
}
static void thickLine(Adafruit_GFX& g, float x0, float y0, float x1, float y1, float w, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0, L = hypotf(dx, dy);
  if (L < 0.5f) return;
  float nx = -dy / L * w / 2, ny = dx / L * w / 2;
  g.fillTriangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
  g.fillTriangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

void renderBase(Adafruit_GFX& g, float cx, float cy, int zoom) {
  if (!edges) edges = (Edge*)renderAlloc(sizeof(Edge) * MAX_EDGES);
  g.setTextWrap(false);
  setView(cx, cy, zoom);
  homeX = mercX(cfg.homeLon);
  homeY = mercY(cfg.homeLat);
  nTaken = 0;

  // (the sea is filled together with the land, row by row, in fillLand)
  take(MAP_W - 66, H - 3 * 54 - 10, MAP_W, H);   // keep labels clear of the buttons
  take(0, H - 104, 150, H);                       // and of the settings button and scale bar
  float hw = MAP_W / 2 * V_MPP, hh = H / 2 * V_MPP, span = 32000 * REG_UNIT;
  bool inRegion = V_MPP < 5000 && V_CX - hw > REG_OX - span && V_CX + hw < REG_OX + span &&
                  V_CY - hh > REG_OY - span && V_CY + hh < REG_OY + span;
  nEdges = 0;
  if (inRegion) {
    Layer L = {REG_PTS, REG_OX, REG_OY, REG_UNIT};
    bool fine = V_MPP < 400;
    collectRings(L, fine ? REG_FILL_FINE : REG_FILL_COARSE, fine ? REG_FILL_FINE_N : REG_FILL_COARSE_N);
    fillLand(g);
    drawLines(g, L, fine ? REG_COAST_FINE : REG_COAST_COARSE, fine ? REG_COAST_FINE_N : REG_COAST_COARSE_N,
              false, C_COAST);
    if (V_ZOOM >= 9) drawLines(g, L, REG_FILL_FINE, REG_FILL_FINE_N, false, C_COAST, 1);
    drawLines(g, L, fine ? REG_BORDER_FINE : REG_BORDER_COARSE,
              fine ? REG_BORDER_FINE_N : REG_BORDER_COARSE_N, true, C_BORDER);
  } else {
    Layer L = {WLD_PTS, 0, 0, WLD_UNIT};
    collectRings(L, WLD_FILL, WLD_FILL_N);
    fillLand(g);
    drawLines(g, L, WLD_COAST, WLD_COAST_N, false, C_COAST);
    drawLines(g, L, WLD_BORDER, WLD_BORDER_N, true, C_BORDER);
  }

  // range rings around home
  static const int rings[] = RANGE_RINGS;
  float hx = sx(homeX), hy = sy(homeY);
  float stretch = 1.0f / cosf(cfg.homeLat * 0.0174533f), unitKm = units.distKm ? 1.0f : 1.852f;
  for (int n : rings) {
    float r = n * unitKm * 1000 * stretch / V_MPP;
    if (r < 30 || r > 1500) continue;
    int step = r < 300 ? 3 : 1;
    for (int i = 0; i < 360; i += step) {
      float a = i * 0.0174533f;
      int x = rnd(hx + r * sinf(a)), y = rnd(hy - r * cosf(a));
      if (x >= 0 && x < MAP_W - 1 && y >= 0 && y < H) g.fillRect(x, y, 2, 2, C_RING);
    }
    if (hx > 0 && hx < MAP_W && hy - r > 10 && hy - r < H) {
      char t[16];
      snprintf(t, sizeof t, "%d %s", n, units.distKm ? "km" : "nm");
      haloText(g, rnd(hx - textW(R11, t) / 2.0f), rnd(hy - r - 7), t, R11, C_RING, C_LAND);
    }
  }

  // runways (zoomed in) and airport symbols
  if (V_ZOOM >= 10) {
    float w = fmaxf(2.0f, 45.0f / V_MPP);
    for (uint32_t i = 0; i < RUNWAYS_N; i++) {
      const MapRunway& r = RUNWAYS[i];
      float ax = sx(r.x1), ay = sy(r.y1), bx = sx(r.x2), by = sy(r.y2);
      if (ax > -50 && ax < MAP_W + 50 && ay > -50 && ay < H + 50)
        thickLine(g, ax, ay, bx, by, w > 3 ? w + 2 : w, C_RUNWAY);
    }
  }
  auto airportShown = [](const MapAirport& ap) {
    return !(V_ZOOM < 5 || !(ap.big || V_ZOOM >= 8) || (!ap.iata[0] && V_ZOOM < 10));
  };
  for (uint32_t i = 0; i < AIRPORTS_N && V_ZOOM < 10; i++) {
    const MapAirport& ap = AIRPORTS[i];
    if (!airportShown(ap)) continue;
    float x = sx(ap.x), y = sy(ap.y);
    if (x < 0 || x >= MAP_W || y < 0 || y >= H) continue;
    int ix = rnd(x), iy = rnd(y);
    g.fillCircle(ix, iy, 5, C_SURFACE);
    ring(g, ix, iy, 4, 2, C_RUNWAY);
    g.drawFastHLine(ix - 3, iy, 7, C_RUNWAY);
    g.drawFastVLine(ix, iy - 3, 7, C_RUNWAY);
  }

  // home
  if (hx > -10 && hx < MAP_W + 10 && hy > -10 && hy < H + 10) {
    int ix = rnd(hx), iy = rnd(hy);
    g.fillCircle(ix, iy, 9, C_SURFACE);
    ring(g, ix, iy, 8, 2, C_ACCENT);
    g.fillCircle(ix, iy, 3, C_ACCENT);
    take(hx - 10, hy - 10, hx + 10, hy + 10);
    // The default name follows the language; a name the user typed is shown as is.
    const char* hn = homeLabel(cfg.homeName);
    mapLabel(g, hx, hy, hn, B13, 12, C_ACCENT);
  }
  // airport names, then towns
  for (uint32_t i = 0; i < AIRPORTS_N; i++) {
    const MapAirport& ap = AIRPORTS[i];
    if (!airportShown(ap)) continue;
    float x = sx(ap.x), y = sy(ap.y);
    if (x < 0 || x >= MAP_W || y < 0 || y >= H) continue;
    char t[48];
    const char* code = ap.iata[0] ? ap.iata : ap.icao;
    if (V_ZOOM >= 11) snprintf(t, sizeof t, "%s (%s)", mapName(ap.name, language == LANG_EN), code);
    else snprintf(t, sizeof t, "%s", code);
    mapLabel(g, x, y, t, B12, 9, C_RUNWAY);
  }
  // Finnish country names (on land) and sea / lake names (on water)
  for (uint32_t i = 0; i < MAP_LABELS_N; i++) {
    const MapLabel& l = MAP_LABELS[i];
    if (V_ZOOM * 10 < l.minz10 || V_ZOOM * 10 > l.maxz10 + 5) continue;
    float x = sx(l.x), y = sy(l.y);
    if (x < -100 || x >= MAP_W + 100 || y < 0 || y >= H) continue;
    if (l.kind == 0) areaLabel(g, x, y, mapName(l.name, language == LANG_EN), B13, C_COUNTRY, C_LAND);
    else areaLabel(g, x, y, mapName(l.name, language == LANG_EN), R13, C_WATER, C_SEA);
  }
  for (uint32_t i = 0; i < PLACES_N; i++) {
    const MapPlace& p = PLACES[i];
    if (p.minz10 / 10.0f > V_ZOOM + 0.5f) continue;
    float x = sx(p.x), y = sy(p.y);
    if (x < 0 || x >= MAP_W || y < 0 || y >= H) continue;
    if (mapLabel(g, x, y, mapName(p.name, language == LANG_EN), p.big ? B14 : R12, 6, C_PLACE))
      g.fillRect(rnd(x) - 2, rnd(y) - 2, 5, 5, C_PLACE);
  }

  // scale bar
  double lat = latFromY(V_CY);
  float real = V_MPP * cos(lat * M_PI / 180), unitM = units.distKm ? 1000 : 1852;
  static const int nice[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000};
  int best = 1;
  for (int n : nice)
    if (n * unitM / real <= 110) best = n;
  int L = rnd(best * unitM / real), x = 12, y = H - 14;
  char t[16];
  snprintf(t, sizeof t, "%d %s", best, units.distKm ? "km" : "nm");
  g.fillRoundRect(x - 6, y - 18, L + 16 + textW(B12, t), 26, 4, C_SURFACE);
  g.fillRect(x, y - 1, L, 3, C_TEXT);
  g.fillRect(x, y - 5, 2, 6, C_TEXT);
  g.fillRect(x + L - 1, y - 5, 2, 6, C_TEXT);
  text(g, x + L + 5, y - 9, t, B12, C_TEXT);
}

// ---------------------------------------------------------------------------
//  Overlay: planes, buttons, panel (every frame)
// ---------------------------------------------------------------------------
static int viewIdx[MAX_PLANES];
static char rowHex[13][8];
static int nRows = 0;
const char* listRowHex(int row) { return row >= 0 && row < nRows ? rowHex[row] : nullptr; }

// On-screen buttons over the map (bottom right).
static const int BTN = 46, BTN_X = MAP_W - BTN - 10;
static const int BTN_Y_IN = H - 3 * (BTN + 8) - 2, BTN_Y_OUT = BTN_Y_IN + BTN + 8,
                 BTN_Y_HOME = BTN_Y_OUT + BTN + 8;
// Settings button: bottom left, just above the scale bar.
static const int SET_X = 10, SET_Y = H - 40 - BTN - 4;
// Buttons in the details panel.
static const int PB_Y = H - 96, PB_H = 40;

static void mapButton(Adafruit_GFX& g, int y, int kind, int bx = BTN_X) {
  g.fillRoundRect(bx + 2, y + 3, BTN, BTN, 8, C_SHADOW);   // shadow
  g.fillRoundRect(bx, y, BTN, BTN, 8, C_SURFACE);
  g.drawRoundRect(bx, y, BTN, BTN, 8, C_BTN_EDGE);
  int cx = bx + BTN / 2, cy = y + BTN / 2;
  if (kind == 3) {                                                  // gear
    for (int i = 0; i < 8; i++) {
      float a = i * 0.785398f;
      g.fillCircle(rnd(cx + 12 * sinf(a)), rnd(cy - 12 * cosf(a)), 4, C_PRIMARY);
    }
    g.fillCircle(cx, cy, 12, C_PRIMARY);
    g.fillCircle(cx, cy, 5, C_SURFACE);
    return;
  }
  if (kind != 2) g.fillRect(cx - 11, cy - 2, 22, 4, C_PRIMARY);        // minus bar
  if (kind == 0) g.fillRect(cx - 2, cy - 11, 4, 22, C_PRIMARY);        // plus
  if (kind == 2) {                                                  // house
    g.fillTriangle(cx, cy - 13, cx - 14, cy, cx + 14, cy, C_PRIMARY);
    g.fillRect(cx - 9, cy, 18, 12, C_PRIMARY);
    g.fillRect(cx - 3, cy + 4, 6, 8, C_SURFACE);
  }
}

// ---------------------------------------------------------------------------
//  Photo card: 288 x 214 px (16 % of the screen), in the top-left corner of the
//  map, or top-right when the selected plane would be underneath it.
// ---------------------------------------------------------------------------
static const int CARD_Y = 8, CARD_W = PHOTO_W + 16, QR_MAX = 74;   // QR up to version 4
static int CARD_X = 8;

static bool photoVisible(const AppState& s) {
  return s.selHex[0] && photo.state != PHOTO_NONE && !photo.hidden && !strcmp(photo.hex, s.selHex);
}
static int cardHeight() {
  return photo.state == PHOTO_MISSING ? 44 : 8 + PHOTO_H + 6 + QR_MAX + 4;   // 214
}
// Top-left normally; top-right if the selected plane would be under the card there
// (and not under it on the right). The map centre always stays uncovered.
static void placeCard(const Plane* sel) {
  if (!sel) return;
  float x = sx(sel->x), y = sy(sel->y);
  auto under = [&](int cx) {
    return x > cx - 16 && x < cx + CARD_W + 16 && y > CARD_Y - 16 && y < CARD_Y + cardHeight() + 12;
  };
  const int left = 8, right = MAP_W - CARD_W - 10;
  if (!under(left)) CARD_X = left;
  else if (!under(right)) CARD_X = right;
}

// QR code of the photo's web page, cached until the link changes.
int drawQr(Adafruit_GFX& g, int x, int y, const char* link, int m) {
  static char last[160] = "";
  static uint8_t buf[256];                 // enough for version 4
  static QRCode qr;
  static bool ok = false;
  if (strcmp(last, link)) {
    snprintf(last, sizeof last, "%s", link);
    // Pick the version from the byte-mode capacity table (ECC low). The library does not
    // check capacity itself, so a too-small version would overflow its buffer.
    static const uint8_t capacity[] = {0, 17, 32, 53, 78};
    size_t n = strlen(link);
    uint8_t v = 0;
    for (uint8_t i = 1; i <= 4 && !v; i++) if (n <= capacity[i]) v = i;
    ok = v && qrcode_getBufferSize(v) <= sizeof buf && qrcode_initText(&qr, buf, v, ECC_LOW, link) == 0;
  }
  if (!ok) return 0;
  int size = qr.size * m + 8;              // m px per module + quiet zone
  g.fillRect(x, y, size, size, C_WHITE);
  for (int r = 0; r < qr.size; r++)
    for (int c = 0; c < qr.size; c++)
      if (qrcode_getModule(&qr, c, r)) g.fillRect(x + 4 + c * m, y + 4 + r * m, m, m, C_BLACK);
  return size;
}

static void drawPhotoCard(Adafruit_GFX& g, uint32_t nowMs) {
  int h = cardHeight();
  g.fillRoundRect(CARD_X + 2, CARD_Y + 3, CARD_W, h, 10, C_SHADOW);    // shadow
  g.fillRoundRect(CARD_X, CARD_Y, CARD_W, h, 10, C_SURFACE);
  g.drawRoundRect(CARD_X, CARD_Y, CARD_W, h, 10, C_BTN_EDGE);
  int px = CARD_X + 8, py = CARD_Y + 8;
  if (photo.state == PHOTO_MISSING) {
    text(g, px + 4, CARD_Y + 14, TR("Tästä koneesta ei ole kuvaa", "No photo of this aircraft"), R13, C_TEXT2);
    return;
  }
  if (photo.state == PHOTO_READY && photo.pix) {
    g.drawRGBBitmap(px, py, photo.pix, PHOTO_W, PHOTO_H);
  } else {                                  // still loading
    g.fillRect(px, py, PHOTO_W, PHOTO_H, theme->photoBg);
    char t[32];
    const char* lbl = TR("Haetaan kuvaa", "Loading photo");
    snprintf(t, sizeof t, "%s%.*s", lbl, (int)((nowMs / 400) % 4), "...");
    text(g, px + PHOTO_W / 2 - (textW(R13, lbl) + textW(R13, "...")) / 2, py + PHOTO_H / 2 - 8, t, R13, C_TEXT2);
  }
  int cx = px + PHOTO_W - 14, cy = py + 14;                     // close mark
  g.fillCircle(cx, cy, 11, RGB(30, 36, 52));
  for (int d = -1; d <= 1; d++) {
    g.drawLine(cx - 5 + d, cy - 5, cx + 5 + d, cy + 5, C_WHITE);
    g.drawLine(cx + 5 + d, cy - 5, cx - 5 + d, cy + 5, C_WHITE);
  }
  // Credit: Planespotters asks for the photographer's name and a link to the photo.
  int ty = py + PHOTO_H + 6, textWmax = PHOTO_W - QR_MAX - 6;
  if (photo.state == PHOTO_READY) {
    char who[64];
    snprintf(who, sizeof who, TR("Kuva: %s", "Photo: %s"), photo.photographer[0] ? photo.photographer : TR("tuntematon", "unknown"));
    fit(who, B12, textWmax);
    text(g, px, ty, who, B12, C_TEXT);
    text(g, px, ty + 17, "planespotters.net", R12, C_TEXT2);
    text(g, px, ty + 40, TR("Kuvan sivu puhelimella:", "Photo page on your phone:"), R11, C_TEXT2);
    text(g, px, ty + 54, TR("skannaa QR-koodi", "scan the QR code"), R11, C_TEXT2);
    drawQr(g, px + PHOTO_W - QR_MAX, ty - 2, photo.link);
  } else {
    text(g, px, ty, TR("Kuva: planespotters.net", "Photo: planespotters.net"), R12, C_TEXT2);
  }
}

UiHit uiHitTest(int x, int y, const AppState& s, int* row) {
  if (s.pickHome) {
    if (x < MAP_W) {
      if (x >= BTN_X && x < BTN_X + BTN) {
        if (y >= BTN_Y_IN && y < BTN_Y_IN + BTN) return HIT_ZOOM_IN;
        if (y >= BTN_Y_OUT && y < BTN_Y_OUT + BTN) return HIT_ZOOM_OUT;
        if (y >= BTN_Y_HOME && y < BTN_Y_HOME + BTN) return HIT_HOME;
      }
      return HIT_MAP;
    }
    if (y >= PB_Y && y < PB_Y + PB_H) return x < PANEL_X + (W - PANEL_X) / 2 ? HIT_PICK_SAVE : HIT_PICK_CANCEL;
    return HIT_PANEL;
  }
  if (photoVisible(s) && x >= CARD_X && x < CARD_X + CARD_W && y >= CARD_Y && y < CARD_Y + cardHeight())
    return HIT_PHOTO;
  if (x < MAP_W) {
    if (x >= SET_X && x < SET_X + BTN && y >= SET_Y && y < SET_Y + BTN) return HIT_SETTINGS;
    if (x >= BTN_X && x < BTN_X + BTN) {
      if (y >= BTN_Y_IN && y < BTN_Y_IN + BTN) return HIT_ZOOM_IN;
      if (y >= BTN_Y_OUT && y < BTN_Y_OUT + BTN) return HIT_ZOOM_OUT;
      if (y >= BTN_Y_HOME && y < BTN_Y_HOME + BTN) return HIT_HOME;
    }
    return HIT_MAP;
  }
  if (s.selHex[0]) {
    if (y >= PB_Y && y < PB_Y + PB_H) return x < PANEL_X + (W - PANEL_X) / 2 ? HIT_FOLLOW : HIT_CLOSE;
    return HIT_PANEL;
  }
  if (y >= 80 && y < 80 + 26 * 13) {
    int r = (y - 80) / 26;
    if (r < nRows) { if (row) *row = r; return HIT_LIST_ROW; }
  }
  return HIT_PANEL;
}

int planeAt(AppState& s, int x, int y, int maxPx) {
  int best = -1;
  float bd = maxPx * maxPx;
  for (int i = 0; i < s.nPlanes; i++) {
    float dx = sx(s.planes[i].x) - x, dy = sy(s.planes[i].y) - y, d = dx * dx + dy * dy;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

// ---- Icon templates ----------------------------------------------------------------
// Drawing an icon from its triangles costs a lot when hundreds of planes are on screen
// (every icon twice: outline, then fill). So each shape is drawn once per kind, size and
// 5-degree heading into a small 1-bit canvas and kept as a list of horizontal runs,
// marked outline or fill. Drawing an icon is then a handful of line fills with no
// overdraw. Helicopters keep the direct path (their rotor turns).
namespace {
struct IconRun { int8_t dx, dy; uint8_t len, fill; };
struct IconEntry { uint32_t key; uint32_t first; uint16_t n; };
const int ICON_SLOTS = 512, ICON_RUNS = 49152, ICON_C = 32;   // canvas 64x64, centre at 32
IconEntry* iconTable = nullptr;
IconRun* iconRuns = nullptr;
uint32_t iconUsed = 0;
int iconCount = 0;

const IconEntry* iconTemplate(IconKind kind, float heading, float ks) {
  if (!iconTable) {
    iconTable = (IconEntry*)renderAlloc(sizeof(IconEntry) * ICON_SLOTS);
    iconRuns = (IconRun*)renderAlloc(sizeof(IconRun) * ICON_RUNS);
    if (!iconTable || !iconRuns) return nullptr;
    memset(iconTable, 0, sizeof(IconEntry) * ICON_SLOTS);
  }
  int hb = ((int)lroundf(heading / 5.0f) % 72 + 72) % 72;
  int sq = (int)lroundf(ks * 2);                       // size in half pixels
  if (sq < 2 || sq > 50) return nullptr;
  uint32_t key = 1u | (uint32_t)kind << 1 | (uint32_t)hb << 4 | (uint32_t)sq << 11;
  uint32_t h = (key * 2654435761u) >> 23;              // 9 bits
  for (int i = 0; i < ICON_SLOTS; i++) {
    IconEntry& e = iconTable[(h + i) & (ICON_SLOTS - 1)];
    if (e.key == key) return &e;
    if (e.key) continue;
    // Not made yet: draw outline and fill into 1-bit canvases and collect the runs.
    if (iconCount >= ICON_SLOTS * 3 / 4 || iconUsed + 64 * 6 > ICON_RUNS) {   // full: start over
      memset(iconTable, 0, sizeof(IconEntry) * ICON_SLOTS);
      iconUsed = 0;
      iconCount = 0;
      return iconTemplate(kind, heading, ks);
    }
    static GFXcanvas1 outer(64, 64), inner(64, 64);
    float hd = hb * 5.0f, k = sq * 0.5f;
    outer.fillScreen(0);
    inner.fillScreen(0);
    planeShape(outer, ICON_C, ICON_C, hd, k + 2, 1, kind);
    planeShape(inner, ICON_C, ICON_C, hd, k, 1, kind);
    e.first = iconUsed;
    for (int y = 0; y < 64; y++) {
      int x = 0;
      while (x < 64) {
        int c = inner.getPixel(x, y) ? 2 : outer.getPixel(x, y) ? 1 : 0;
        int x1 = x + 1;
        while (x1 < 64 && (inner.getPixel(x1, y) ? 2 : outer.getPixel(x1, y) ? 1 : 0) == c) x1++;
        if (c && iconUsed < ICON_RUNS)
          iconRuns[iconUsed++] = {(int8_t)(x - ICON_C), (int8_t)(y - ICON_C), (uint8_t)(x1 - x), (uint8_t)(c == 2)};
        x = x1;
      }
    }
    e.n = (uint16_t)(iconUsed - e.first);
    e.key = key;
    iconCount++;
    return &e;
  }
  return nullptr;
}
}  // namespace

// An aircraft icon with its outline, centred on (x, y).
static void drawIcon(Adafruit_GFX& g, float x, float y, float heading, float ks, uint16_t outline,
                     uint16_t fill, IconKind kind, float rotor) {
  const IconEntry* e = kind == ICON_HELI ? nullptr : iconTemplate(kind, heading, ks);
  if (!e) {
    planeShape(g, x, y, heading, ks + 2, outline, kind, rotor);
    planeShape(g, x, y, heading, ks, fill, kind, rotor);
    return;
  }
  int ix = rnd(x), iy = rnd(y);
  const IconRun* r = iconRuns + e->first;
  for (int i = 0; i < e->n; i++) g.drawFastHLine(ix + r[i].dx, iy + r[i].dy, r[i].len, r[i].fill ? fill : outline);
}

static void drawPlanes(Adafruit_GFX& g, AppState& s, int n, uint32_t nowMs) {
  float rotor = (nowMs % 3600000) * 0.17f;            // helicopter rotors: about half a turn a second
  float size = V_ZOOM < 7 ? 8 : 10;
  bool manyPlanes = n > 150;                 // then only the selected plane gets its trail
  for (int k = 0; k < n; k++) {             // trails underneath
    const Plane& p = s.planes[viewIdx[k]];
    bool sel = !strcmp(p.hex, s.selHex);
    if (manyPlanes && !sel) continue;
    uint16_t c = altColor(p);
    float lx = 0, ly = 0;
    for (int i = 0; i < p.trailN; i++) {
      const float* t = p.trailAt(i);
      float tx = sx(t[0]), ty = sy(t[1]);
      if (sel && i) { g.drawLine(lx, ly, tx, ty, c); g.drawLine(lx + 1, ly, tx + 1, ty, c); }
      else g.fillRect(rnd(tx) - 1, rnd(ty) - 1, 2, 2, c);
      lx = tx; ly = ty;
    }
    if (sel && p.trailN) g.drawLine(lx, ly, sx(p.x), sy(p.y), c);
  }
  for (int pass = 0; pass < 2; pass++)       // icons; selected on top
    for (int k = 0; k < n; k++) {
      const Plane& p = s.planes[viewIdx[k]];
      bool sel = !strcmp(p.hex, s.selHex);
      if (sel != (pass == 1)) continue;
      float x = sx(p.x), y = sy(p.y), sz = size + (sel ? 3 : 0), hd = p.hasTrack ? p.track : 0;
      if (sel) {
        g.fillCircle(rnd(x), rnd(y), rnd(sz + 9), theme->selected);
        ring(g, rnd(x), rnd(y), rnd(sz + 8), 2, C_ACCENT);
      }
      IconKind kind = iconKind(p);
      float ks = sz * iconScale(p, kind);
      drawIcon(g, x, y, hd, ks, p.emergency() ? C_EMERG : C_OUTLINE,
               p.emergency() ? RGB(255, 200, 200) : altColor(p), kind, rotor);
      take(x - size, y - size, x + size, y + size);
    }
  bool crowded = n > 25 && V_ZOOM < 8;
  for (int pass = 0; pass < 2; pass++)       // tags: selected first, then nearest
    for (int k = 0; k < n; k++) {
      const Plane& p = s.planes[viewIdx[k]];
      bool sel = !strcmp(p.hex, s.selHex);
      if (sel != (pass == 0)) continue;
      if (crowded && !sel && !p.emergency()) continue;
      char alt[24], l2[32];
      fmtAlt(alt, sizeof alt, p, true);
      if (p.hasVrate && abs(p.vrate) >= 300) strcat(alt, p.vrate > 0 ? " \x80" : " \x81");
      if (p.emergency()) snprintf(l2, sizeof l2, TR("HÄTÄ %s", "EMERG %s"), p.squawk);
      else snprintf(l2, sizeof l2, "%s", alt);
      if (p.emergency())
        planeTag(g, sx(p.x), sy(p.y), p.label(), B12, l2, C_EMERG, C_WHITE, C_WHITE, C_EMERG, sel);
      else if (sel)
        planeTag(g, sx(p.x), sy(p.y), p.label(), B14, l2, C_BAR, C_ON_BAR, theme->onBarSoft, C_BAR, true);
      else
        planeTag(g, sx(p.x), sy(p.y), p.label(), B12, l2, C_SURFACE, C_TEXT, C_TEXT2, C_BTN_EDGE, false);
    }
}

// The selected plane's path since take-off, coloured by altitude like the planes. Without
// a track history: a dashed line from the departure airport, if it is known.
static void drawFlightPath(Adafruit_GFX& g, AppState& s) {
  Plane* p = s.selected();
  if (!p || strcmp(flightPath.hex, p->hex)) return;
  float px = sx(p->x), py = sy(p->y);
  auto onScreen = [](float ax, float ay, float bx, float by) {
    return !((ax < -20 && bx < -20) || (ax > MAP_W + 20 && bx > MAP_W + 20) ||
             (ay < -20 && by < -20) || (ay > H + 20 && by > H + 20));
  };
  if (flightPath.state == PATH_READY && flightPath.n >= 2) {
    const PathPoint* pt = flightPath.pts;
    float lx = sx(pt[0].x), ly = sy(pt[0].y);
    for (int i = 1; i <= flightPath.n; i++) {
      bool last = i == flightPath.n;               // the last piece ends at the plane itself
      float x = last ? px : sx(pt[i].x), y = last ? py : sy(pt[i].y);
      int alt100 = last ? (p->hasAlt ? p->alt / 100 : pt[i - 1].alt100) : pt[i].alt100;
      if (onScreen(lx, ly, x, y) && fabsf(x - lx) + fabsf(y - ly) >= 1.5f) {
        thickLine(g, lx, ly, x, y, 3, altColorFt((pt[i - 1].alt100 + alt100) * 50.0f));
        lx = x; ly = y;
      } else if (!onScreen(lx, ly, x, y)) {
        lx = x; ly = y;
      }
    }
    return;
  }
  Route* r = s.route(p->cs);
  if (!r || r->state != ROUTE_KNOWN || !r->hasOrigin) return;
  float ox = sx(mercX(r->fromLon)), oy = sy(mercY(r->fromLat));
  float len = hypotf(px - ox, py - oy);
  for (float t = 0; t < len; t += 12) {             // dashes: 7 px on, 5 px off
    float a = t / len, b = fminf(t + 7, len) / len;
    float ax = ox + (px - ox) * a, ay = oy + (py - oy) * a, bx = ox + (px - ox) * b, by = oy + (py - oy) * b;
    if (onScreen(ax, ay, bx, by)) thickLine(g, ax, ay, bx, by, 2, C_TEXT2);
  }
}

static void drawCoverage(Adafruit_GFX& g, AppState& s) {
  if (s.fetchRadiusNm < 250 || s.demo) return;
  double lat = latFromY(s.fetchCy);
  float r = 250 * 1852 / cos(lat * M_PI / 180) / V_MPP, cx = sx(s.fetchCx), cy = sy(s.fetchCy);
  const float corners[4][2] = {{0, 0}, {MAP_W, 0}, {0, H}, {MAP_W, H}};
  bool covered = true;
  for (auto& c : corners)
    if (hypotf(c[0] - cx, c[1] - cy) >= r) covered = false;
  if (covered) return;
  for (int i = 0; i < 360; i += 2) {
    float a = i * 0.0174533f;
    g.fillRect(rnd(cx + r * sinf(a)) - 1, rnd(cy - r * cosf(a)) - 1, 3, 3, C_PRIMARY);
  }
  const char* t = units.distKm ? TR("Koneet 460 km:n säteellä keskipisteestä", "Aircraft within 460 km of the centre")
                               : TR("Koneet 250 nm:n säteellä keskipisteestä", "Aircraft within 250 nm of the centre");
  int w = textW(B12, t);
  int nx = 312 - w / 2, ny = H - 34;     // bottom centre, between the scale bar and the buttons
  g.fillRoundRect(nx - 8, ny, w + 16, 22, 5, C_BAR);
  text(g, nx, ny + 3, t, B12, C_ON_BAR);
}

static void row(Adafruit_GFX& g, int y, const char* lab, const char* value, uint16_t swatch = 0) {
  char v[48];
  snprintf(v, sizeof v, "%s", value);
  fit(v, B16, W - PANEL_X - 32 - textW(R12, lab) - 12);   // never run into the label
  text(g, PANEL_X + 16, y + 3, lab, R12, C_TEXT2);
  int vx = W - 16 - textW(B16, v);
  text(g, vx, y, v, B16, C_TEXT);
  if (swatch) g.fillRoundRect(vx - 16, y + 3, 10, 12, 2, swatch);
}

// Is the registered owner the airline flying it? By ICAO code when both have one,
// otherwise by the first word of the name ("Ryanair" and "Ryanair DAC").
static bool sameCompany(const Route& r) {
  if (r.airlineCode[0] && r.ownerCode[0]) return !strcmp(r.airlineCode, r.ownerCode);
  const char *a = r.airline, *b = r.owner;
  int n = 0;
  while (a[n] && a[n] != ' ' && tolower((uint8_t)a[n]) == tolower((uint8_t)b[n])) n++;
  return n >= 3 && (!a[n] || a[n] == ' ') && (!b[n] || b[n] == ' ');
}

static void panelButton(Adafruit_GFX& g, int x, int w, const char* label, bool on) {
  g.fillRoundRect(x, PB_Y, w, PB_H, 8, on ? C_BAR : C_SURFACE);
  g.drawRoundRect(x, PB_Y, w, PB_H, 8, on ? C_PRIMARY : C_BTN_EDGE);
  textC(g, x + w / 2, PB_Y + 12, label, B14, on ? C_ON_BAR : C_PRIMARY);
}

static void panelList(Adafruit_GFX& g, AppState& s, int n) {
  int x0 = PANEL_X + 16, x1 = W - 16;
  char t[48];
  text(g, x0, 50, TR("NÄKYVISSÄ", "IN VIEW"), B15, C_TEXT);
  snprintf(t, sizeof t, "%d %s", n, n == 1 ? TR("kone", "aircraft") : TR("konetta", "aircraft"));
  textR(g, x1, 51, t, R14, C_TEXT2);
  g.fillRect(x0, 74, x1 - x0, 2, C_PRIMARY);
  nRows = 0;
  if (!n) {
    if (s.wifiDown && !s.demo) {
      text(g, x0, 92, TR("Ei Wi-Fi-yhteyttä", "No Wi-Fi connection"), B18, C_EMERG);
      char nm[40];
      snprintf(nm, sizeof nm, "%s", s.wifiName);
      fit(nm, B14, x1 - x0);
      text(g, x0, 124, TR("Yhdistetään uudelleen verkkoon", "Reconnecting to the network"), R13, C_TEXT2);
      text(g, x0, 142, nm, B14, C_TEXT);
      text(g, x0, 172, TR("Laite yrittää itse, kunnes yhteys", "It keeps trying until the"), R13, C_TEXT2);
      text(g, x0, 189, TR("palaa. Verkon voi vaihtaa", "connection is back. The network"), R13, C_TEXT2);
      text(g, x0, 206, TR("asetuksista (ratas vasemmalla).", "can be changed in Settings (gear)."), R13, C_TEXT2);
      return;
    }
    if (!s.apiOk && !s.demo) {
      text(g, x0, 92, TR("Lentotietoja ei saada", "No flight data"), B18, C_EMERG);
      text(g, x0, 122, TR("Kokeiltu:", "Tried:"), R13, C_TEXT2);
      char buf[sizeof s.apiError];
      snprintf(buf, sizeof buf, "%s", s.apiError[0] ? s.apiError : TR("tuntematon syy", "unknown reason"));
      int y = 140;
      for (char* tok = strtok(buf, ";"); tok && y < 260; tok = strtok(nullptr, ";")) {
        while (*tok == ' ') tok++;
        char line[64];
        snprintf(line, sizeof line, "%s", tok);
        fit(line, R13, x1 - x0);
        text(g, x0, y, line, R13, C_TEXT);
        y += 17;
      }
      snprintf(t, sizeof t, TR("Yritetään uudelleen %d s välein.", "Retrying every %d s."), POLL_SECONDS);
      text(g, x0, y + 17, t, R13, C_TEXT2);
      return;
    }
    text(g, x0, 92, s.updatedEpoch ? TR("Taivas on tyhjä.", "The sky is empty.") : TR("Etsitään koneita…", "Looking for aircraft…"), B18, C_TEXT);
    text(g, x0, 122, TR("Loitonna nähdäksesi laajemmalle.", "Zoom out to see a wider area."), R13, C_TEXT2);
    return;
  }
  int y = 82;
  for (int k = 0; k < n && k < 13; k++) {
    const Plane& p = s.planes[viewIdx[k]];
    if (k % 2) g.fillRect(PANEL_X + 8, y - 4, W - PANEL_X - 16, 26, theme->stripe);
    g.fillCircle(x0 + 4, y + 9, 5, altColor(p));
    text(g, x0 + 16, y, p.label(), B15, C_TEXT);
    fmtAlt(t, sizeof t, p, true);
    text(g, x0 + 122, y + 1, t, R13, C_TEXT2);
    fmtDist(t, sizeof t, haversineKm(cfg.homeLat, cfg.homeLon, p.lat, p.lon));
    textR(g, x1, y + 1, t, R13, C_TEXT2);
    snprintf(rowHex[nRows++], 8, "%s", p.hex);
    y += 26;
  }
  if (n > 13) {
    snprintf(t, sizeof t, TR("+ %d lisää", "+ %d more"), n - 13);
    text(g, x0, y, t, R12, C_TEXT2);
  }
}

// "14:05" from minutes past midnight (same style as the clock at the top).
static void fmtHm(char* out, size_t n, int m) {
  m = (m % 1440 + 1440) % 1440;
  snprintf(out, n, "%02d:%02d", m / 60, m % 60);
}
// Minutes b - a on a 24-hour clock, in -720..719.
static int clockDiff(int a, int b) { return ((b - a) % 1440 + 1440 + 720) % 1440 - 720; }

// Departure and arrival line under the city names. Times from the timetable service
// when there are some; otherwise an arrival estimate from distance and ground speed.
static void panelTimes(Adafruit_GFX& g, const Route& r, const Plane& p, const struct tm* now,
                       int x0, int x1, int y) {
  const uint16_t C_LATE = theme->late;
  char t[32], c[12];
  bool sched = r.hasTimes;
  // Departure
  int dep = sched ? (r.depEst >= 0 ? r.depEst : r.depSched) : -1;
  if (dep >= 0) {
    fmtHm(c, sizeof c, dep);
    int d = r.depSched >= 0 ? clockDiff(r.depSched, dep) : 0;
    if (d >= 5) snprintf(t, sizeof t, TR("Lähti %s (+%d)", "Dep %s (+%d)"), c, d);   // minutes behind the timetable
    else snprintf(t, sizeof t, TR("Lähti %s", "Dep %s"), c);
    text(g, x0, y, t, R12, d >= 15 ? C_LATE : C_TEXT);
  }
  // Arrival
  int arr = -1, late = 0;
  bool est = false;
  if (sched && (r.arrEst >= 0 || r.arrSched >= 0)) {
    arr = r.arrEst >= 0 ? r.arrEst : r.arrSched;
    if (r.arrSched >= 0) late = clockDiff(r.arrSched, arr);
  } else if (r.hasDest && p.hasGs && p.gs > 60 && now) {
    double km = haversineKm(p.lat, p.lon, r.toLat, r.toLon);
    double min = km / (p.gs * 1.852) * 60;
    if (min < 20 * 60) {
      arr = now->tm_hour * 60 + now->tm_min + (int)lround(min);
      est = true;
    }
  }
  if (arr >= 0) {
    fmtHm(c, sizeof c, arr);
    if (est) snprintf(t, sizeof t, TR("Saapuu n. %s", "Arr ~%s"), c);         // our own estimate
    else if (late >= 5) snprintf(t, sizeof t, TR("Saapuu %s (+%d)", "Arr %s (+%d)"), c, late);
    else snprintf(t, sizeof t, TR("Saapuu %s", "Arr %s"), c);
    textR(g, x1, y, t, R12, late >= 15 ? C_LATE : C_TEXT);
  }
}

static void panelDetails(Adafruit_GFX& g, AppState& s, const Plane& p, const struct tm* now) {
  int x0 = PANEL_X + 16, x1 = W - 16;
  Route* r = s.route(p.cs);
  bool known = r && r->state == ROUTE_KNOWN;
  bool looking = p.cs[0] && (!r || r->state == ROUTE_PENDING);
  // The registered owner, only where it adds something: in place of the airline when
  // there is none, or on its own line when it isn't the airline flying (e.g. leased).
  const char* owner = r && r->owner[0] && !strcmp(r->hex, p.hex) ? r->owner : nullptr;
  bool airlineShown = known && r->airline[0];
  const char* ownerLine = owner && airlineShown && !sameCompany(*r) ? owner : nullptr;
  char title[16], sub[64], t[48];
  snprintf(title, sizeof title, "%s", known && r->flight[0] ? r->flight : p.label());
  const Fnt* tf = &B34;
  if (textW(*tf, title) > x1 - x0) tf = &B26;
  if (textW(*tf, title) > x1 - x0) tf = &B22;
  text(g, x0, 46, title, *tf, C_PRIMARY);
  if (known && r->airline[0]) {
    if (r->flight[0] && p.cs[0]) snprintf(sub, sizeof sub, "%s  \x83  %s", r->airline, p.cs);
    else snprintf(sub, sizeof sub, "%s", r->airline);
  } else if (owner) {
    snprintf(sub, sizeof sub, "%s", owner);
  } else {
    snprintf(sub, sizeof sub, "%s", looking ? "" : typeName(p.type));
  }
  fit(sub, R14, x1 - x0);
  text(g, x0, 88, sub, R14, C_TEXT2);
  if (p.emergency()) {
    g.fillRoundRect(x0, 106, x1 - x0, 22, 4, C_EMERG);
    snprintf(t, sizeof t, TR("HÄTÄKOODI %s", "EMERGENCY %s"), p.squawk);
    text(g, x0 + 6, 110, t, B13, C_WHITE);
  }
  int y = 132;
  if (known && r->from[0] && r->to[0]) {
    text(g, x0, y, r->from, B30, C_TEXT);
    textR(g, x1, y, r->to, B30, C_TEXT);
    int ax0 = x0 + textW(B30, r->from) + 10, ax1 = x1 - textW(B30, r->to) - 10;
    g.fillRect(ax0, y + 17, ax1 - 4 - ax0, 2, C_ACCENT);
    g.fillTriangle(ax1, y + 18, ax1 - 9, y + 12, ax1 - 9, y + 24, C_ACCENT);
    char c1[24], c2[24];
    snprintf(c1, sizeof c1, "%s", r->fromCity);
    snprintf(c2, sizeof c2, "%s", r->toCity);
    fit(c1, R12, 115);
    fit(c2, R12, 115);
    text(g, x0, y + 38, c1, R12, C_TEXT2);
    textR(g, x1, y + 38, c2, R12, C_TEXT2);
    panelTimes(g, *r, p, now, x0, x1, y + 55);
  } else {
    text(g, x0, y + 4, looking ? TR("Haetaan reittiä…", "Looking up route…") : TR("Reittiä ei ole julkaistu", "No published route"), B16, C_TEXT);
    if (!looking) {
      text(g, x0, y + 28, TR("Kaikki lennot eivät kerro määränpäätään", "Not every flight publishes its route"), R12, C_TEXT2);
      text(g, x0, y + 43, TR("(esim. yksityis- ja sotilaslennot).", "(e.g. private and military flights)."), R12, C_TEXT2);
    }
  }
  g.fillRect(x0, 207, x1 - x0, 1, C_LINE);

  double dist = haversineKm(cfg.homeLat, cfg.homeLon, p.lat, p.lon);
  double brg = bearingDeg(cfg.homeLat, cfg.homeLon, p.lat, p.lon);
  y = 213;
  int dy = ownerLine ? 21 : 23;              // a little tighter to fit the owner line
  fmtAlt(t, sizeof t, p, false);  row(g, y, TR("KORKEUS", "ALTITUDE"), t, altColor(p)); y += dy;
  fmtVrate(t, sizeof t, p);       row(g, y, TR("NOUSU/LASKU", "VERTICAL RATE"), t);              y += dy;
  fmtSpeed(t, sizeof t, p);       row(g, y, TR("NOPEUS", "GROUND SPEED"), t);                 y += dy;
  if (p.hasTrack) snprintf(t, sizeof t, "%03d\x82 %s", rnd(p.track) % 360, compass(p.track));
  else snprintf(t, sizeof t, "--");
  row(g, y, TR("SUUNTA", "TRACK"), t);        y += dy;
  char d[24];
  fmtDist(d, sizeof d, dist);
  snprintf(t, sizeof t, "%s %s", d, compass(brg));
  row(g, y, TR("ETÄISYYS KODISTA", "DISTANCE FROM HOME"), t);      y += dy;
  row(g, y, TR("KONETYYPPI", "AIRCRAFT TYPE"), p.type[0] ? typeName(p.type) : "--");  y += dy;
  row(g, y, TR("TUNNUS", "REGISTRATION"), p.reg[0] ? p.reg : "--");
  if (ownerLine) {
    char o[64];
    snprintf(o, sizeof o, TR("omistaja %s", "owned by %s"), ownerLine);
    fit(o, R12, x1 - x0);
    textR(g, x1, y + 20, o, R12, C_TEXT2);
  }

  int bw = (x1 - x0 - 10) / 2;
  panelButton(g, x0, bw, s.follow ? TR("SEURATAAN", "FOLLOWING") : TR("SEURAA", "FOLLOW"), s.follow);
  panelButton(g, x0 + bw + 10, bw, TR("SULJE", "CLOSE"), false);
}

// ---- Setting home: a cross in the middle of the map, and Save / Cancel -----------------
static void drawPickCross(Adafruit_GFX& g) {
  int cx = MAP_W / 2, cy = H / 2;
  for (int r = 15; r <= 17; r++) g.drawCircle(cx, cy, r, C_SURFACE);
  for (int r = 13; r <= 14; r++) g.drawCircle(cx, cy, r, C_ACCENT);
  g.fillRect(cx - 30, cy - 2, 16, 4, C_ACCENT);
  g.fillRect(cx + 15, cy - 2, 16, 4, C_ACCENT);
  g.fillRect(cx - 2, cy - 30, 4, 16, C_ACCENT);
  g.fillRect(cx - 2, cy + 15, 4, 16, C_ACCENT);
  g.fillCircle(cx, cy, 3, C_ACCENT);
  const char* t = TR("Siirrä karttaa, kunnes risti on kotisi kohdalla", "Move the map until the cross is on your home");
  int w = textW(B13, t);
  g.fillRoundRect(MAP_W / 2 - w / 2 - 10, 10, w + 20, 26, 6, C_BAR);
  text(g, MAP_W / 2 - w / 2, 15, t, B13, C_ON_BAR);
}
static void panelPick(Adafruit_GFX& g, const AppState& s) {
  int x0 = PANEL_X + 16, x1 = W - 16;
  text(g, x0, 50, TR("Aseta koti", "Set home"), B26, C_PRIMARY);
  char t[64];
  if (s.pickName[0]) {
    text(g, x0, 94, TR("Haun tulos:", "Search result:"), R13, C_TEXT2);
    snprintf(t, sizeof t, "%s", s.pickName);
    fit(t, B16, x1 - x0);
    text(g, x0, 112, t, B16, C_TEXT);
  }
  const char* fi[] = {"Risti on kohdassa, joka löytyi.", "Hienosäädä vetämällä karttaa;",
                      "+ lähentää. Kun risti on kotisi", "kohdalla, paina TALLENNA."};
  const char* en[] = {"The cross marks the place found.", "Drag the map to fine-tune it;",
                      "+ zooms in. When the cross is on", "your home, press SAVE."};
  const char** lines = TR(fi, en);
  for (int i = 0; i < 4; i++) text(g, x0, 150 + i * 19, lines[i], R13, C_TEXT);
  double lat = latFromY(s.cy), lon = lonFromX(s.cx);
  snprintf(t, sizeof t, "%.4f\x82 %s   %.4f\x82 %s", fabs(lat), lat >= 0 ? TR("P", "N") : TR("E", "S"),
           fabs(lon), lon >= 0 ? TR("I", "E") : TR("L", "W"));
  localDecimal(t);
  text(g, x0, 240, t, R12, C_TEXT2);
  text(g, x0, 300, TR("Kodin voi vaihtaa myöhemmin", "Home can be changed later"), R12, C_TEXT2);
  text(g, x0, 316, TR("asetuksista.", "in Settings."), R12, C_TEXT2);
  int bw = (x1 - x0 - 10) / 2;
  panelButton(g, x0, bw, TR("TALLENNA", "SAVE"), true);
  panelButton(g, x0 + bw + 10, bw, TR("PERUUTA", "CANCEL"), false);
}

void renderOverlay(Adafruit_GFX& g, AppState& s, uint32_t nowMs, const struct tm* now,
                   const struct tm* upd) {
  g.setTextWrap(false);
  s.advance(nowMs);
  setView(s.cx, s.cy, s.zoom);
  int n = s.inView(viewIdx, MAX_PLANES);
  nTaken = 0;
  // keep tags away from the buttons
  take(BTN_X - 4, BTN_Y_IN - 4, MAP_W, H);
  take(0, SET_Y - 4, 150, H);
  bool card = photoVisible(s) && !s.pickHome;
  if (s.pickHome) n = 0;                           // no planes while setting home
  if (card) {
    placeCard(s.selected());
    take(CARD_X - 4, 0, CARD_X + CARD_W + 4, CARD_Y + cardHeight() + 4);   // keep tags off the card
  }
  drawFlightPath(g, s);                      // under the planes
  drawPlanes(g, s, n, nowMs);
  drawCoverage(g, s);
  if (card) drawPhotoCard(g, nowMs);
  Plane* sel = s.selected();
  if (s.follow && sel) {
    char t[32];
    snprintf(t, sizeof t, TR("SEURATAAN %s", "FOLLOWING %s"), sel->label());
    int w = textW(B13, t), by = card ? CARD_Y + cardHeight() + 10 : 8, bx = card ? CARD_X : 8;
    g.fillRoundRect(bx, by, w + 16, 24, 5, C_BAR);
    text(g, bx + 8, by + 4, t, B13, C_ON_BAR);
  }
  mapButton(g, BTN_Y_IN, 0);
  mapButton(g, BTN_Y_OUT, 1);
  mapButton(g, BTN_Y_HOME, 2);
  if (!s.pickHome) mapButton(g, SET_Y, 3, SET_X);
  if (s.pickHome) drawPickCross(g);

  // panel
  int x0 = PANEL_X + 16, x1 = W - 16;
  g.fillRect(PANEL_X, 0, W - PANEL_X, H, C_SURFACE);
  g.fillRect(PANEL_X, 0, 2, H, C_BAR);
  g.fillRect(PANEL_X, 0, W - PANEL_X, 38, C_BAR);
  char t[64], c[16];
  fmtClock(c, sizeof c, now, false);
  textR(g, x1, 9, c, B16, C_ON_BAR);
  // Name: the biggest font that fits beside the clock
  const Fnt* nf = &B12;
  for (const Fnt* f : {&B16, &B15, &B14, &B13})
    if (textW(*f, APP_NAME) <= x1 - textW(B16, c) - 12 - x0) { nf = f; break; }
  text(g, x0, 9 + (16 - nf->size) / 2 + 1, APP_NAME, *nf, C_ON_BAR);
  if (s.pickHome) panelPick(g, s);
  else if (sel) panelDetails(g, s, *sel, now);
  else panelList(g, s, n);

  g.fillRect(PANEL_X + 10, H - 46, W - PANEL_X - 20, 1, C_LINE);
  if ((s.wifiDown || !s.apiOk) && !s.demo) {
    g.fillRoundRect(PANEL_X + 10, H - 41, W - PANEL_X - 20, 19, 4, C_EMERG);
    text(g, x0, H - 39, s.wifiDown ? TR("Ei Wi-Fi-yhteyttä", "No Wi-Fi connection") : TR("Ei lentotietoja", "No flight data"), B12, C_WHITE);
  } else {
    fmtClock(c, sizeof c, s.updatedEpoch ? upd : nullptr, true);
    snprintf(t, sizeof t, TR("Päivitetty %s  \x83  %s", "Updated %s  \x83  %s"), c, s.demo ? "DEMO" : s.source);
    text(g, x0, H - 39, t, R12, C_TEXT2);
  }
  text(g, x0, H - 21, s.pickHome ? TR("Vedä karttaa \x83 nipistä", "Drag the map \x83 pinch")
       : sel ? TR("Napauta toista konetta tai SULJE", "Tap another aircraft or CLOSE")
       : TR("Napauta konetta \x83 vedä \x83 nipistä", "Tap an aircraft \x83 drag \x83 pinch"), R11, C_TEXT2);
}

void renderMessage(Adafruit_GFX& g, const char* big, const char* small) {
  g.fillScreen(C_BAR);
  g.setTextWrap(false);
  planeShape(g, W / 2, 170, 45, 30, C_ON_BAR);
  textC(g, W / 2, 222, big, B22, C_ON_BAR);
  textC(g, W / 2, 258, small, R14, theme->onBarSoft);
}

// ============================================================================
//  Text drawing
// ============================================================================
// Text drawing. Produces exactly the same pixels as Adafruit_GFX's print() with a
// custom font, but draws each glyph row as horizontal runs instead of pixel by pixel,
// and draws outlined text in one pass instead of printing the string 13 times.
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
