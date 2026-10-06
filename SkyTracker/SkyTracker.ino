// ============================================================================
//  SkyTracker: a live flight map for the Waveshare ESP32-S3-Touch-LCD-7.
//
//  Drag to move the map, pinch (or the +/- buttons) to zoom, tap a plane to see
//  where it's flying. Settings: config.h.   Guide: README.md.
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include "config.h"
#include "model.h"
#include "board.h"
#include "render.h"
#include "net.h"
#include "demo.h"
#include "ui.h"

AppState state;
SemaphoreHandle_t lock;
QueueHandle_t events;
TaskHandle_t fetchTask;
Canvas canvas(SCREEN_W, SCREEN_H, false), baseCanvas(SCREEN_W, SCREEN_H, false);   // draw into our buffers
uint16_t* baseBuf;                 // cached map background

void* renderAlloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }

// ---------------------------------------------------------------------------
//  Touch gestures (own task, ~60 polls a second)
// ---------------------------------------------------------------------------
void emitEvent(const Ev& e) { xQueueSend(events, &e, 0); }

void touchTask(void*) {
  TouchPt p[5];
  Gestures g;
  for (;;) {
    int n = boardTouch(p, 5);
    g.update(p, n, millis(), emitEvent);
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

// ---------------------------------------------------------------------------
//  Background fetching (other CPU core)
// ---------------------------------------------------------------------------
void updateTick();
void fetchLoop(void*) {
  esp_task_wdt_add(nullptr);           // restart the board if this task ever hangs
  for (;;) {
    esp_task_wdt_reset();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(pollSeconds(state.nPlanes) * 1000));
    esp_task_wdt_reset();
    updateTick();                      // new firmware? (also at night, with the screen off)
    if (state.night) continue;
    if (state.demo) {
      xSemaphoreTake(lock, portMAX_DELAY);
      time_t t = time(nullptr);
      struct tm lt;
      localtime_r(&t, &lt);
      demoStep(state, millis(), lt.tm_hour * 60 + lt.tm_min);
      pathFollow(state);
      state.updatedEpoch = time(nullptr);
      xSemaphoreGive(lock);
    } else if (WiFi.status() == WL_CONNECTED) {
      netLookupRoute(state, lock);        // quick things for the selected plane first
      netLookupTimes(state, lock);
      if (cfg.photos) netFetchPhoto(lock);
      netFetchPath(lock);
      netFetchPlanes(state, lock);
    }
  }
}

// Route and aircraft details for a plane that just got selected (adsbdb, see net.cpp).
void requestRouteLocked(const Plane& p) {
  static int next = 0;
  if (!p.cs[0] || state.demo || state.route(p.cs)) return;
  Route& r = state.routes[next];
  next = (next + 1) % ROUTE_CACHE;
  memset(&r, 0, sizeof r);
  snprintf(r.cs, sizeof r.cs, "%s", p.cs);
  snprintf(r.hex, sizeof r.hex, "%s", p.hex);
  r.state = ROUTE_PENDING;
  xTaskNotifyGive(fetchTask);
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------
float baseCx = NAN, baseCy = NAN;
int baseZoom = -1;
bool baseStale = true;

// While dragging, the cached map is drawn shifted instead of redrawn (much faster);
// it is redrawn properly when the finger lifts. The shift is applied while copying the
// map into the frame, so dragging costs no more memory traffic than a normal frame.
int baseOffX = 0, baseOffY = 0;
void shiftBase(int dx, int dy) {
  baseOffX += dx;
  baseOffY += dy;
  if (abs(baseOffX) >= MAP_W || abs(baseOffY) >= SCREEN_H) baseStale = true;
}
static void fill16(uint16_t* p, int n, uint16_t c) { while (n--) *p++ = c; }
// Copy the cached map into a frame, moved by the drag offset. Only the map part: the
// side panel is drawn over completely every frame, so copying it would be wasted
// PSRAM traffic (which the LCD needs for itself).
static void composeBase(uint16_t* dst) {
  const int ox = baseOffX, oy = baseOffY;
  const uint16_t sea = baseBuf[0];   // corner pixel is (nearly always) sea colour
  int x0 = ox > 0 ? ox : 0, x1 = ox < 0 ? MAP_W + ox : MAP_W;   // columns the old map covers
  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t* d = dst + y * SCREEN_W;
    int sy = y - oy;
    if (sy < 0 || sy >= SCREEN_H) { fill16(d, MAP_W, sea); continue; }
    fill16(d, x0, sea);
    memcpy(d + x0, baseBuf + sy * SCREEN_W + x0 - ox, (x1 - x0) * 2);
    fill16(d + x1, MAP_W - x1, sea);
  }
}

// Light or dark colours, by the setting and, in automatic mode, by the sun at home.
static void applyTheme() {
  time_t t = time(nullptr);
  if (useDarkTheme(darkWanted(t > 1600000000 ? (double)t : 0))) baseStale = true;
}

void drawFrame() {
  struct tm now, upd;
  bool haveTime = getLocalTime(&now, 0);
  xSemaphoreTake(lock, portMAX_DELAY);
  applyTheme();
  if (baseStale || state.zoom != baseZoom || fabsf(state.cx - baseCx) > 0.5f * metresPerPx(state.zoom) ||
      fabsf(state.cy - baseCy) > 0.5f * metresPerPx(state.zoom)) {
    renderBase(baseCanvas, state.cx, state.cy, state.zoom);
    baseCx = state.cx; baseCy = state.cy; baseZoom = state.zoom; baseStale = false;
    baseOffX = baseOffY = 0;
  }
  uint16_t* fbuf = boardBackBuffer();
  composeBase(fbuf);
  canvas.use(fbuf);
  time_t u = state.updatedEpoch;
  localtime_r(&u, &upd);
  renderOverlay(canvas, state, millis(), haveTime ? &now : nullptr, u > 100000 ? &upd : nullptr);
  xSemaphoreGive(lock);
  boardPresent();
}

void message(const char* big, const char* small) {
  canvas.use(boardBackBuffer());
  renderMessage(canvas, big, small);
  boardPresent();
}

// ---------------------------------------------------------------------------
//  Saved settings. Everything chosen on the screen (Wi-Fi network, units, demo
//  mode) is stored in the board's flash, so it survives restarts and power cuts.
//  WIFI_SSID in config.h is only used if no network has been saved yet.
// ---------------------------------------------------------------------------
Preferences prefs;
char savedSsid[33] = "", savedPass[64] = "";
bool savedDemo = false;              // the user chose demo planes
bool homeAsked = false;              // home has been set (or the question skipped)

void loadSettings() {
  prefs.begin("skytracker", true);
  prefs.getString("ssid", savedSsid, sizeof savedSsid);
  prefs.getString("pass", savedPass, sizeof savedPass);
  units.distKm = prefs.getBool("distKm", DEFAULT_DISTANCE_KM);
  units.speedKmh = prefs.getBool("speedKmh", DEFAULT_SPEED_KMH);
  units.altM = prefs.getBool("altM", DEFAULT_ALTITUDE_M);
  savedDemo = prefs.getBool("demo", false);
  language = prefs.getUChar("lang", DEFAULT_LANGUAGE) == LANG_EN ? LANG_EN : LANG_FI;
  // Changed on the phone settings page (defaults from config.h)
  cfg.homeLat = prefs.getDouble("homeLat", cfg.homeLat);
  cfg.homeLon = prefs.getDouble("homeLon", cfg.homeLon);
  if (prefs.isKey("homeName")) prefs.getString("homeName", cfg.homeName, sizeof cfg.homeName);
  cfg.nightStart = prefs.getChar("nightStart", cfg.nightStart);
  cfg.nightEnd = prefs.getChar("nightEnd", cfg.nightEnd);
  cfg.darkMode = prefs.getUChar("dark", cfg.darkMode) % 3;
  useDarkTheme(darkWanted(0));         // boot messages too, if dark is always on
  cfg.photos = prefs.getBool("photos", cfg.photos);
  fwUpdate.skipBuild = prefs.getInt("skipBuild", 0);
  if (prefs.isKey("contact")) prefs.getString("contact", cfg.contact, sizeof cfg.contact);
  if (prefs.isKey("airlabs")) prefs.getString("airlabs", cfg.airlabsKey, sizeof cfg.airlabsKey);
  homeAsked = prefs.getBool("homeAsked", false) || prefs.isKey("homeLat");
  prefs.end();
  Serial.printf("Settings: wifi \"%s\", units %s/%s/%s%s\n", savedSsid, units.distKm ? "km" : "nm",
                units.speedKmh ? "km/h" : "kt", units.altM ? "m" : "ft", savedDemo ? ", demo" : "");
  if (!savedSsid[0] && strcmp(WIFI_SSID, "YourWiFiName")) {
    snprintf(savedSsid, sizeof savedSsid, "%s", WIFI_SSID);
    snprintf(savedPass, sizeof savedPass, "%s", WIFI_PASSWORD);
  }
}

void leaveDemo() {                         // switch from simulated planes to live data
  xSemaphoreTake(lock, portMAX_DELAY);
  if (state.demo) {
    state.demo = false;
    state.nPlanes = 0;
    state.selHex[0] = 0;
    state.follow = false;
    memset(state.routes, 0, sizeof state.routes);
    state.updatedEpoch = 0;
    state.apiOk = true;
  }
  xSemaphoreGive(lock);
  if (fetchTask) xTaskNotifyGive(fetchTask);
}

// Hooks for the on-screen Wi-Fi setup (ui.cpp).
void hScan() { WiFi.scanDelete(); WiFi.scanNetworks(true); }
int hResults(WifiNet* out, int max) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n < 0) { WiFi.scanNetworks(true); return -1; }    // failed: try again
  int count = 0;
  for (int i = 0; i < n; i++) {
    String name = WiFi.SSID(i);
    if (!name.length()) continue;                       // hidden network
    int rssi = WiFi.RSSI(i), dup = -1;
    for (int k = 0; k < count; k++) if (name == out[k].ssid) dup = k;
    if (dup >= 0) { if (rssi > out[dup].rssi) out[dup].rssi = rssi; continue; }
    if (count >= max) continue;
    snprintf(out[count].ssid, sizeof out[count].ssid, "%s", name.c_str());
    out[count].rssi = rssi;
    out[count].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    count++;
  }
  for (int i = 1; i < count; i++)                        // strongest first
    for (int j = i; j > 0 && out[j].rssi > out[j - 1].rssi; j--) { WifiNet t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
  WiFi.scanDelete();
  return count;
}
void hConnect(const char* ssid, const char* pass) {
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, pass[0] ? pass : nullptr);
}
int hStatus() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) return 1;
  if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) return -1;
  return 0;
}
void hCurrent(char* ssid, size_t n, char* ip, size_t ipn, int* rssi) {
  bool on = WiFi.status() == WL_CONNECTED;
  snprintf(ssid, n, "%s", on ? WiFi.SSID().c_str() : "");
  snprintf(ip, ipn, "%s", on ? WiFi.localIP().toString().c_str() : "");
  *rssi = on ? WiFi.RSSI() : -100;
}
void hConnected(const char* ssid, const char* pass) {
  snprintf(savedSsid, sizeof savedSsid, "%s", ssid);
  snprintf(savedPass, sizeof savedPass, "%s", pass);
  prefs.begin("skytracker", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putBool("demo", false);        // a real network means live planes
  prefs.end();
  savedDemo = false;
  Serial.printf("Saved Wi-Fi \"%s\"\n", ssid);
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
  leaveDemo();
}
void hForget() {
  prefs.begin("skytracker", false);    // only the network; units stay
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  savedSsid[0] = savedPass[0] = 0;
  WiFi.disconnect();
}
void hDemo() {
  xSemaphoreTake(lock, portMAX_DELAY);
  demoInit(state, millis());
  xSemaphoreGive(lock);
  prefs.begin("skytracker", false);
  prefs.putBool("demo", true);
  prefs.end();
  savedDemo = true;
}
// Things that hold text in the old language once the language is switched.
static uint8_t shownLanguage = 0xFF;
static void languageChanged() {
  if (language == shownLanguage) return;
  shownLanguage = language;
  if (state.demo) demoRelabel(state);
}
void hSaveSettings() {
  prefs.begin("skytracker", false);
  prefs.putBool("distKm", units.distKm);
  prefs.putBool("speedKmh", units.speedKmh);
  prefs.putBool("altM", units.altM);
  prefs.putUChar("lang", language);
  prefs.putUChar("dark", cfg.darkMode);
  prefs.putInt("skipBuild", fwUpdate.skipBuild);
  prefs.end();
  languageChanged();
  baseStale = true;                    // range rings and scale bar change unit
}
// The phone settings page (web.cpp) changed something. Called with the state locked.
void webSaved(bool homeMoved, bool keyChanged) {
  prefs.begin("skytracker", false);
  prefs.putBool("distKm", units.distKm);
  prefs.putBool("speedKmh", units.speedKmh);
  prefs.putBool("altM", units.altM);
  prefs.putDouble("homeLat", cfg.homeLat);
  prefs.putDouble("homeLon", cfg.homeLon);
  prefs.putString("homeName", cfg.homeName);
  prefs.putChar("nightStart", cfg.nightStart);
  prefs.putChar("nightEnd", cfg.nightEnd);
  prefs.putUChar("dark", cfg.darkMode);
  prefs.putBool("photos", cfg.photos);
  prefs.putString("contact", cfg.contact);
  prefs.putString("airlabs", cfg.airlabsKey);
  prefs.putUChar("lang", language);
  prefs.end();
  languageChanged();
  baseStale = true;                    // units, home marker or its name may have changed
  if (homeMoved) {
    if (state.demo) demoInit(state, millis());           // simulated planes around the new home
    appGoHome(state);
    if (fetchTask) xTaskNotifyGive(fetchTask);
  }
  if (keyChanged)                      // ask again with the new key
    for (auto& r : state.routes) { r.times = TIMES_NONE; r.hasTimes = false; r.timesMs = 0; }
  if (!cfg.photos) { photo.hex[0] = 0; photo.state = PHOTO_NONE; }
  Serial.println("Settings changed on the phone page");
}

// ---- Setting home: address search (in its own short task), then the map ------------------
struct {
  char query[64];
  Place res[6];
  volatile int n = -1;                 // -1 searching, -2 failed, else how many
  volatile bool busy = false;
} search;

void searchTask(void*) {
  Place tmp[6];
  int n = WiFi.status() == WL_CONNECTED ? netSearchPlaces(search.query, tmp, 6) : -1;
  if (n <= 0) {                        // nothing online (or no connection): the map's own towns
    int m = placeSearchOffline(search.query, tmp, 6);
    if (m > 0 || n == 0) n = m;
  }
  memcpy(search.res, tmp, sizeof tmp);
  search.n = n < 0 ? -2 : n;
  search.busy = false;
  vTaskDelete(nullptr);
}
void hPlaceSearch(const char* q) {
  if (search.busy) return;
  snprintf(search.query, sizeof search.query, "%s", q);
  search.n = -1;
  search.busy = true;
  xTaskCreatePinnedToCore(searchTask, "search", 12288, nullptr, 1, nullptr, 0);
}
int hPlaceResults(Place* out, int max) {
  int n = search.n;
  if (n > 0) memcpy(out, search.res, sizeof(Place) * (n < max ? n : max));
  return n;
}
void hPlaceChosen(const Place& p) {
  xSemaphoreTake(lock, portMAX_DELAY);
  appStartPickHome(state, p);
  xSemaphoreGive(lock);
  baseStale = true;
}
bool hNeedHome() { return !homeAsked; }
void rememberHomeAsked() {
  homeAsked = true;
  prefs.begin("skytracker", false);
  prefs.putBool("homeAsked", true);
  prefs.end();
}
void hHomeSkipped() { rememberHomeAsked(); }
// TALLENNA / PERUUTA on the map. Called with the state locked.
void finishPickHome(bool save) {
  appEndPickHome(state, save);
  if (save) webSaved(true, false);     // stores home in flash, redraws, fetches the new area
  else baseStale = true;
  rememberHomeAsked();
  xTaskNotifyGive(fetchTask);
}

// ---- Finding a flight: the planes already on the map first, then the whole world ----------
struct {
  char query[16];
  Plane res[6];
  volatile int n = -1;                 // -1 searching, -2 failed, else how many
  volatile bool busy = false;
} flightSearch;

void flightSearchTask(void*) {
  static Plane local[6], online[6];
  bool exact;
  xSemaphoreTake(lock, portMAX_DELAY);
  int nLocal = appFindFlights(state, flightSearch.query, local, 6, &exact);
  bool canAsk = !state.demo && WiFi.status() == WL_CONNECTED;
  xSemaphoreGive(lock);
  int n = nLocal;
  const Plane* res = local;
  if (!exact && canAsk) {
    int m = netFindFlights(flightSearch.query, online, 6);
    if (m > 0) { n = m; res = online; }
    else if (m < 0 && nLocal == 0) n = -2;
  }
  if (n > 0) memcpy(flightSearch.res, res, sizeof(Plane) * n);
  flightSearch.n = n;
  flightSearch.busy = false;
  vTaskDelete(nullptr);
}
void hFlightSearch(const char* q) {
  if (flightSearch.busy) return;
  snprintf(flightSearch.query, sizeof flightSearch.query, "%s", q);
  flightSearch.n = -1;
  flightSearch.busy = true;
  xTaskCreatePinnedToCore(flightSearchTask, "flights", 12288, nullptr, 1, nullptr, 0);
}
int hFlightResults(Plane* out, int max) {
  int n = flightSearch.n;
  if (n > 0) memcpy(out, flightSearch.res, sizeof(Plane) * (n < max ? n : max));
  return n;
}
void hFlightChosen(const Plane& p) {
  xSemaphoreTake(lock, portMAX_DELAY);
  appShowFlight(state, p, requestRouteLocked);
  xSemaphoreGive(lock);
  baseStale = true;
  xTaskNotifyGive(fetchTask);          // positions around it right away
}

void hWakeNet() { xTaskNotifyGive(fetchTask); }
const WifiHooks wifiHooks = {hScan, hResults, hConnect, hStatus, hCurrent, hConnected, hForget, hDemo,
                             hSaveSettings, hPlaceSearch, hPlaceResults, hPlaceChosen, hNeedHome, hHomeSkipped,
                             hWakeNet, hFlightSearch, hFlightResults, hFlightChosen};

bool connectSaved() {
  if (!savedSsid[0]) return false;
  WiFi.begin(savedSsid, savedPass[0] ? savedPass : nullptr);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

uint32_t lastInput = 0;
bool backlightOn = true;

bool isNightHour() {
  struct tm t;
  if (cfg.nightStart == cfg.nightEnd || !getLocalTime(&t, 0)) return false;
  int h = t.tm_hour, a = cfg.nightStart, b = cfg.nightEnd;
  return a > b ? (h >= a || h < b) : (h >= a && h < b);
}

// ---------------------------------------------------------------------------
//  Staying up for months: watchdog, Wi-Fi recovery, memory check, daily restart
// ---------------------------------------------------------------------------
// Why the board last started (shown on the phone settings page, in the current language).
const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return TR("virta kytketty", "power on");
    case ESP_RST_SW:       return TR("ohjelma käynnisti uudelleen", "software restart");
    case ESP_RST_PANIC:    return TR("ohjelmavirhe (käynnistyi itse uudelleen)", "software error (restarted itself)");
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return TR("jumiutui (vahtikoira käynnisti uudelleen)", "hung (restarted by the watchdog)");
    case ESP_RST_BROWNOUT: return TR("jännite notkahti (heikko laturi tai johto?)", "voltage dip (weak charger or cable?)");
    case ESP_RST_EXT:      return TR("RESET-nappi", "RESET button");
    default:               return TR("tuntematon", "unknown");
  }
}
uint32_t uptimeMinutes() { return (uint32_t)(esp_timer_get_time() / 60000000LL); }

// A planned restart at night keeps the screen dark while it starts again.
RTC_NOINIT_ATTR uint32_t quietRestart;
bool quietBoot = false;                // started by such a restart: wait for the clock
const uint32_t QUIET_MAGIC = 0x51E7BEEF;
void restartNow(const char* why, bool quiet) {
  Serial.printf("Restarting: %s\n", why);
  quietRestart = quiet ? QUIET_MAGIC : 0;
  delay(200);
  ESP.restart();
}

// ---------------------------------------------------------------------------
//  Firmware updates (see config.h). Runs in the fetch task.
// ---------------------------------------------------------------------------
// A freshly installed build only counts as good once it has shown it works (below);
// if it crashes or hangs before that, the bootloader starts the previous build again.
extern "C" bool verifyRollbackLater() { return true; }   // replaces the core's own check
void confirmFirmware() {
  static bool done = false;
  if (done) return;
  bool working = (state.apiOk && state.updatedEpoch > 1 && !state.demo) || millis() > 10 * 60000UL;
  if (!working) return;
  done = true;
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("Firmware build %d confirmed\n", FW_BUILD);
  }
}

static void updateProgress(int pct) { fwUpdate.percent = pct; }
// Just before the new firmware is written to flash: let the popup say so, then switch the
// screen off (writing flash pauses its feed, which would show as a mess for ~15 s).
static void beforeFlashWrite() {
  delay(700);
  boardBacklight(false);
}

// Checks every few hours (and when asked); a new version raises fwUpdate.prompt, which
// opens the update screen. Installing only happens when someone presses "install".
void updateTick() {
  static uint32_t nextCheck = 120000;           // first look two minutes after starting
  static char url[256];
  static uint32_t size = 0;
  static int urlBuild = 0;                      // the build url points to
  bool asked = fwUpdate.check, install = fwUpdate.install;
  if (!asked && !install && (int32_t)(millis() - nextCheck) < 0) return;
  fwUpdate.check = false;
  fwUpdate.install = false;
  char err[64] = "";
  if (WiFi.status() != WL_CONNECTED) {
    if (asked || install) {
      snprintf(fwUpdate.error, sizeof fwUpdate.error, "%s", TR("ei Wi-Fi-yhteyttä", "no Wi-Fi connection"));
      fwUpdate.state = UPD_FAILED;
    }
    return;
  }
  if (!install || !urlBuild) {                  // find out what the newest version is
    nextCheck = millis() + UPDATE_CHECK_HOURS * 3600000UL;
    fwUpdate.state = UPD_CHECKING;
    char notes[80] = "";
    int latest = netLatestFirmware(url, sizeof url, &size, notes, sizeof notes, err, sizeof err);
    if (!latest) {
      snprintf(fwUpdate.error, sizeof fwUpdate.error, "%s", err);
      fwUpdate.state = UPD_FAILED;
      Serial.printf("Update check: %s\n", err);
      return;
    }
    urlBuild = latest;
    fwUpdate.latest = latest;
    snprintf(fwUpdate.notes, sizeof fwUpdate.notes, "%s", notes);
    Serial.printf("Update check: running build %d, newest %d\n", FW_BUILD, latest);
  }
  if (urlBuild <= FW_BUILD) { fwUpdate.state = UPD_CURRENT; return; }
  if (!install) {
    fwUpdate.state = UPD_AVAILABLE;
    // Ask, unless this version was skipped or "later" hasn't come yet (asking by hand always shows it).
    if (asked || (urlBuild != fwUpdate.skipBuild && (int32_t)(millis() - fwUpdate.remindAt) >= 0))
      fwUpdate.prompt = true;
    return;
  }
  Serial.printf("Installing build %d (%u bytes)\n", urlBuild, (unsigned)size);
  fwUpdate.percent = -1;                        // connecting
  fwUpdate.state = UPD_INSTALLING;
  if (netInstallFirmware(url, size, updateProgress, beforeFlashWrite, err, sizeof err)) {
    restartNow("firmware update", !backlightOn);
    return;
  }
  if (backlightOn) boardBacklight(true);       // (if it got as far as switching it off)
  snprintf(fwUpdate.error, sizeof fwUpdate.error, "%s", err);
  fwUpdate.state = UPD_FAILED;
  Serial.printf("Update failed: %s\n", err);
}

void watchdogInit() {
  // 90 s: longer than the slowest round of network requests (each has a 12 s timeout
  // and the fetch task feeds the watchdog between them).
  esp_task_wdt_config_t c = {};
  c.timeout_ms = 90000;
  c.idle_core_mask = 0;
  c.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&c) != ESP_OK) esp_task_wdt_init(&c);
  enableLoopWDT();                     // the main loop is fed after every pass
}

// The router may restart, or (after a power cut) come up later than the device.
// Keep trying the saved network, a little less often as time goes on.
void wifiWatch() {
  static uint32_t downSince = 0, lastTry = 0;
  static int tries = 0;
  if (!savedSsid[0] || uiActive()) {         // no network saved, or the setup screen is open
    downSince = 0;
    if (state.wifiDown) { xSemaphoreTake(lock, portMAX_DELAY); state.wifiDown = false; xSemaphoreGive(lock); }
    return;
  }
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (downSince) {
      Serial.printf("Wi-Fi back after %lu s\n", (unsigned long)((now - downSince) / 1000));
      downSince = 0;
      tries = 0;
      xSemaphoreTake(lock, portMAX_DELAY);
      state.wifiDown = false;
      xSemaphoreGive(lock);
      xTaskNotifyGive(fetchTask);
    }
    return;
  }
  if (!downSince) { downSince = now ? now : 1; lastTry = now; Serial.println("Wi-Fi lost"); }
  uint32_t down = now - downSince;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (down > 20000 && !state.wifiDown) {
    state.wifiDown = true;
    snprintf(state.wifiName, sizeof state.wifiName, "%s", savedSsid);
  }
  if (down > 120000 && !state.demo && state.nPlanes) {   // don't leave old planes frozen
    state.nPlanes = 0;
    state.selHex[0] = 0;
    state.follow = false;
  }
  xSemaphoreGive(lock);
  uint32_t gap = 30000UL << (tries < 3 ? tries : 3);        // 30 s, 1, 2, then every 4 min
  if (now - lastTry > gap) {
    lastTry = now;
    tries++;
    Serial.printf("Wi-Fi: trying \"%s\" again (%d)\n", savedSsid, tries);
    WiFi.disconnect();
    WiFi.begin(savedSsid, savedPass[0] ? savedPass : nullptr);
  }
  // Still nothing after half an hour: a fresh start resets the radio completely.
  if (down > 30 * 60000UL && !state.demo && millis() - lastInput > 5 * 60000UL)
    restartNow("no Wi-Fi for 30 min", isNightHour());
}

void healthCheck(bool idleLong) {
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 10 * 60000UL) {
    lastLog = millis();
    Serial.printf("Up %lu min, free RAM %u KB (lowest %u KB), PSRAM %u KB\n", (unsigned long)uptimeMinutes(),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                  heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024,
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
  }
  if (!idleLong || uiActive()) return;       // never in the middle of someone using it
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 20000) restartNow("memory running low", isNightHour());
  // Once a day at 04:00-04:10, a quick restart clears anything that has slowly built up.
  struct tm t;
  if (uptimeMinutes() > 20 * 60 && getLocalTime(&t, 0) && t.tm_hour == 4 && t.tm_min < 10)
    restartNow("daily restart", isNightHour());
}

// ---------------------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("SkyTracker starting");
  if (!psramFound()) Serial.println("!! No PSRAM found: set Tools > PSRAM to 'OPI PSRAM'");
  bool quiet = quietRestart == QUIET_MAGIC && esp_reset_reason() == ESP_RST_SW;
  quietRestart = 0;
  Serial.printf("Started because: %s%s\n", resetReasonText(), quiet ? " (quiet night restart)" : "");

  loadSettings();                      // before anything is drawn
  lock = xSemaphoreCreateMutex();
  events = xQueueCreate(64, sizeof(Ev));
  memset(&state, 0, sizeof state);
  state.planes = (Plane*)heap_caps_calloc(MAX_PLANES, sizeof(Plane), MALLOC_CAP_SPIRAM);
  state.cx = mercX(cfg.homeLon);
  state.cy = mercY(cfg.homeLat);
  state.zoom = START_ZOOM;
  state.apiOk = true;
  netInit();

  baseBuf = (uint16_t*)heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  baseCanvas.use(baseBuf);
  if (!boardInit()) { for (;;) delay(1000); }
  message(APP_NAME, TR("Käynnistyy…", "Starting…"));
  quietBoot = quiet;
  if (quiet) {                         // planned restart at night: stay dark until touched
    backlightOn = false;
    state.night = true;
    lastInput = millis() - RETURN_HOME_AFTER_MIN * 60000UL - 1;
  } else {
    boardBacklight(true);
  }

  // Wi-Fi: use the saved network; if there is none (or it can't be reached),
  // show the on-screen setup so it can be chosen by touch.
  WiFi.mode(WIFI_STA);
  uiInit(&wifiHooks);
  if (savedDemo) {                     // demo planes were chosen last time: keep them
    demoInit(state, millis());
    if (savedSsid[0]) WiFi.begin(savedSsid, savedPass[0] ? savedPass : nullptr);
  } else if (savedSsid[0]) {
    char sub[64];
    snprintf(sub, sizeof sub, TR("Verkko: %s", "Network: %s"), savedSsid);
    message(TR("Yhdistetään Wi-Fi-verkkoon…", "Connecting to Wi-Fi…"), sub);
  }
  if (!savedDemo && !savedSsid[0]) {
    uiOpenWifi("", true, false);
  } else if (!savedDemo && !connectSaved()) {
    // Not reachable yet (e.g. the router is still starting after a power cut): show the
    // map and keep trying in the background (wifiWatch). The network can be changed
    // in Settings.
    Serial.printf("\"%s\" not reachable yet: retrying in the background\n", savedSsid);
  }
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");

  // Connected but home never set (e.g. Wi-Fi was filled in config.h): ask now.
  if (!savedDemo && !homeAsked && !uiActive() && WiFi.status() == WL_CONNECTED) uiOpenHome(true);
  webInit(&state, lock);
  watchdogInit();
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(fetchLoop, "fetch", 20480, nullptr, 1, &fetchTask, 0);
  xTaskNotifyGive(fetchTask);
}

// ---------------------------------------------------------------------------
//  Main loop: handle touches, redraw a few times a second
// ---------------------------------------------------------------------------
void loop() {
  static uint32_t lastFrame = 0;
  webLoop();                                       // the phone settings page
  confirmFirmware();
  // The fetch task is writing new firmware: an "update in progress" popup over the
  // screen as it was (dimmed). The cached map buffer holds that dimmed copy meanwhile.
  static bool popupBack = false;
  if (fwUpdate.state == UPD_INSTALLING || fwUpdate.install) {   // (install: pressed, about to start)
    static uint32_t lastPopup = 0;
    static int shownPct = -2, fullFrames = 0;
    if (backlightOn) {
      if (!popupBack) {
        const uint16_t* front = boardFrontBuffer();
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) baseBuf[i] = (front[i] >> 1) & 0x7BEF;   // half brightness
        popupBack = true;
        shownPct = -2;
        fullFrames = 0;
      }
      int pct = fwUpdate.state == UPD_INSTALLING ? (int)fwUpdate.percent : -1;
      if (pct != shownPct && millis() - lastPopup > 150) {
        lastPopup = millis();
        shownPct = pct;
        // The first two frames fill both buffers with the dimmed screen; after that only
        // the popup's strip is redrawn (less PSRAM traffic while the download needs it).
        uint16_t* fbuf = boardBackBuffer();
        if (fullFrames < 2) { memcpy(fbuf, baseBuf, SCREEN_W * SCREEN_H * 2); fullFrames++; }
        else memcpy(fbuf + UPDATE_POPUP_Y0 * SCREEN_W, baseBuf + UPDATE_POPUP_Y0 * SCREEN_W,
                    (UPDATE_POPUP_Y1 - UPDATE_POPUP_Y0) * SCREEN_W * 2);
        canvas.use(fbuf);
        renderUpdatePopup(canvas, pct, fwUpdate.latest);
        boardPresent();
      }
    }
    delay(20);
    return;
  }
  if (popupBack) {                                 // it failed: back, and say why
    popupBack = false;
    baseStale = true;
    if (fwUpdate.state == UPD_FAILED) uiOpenUpdate();
  }
  wifiWatch();
  // A new version found in the background: ask, once the screen is on and nobody is busy with it.
  if (fwUpdate.prompt && backlightOn && !uiActive() && millis() - lastInput > 20000) {
    fwUpdate.prompt = false;
    uiOpenUpdate();
  }
  Ev e;
  bool got = false, viewChanged = false, dragging = false;
  // Menus (settings, Wi-Fi setup) cover the whole screen and only use taps.
  if (uiActive()) {
    static uint32_t lastUi = 0;
    while (xQueueReceive(events, &e, pdMS_TO_TICKS(10)) == pdTRUE) {
      lastInput = millis();
      if (!backlightOn) { backlightOn = true; state.night = false; boardBacklight(true); continue; }
      if (e.type == EV_TAP) {
        uiTap(e.x, e.y, millis(), state);        // (hooks take the lock themselves)
        lastUi = 0;                               // redraw right away
      }
    }
    uiTick(millis());
    if (!uiBusy() && millis() - lastInput > RETURN_HOME_AFTER_MIN * 60000UL) uiClose();
    if (!uiActive()) { baseStale = true; return; }
    if (millis() - lastUi >= 100) {
      lastUi = millis();
      canvas.use(boardBackBuffer());
      xSemaphoreTake(lock, portMAX_DELAY);
      applyTheme();
      uiRender(canvas, state, millis());
      xSemaphoreGive(lock);
      boardPresent();
    }
    return;
  }

  while (xQueueReceive(events, &e, got ? 0 : pdMS_TO_TICKS(10)) == pdTRUE) {
    got = true;
    lastInput = millis();
    if (!backlightOn) {                            // first touch just wakes the screen
      backlightOn = true;
      state.night = false;
      boardBacklight(true);
      baseStale = true;
      xTaskNotifyGive(fetchTask);
      xQueueReset(events);
      break;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    float cx = state.cx, cy = state.cy;
    int z = state.zoom;
    float m = metresPerPx(state.zoom);
    switch (e.type) {
      case EV_DRAG:                                // the map follows the finger
        appPan(state, e.dx, e.dy);
        if (state.zoom == baseZoom) {              // slide the cached map along
          float fx = (baseCx - state.cx) / m, fy = (state.cy - baseCy) / m;
          int ix = lroundf(fx), iy = lroundf(fy);
          if (ix || iy) { shiftBase(ix, iy); baseCx -= ix * m; baseCy += iy * m; }
        }
        dragging = true;
        break;
      case EV_DRAG_END: baseStale = true; break;
      case EV_SCROLL: if (!state.selHex[0]) appScroll(state, e.dy); break;
      case EV_ZOOM: appZoom(state, e.dx); break;
      case EV_TAP:
        switch (appTap(state, e.x, e.y, millis(), requestRouteLocked)) {
          case HIT_SETTINGS: uiOpenSettings(); break;
          case HIT_SEARCH: uiOpenFlightSearch(); break;
          case HIT_PICK_SAVE: finishPickHome(true); break;
          case HIT_PICK_CANCEL: finishPickHome(false); break;
          default: break;
        }
        break;
    }
    viewChanged |= cx != state.cx || cy != state.cy || z != state.zoom;
    xSemaphoreGive(lock);
  }
  if (viewChanged && !dragging) xTaskNotifyGive(fetchTask);   // fetch the new area
  if (got && e.type == EV_DRAG_END) xTaskNotifyGive(fetchTask);

  uint32_t idle = millis() - lastInput;
  bool idleLong = idle > RETURN_HOME_AFTER_MIN * 60000UL;

  healthCheck(idleLong);

  // Night: screen off until a tap.
  if (isNightHour() && idleLong) {
    if (backlightOn) {
      backlightOn = false;
      state.night = true;
      boardBacklight(false);
    }
    delay(50);
    return;
  }
  if (!backlightOn) {                              // night is over (or its hours were changed)
    struct tm t;
    if (quietBoot && !getLocalTime(&t, 0) && millis() < 180000) { delay(50); return; }  // clock not set yet
    quietBoot = false;
    backlightOn = true;
    state.night = false;
    boardBacklight(true);
    baseStale = true;
    xTaskNotifyGive(fetchTask);
  }

  xSemaphoreTake(lock, portMAX_DELAY);
  bool away = state.zoom != START_ZOOM || state.selHex[0] ||
              fabsf(state.cx - mercX(cfg.homeLon)) > 1 || fabsf(state.cy - mercY(cfg.homeLat)) > 1;
  // Back to the home view after a while untouched, but not while a plane is selected:
  // its details and path stay until it's closed or the plane leaves the data.
  if (away && idleLong && !state.follow && !state.pickHome && !state.selHex[0]) {
    appGoHome(state);
    xTaskNotifyGive(fetchTask);
  }
  Plane* sel = state.selected();
  if (sel && !state.route(sel->cs)) requestRouteLocked(*sel);   // retry failed lookups
  // Departure/arrival times for the selected flight: once, then refreshed now and then.
  if (Route* r = sel && cfg.airlabsKey[0] && !state.demo ? state.route(sel->cs) : nullptr) {
    uint32_t age = millis() - r->timesMs;
    bool due = (r->times == TIMES_NONE && (!r->timesMs || age > 60000)) ||
               (r->times == TIMES_KNOWN && age > TIMES_REFRESH_MIN * 60000UL);
    if (r->state == ROUTE_KNOWN && due) { r->times = TIMES_PENDING; xTaskNotifyGive(fetchTask); }
  }
  // A newly selected plane: fetch its photo (not in demo mode: those planes are simulated).
  if (sel && cfg.photos && !state.demo && strcmp(photo.hex, sel->hex)) {
    snprintf(photo.hex, sizeof photo.hex, "%s", sel->hex);
    snprintf(photo.reg, sizeof photo.reg, "%s", sel->reg);
    photo.state = PHOTO_LOADING;
    photo.hidden = false;
    xTaskNotifyGive(fetchTask);
  }
  if (!sel && photo.hex[0]) { photo.hex[0] = 0; photo.state = PHOTO_NONE; }
  // A newly selected plane: its flight path (fetched for live planes, made up for demo ones).
  if (sel && strcmp(flightPath.hex, sel->hex)) {
    snprintf(flightPath.hex, sizeof flightPath.hex, "%s", sel->hex);
    flightPath.clear();
    flightPath.tries = 0;
    if (state.demo) demoPath(state, *sel, flightPath);
    else { flightPath.state = PATH_LOADING; xTaskNotifyGive(fetchTask); }
  }
  // Nothing found yet (e.g. a plane that only just took off): look again a few times.
  if (sel && !state.demo && flightPath.state == PATH_MISSING && flightPath.tries < 4 &&
      millis() - flightPath.triedMs > 90000) {
    flightPath.state = PATH_LOADING;
    xTaskNotifyGive(fetchTask);
  }
  if (!sel && flightPath.hex[0]) { flightPath.hex[0] = 0; flightPath.state = PATH_NONE; }
  if (state.follow && sel) {
    state.advance(millis());
    state.cx = sel->x;
    state.cy = sel->y;
  }
  xSemaphoreGive(lock);

  // Redraw soon after a touch (but not more than 25 times a second, so the LCD's own
  // data stream keeps up), otherwise a few times a second for the gliding planes.
  static bool pending = false;
  pending |= got;
  uint32_t since = millis() - lastFrame;
  if ((pending && since >= FRAME_MS_TOUCH) || since >= FRAME_MS) {
    pending = false;
    lastFrame = millis();
    drawFrame();
  }
}
