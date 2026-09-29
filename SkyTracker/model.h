// Shared data: the planes, the view, and what the screen should show.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "lang.h"

// Display units, chosen on screen and saved in flash (see SkyTracker.ino).
struct Units {
  bool distKm = DEFAULT_DISTANCE_KM;
  bool speedKmh = DEFAULT_SPEED_KMH;
  bool altM = DEFAULT_ALTITUDE_M;
};
inline Units units;

// Settings that can also be changed on the phone settings page (web.cpp) and are saved
// in flash. config.h gives the starting values.
struct Config {
  double homeLat = HOME_LAT, homeLon = HOME_LON;
  char homeName[24] = HOME_NAME;   // empty: the default label in the current language
  int8_t nightStart = NIGHT_START_HOUR, nightEnd = NIGHT_END_HOUR;   // equal = never off
  bool photos = SHOW_PHOTOS;
  char contact[64] = PHOTO_CONTACT;          // for Planespotters (see config.h)
  char airlabsKey[80] = AIRLABS_KEY;         // "" = no timetable times
};
inline Config cfg;

static const float WORLD_M = 20037508.34f;             // half the mercator world width
static const int SCREEN_W = 800, SCREEN_H = 480, MAP_W = 530;

inline float mercX(double lon) { return (float)(lon * 0.017453292519943295 * 6378137.0); }
inline float mercY(double lat) {
  if (lat > 85) lat = 85;
  if (lat < -85) lat = -85;
  return (float)(log(tan(M_PI / 4 + lat * 0.017453292519943295 / 2)) * 6378137.0);
}
inline double latFromY(float y) { return (2 * atan(exp(y / 6378137.0)) - M_PI / 2) * 57.29577951308232; }
inline double lonFromX(float x) { return x / 6378137.0 * 57.29577951308232; }
inline float metresPerPx(float zoom) { return 2 * WORLD_M / (256.0f * powf(2.0f, zoom)); }

struct Plane {
  char hex[8], cs[10], reg[12], type[6], squawk[6];
  double lat, lon;
  float fx, fy;          // last reported position (mercator metres)
  float x, y;            // position shown now (moved along its track since the report)
  uint32_t tMs;          // millis() when the reported position was measured
  int32_t alt, vrate;
  float gs, track;
  bool hasAlt, hasGs, hasTrack, hasVrate;
  uint8_t trailN, trailHead;
  float trail[TRAIL_POINTS][2];

  const char* label() const { return cs[0] ? cs : reg[0] ? reg : hex; }
  bool emergency() const {
    return !strcmp(squawk, "7500") || !strcmp(squawk, "7600") || !strcmp(squawk, "7700");
  }
  void addTrail(float px, float py) {
    if (trailN) {
      int last = (trailHead + TRAIL_POINTS - 1) % TRAIL_POINTS;
      if (trail[last][0] == px && trail[last][1] == py) return;
    }
    trail[trailHead][0] = px;
    trail[trailHead][1] = py;
    trailHead = (trailHead + 1) % TRAIL_POINTS;
    if (trailN < TRAIL_POINTS) trailN++;
  }
  // i = 0 is the oldest breadcrumb
  const float* trailAt(int i) const {
    return trail[(trailHead + TRAIL_POINTS - trailN + i) % TRAIL_POINTS];
  }
};

enum RouteState : uint8_t { ROUTE_EMPTY = 0, ROUTE_PENDING, ROUTE_KNOWN, ROUTE_UNKNOWN };
enum TimesState : uint8_t { TIMES_NONE = 0, TIMES_PENDING, TIMES_KNOWN, TIMES_UNKNOWN };
struct Route {
  char cs[10];
  RouteState state;
  char flight[10], airline[40], from[5], fromCity[24], to[5], toCity[24];
  bool hasDest;              // destination airport position known (for the arrival estimate)
  float toLat, toLon;
  // Schedule from AirLabs, as local minutes past midnight (-1 = not known)
  TimesState times;
  bool hasTimes;             // the times below are filled in (kept while being refreshed)
  int16_t depSched, depEst, arrSched, arrEst;
  uint32_t timesMs;          // millis() of the last lookup (or failed attempt)
};

static const int ROUTE_CACHE = 32;

// A place found when setting the home position (by address search or from the map's towns).
struct Place { char name[48]; char detail[64]; double lat, lon; };

struct AppState {
  // view
  float cx, cy;
  int zoom;
  // traffic
  Plane* planes;          // MAX_PLANES entries (in PSRAM)
  int nPlanes;
  char selHex[8];         // "" = nothing selected
  bool follow;
  Route routes[ROUTE_CACHE];
  // status
  uint32_t updatedEpoch;
  bool apiOk;
  char apiError[120];
  char source[16];
  float fetchCx, fetchCy;
  int fetchRadiusNm;
  bool demo;
  bool night;
  bool wifiDown;          // the saved network can't be reached (being retried)
  bool pickHome;          // setting home: move the map under the cross, then save
  char pickName[48];      // the place that was chosen
  float pickX, pickY;     // where it is (the house button goes back there)
  char wifiName[33];

  Plane* find(const char* hex) {
    for (int i = 0; i < nPlanes; i++)
      if (!strcmp(planes[i].hex, hex)) return &planes[i];
    return nullptr;
  }
  Plane* selected() { return selHex[0] ? find(selHex) : nullptr; }
  Route* route(const char* cs) {
    if (!cs || !cs[0]) return nullptr;
    for (auto& r : routes)
      if (r.state != ROUTE_EMPTY && !strcmp(r.cs, cs)) return &r;
    return nullptr;
  }
  // Move every plane along its track since it was last reported, so planes glide
  // smoothly between fetches.
  void advance(uint32_t nowMs) {
    for (int i = 0; i < nPlanes; i++) {
      Plane& p = planes[i];
      if (!p.hasGs || !p.hasTrack) { p.x = p.fx; p.y = p.fy; continue; }
      float age = (int32_t)(nowMs - p.tMs) / 1000.0f;
      if (age < 0) age = 0;
      if (age > 60) age = 60;
      float c = cosf(p.lat * 0.0174533f);
      float v = p.gs * 0.514444f / (c < 0.1f ? 0.1f : c);
      float a = p.track * 0.0174533f;
      p.x = p.fx + sinf(a) * v * age;
      p.y = p.fy + cosf(a) * v * age;
    }
  }
  void viewBounds(float margin, float& x0, float& y0, float& x1, float& y1) const {
    float m = metresPerPx(zoom);
    x0 = cx - (MAP_W / 2 + margin) * m;
    x1 = cx + (MAP_W / 2 + margin) * m;
    y0 = cy - (SCREEN_H / 2 + margin) * m;
    y1 = cy + (SCREEN_H / 2 + margin) * m;
  }
  // Indexes of planes in view, nearest to the centre first. Returns the count.
  int inView(int* out, int max) const {
    float x0, y0, x1, y1;
    viewBounds(0, x0, y0, x1, y1);
    int n = 0;
    for (int i = 0; i < nPlanes && n < max; i++) {
      const Plane& p = planes[i];
      if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) out[n++] = i;
    }
    for (int i = 1; i < n; i++) {           // insertion sort by distance to centre
      int v = out[i];
      float dv = (planes[v].x - cx) * (planes[v].x - cx) + (planes[v].y - cy) * (planes[v].y - cy);
      int j = i - 1;
      while (j >= 0) {
        const Plane& q = planes[out[j]];
        float dq = (q.x - cx) * (q.x - cx) + (q.y - cy) * (q.y - cy);
        if (dq <= dv) break;
        out[j + 1] = out[j];
        j--;
      }
      out[j + 1] = v;
    }
    return n;
  }
};
