// Browser simulator: runs the firmware's own drawing and touch code (compiled to
// WebAssembly) with simulated planes. The web page feeds in mouse/touch input.
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include "render.h"
#include "ui.h"
#include "demo.h"
#include "sim_photo.h"
#include <stdio.h>

void* renderAlloc(size_t n) { return malloc(n); }

static AppState s;
static Canvas* frame;
static Gestures gest;
static uint32_t nowMs, lastPoll;
static double epochNow = 0;          // real time from the page, for the automatic dark mode

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
static void fPlaceSearch(const char* q) {
  snprintf(simQuery, sizeof simQuery, "%s", q);
  searchAt = nowMs;
}
static int fPlaceResults(Place* out, int max) {
  if (nowMs - searchAt < 900) return -1;               // "searching…" for a moment
  return placeSearchOffline(simQuery, out, max);
}
static void fPlaceChosen(const Place& p) { appStartPickHome(s, p); }
static bool fNeedHome() { return !homeAsked; }
static void fHomeSkipped() { homeAsked = true; }
// Updates: the simulator is always the newest version.
static void fWakeNet() {
  if (fwUpdate.check || fwUpdate.install) { fwUpdate.check = false; fwUpdate.install = false; fwUpdate.state = UPD_CURRENT; }
}
static const WifiHooks hooks = {fScan, fResults, fConnect, fStatus, fCurrent, fConnected, fForget, fDemo, fSave,
                                fPlaceSearch, fPlaceResults, fPlaceChosen, fNeedHome, fHomeSkipped, fWakeNet};

static void onEvent(const Ev& e) {
  if (uiActive()) {                        // menus only use taps
    if (e.type == EV_TAP) uiTap(e.x, e.y, nowMs, s);
    return;
  }
  switch (e.type) {
    case EV_DRAG: appPan(s, e.dx, e.dy); break;
    case EV_SCROLL: if (!s.selHex[0]) appScroll(s, e.dy); break;
    case EV_ZOOM: appZoom(s, e.dx); break;
    case EV_TAP:
      switch (appTap(s, e.x, e.y, nowMs, nullptr)) {
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
  appGoHome(s);
  s.apiOk = true;
  frame = new Canvas(SCREEN_W, SCREEN_H);
  uiInit(&hooks);
  cfg.darkMode = DARK_ON;                  // the demo starts dark (the board's default is light)
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

// The page's clock (Unix seconds), so "dark after sunset" works like on the board.
__attribute__((export_name("sim_clock"))) void sim_clock(double epoch) { epochNow = epoch; }

// Draw a frame; returns a pointer to 800x480 RGB565 pixels.
__attribute__((export_name("sim_frame"))) uint16_t* sim_frame(uint32_t ms, int hour, int minute, int second) {
  nowMs = ms;
  useDarkTheme(darkWanted(epochNow));
  if (uiActive()) {
    uiTick(ms);
    if (uiActive()) { uiRender(*frame, s, ms); return frame->getBuffer(); }
  }
  if (ms - lastPoll >= POLL_SECONDS * 1000) {   // demo: "fresh positions" every few seconds
    lastPoll = ms;
    demoStep(s, ms, hour * 60 + minute);
    pathFollow(s);
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
  if (sel && strcmp(flightPath.hex, sel->hex)) {        // a newly selected plane: its path
    snprintf(flightPath.hex, sizeof flightPath.hex, "%s", sel->hex);
    demoPath(s, *sel, flightPath);
  }
  if (!sel && flightPath.hex[0]) { flightPath.hex[0] = 0; flightPath.state = PATH_NONE; }
  if (photo.state == PHOTO_LOADING && ms - photoAt > 1200) {
    photo.pix = (uint16_t*)SIM_PHOTO;
    snprintf(photo.photographer, sizeof photo.photographer, "%s", TR("esimerkki (simulaattori)", "example (simulator)"));
    snprintf(photo.link, sizeof photo.link, "https://www.planespotters.net/photo/1234567/oh-lwa-finnair-airbus-a350-941");
    photo.state = PHOTO_READY;
  }
  struct tm now = {};
  now.tm_hour = hour; now.tm_min = minute; now.tm_sec = second;
  static struct tm upd = {};
  if (ms == lastPoll) upd = now;
  if (!s.updatedEpoch) { s.updatedEpoch = 1; upd = now; }
  // Like the board: the map background is drawn only when something it shows changed.
  static Canvas* base = new Canvas(SCREEN_W, SCREEN_H);
  static struct Key { float cx, cy; int zoom; uint8_t lang; Units u; double hlat, hlon; char hname[24]; const Theme* th; } last;
  Key k;
  memset(&k, 0, sizeof k);
  k.cx = s.cx; k.cy = s.cy; k.zoom = s.zoom; k.lang = language; k.u = units;
  k.hlat = cfg.homeLat; k.hlon = cfg.homeLon; k.th = theme;
  snprintf(k.hname, sizeof k.hname, "%s", cfg.homeName);
  if (memcmp(&k, &last, sizeof k)) {
    renderBase(*base, s.cx, s.cy, s.zoom);
    memcpy(&last, &k, sizeof k);
  }
  memcpy(frame->getBuffer(), base->getBuffer(), SCREEN_W * SCREEN_H * 2);
  renderOverlay(*frame, s, ms, &now, &upd);
  return frame->getBuffer();
}

}  // extern "C"
