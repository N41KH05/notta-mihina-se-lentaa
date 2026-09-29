// Browser simulator: runs the firmware's own drawing and touch code (compiled to
// WebAssembly) with simulated planes. The web page feeds in mouse/touch input.
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include "render.h"
#include "demo.h"
#include "app.h"
#include "ui.h"
#include "photo.h"
#include "sim_photo.h"
#include "places.h"
#include "traffic.h"
#include "canvas.h"
#include <stdio.h>

void* renderAlloc(size_t n) { return malloc(n); }

static AppState s;
static Canvas* frame;
static Gestures gest;
static uint32_t nowMs, lastPoll;
static bool live = false;                  // live data from the page (else demo traffic)

// ---- simulated Wi-Fi, so the setup screens can be tried -------------------------------
static const WifiNet FAKE[] = {
  {"Koti_2G", -48, true}, {"DNA-WIFI-4B21", -58, true}, {"Telia-8F3A-2.4G", -63, true},
  {"Elisa-Mobile-77", -69, true}, {"Kahvila Guest", -71, false}, {"Naapurin verkko", -80, true},
  {"Printer-Setup", -84, false},
};
static uint32_t scanAt, connAt;
static char simSsid[33] = "", simPass[64] = "";
static bool simConnected = false;
static void fScan() { scanAt = nowMs; }
static int fResults(WifiNet* out, int max) {
  if (nowMs - scanAt < 1500) return -1;
  int n = sizeof FAKE / sizeof FAKE[0];
  for (int i = 0; i < n && i < max; i++) out[i] = FAKE[i];
  return n;
}
static char tryPass[64];
static bool trySecure;
static void fConnect(const char* ssid, const char* pass) {
  connAt = nowMs;
  snprintf(tryPass, sizeof tryPass, "%s", pass);
  trySecure = pass[0] != 0;
  for (const auto& f : FAKE) if (!strcmp(f.ssid, ssid)) trySecure = f.secure;
}
// In the simulator any password of 8+ characters "works".
static int fStatus() {
  if (nowMs - connAt < 2500) return 0;
  return (!trySecure || strlen(tryPass) >= 8) ? 1 : -1;
}
static void fCurrent(char* ssid, size_t n, char* ip, size_t ipn, int* rssi) {
  snprintf(ssid, n, "%s", simConnected ? simSsid : "");
  snprintf(ip, ipn, "192.168.1.42");
  *rssi = -52;
}
static void fConnected(const char* ssid, const char* pass) {
  snprintf(simSsid, sizeof simSsid, "%s", ssid);
  snprintf(simPass, sizeof simPass, "%s", pass);
  simConnected = true;
}
static void fForget() { simConnected = false; simSsid[0] = 0; }
static void fDemo() { demoInit(s, nowMs); }
static void fSave() { if (s.demo) demoRelabel(s); }   // (the board also saves to flash)
// Home setup: the simulator has no internet, so it searches the map's own towns.
static char simQuery[64];
static uint32_t searchAt;
static bool homeAsked = false;
// With live data on, the page asks OpenStreetMap (sim_search_*); otherwise the map's towns.
static int searchN = -1;
static Place searchRes[6];
static bool searchWanted = false;
static void fPlaceSearch(const char* q) {
  snprintf(simQuery, sizeof simQuery, "%s", q);
  searchAt = nowMs;
  searchN = -1;
  searchWanted = live;
}
static int fPlaceResults(Place* out, int max) {
  if (live) {
    if (searchN < 0 && nowMs - searchAt < 8000) return -1;
    if (searchN > 0) { memcpy(out, searchRes, sizeof(Place) * (searchN < max ? searchN : max)); return searchN; }
    searchWanted = false;                              // no answer: fall back to the towns
  } else if (nowMs - searchAt < 900) {
    return -1;
  }
  return placeSearchOffline(simQuery, out, max);
}
static void fPlaceChosen(const Place& p) { appStartPickHome(s, p); }
static bool fNeedHome() { return !homeAsked; }
static void fHomeSkipped() { homeAsked = true; }
static const WifiHooks hooks = {fScan, fResults, fConnect, fStatus, fCurrent, fConnected, fForget, fDemo, fSave,
                                fPlaceSearch, fPlaceResults, fPlaceChosen, fNeedHome, fHomeSkipped};

// ---- Live data: the web page fetches the same services as the board and hands the
// replies to the firmware's own parsing code (traffic.cpp). -----------------------------
static Plane* incoming;
static uint16_t* livePix;                  // photo pixels written by the page
static char* buf;                          // shared text buffer the page writes into
static int bufCap = 0;
static float qCx, qCy;                     // centre of the last live query
static int qRadius;
static bool freshUpdate = false;
static int nextRoute = 0;
static void liveRequestRoute(const char* cs) {
  if (!live || !cs || !cs[0] || s.route(cs)) return;
  Route& r = s.routes[nextRoute];
  nextRoute = (nextRoute + 1) % ROUTE_CACHE;
  memset(&r, 0, sizeof r);
  snprintf(r.cs, sizeof r.cs, "%s", cs);
  r.state = ROUTE_PENDING;
}

static void onEvent(const Ev& e) {
  if (uiActive()) {                        // menus only use taps
    if (e.type == EV_TAP) uiTap(e.x, e.y, nowMs, s);
    return;
  }
  switch (e.type) {
    case EV_DRAG: appPan(s, e.dx, e.dy); break;
    case EV_ZOOM: appZoom(s, e.dx); break;
    case EV_TAP:
      switch (appTap(s, e.x, e.y, nowMs, liveRequestRoute)) {
        case HIT_SETTINGS: uiOpenSettings(); break;
        case HIT_PICK_SAVE: appEndPickHome(s, true); homeAsked = true; if (s.demo) demoInit(s, nowMs); break;
        case HIT_PICK_CANCEL: appEndPickHome(s, false); homeAsked = true; break;
        default: break;
      }
      break;
    default: break;
  }
}

extern "C" {

__attribute__((export_name("sim_init"))) void sim_init(uint32_t ms) {
  memset(&s, 0, sizeof s);
  s.planes = (Plane*)calloc(MAX_PLANES, sizeof(Plane));
  incoming = (Plane*)calloc(MAX_PLANES, sizeof(Plane));
  livePix = (uint16_t*)calloc(PHOTO_W * PHOTO_H, 2);
  appGoHome(s);
  s.apiOk = true;
  frame = new Canvas(SCREEN_W, SCREEN_H);
  uiInit(&hooks);
  nowMs = lastPoll = ms;
  demoInit(s, ms);
  for (int i = 0; i < 6; i++) demoStep(s, ms);       // a few breadcrumbs to start with
}

// n = number of fingers (0-2) with their positions in screen pixels.
__attribute__((export_name("sim_touch"))) void sim_touch(int n, int x0, int y0, int x1, int y1, uint32_t ms) {
  nowMs = ms;
  TouchPt p[2] = {{(int16_t)x0, (int16_t)y0}, {(int16_t)x1, (int16_t)y1}};
  gest.update(p, n, ms, onEvent);
}

__attribute__((export_name("sim_zoom"))) void sim_zoom(int d) { if (!uiActive()) appZoom(s, d); }

// Show the first-start Wi-Fi setup, as a new device would.
__attribute__((export_name("sim_first_run"))) void sim_first_run() {
  simConnected = false;
  uiOpenWifi("", true, false);
}

// Draw a frame; returns a pointer to 800x480 RGB565 pixels.
__attribute__((export_name("sim_frame"))) uint16_t* sim_frame(uint32_t ms, int hour, int minute, int second) {
  nowMs = ms;
  if (uiActive()) {
    uiTick(ms);
    if (uiActive()) { uiRender(*frame, s, ms); return frame->getBuffer(); }
  }
  if (!live && ms - lastPoll >= POLL_SECONDS * 1000) {   // demo: "fresh positions" every few seconds
    lastPoll = ms;
    demoStep(s, ms, hour * 60 + minute);
    s.updatedEpoch = 1;
  }
  Plane* sel = s.selected();
  if (s.follow && sel) { s.advance(ms); s.cx = sel->x; s.cy = sel->y; }
  // Photo card: the board fetches the real photo; here an example picture appears after a moment.
  static uint32_t photoAt = 0;
  if (sel && strcmp(photo.hex, sel->hex)) {
    snprintf(photo.hex, sizeof photo.hex, "%s", sel->hex);
    snprintf(photo.reg, sizeof photo.reg, "%s", sel->reg);
    photo.state = PHOTO_LOADING;
    photo.hidden = false;
    photoAt = ms;
  }
  if (!sel && photo.hex[0]) { photo.hex[0] = 0; photo.state = PHOTO_NONE; }
  if (!live && photo.state == PHOTO_LOADING && ms - photoAt > 1200) {
    photo.pix = (uint16_t*)SIM_PHOTO;
    snprintf(photo.photographer, sizeof photo.photographer, "%s", TR("esimerkki (simulaattori)", "example (simulator)"));
    snprintf(photo.link, sizeof photo.link, "https://www.planespotters.net/photo/1234567/oh-lwa-finnair-airbus-a350-941");
    photo.state = PHOTO_READY;
  }
  struct tm now = {};
  now.tm_hour = hour; now.tm_min = minute; now.tm_sec = second;
  static struct tm upd = {};
  if (ms == lastPoll || freshUpdate) { upd = now; freshUpdate = false; }
  if (!s.updatedEpoch) { s.updatedEpoch = 1; upd = now; }
  // Like the board: the map background is drawn only when something it shows changed.
  static Canvas* base = new Canvas(SCREEN_W, SCREEN_H);
  static struct Key { float cx, cy; int zoom; uint8_t lang; Units u; double hlat, hlon; char hname[24]; } last;
  Key k;
  memset(&k, 0, sizeof k);
  k.cx = s.cx; k.cy = s.cy; k.zoom = s.zoom; k.lang = language; k.u = units;
  k.hlat = cfg.homeLat; k.hlon = cfg.homeLon;
  snprintf(k.hname, sizeof k.hname, "%s", cfg.homeName);
  if (memcmp(&k, &last, sizeof k)) {
    renderBase(*base, s.cx, s.cy, s.zoom);
    memcpy(&last, &k, sizeof k);
  }
  memcpy(frame->getBuffer(), base->getBuffer(), SCREEN_W * SCREEN_H * 2);
  renderOverlay(*frame, s, ms, &now, &upd);
  return frame->getBuffer();
}

// ---- live data API for the page -----------------------------------------------------------
__attribute__((export_name("sim_buf"))) char* sim_buf(int need) {   // text buffer of at least `need` bytes
  if (need + 1 > bufCap) { free(buf); bufCap = need + 1024; buf = (char*)malloc(bufCap); }
  return buf;
}
__attribute__((export_name("sim_live"))) void sim_live(int on) {
  if (on == live) return;
  live = on;
  s.nPlanes = 0;
  s.selHex[0] = 0;
  s.follow = false;
  memset(s.routes, 0, sizeof s.routes);
  photo.hex[0] = 0;
  photo.state = PHOTO_NONE;
  if (live) {
    s.demo = false;
    s.apiOk = true;
    s.updatedEpoch = 0;
    s.source[0] = 0;
  } else {
    demoInit(s, nowMs);
    for (int i = 0; i < 6; i++) demoStep(s, nowMs);
  }
}
// Where to ask for planes: lat, lon, radius (nm). Remembers it for the reply.
__attribute__((export_name("sim_query_lat"))) double sim_query_lat() { qCx = s.cx; qCy = s.cy; qRadius = trafficRadiusNm(s); return latFromY(qCy); }
__attribute__((export_name("sim_query_lon"))) double sim_query_lon() { return lonFromX(qCx); }
__attribute__((export_name("sim_query_radius"))) int sim_query_radius() { return qRadius; }
// A reply (JSON text in the buffer, `len` bytes) from the named service.
__attribute__((export_name("sim_live_planes"))) int sim_live_planes(int len, int nameOffset) {
  if (!live) return -1;
  JsonDocument filter, doc;
  trafficFilter(filter);
  if (deserializeJson(doc, buf, len, DeserializationOption::Filter(filter))) return -1;
  int n = trafficParse(doc, incoming, nowMs);
  trafficMerge(s, incoming, n);
  s.apiOk = true;
  s.apiError[0] = 0;
  snprintf(s.source, sizeof s.source, "%s", buf + nameOffset);
  s.fetchCx = qCx;
  s.fetchCy = qCy;
  s.fetchRadiusNm = qRadius;
  s.updatedEpoch = 1;
  freshUpdate = true;
  return n;
}
__attribute__((export_name("sim_live_error"))) void sim_live_error() {   // message in the buffer
  s.apiOk = false;
  snprintf(s.apiError, sizeof s.apiError, "%s", buf);
}
// Routes: the callsign waiting for a lookup (or 0), then its reply.
__attribute__((export_name("sim_route_pending"))) const char* sim_route_pending() {
  for (auto& r : s.routes) if (r.state == ROUTE_PENDING) return r.cs;
  return nullptr;
}
__attribute__((export_name("sim_route_result"))) void sim_route_result(const char* cs, int len) {  // len < 0: failed
  Route* r = s.route(cs);
  if (!r || r->state != ROUTE_PENDING) return;
  JsonDocument filter, doc;
  routeFilter(filter);
  bool ok = len > 0 && !deserializeJson(doc, buf, len, DeserializationOption::Filter(filter)) && routeParse(doc, *r);
  r->state = ok ? ROUTE_KNOWN : ROUTE_UNKNOWN;
}
// Photos: the selected plane that needs one (hex, 0 if none), the pixel buffer
// (PHOTO_W x PHOTO_H RGB565) and the result.
__attribute__((export_name("sim_photo_wanted"))) const char* sim_photo_wanted() {
  return live && photo.state == PHOTO_LOADING && photo.hex[0] ? photo.hex : nullptr;
}
__attribute__((export_name("sim_photo_reg"))) const char* sim_photo_reg() { return photo.reg; }
__attribute__((export_name("sim_photo_pixels"))) uint16_t* sim_photo_pixels() { return livePix; }
__attribute__((export_name("sim_photo_done"))) void sim_photo_done(int state, int whoOffset, int linkOffset) {
  // buffer: hex \0 photographer \0 link
  if (strcmp(buf, photo.hex)) return;                  // another plane was selected meanwhile
  if (state == PHOTO_READY) {
    photo.pix = livePix;
    snprintf(photo.photographer, sizeof photo.photographer, "%s", buf + whoOffset);
    snprintf(photo.link, sizeof photo.link, "%s", buf + linkOffset);
  }
  photo.state = (PhotoState)state;
}
// Address search for home.
__attribute__((export_name("sim_search_wanted"))) const char* sim_search_wanted() {
  if (!searchWanted) return nullptr;
  searchWanted = false;
  return simQuery;
}
__attribute__((export_name("sim_search_result"))) void sim_search_result(int len) {   // len < 0: failed
  if (len <= 0) { searchN = 0; return; }
  JsonDocument filter, doc;
  nominatimFilter(filter);
  if (deserializeJson(doc, buf, len, DeserializationOption::Filter(filter))) { searchN = 0; return; }
  searchN = nominatimParse(doc, searchRes, 6);
}

}  // extern "C"
