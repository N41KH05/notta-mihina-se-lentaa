// Live data: plane positions from adsb.fi / airplanes.live / adsb.lol,
// flight routes from adsbdb.com.
#include "net.h"
#include <esp_task_wdt.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <miniz.h>                         // the ROM's inflater (for gzip'ed track histories)
#include <ctype.h>
#include <math.h>

#define LOCK(l) xSemaphoreTake((SemaphoreHandle_t)(l), portMAX_DELAY)
#define UNLOCK(l) xSemaphoreGive((SemaphoreHandle_t)(l))

// ============================================================================
//  Reading the services' replies
// ============================================================================
namespace {
namespace {
void copyStr(char* dst, size_t n, const char* src) {
  snprintf(dst, n, "%s", src ? src : "");
  for (int i = (int)strlen(dst) - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;   // trim
}
// "1, Aleksanterinkatu, Kluuvi, Eteläinen suurpiiri, Helsinki, Helsingin seutukunta, ..." ->
// name "Aleksanterinkatu 1", detail "Kluuvi, Eteläinen suurpiiri, Helsinki"
void splitName(const char* full, Place& p) {
  char parts[8][64];
  int n = 0;
  const char* s = full;
  while (*s && n < 8) {
    while (*s == ' ') s++;
    const char* e = strchr(s, ',');
    size_t len = e ? (size_t)(e - s) : strlen(s);
    snprintf(parts[n++], sizeof parts[0], "%.*s", (int)(len < 63 ? len : 63), s);
    if (!e) break;
    s = e + 1;
  }
  int first = 1;
  if (n >= 2 && isdigit((uint8_t)parts[0][0])) {         // house number first: swap
    snprintf(p.name, sizeof p.name, "%.36s %.10s", parts[1], parts[0]);
    first = 2;
  } else {
    snprintf(p.name, sizeof p.name, "%.47s", n ? parts[0] : full);
  }
  p.detail[0] = 0;
  for (int i = first, k = 0; i < n && k < 3; i++) {
    if (isdigit((uint8_t)parts[i][0])) continue;         // postcodes
    size_t l = strlen(p.detail);
    snprintf(p.detail + l, sizeof p.detail - l, "%s%s", k ? ", " : "", parts[i]);
    k++;
  }
}
}  // namespace

void trafficFilter(JsonDocument& filter) {
  for (const char* key : {"ac", "aircraft"}) {
    JsonObject f = filter[key][0].to<JsonObject>();
    for (const char* k : {"hex", "flight", "r", "t", "alt_baro", "baro_rate", "gs", "track",
                          "lat", "lon", "seen_pos", "squawk", "category"})
      f[k] = true;
  }
}

// Reads the planes of a report into out. If there are more than MAX_PLANES (a wide view
// over busy airspace), keeps the ones nearest to (cx, cy), the middle of the view.
int trafficParse(JsonDocument& doc, Plane* out, uint32_t now, float cx, float cy) {
  JsonArray list = doc["ac"].is<JsonArray>() ? doc["ac"].as<JsonArray>() : doc["aircraft"].as<JsonArray>();
  static float dist[MAX_PLANES];
  int n = 0, far = -1;                       // far: the kept plane furthest from the centre
  for (JsonObject a : list) {
    if (!a["lat"].is<float>() || !a["lon"].is<float>()) continue;
    if ((a["seen_pos"] | 0.0f) > 60) continue;
    bool ground = a["alt_baro"].is<const char*>();
    if (ground && HIDE_ON_GROUND) continue;
    double lat = a["lat"].as<double>(), lon = a["lon"].as<double>();
    float x = mercX(lon), y = mercY(lat), d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
    int slot;
    if (n < MAX_PLANES) slot = n++;
    else if (d < dist[far]) slot = far;      // closer than the furthest one kept: replace it
    else continue;
    dist[slot] = d;
    if (n == MAX_PLANES && (far < 0 || slot == far)) {
      far = 0;
      for (int i = 1; i < n; i++) if (dist[i] > dist[far]) far = i;
    }
    Plane& p = out[slot];
    memset(&p, 0, sizeof(Plane));
    copyStr(p.hex, sizeof p.hex, a["hex"] | "");
    copyStr(p.cs, sizeof p.cs, a["flight"] | "");
    copyStr(p.reg, sizeof p.reg, a["r"] | "");
    copyStr(p.type, sizeof p.type, a["t"] | "");
    copyStr(p.squawk, sizeof p.squawk, a["squawk"] | "");
    copyStr(p.category, sizeof p.category, a["category"] | "");
    p.lat = lat;
    p.lon = lon;
    p.fx = p.x = x;
    p.fy = p.y = y;
    p.tMs = now - (uint32_t)((a["seen_pos"] | 0.0f) * 1000);
    p.hasAlt = ground || a["alt_baro"].is<float>();
    p.alt = ground ? 0 : (int32_t)(a["alt_baro"] | 0.0f);
    p.hasGs = a["gs"].is<float>();
    p.gs = a["gs"] | 0.0f;
    p.hasTrack = a["track"].is<float>();
    p.track = a["track"] | 0.0f;
    p.hasVrate = a["baro_rate"].is<float>();
    p.vrate = (int32_t)(a["baro_rate"] | 0.0f);
  }
  return n;
}

// Finds planes of the previous report by their hex code in one pass instead of
// searching the whole list for each new plane (up to 400 x 400 string compares).
namespace {
const int HASH_SIZE = 1024;                  // power of two, > 2 x MAX_PLANES
uint32_t hexKey(const char* h) {
  uint32_t k = 2166136261u;                  // FNV-1a
  for (; *h; h++) k = (k ^ (uint8_t)*h) * 16777619u;
  return k;
}
}  // namespace

void trafficMerge(AppState& s, Plane*& incoming, int n) {
  static int16_t slot[HASH_SIZE];
  memset(slot, 0xFF, sizeof slot);           // -1: empty
  for (int i = 0; i < s.nPlanes; i++) {
    uint32_t h = hexKey(s.planes[i].hex) & (HASH_SIZE - 1);
    while (slot[h] >= 0) h = (h + 1) & (HASH_SIZE - 1);
    slot[h] = i;
  }
  auto findOld = [&](const char* hex) -> Plane* {
    for (uint32_t h = hexKey(hex) & (HASH_SIZE - 1); slot[h] >= 0; h = (h + 1) & (HASH_SIZE - 1))
      if (!strcmp(s.planes[slot[h]].hex, hex)) return &s.planes[slot[h]];
    return nullptr;
  };
  for (int i = 0; i < n; i++) {
    Plane& p = incoming[i];
    Plane* old = findOld(p.hex);
    if (old) {
      p.trailN = old->trailN;
      p.trailHead = old->trailHead;
      memcpy(p.trail, old->trail, sizeof p.trail);
    }
    p.addTrail(p.fx, p.fy);
  }
  // The selected plane missing from this report (the view moved away from it, it fell
  // out of the nearest 400, or one report simply missed it): keep it, still gliding
  // along its last track, for up to 90 s rather than dropping the selection and its path.
  if (s.selHex[0]) {
    bool found = false;
    for (int i = 0; i < n && !found; i++) found = !strcmp(incoming[i].hex, s.selHex);
    Plane* old = found ? nullptr : findOld(s.selHex);
    if (old && (int32_t)(millis() - old->tMs) < 90000) {
      int slot = n < MAX_PLANES ? n++ : n - 1;
      incoming[slot] = *old;
    }
  }
  Plane* t = s.planes;
  s.planes = incoming;
  incoming = t;
  s.nPlanes = n;
  if (s.selHex[0] && !s.find(s.selHex)) { s.selHex[0] = 0; s.follow = false; }
}

int trafficRadiusNm(const AppState& s) {
  float x0, y0, x1, y1;
  s.viewBounds(60, x0, y0, x1, y1);
  double lat = latFromY(s.cy);
  double halfDiag = hypot(x1 - x0, y1 - y0) / 2 * cos(lat * M_PI / 180);
  int radius = (int)ceil(halfDiag / 1852);
  if (radius < 5) radius = 5;
  if (radius > 250) radius = 250;
  return radius;
}

void routeFilter(JsonDocument& filter) {
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["callsign_iata"] = true;
  fr["airline"]["name"] = true;
  fr["airline"]["icao"] = true;
  JsonObject ac = filter["response"]["aircraft"].to<JsonObject>();
  ac["registered_owner"] = true;
  ac["registered_owner_operator_flag_code"] = true;
  for (const char* end : {"origin", "destination"}) {
    fr[end]["iata_code"] = true;
    fr[end]["icao_code"] = true;
    fr[end]["municipality"] = true;
    fr[end]["latitude"] = true;
    fr[end]["longitude"] = true;
  }
}

// The aircraft part of an adsbdb answer (only there when asked by transponder code).
void aircraftParse(JsonDocument& doc, Route& r) {
  JsonObject ac = doc["response"]["aircraft"];
  if (ac.isNull()) return;
  copyStr(r.owner, sizeof r.owner, ac["registered_owner"] | "");
  copyStr(r.ownerCode, sizeof r.ownerCode, ac["registered_owner_operator_flag_code"] | "");
}

bool routeParse(JsonDocument& doc, Route& r) {
  JsonObject route = doc["response"]["flightroute"];
  if (route.isNull()) return false;
  copyStr(r.flight, sizeof r.flight, route["callsign_iata"] | "");
  copyStr(r.airline, sizeof r.airline, route["airline"]["name"] | "");
  copyStr(r.airlineCode, sizeof r.airlineCode, route["airline"]["icao"] | "");
  copyStr(r.from, sizeof r.from, route["origin"]["iata_code"] | (route["origin"]["icao_code"] | ""));
  copyStr(r.to, sizeof r.to, route["destination"]["iata_code"] | (route["destination"]["icao_code"] | ""));
  copyStr(r.fromCity, sizeof r.fromCity, route["origin"]["municipality"] | "");
  copyStr(r.toCity, sizeof r.toCity, route["destination"]["municipality"] | "");
  JsonVariant dlat = route["destination"]["latitude"], dlon = route["destination"]["longitude"];
  if (dlat.is<float>() && dlon.is<float>()) {
    r.hasDest = true;
    r.toLat = dlat.as<float>();
    r.toLon = dlon.as<float>();
  }
  JsonVariant olat = route["origin"]["latitude"], olon = route["origin"]["longitude"];
  if (olat.is<float>() && olon.is<float>()) {
    r.hasOrigin = true;
    r.fromLat = olat.as<float>();
    r.fromLon = olon.as<float>();
  }
  return true;
}

// Track history in tar1090's trace format, from adsb.lol: /data/traces/xx/trace_full_<hex>.json
// and trace_recent_<hex>.json, both {"timestamp": <unix seconds>, "trace": [[seconds after
// timestamp, lat, lon, altitude or "ground", speed, track, flags, ...], ...]}. The full file
// is only rewritten every few minutes; the recent one has the latest stretch.
void traceFilter(JsonDocument& filter) {
  filter["timestamp"] = true;
  filter["trace"][0][0] = true;
}
// Appends the points of one trace file that are later than *lastT (so the recent file only
// adds what the full one doesn't have yet). Returns the new number of points.
int traceCollect(JsonDocument& doc, TracePt* pts, int n, int max, double* lastT) {
  double base = doc["timestamp"] | 0.0;
  for (JsonArray p : doc["trace"].as<JsonArray>()) {
    if (n >= max) break;
    double t = base + (p[0] | 0.0);
    if (t <= *lastT) continue;
    if (!p[1].is<float>() || !p[2].is<float>()) continue;
    TracePt& q = pts[n++];
    q.t = t;
    q.lat = p[1].as<float>();
    q.lon = p[2].as<float>();
    q.ground = p[3].is<const char*>();
    q.alt = p[3].is<int>() ? p[3].as<int>() : 0;
    q.flags = p[6] | 0;
    *lastT = t;
  }
  return n;
}
// Keeps the current flight only: from the last point on the ground, the last "new leg"
// flag, or the last gap of over 30 minutes. Returns how many points went into out.
int traceToPath(const TracePt* pts, int n, FlightPath& out) {
  int start = 0;
  for (int i = 0; i < n; i++) {
    if (pts[i].flags & 2) start = i;                                  // a new leg starts here
    if (i && pts[i].t - pts[i - 1].t > 1800) start = i;               // long gap: a later flight
    if (pts[i].ground) start = i;                                     // not airborne yet
  }
  out.clear();
  for (int i = start; i < n; i++) out.add(mercX(pts[i].lon), mercY(pts[i].lat), pts[i].alt);
  return out.n;
}

void nominatimFilter(JsonDocument& filter) {
  filter[0]["display_name"] = true;
  filter[0]["lat"] = true;
  filter[0]["lon"] = true;
}

int nominatimParse(JsonDocument& doc, Place* out, int max) {
  int n = 0;
  for (JsonObject r : doc.as<JsonArray>()) {
    if (n >= max) break;
    const char* name = r["display_name"] | "";
    const char* lat = r["lat"] | "";
    const char* lon = r["lon"] | "";
    if (!name[0] || !lat[0] || !lon[0]) continue;
    splitName(name, out[n]);
    out[n].lat = atof(lat);
    out[n].lon = atof(lon);
    n++;
  }
  return n;
}

}  // namespace

// ============================================================================
//  Fetching
// ============================================================================
namespace {

struct Source { const char* name; const char* url; };
const Source SOURCES[] = API_SOURCES;
const int N_SOURCES = sizeof(SOURCES) / sizeof(SOURCES[0]);
int sourceIdx = 0;
uint32_t lastRequest = 0;

// Let ArduinoJson use the big PSRAM instead of the small internal RAM.
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} psram;

Plane* incoming = nullptr;     // second buffer the new report is assembled in

void politeWait() {             // the services allow about 1 request per second
  esp_task_wdt_reset();         // each request is a step forward: feed the watchdog
  uint32_t since = millis() - lastRequest;
  if (since < 1200) delay(1200 - since);
  lastRequest = millis();
}

// GET url and parse JSON (through filter). Returns HTTP status, or <0 on failure.
int getJson(const char* url, JsonDocument& doc, JsonDocument& filter, char* err, size_t errLen,
            const char* userAgent = nullptr) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();           // verify server certificates (built-in root CA list)
  client.setHandshakeTimeout(15);            // seconds; the default (2 min) would trip the watchdog
  HTTPClient http;
  http.useHTTP10(true);                      // plain body, so it can be parsed as it streams
  http.setTimeout(12000);
  http.setUserAgent(userAgent ? userAgent : "SkyTracker desk display (personal, non-commercial)");
  if (!http.begin(client, url)) { snprintf(err, errLen, "%s", TR("virheellinen osoite", "invalid address")); return -1; }
  politeWait();
  int status = http.GET();
  if (status == 200) {
    DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (e) { snprintf(err, errLen, TR("virheellinen vastaus (%s)", "invalid response (%s)"), e.c_str()); status = -2; }
  } else if (status > 0) {
    snprintf(err, errLen, "HTTP %d", status);
  } else {
    snprintf(err, errLen, "%s", WiFi.status() == WL_CONNECTED ? TR("ei yhteyttä palvelimeen", "no connection to server") : TR("Wi-Fi ei ole yhdistetty", "Wi-Fi not connected"));
  }
  http.end();
  if (status != 200) {                       // log the URL without any API key
    const char* k = strstr(url, "api_key=");
    Serial.printf("GET %.*s -> %s\n", k ? (int)(k - url) : (int)strlen(url), url, err);
  }
  return status;
}

// Planespotters asks apps to name themselves and give a contact address.
// Filled in (under the lock) at the start of each photo fetch.
char userAgent[160];
const char* photoUserAgent() { return userAgent; }

// Values from the flight data (callsigns, registrations, hex codes) go into request
// URLs only if they contain nothing but letters, digits and '-'.
bool urlSafe(const char* s) {
  if (!*s) return false;
  for (; *s; s++)
    if (!isalnum((uint8_t)*s) && *s != '-') return false;
  return true;
}

}  // namespace

void netInit() {
  incoming = (Plane*)heap_caps_calloc(MAX_PLANES, sizeof(Plane), MALLOC_CAP_SPIRAM);
}

void netFetchPlanes(AppState& s, void* lock) {
  // Which area? Everything the map shows (the services answer up to 250 nm).
  LOCK(lock);
  int radius = trafficRadiusNm(s);
  float cx = s.cx, cy = s.cy;
  UNLOCK(lock);
  double lat = latFromY(cy), lon = lonFromX(cx);

  JsonDocument filter;
  trafficFilter(filter);

  // Try the service that worked last time first, then the others.
  char errors[120] = "";
  int status = -1;
  JsonDocument doc(&psram);
  int used = sourceIdx;
  for (int i = 0; i < N_SOURCES; i++) {
    used = (sourceIdx + i) % N_SOURCES;
    char url[160], err[48] = "";
    snprintf(url, sizeof url, SOURCES[used].url, lat, lon, radius);
    doc.clear();
    status = getJson(url, doc, filter, err, sizeof err);
    if (status == 200) break;
    size_t n = strlen(errors);
    snprintf(errors + n, sizeof errors - n, "%s%s: %s", n ? "; " : "", SOURCES[used].name, err);  // shown on screen
  }
  if (status == 200 && used != sourceIdx) Serial.printf("Using %s for live data\n", SOURCES[used].name);
  if (status == 200) sourceIdx = used;

  int n = status == 200 ? trafficParse(doc, incoming, millis(), cx, cy) : 0;

  LOCK(lock);
  s.apiOk = status == 200;
  snprintf(s.apiError, sizeof s.apiError, "%s", s.apiOk ? "" : errors);
  snprintf(s.source, sizeof s.source, "%s", SOURCES[sourceIdx].name);
  s.fetchCx = cx;
  s.fetchCy = cy;
  s.fetchRadiusNm = radius;
  if (s.apiOk) {
    trafficMerge(s, incoming, n);
    pathFollow(s);                         // the selected plane's path grows with it
    s.updatedEpoch = time(nullptr);
  }
  UNLOCK(lock);
  Serial.printf("%d planes (%s), free PSRAM %u KB\n", n, s.apiOk ? SOURCES[sourceIdx].name : "failed",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void netLookupRoute(AppState& s, void* lock) {
  char cs[10] = "", hex[8] = "";
  LOCK(lock);
  for (auto& r : s.routes)
    if (r.state == ROUTE_PENDING) {
      snprintf(cs, sizeof cs, "%s", r.cs);
      snprintf(hex, sizeof hex, "%s", r.hex);
      break;
    }
  UNLOCK(lock);
  if (!cs[0]) return;

  // One request gives both the route and the aircraft. adsbdb answers 404 to the whole
  // thing if it doesn't know the aircraft, so then the route is asked for on its own.
  JsonDocument filter;
  routeFilter(filter);
  JsonDocument doc(&psram);
  char url[112], err[48];
  int status = 404;                          // not a normal callsign: no route to look up
  if (urlSafe(cs) && urlSafe(hex)) {
    snprintf(url, sizeof url, "https://api.adsbdb.com/v0/aircraft/%s?callsign=%s", hex, cs);
    status = getJson(url, doc, filter, err, sizeof err);
  }
  if (status == 404 && urlSafe(cs)) {
    doc.clear();
    snprintf(url, sizeof url, "https://api.adsbdb.com/v0/callsign/%s", cs);
    status = getJson(url, doc, filter, err, sizeof err);
  }

  LOCK(lock);
  Route* r = s.route(cs);
  if (r && r->state == ROUTE_PENDING) {
    if (status == 200) aircraftParse(doc, *r);
    if (status == 200 && routeParse(doc, *r)) r->state = ROUTE_KNOWN;
    else if (status == 404 || status == 200) r->state = ROUTE_UNKNOWN;   // no route published
    else r->state = ROUTE_EMPTY;              // network trouble: forget it, ask again later
  }
  UNLOCK(lock);
}

// ---------------------------------------------------------------------------
//  Address search for the home position: OpenStreetMap's Nominatim. Their usage
//  policy: at most one request a second, name the app, show the attribution (the
//  results screen does). Used only while setting home.
// ---------------------------------------------------------------------------
namespace {
void urlEncode(const char* in, char* out, size_t n) {
  size_t o = 0;
  for (const uint8_t* p = (const uint8_t*)in; *p && o + 4 < n; p++) {
    if (isalnum(*p) || strchr("-_.~", *p)) out[o++] = (char)*p;
    else if (*p == ' ') out[o++] = '+';
    else o += snprintf(out + o, n - o, "%%%02X", *p);
  }
  out[o] = 0;
}
}  // namespace

int netSearchPlaces(const char* query, Place* out, int max) {
  char q[160], url[260], err[48];
  urlEncode(query, q, sizeof q);
  snprintf(url, sizeof url,
           "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=%d&accept-language=fi&q=%s", max, q);
  JsonDocument filter;
  nominatimFilter(filter);
  JsonDocument doc(&psram);
  char ua[160];
  snprintf(ua, sizeof ua, "NottaMihinaSeLentaa/1.0 (ESP32 flight-map desk display; contact: %s)", cfg.contact);
  if (getJson(url, doc, filter, err, sizeof err, ua) != 200) return -1;
  int n = nominatimParse(doc, out, max);
  Serial.printf("Place search \"%s\": %d found\n", query, n);
  return n;
}

// ---------------------------------------------------------------------------
//  Departure and arrival times from AirLabs (only with a key in config.h).
// ---------------------------------------------------------------------------
namespace {
int16_t localMinutes(JsonVariant ts) {       // UNIX time -> local minutes past midnight
  if (!ts.is<long>() || ts.as<long>() <= 0) return -1;
  time_t t = ts.as<long>();
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour * 60 + lt.tm_min;
}
int callsToday = 0, callsDay = -1;
}  // namespace

int netAirlabsCallsToday() { return callsToday; }

void netLookupTimes(AppState& s, void* lock) {
  char cs[10] = "", flight[10] = "", apiKey[sizeof cfg.airlabsKey];
  LOCK(lock);
  snprintf(apiKey, sizeof apiKey, "%s", cfg.airlabsKey);   // can change on the settings page
  for (auto& r : s.routes)
    if (r.state == ROUTE_KNOWN && r.times == TIMES_PENDING) {
      snprintf(cs, sizeof cs, "%s", r.cs);
      snprintf(flight, sizeof flight, "%s", r.flight);
      break;
    }
  UNLOCK(lock);
  if (!cs[0] || !apiKey[0]) return;

  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  if (lt.tm_yday != callsDay) { callsDay = lt.tm_yday; callsToday = 0; }

  JsonDocument filter;
  for (const char* f : {"dep_time_ts", "dep_estimated_ts", "arr_time_ts", "arr_estimated_ts"}) {
    filter[f] = true;                        // the reply may or may not be wrapped in "response"
    filter["response"][f] = true;
  }
  filter["error"]["message"] = true;
  JsonDocument doc(&psram);
  char url[200], err[48];
  bool found = false, failed = false;
  // First by the radio callsign (ICAO flight code), then by the flight number.
  for (int attempt = 0; attempt < 2 && !found && !failed; attempt++) {
    const char* key = attempt == 0 ? "flight_icao" : "flight_iata";
    const char* code = attempt == 0 ? cs : flight;
    if (!code[0] || !urlSafe(code) || (attempt == 1 && !strcmp(flight, cs))) continue;
    if (callsToday >= AIRLABS_DAILY_MAX) { Serial.println("AirLabs: daily limit reached"); failed = true; break; }
    callsToday++;
    snprintf(url, sizeof url, "https://airlabs.co/api/v9/flight?%s=%s&api_key=%s", key, code, apiKey);
    doc.clear();
    int status = getJson(url, doc, filter, err, sizeof err);
    JsonVariant r = doc["response"].isNull() ? doc.as<JsonVariant>() : doc["response"].as<JsonVariant>();
    const char* msg = doc["error"]["message"] | "";
    if (status == 200 && !r["dep_time_ts"].isNull()) {
      found = true;
    } else {
      // Network trouble, overload or a used-up quota: try again later. Otherwise the
      // flight just isn't in their schedule.
      if (status < 0 || status == 429 || status >= 500 || strstr(msg, "limit")) failed = true;
      Serial.printf("AirLabs %s: %s\n", code, msg[0] ? msg : status == 200 ? "no times" : err);
    }
  }

  LOCK(lock);
  Route* r = s.route(cs);
  if (r && r->times == TIMES_PENDING) {
    r->timesMs = millis();
    if (found) {
      JsonVariant v = doc["response"].isNull() ? doc.as<JsonVariant>() : doc["response"].as<JsonVariant>();
      r->depSched = localMinutes(v["dep_time_ts"]);
      r->depEst = localMinutes(v["dep_estimated_ts"]);
      r->arrSched = localMinutes(v["arr_time_ts"]);
      r->arrEst = localMinutes(v["arr_estimated_ts"]);
      r->times = TIMES_KNOWN;
      r->hasTimes = true;
    } else {
      r->times = failed ? TIMES_NONE : TIMES_UNKNOWN;
    }
  }
  UNLOCK(lock);
  Serial.printf("Times for %s: %s (%d/%d requests today)\n", cs,
                found ? "found" : failed ? "failed" : "none", callsToday, AIRLABS_DAILY_MAX);
}

// ---------------------------------------------------------------------------
//  Plane photos from Planespotters.net. Their terms: credit the photographer,
//  link to the photo page (we show a QR code), identify the app in the
//  User-Agent, and never store the images (they only live in memory here).
// ---------------------------------------------------------------------------
namespace {
// JPEG decoding with stb_image (public domain, stb_image.h): handles both baseline and
// progressive JPEGs, which photo sites often serve. Its buffers go to PSRAM.
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT(x)
#define STBI_MALLOC(n) heap_caps_malloc(n, MALLOC_CAP_SPIRAM)
#define STBI_REALLOC(p, n) heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM)
#define STBI_FREE(p) heap_caps_free(p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "stb_image.h"
#pragma GCC diagnostic pop

// JPEG -> RGB565 pixels at the picture's own size, in a new PSRAM buffer (free it with
// heap_caps_free). Returns nullptr if it can't be read or is unreasonably big.
uint16_t* decodeJpeg(const uint8_t* jpg, size_t len, int* w, int* h) {
  int iw, ih, comp;
  if (!stbi_info_from_memory(jpg, (int)len, &iw, &ih, &comp) || iw <= 0 || ih <= 0 || iw * ih > 1600 * 1200)
    return nullptr;
  uint8_t* rgb = stbi_load_from_memory(jpg, (int)len, &iw, &ih, &comp, 3);
  if (!rgb) return nullptr;
  uint16_t* px = (uint16_t*)rgb;            // convert in place: 2 bytes out for every 3 read
  for (int i = 0, n = iw * ih; i < n; i++) {
    const uint8_t* c = rgb + i * 3;
    px[i] = (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
  }
  *w = iw;
  *h = ih;
  return px;
}

// Scale the decoded picture to fill PHOTO_W x PHOTO_H, cropping the edges (bilinear).
void coverResize(const uint16_t* src, int w, int h, uint16_t* dst) {
  float scale = fmaxf((float)PHOTO_W / w, (float)PHOTO_H / h);
  float x0 = (w - PHOTO_W / scale) / 2, y0 = (h - PHOTO_H / scale) / 2;
  for (int j = 0; j < PHOTO_H; j++) {
    float fy = y0 + (j + 0.5f) / scale - 0.5f;
    int iy = (int)fy; float ty = fy - iy;
    if (iy < 0) { iy = 0; ty = 0; }
    int iy1 = iy + 1 < h ? iy + 1 : iy;
    for (int i = 0; i < PHOTO_W; i++) {
      float fx = x0 + (i + 0.5f) / scale - 0.5f;
      int ix = (int)fx; float tx = fx - ix;
      if (ix < 0) { ix = 0; tx = 0; }
      int ix1 = ix + 1 < w ? ix + 1 : ix;
      uint16_t p[4] = {src[iy * w + ix], src[iy * w + ix1], src[iy1 * w + ix], src[iy1 * w + ix1]};
      float wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
      float r = 0, g = 0, b = 0;
      for (int k = 0; k < 4; k++) {
        r += ((p[k] >> 11) & 31) * wt[k];
        g += ((p[k] >> 5) & 63) * wt[k];
        b += (p[k] & 31) * wt[k];
      }
      dst[j * PHOTO_W + i] = ((int)(r + 0.5f) << 11) | ((int)(g + 0.5f) << 5) | (int)(b + 0.5f);
    }
  }
}

// Download a URL into a new PSRAM buffer (at most maxLen bytes). Returns its length,
// or 0 on failure. Works with or without a Content-Length.
size_t download(const char* url, uint8_t** out, size_t maxLen, bool acceptGzip = false) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);                      // no chunked encoding: the body as it is
  http.setTimeout(12000);
  http.setUserAgent(photoUserAgent());
  if (!http.begin(client, url)) return 0;
  if (acceptGzip) http.addHeader("Accept-Encoding", "gzip");
  politeWait();
  size_t got = 0;
  if (http.GET() == 200) {
    int len = http.getSize();                  // -1 if the server didn't say
    size_t cap = len > 0 ? (size_t)len : maxLen;
    uint8_t* buf = cap <= maxLen ? (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM) : nullptr;
    WiFiClient* st = http.getStreamPtr();
    uint32_t t0 = millis();
    while (buf && got < cap && millis() - t0 < 20000) {
      int n = st->readBytes(buf + got, cap - got);
      if (n > 0) got += n;
      else if (len <= 0 && !st->connected() && !st->available()) break;   // end of the body
      else delay(5);
    }
    if (got && (len <= 0 || got == (size_t)len)) *out = buf;
    else { heap_caps_free(buf); got = 0; }
  }
  http.end();
  return got;
}

// gzip -> plain bytes in a new PSRAM buffer, using the inflater in the ESP32's ROM.
size_t gunzip(const uint8_t* in, size_t n, uint8_t** out, size_t maxOut) {
  if (n < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return 0;
  size_t pos = 10;
  uint8_t flg = in[3];
  if (flg & 4) pos += 2 + (in[pos] | in[pos + 1] << 8);          // extra field
  if (flg & 8) while (pos < n && in[pos++]) {}                    // file name
  if (flg & 16) while (pos < n && in[pos++]) {}                   // comment
  if (flg & 2) pos += 2;                                          // header CRC
  if (pos >= n) return 0;
  size_t size = in[n - 4] | in[n - 3] << 8 | in[n - 2] << 16 | (size_t)in[n - 1] << 24;   // (mod 2^32)
  if (!size || size > maxOut) return 0;
  uint8_t* buf = (uint8_t*)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
  tinfl_decompressor* d = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM);
  size_t inLen = n - pos - 8, outLen = size;
  bool ok = buf && d;
  if (ok) {
    tinfl_init(d);
    tinfl_status st = tinfl_decompress(d, in + pos, &inLen, buf, buf, &outLen,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    ok = st == TINFL_STATUS_DONE && outLen == size;
  }
  heap_caps_free(d);
  if (!ok) { heap_caps_free(buf); return 0; }
  buf[size] = 0;
  *out = buf;
  return size;
}
}  // namespace

// ---------------------------------------------------------------------------
//  Firmware updates: GitHub releases tagged build-<number>, each with a SkyTracker.bin
// ---------------------------------------------------------------------------
int netLatestFirmware(char* url, size_t urlLen, uint32_t* size, char* err, size_t errLen) {
  JsonDocument filter;
  filter["tag_name"] = true;
  JsonObject a = filter["assets"][0].to<JsonObject>();
  a["name"] = true;
  a["browser_download_url"] = true;
  a["size"] = true;
  JsonDocument doc(&psram);
  int status = getJson("https://api.github.com/repos/" UPDATE_REPO "/releases/latest", doc, filter, err, errLen);
  if (status != 200) {
    if (status == 404) snprintf(err, errLen, "%s", TR("ei julkaisuja", "no releases"));
    else if (status > 0) snprintf(err, errLen, "GitHub: HTTP %d", status);
    return 0;
  }
  const char* tag = doc["tag_name"] | "";
  if (strncmp(tag, "build-", 6) != 0 || atoi(tag + 6) <= 0) {
    snprintf(err, errLen, "%s %s", TR("outo julkaisu", "unexpected release"), tag);
    return 0;
  }
  for (JsonObject f : doc["assets"].as<JsonArray>()) {
    if (strcmp(f["name"] | "", "SkyTracker.bin") != 0) continue;
    copyStr(url, urlLen, f["browser_download_url"] | "");
    *size = f["size"] | 0u;
    if (url[0] && *size > 100000) return atoi(tag + 6);
  }
  snprintf(err, errLen, "%s", TR("julkaisusta puuttuu SkyTracker.bin", "release has no SkyTracker.bin"));
  return 0;
}

bool netInstallFirmware(const char* url, uint32_t size, void (*progress)(int pct), char* err, size_t errLen) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // GitHub sends the file from another host
  http.setUserAgent("SkyTracker desk display (personal, non-commercial)");
  if (!http.begin(client, url)) { snprintf(err, errLen, "%s", TR("virheellinen osoite", "invalid address")); return false; }
  int status = http.GET();
  if (status != 200) {
    snprintf(err, errLen, "%s %d", TR("lataus epäonnistui, HTTP", "download failed, HTTP"), status);
    http.end();
    return false;
  }
  int len = http.getSize();
  if (len <= 0 || (uint32_t)len != size) {
    snprintf(err, errLen, "%s", TR("väärän kokoinen tiedosto", "file has the wrong size"));
    http.end();
    return false;
  }
  if (!Update.begin(len, U_FLASH)) {
    snprintf(err, errLen, "%s", Update.errorString());
    http.end();
    return false;
  }
  static uint8_t buf[4096];
  WiFiClient* st = http.getStreamPtr();
  int done = 0, lastPct = -1;
  uint32_t lastData = millis();
  while (done < len && millis() - lastData < 20000) {
    esp_task_wdt_reset();
    int n = st->readBytes(buf, min((int)sizeof buf, len - done));
    if (n <= 0) { delay(5); continue; }
    lastData = millis();
    if (Update.write(buf, n) != (size_t)n) break;
    done += n;
    int pct = (int)((int64_t)done * 100 / len);
    if (pct != lastPct && progress) progress(lastPct = pct);
  }
  http.end();
  // end() checks the whole image (its SHA-256 included) before switching to it.
  if (done != len || !Update.end(true)) {
    snprintf(err, errLen, "%s", done != len ? TR("lataus katkesi", "download interrupted") : Update.errorString());
    Update.abort();
    return false;
  }
  return true;
}

void netFetchPhoto(void* lock) {
  static uint16_t* ready = nullptr;       // finished picture, copied in under the lock
  if (!ready) ready = (uint16_t*)heap_caps_malloc(PHOTO_W * PHOTO_H * 2, MALLOC_CAP_SPIRAM);
  char hex[8], reg[12];
  LOCK(lock);
  bool want = photo.state == PHOTO_LOADING;
  snprintf(hex, sizeof hex, "%s", photo.hex);
  snprintf(reg, sizeof reg, "%s", photo.reg);
  snprintf(userAgent, sizeof userAgent, "SkyTracker/1.0 (personal flight-tracker desk display; contact: %s)",
           cfg.contact);
  UNLOCK(lock);
  if (!want || !hex[0]) return;

  // 1. Ask Planespotters for the latest photo of this aircraft (by transponder code, then registration).
  JsonDocument filter;
  JsonObject f = filter["photos"][0].to<JsonObject>();
  f["thumbnail_large"]["src"] = true;
  f["photographer"] = true;
  f["link"] = true;
  JsonDocument doc(&psram);
  char url[128], err[48];
  bool found = false;
  for (int attempt = 0; attempt < 2 && !found; attempt++) {
    if (attempt == 0 && urlSafe(hex)) snprintf(url, sizeof url, "https://api.planespotters.net/pub/photos/hex/%s", hex);
    else if (attempt == 1 && urlSafe(reg)) snprintf(url, sizeof url, "https://api.planespotters.net/pub/photos/reg/%s", reg);
    else break;
    doc.clear();
    found = getJson(url, doc, filter, err, sizeof err, photoUserAgent()) == 200 &&
            doc["photos"][0]["thumbnail_large"]["src"].is<const char*>();
  }
  char src[200] = "", who[48] = "", link[160] = "";
  if (found) {
    copyStr(src, sizeof src, doc["photos"][0]["thumbnail_large"]["src"] | "");
    copyStr(who, sizeof who, doc["photos"][0]["photographer"] | "");
    copyStr(link, sizeof link, doc["photos"][0]["link"] | "");
    char* q = strchr(link, '?');           // drop tracking parameters: shorter QR code
    if (q) *q = 0;
    if (strlen(link) > 78) {               // too long for the QR code: keep just the photo number
      char* p = strstr(link, "/photo/");
      char* slash = p ? strchr(p + 7, '/') : nullptr;
      if (slash) *slash = 0;
    }
  }

  // 2. Download and decode the picture, then scale it to the card.
  bool ok = false;
  uint8_t* jpg = nullptr;
  size_t len = src[0] ? download(src, &jpg, 400000) : 0;
  int w = 0, h = 0;
  uint16_t* px = len ? decodeJpeg(jpg, len, &w, &h) : nullptr;
  if (px) {
    coverResize(px, w, h, ready);
    heap_caps_free(px);
    ok = true;
  }
  heap_caps_free(jpg);

  LOCK(lock);
  if (!strcmp(photo.hex, hex) && photo.state == PHOTO_LOADING) {   // still the same plane?
    if (ok) {
      if (!photo.pix) photo.pix = (uint16_t*)heap_caps_malloc(PHOTO_W * PHOTO_H * 2, MALLOC_CAP_SPIRAM);
      if (photo.pix) memcpy(photo.pix, ready, PHOTO_W * PHOTO_H * 2);
      snprintf(photo.photographer, sizeof photo.photographer, "%s", who);
      snprintf(photo.link, sizeof photo.link, "%s", link);
      photo.state = photo.pix ? PHOTO_READY : PHOTO_MISSING;
    } else {
      photo.state = PHOTO_MISSING;
    }
  }
  UNLOCK(lock);
  Serial.printf("Photo for %s: %s (%u bytes, %dx%d)\n", hex, ok ? "shown" : found ? "could not be shown" : "none",
                (unsigned)len, w, h);
}

// ---------------------------------------------------------------------------
//  The selected plane's flight path so far: its track history from adsb.lol
//  (open data, ODbL). If that isn't available the map shows a dashed line from
//  the departure airport instead.
// ---------------------------------------------------------------------------
void netFetchPath(void* lock) {
  char hex[8];
  LOCK(lock);
  bool want = flightPath.state == PATH_LOADING;
  snprintf(hex, sizeof hex, "%s", flightPath.hex);
  UNLOCK(lock);
  if (!want || !urlSafe(hex) || strlen(hex) < 2) return;
  for (char* c = hex; *c; c++) *c = tolower((uint8_t)*c);

  static FlightPath* tmp = (FlightPath*)heap_caps_malloc(sizeof(FlightPath), MALLOC_CAP_SPIRAM);
  static TracePt* pts = (TracePt*)heap_caps_malloc(sizeof(TracePt) * MAX_TRACE, MALLOC_CAP_SPIRAM);
  int np = 0, n = 0;
  double lastT = 0;
  for (const char* kind : {"full", "recent"}) {       // the whole day, then the last stretch
    if (!tmp || !pts) break;
    char url[96];
    snprintf(url, sizeof url, "https://adsb.lol/data/traces/%s/trace_%s_%s.json", hex + strlen(hex) - 2, kind, hex);
    uint8_t* raw = nullptr;
    size_t len = download(url, &raw, 1500000, true);
    if (len) {
      uint8_t* json = raw;
      size_t jlen = len;
      uint8_t* plain = nullptr;
      if (raw[0] == 0x1f && raw[1] == 0x8b) {        // gzip'ed: unpack it first
        jlen = gunzip(raw, len, &plain, 4000000);
        json = plain;
      }
      if (json && jlen) {
        JsonDocument filter;
        traceFilter(filter);
        JsonDocument doc(&psram);
        if (!deserializeJson(doc, (const char*)json, jlen, DeserializationOption::Filter(filter)))
          np = traceCollect(doc, pts, np, MAX_TRACE, &lastT);
      }
      heap_caps_free(plain);
    }
    heap_caps_free(raw);
    esp_task_wdt_reset();
  }
  if (np && tmp) n = traceToPath(pts, np, *tmp);

  LOCK(lock);
  if (!strcasecmp(flightPath.hex, hex) && flightPath.state == PATH_LOADING) {
    if (n >= 2) {
      memcpy(flightPath.pts, tmp->pts, sizeof(PathPoint) * n);
      flightPath.n = n;
      flightPath.state = PATH_READY;
    } else {
      flightPath.state = PATH_MISSING;
    }
    flightPath.triedMs = millis();
    flightPath.tries++;
  }
  UNLOCK(lock);
  Serial.printf("Flight path for %s: %d points (%d in the traces)\n", hex, n, np);
}
