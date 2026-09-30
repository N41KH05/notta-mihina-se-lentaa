#include "demo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simulated traffic around home, for trying the device without internet.
namespace {
struct Airline { const char *icao, *iata, *name; };
const Airline AIRLINES[] = {{"FIN", "AY", "Finnair"}, {"SAS", "SK", "Scandinavian Airlines"},
                            {"NOZ", "DY", "Norwegian"}, {"DLH", "LH", "Lufthansa"},
                            {"RYR", "FR", "Ryanair"}, {"BAW", "BA", "British Airways"},
                            {"KLM", "KL", "KLM"}, {"QTR", "QR", "Qatar Airways"}};
// City names as shown on screen (Finnish, English), and the airport positions.
struct Airport { const char *code, *cityFi, *cityEn; float lat, lon; };
const Airport DEMO_AIRPORTS[] = {
  {"HEL", "Helsinki", "Helsinki", 60.317f, 24.963f},  {"ARN", "Tukholma", "Stockholm", 59.652f, 17.919f},
  {"LHR", "Lontoo", "London", 51.470f, -0.454f},      {"FRA", "Frankfurt", "Frankfurt", 50.033f, 8.570f},
  {"AMS", "Amsterdam", "Amsterdam", 52.310f, 4.768f}, {"OUL", "Oulu", "Oulu", 64.930f, 25.355f},
  {"CPH", "Kööpenhamina", "Copenhagen", 55.618f, 12.656f}, {"DOH", "Doha", "Doha", 25.273f, 51.608f},
  {"TLL", "Tallinna", "Tallinn", 59.413f, 24.833f},   {"RIX", "Riika", "Riga", 56.924f, 23.971f},
  {"TKU", "Turku", "Turku", 60.514f, 22.263f},        {"OSL", "Oslo", "Oslo", 60.194f, 11.100f}};
const int N_DEMO_AIRPORTS = sizeof DEMO_AIRPORTS / sizeof DEMO_AIRPORTS[0];
const char* cityOf(const char* code) {
  for (int i = 0; i < N_DEMO_AIRPORTS; i++)
    if (!strcmp(DEMO_AIRPORTS[i].code, code)) return TR(DEMO_AIRPORTS[i].cityFi, DEMO_AIRPORTS[i].cityEn);
  return code;
}
const char* TYPES[] = {"A20N", "A321", "A359", "E190", "AT76", "B738", "A333", "BCS3", "B789"};
const int ALTS[] = {3500, 8000, 17000, 24000, 34000, 36000, 38000, 12000, 29000};
uint32_t seed = 7;
uint32_t rnd() { seed = seed * 1103515245u + 12345u; return (seed >> 8) & 0xFFFFFF; }
float frand(float a, float b) { return a + (b - a) * (rnd() / 16777215.0f); }
}  // namespace

void demoInit(AppState& s, uint32_t nowMs) {
  float hx = mercX(cfg.homeLon), hy = mercY(cfg.homeLat);
  s.nPlanes = 0;
  for (int i = 0; i < 28 && i < MAX_PLANES; i++) {
    Plane& p = s.planes[s.nPlanes++];
    memset(&p, 0, sizeof p);
    const Airline& al = AIRLINES[rnd() % 8];
    int num = 100 + rnd() % 2900;
    snprintf(p.hex, sizeof p.hex, "%06x", 0x460000 + i * 97);
    snprintf(p.cs, sizeof p.cs, "%s%d", al.icao, num);
    snprintf(p.reg, sizeof p.reg, "OH-L%c%c", 'A' + i % 26, 'B' + (i * 7) % 24);
    snprintf(p.type, sizeof p.type, "%s", TYPES[rnd() % 9]);
    snprintf(p.squawk, sizeof p.squawk, "%s", i == 11 ? "7700" : "1000");
    float ang = frand(0, 6.2832f), dist = frand(5, 260) * 2000;
    p.fx = hx + cosf(ang) * dist;
    p.fy = hy + sinf(ang) * dist;
    p.lat = latFromY(p.fy);
    p.lon = lonFromX(p.fx);
    p.track = frand(0, 360);
    p.gs = frand(250, 480);
    p.alt = ALTS[i % 9];
    p.vrate = (rnd() % 10 < 3) ? ((rnd() & 1) ? 1800 : -1200) : 0;
    p.hasAlt = p.hasGs = p.hasTrack = p.hasVrate = true;
    p.tMs = nowMs;
    p.x = p.fx; p.y = p.fy;
    p.addTrail(p.fx, p.fy);
    // a matching simulated route
    Route& r = s.routes[i % ROUTE_CACHE];
    memset(&r, 0, sizeof r);
    snprintf(r.cs, sizeof r.cs, "%s", p.cs);
    r.state = ROUTE_KNOWN;
    snprintf(r.flight, sizeof r.flight, "%s%d", al.iata, num);
    snprintf(r.airline, sizeof r.airline, "%s", al.name);
    int a = rnd() % 12, b = (a + 1 + rnd() % 11) % 12;
    snprintf(r.from, sizeof r.from, "%s", DEMO_AIRPORTS[a].code);
    snprintf(r.fromCity, sizeof r.fromCity, "%s", cityOf(DEMO_AIRPORTS[a].code));
    snprintf(r.to, sizeof r.to, "%s", DEMO_AIRPORTS[b].code);
    snprintf(r.toCity, sizeof r.toCity, "%s", cityOf(DEMO_AIRPORTS[b].code));
    r.hasDest = true;
    r.toLat = DEMO_AIRPORTS[b].lat;
    r.toLon = DEMO_AIRPORTS[b].lon;
  }
  s.demo = true;
  s.apiOk = true;
  snprintf(s.source, sizeof s.source, "demo");
}

// Simulate a fresh report: move planes to where they'd be now.
void demoStep(AppState& s, uint32_t nowMs, int clockMin) {
  s.advance(nowMs);
  // Simulated timetables for every other flight (as if an AirLabs key were set); the
  // rest show only the estimate worked out from distance and speed.
  if (clockMin >= 0)
    for (int i = 0; i < ROUTE_CACHE; i += 2) {
      Route& r = s.routes[i];
      if (r.state != ROUTE_KNOWN || r.hasTimes) continue;
      auto wrap = [](int m) { return (int16_t)((m % 1440 + 1440) % 1440); };
      int dep = clockMin - 20 - (int)(rnd() % 100);
      int late = (rnd() % 3 == 0) ? 15 + (int)(rnd() % 40) : (int)(rnd() % 8);
      r.depSched = wrap(dep - late);
      r.depEst = wrap(dep);
      int arr = clockMin + 10 + (int)(rnd() % 120);
      for (int k = 0; k < s.nPlanes; k++)          // when it would get there at its speed
        if (!strcmp(s.planes[k].cs, r.cs) && s.planes[k].gs > 60) {
          const Plane& p = s.planes[k];
          float dy = (p.lat - r.toLat) * 111.2f, dx = (p.lon - r.toLon) * 111.2f * cosf(p.lat * 0.01745f);
          arr = clockMin + (int)(sqrtf(dx * dx + dy * dy) / (p.gs * 1.852f) * 60) + (int)(rnd() % 6);
        }
      r.arrEst = wrap(arr);
      r.arrSched = wrap(arr - late + 4);
      r.times = TIMES_KNOWN;
      r.hasTimes = true;
    }
  for (int i = 0; i < s.nPlanes; i++) {
    Plane& p = s.planes[i];
    p.fx = p.x; p.fy = p.y;
    p.lat = latFromY(p.fy);
    p.lon = lonFromX(p.fx);
    p.tMs = nowMs;
    p.track = fmodf(p.track + frand(-2, 2) + 360, 360);
    p.addTrail(p.fx, p.fy);
  }
}

// The language changed: show the simulated routes' city names in the new language.
void demoRelabel(AppState& s) {
  for (int i = 0; i < ROUTE_CACHE; i++) {
    Route& r = s.routes[i];
    if (r.state != ROUTE_KNOWN) continue;
    snprintf(r.fromCity, sizeof r.fromCity, "%s", cityOf(r.from));
    snprintf(r.toCity, sizeof r.toCity, "%s", cityOf(r.to));
  }
}
