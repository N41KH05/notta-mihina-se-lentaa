// Shared data: the planes, the view, and what the screen should show.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "config.h"

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
  uint8_t darkMode = DEFAULT_DARK_MODE;      // DARK_OFF, DARK_ON or DARK_AUTO
  bool photos = SHOW_PHOTOS;
  char contact[64] = PHOTO_CONTACT;          // for Planespotters (see config.h)
  char airlabsKey[80] = AIRLABS_KEY;         // "" = no timetable times
  uint16_t logKm = LOG_RADIUS_KM;            // logbook circle around home
};
inline Config cfg;

// Firmware updates (net.cpp downloads, SkyTracker.ino checks and installs in the fetch
// task, ui.cpp asks the user).
enum UpdateState : uint8_t { UPD_NONE, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_INSTALLING, UPD_FAILED };
struct FirmwareUpdate {
  volatile uint8_t state = UPD_NONE;
  volatile bool check = false;       // "check for updates" was pressed
  volatile bool install = false;     // "install now" was pressed
  volatile bool prompt = false;      // a new version was found: show the update screen
  int latest = 0;                    // newest build on GitHub (0 = not known yet)
  char notes[80] = "";               // what changed in it (first line of the release notes)
  volatile int percent = 0;          // while installing
  int skipBuild = 0;                 // "skip this version" (saved)
  uint32_t remindAt = 0;             // "later": not before this millis()
  char error[64] = "";
};
inline FirmwareUpdate fwUpdate;
// The micro SD card (in the slot on the back of the board). Checked at start-up and
// written to once a minute, so a bad card or a clash with the screen shows up early.
enum SdState : uint8_t { SD_NONE = 0, SD_OK, SD_FAILED };
struct SdStatus {
  SdState state;
  uint32_t totalMB, freeMB;
  uint32_t writes;           // successful writes since start-up
  char error[48];
};
inline SdStatus sdStatus;

// Screen test (tap the STATUS heading in Settings): four 12-second steps, each taking away
// one more possible cause of lines on the screen (see screenTest() in SkyTracker.ino).
inline volatile uint32_t screenTestUntil = 0;

enum DarkMode : uint8_t { DARK_OFF = 0, DARK_ON = 1, DARK_AUTO = 2 };

// The sun's height above the horizon in degrees at a place and time (Unix seconds).
// A short almanac formula, good to a fraction of a degree.
inline double sunElevation(double epoch, double lat, double lon) {
  const double R = M_PI / 180;
  double n = (epoch - 946728000.0) / 86400.0;            // days since 2000-01-01 12:00 UTC
  double L = 280.460 + 0.9856474 * n, g = (357.528 + 0.9856003 * n) * R;
  double lam = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * R, eps = (23.439 - 0.0000004 * n) * R;
  double dec = asin(sin(eps) * sin(lam));
  double ra = atan2(cos(eps) * sin(lam), cos(lam));
  double gmst = fmod(280.46061837 + 360.98564736629 * n, 360.0) * R;
  double ha = gmst + lon * R - ra;
  return asin(sin(lat * R) * sin(dec) + cos(lat * R) * cos(dec) * cos(ha)) / R;
}
// Should the screen use the dark colours now? epoch 0 = the clock isn't set yet.
inline bool darkWanted(double epoch) {
  if (cfg.darkMode == DARK_ON) return true;
  if (cfg.darkMode != DARK_AUTO || epoch < 1.6e9) return false;
  return sunElevation(epoch, cfg.homeLat, cfg.homeLon) < -0.833;   // the sun has set
}

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
  char category[3];      // ADS-B emitter category: A1 light ... A5 heavy, A7 helicopter, B1 glider
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
  char flight[10], airline[40], airlineCode[4], from[5], fromCity[24], to[5], toCity[24];
  // The aircraft flying it, from the same adsbdb request: its registered owner (often
  // the airline itself; a leasing company, or a person for private planes).
  char hex[8];               // the plane the lookup was made for
  char owner[40], ownerCode[4];
  bool hasDest;              // destination airport position known (for the arrival estimate)
  float toLat, toLon;
  bool hasOrigin;            // departure airport position known (flight path fallback)
  float fromLat, fromLon;
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
  int listScroll;          // how far the side panel's plane list is scrolled, in pixels
  // status
  uint32_t updatedEpoch;
  bool apiOk;
  char apiError[120];
  char source[16];
  // What the traffic requests cover. A wide view is covered by several circles (net.cpp);
  // coverCapped: there were too many, and only the area cov* (mercator metres) is shown.
  bool coverCapped;
  float covX0, covY0, covX1, covY1;
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
      if (age > 120) age = 120;           // (a wide view refreshes each area every minute or so)
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
    // Sort by distance to the centre (ties by index, so the order is stable).
    struct Near { float d; int i; };
    static Near near[MAX_PLANES];
    for (int k = 0; k < n; k++) {
      const Plane& p = planes[out[k]];
      near[k] = {(p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy), out[k]};
    }
    std::sort(near, near + n, [](const Near& a, const Near& b) { return a.d < b.d || (a.d == b.d && a.i < b.i); });
    for (int k = 0; k < n; k++) out[k] = near[k].i;
    return n;
  }
};

// ============================================================================
//  Screen language
// ============================================================================
enum Lang : uint8_t { LANG_FI = 0, LANG_EN = 1 };
inline uint8_t language = DEFAULT_LANGUAGE;      // chosen in Settings, saved in flash

// How often new positions are fetched: less often when the view is full of planes.
inline int pollSeconds(int nPlanes) { return nPlanes >= BUSY_PLANES ? POLL_SECONDS_BUSY : POLL_SECONDS; }

#define TR(fi, en) (language == LANG_EN ? (en) : (fi))

inline char decimalSep() { return language == LANG_EN ? '.' : ','; }
inline char thousandsSep() { return language == LANG_EN ? ',' : ' '; }
// Replace '.' with the local decimal separator (in place).
// The home marker's label. The default (empty, or the default word in either language)
// follows the language; a name of the user's own is shown as it is.
inline bool isDefaultHomeName(const char* n) { return !n[0] || !strcmp(n, "Koti") || !strcmp(n, "Home"); }
inline const char* homeLabel(const char* n) { return isDefaultHomeName(n) ? TR("Koti", "Home") : n; }
inline void localDecimal(char* s) {
  for (char sep = decimalSep(); *s; s++) if (*s == '.') *s = sep;
}

// ============================================================================
//  Photo card
// ============================================================================
// The photo card shown over the map for the selected plane (Planespotters.net).
enum PhotoState : uint8_t { PHOTO_NONE, PHOTO_LOADING, PHOTO_READY, PHOTO_MISSING };
static const int PHOTO_W = 272, PHOTO_H = 122;         // picture size inside the card

struct PhotoCard {
  char hex[8];             // which plane this is for
  char reg[12];
  PhotoState state;
  bool hidden;             // tapped away by the user
  uint16_t* pix;           // PHOTO_W x PHOTO_H pixels, RGB565 (kept in memory only)
  char photographer[48];
  char link[160];          // the photo's page (Planespotters asks for a link: shown as a QR code)
};
inline PhotoCard photo;

// ============================================================================
//  Flight path of the selected plane
// ============================================================================
// Where the selected plane has flown since it took off. Live: its track history from
// adsb.lol (net.cpp), then extended with each new position report. Demo: made up.
enum PathState : uint8_t { PATH_NONE, PATH_LOADING, PATH_READY, PATH_MISSING };
static const int PATH_POINTS = 400;
struct PathPoint { float x, y; int16_t alt100; };      // mercator metres; altitude / 100 ft
struct FlightPath {
  char hex[8];               // which plane this is for
  PathState state;
  uint32_t triedMs;          // when it was last looked up (retried if nothing was found)
  uint8_t tries;
  int n;
  PathPoint pts[PATH_POINTS];

  void clear() { n = 0; }
  // Add a point (skipped if closer than ~300 m to the last one). When full, every
  // second point is dropped, so the whole flight always fits.
  void add(float x, float y, int altFt) {
    if (n && fabsf(x - pts[n - 1].x) + fabsf(y - pts[n - 1].y) < 300) return;
    if (n == PATH_POINTS) {
      int k = 0;
      for (int i = 0; i < n; i += 2) pts[k++] = pts[i];
      n = k;
    }
    pts[n++] = {x, y, (int16_t)(altFt / 100)};
  }
};
inline FlightPath flightPath;
// One point of a track history while it is being read (net.cpp).
struct TracePt { double t; float lat, lon; int32_t alt; uint8_t flags; bool ground; };
static const int MAX_TRACE = 20000;

// After new positions arrive: extend the selected plane's path to where it was reported.
// Call with the state locked.
inline void pathFollow(AppState& s) {
  if (flightPath.state != PATH_READY) return;
  if (Plane* p = s.find(flightPath.hex)) flightPath.add(p->fx, p->fy, p->hasAlt ? p->alt : 0);
}
