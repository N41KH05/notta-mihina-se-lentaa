#include "traffic.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>

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
                          "lat", "lon", "seen_pos", "squawk"})
      f[k] = true;
  }
}

int trafficParse(JsonDocument& doc, Plane* out, uint32_t now) {
  JsonArray list = doc["ac"].is<JsonArray>() ? doc["ac"].as<JsonArray>() : doc["aircraft"].as<JsonArray>();
  int n = 0;
  for (JsonObject a : list) {
    if (n >= MAX_PLANES) break;
    if (!a["lat"].is<float>() || !a["lon"].is<float>()) continue;
    if ((a["seen_pos"] | 0.0f) > 60) continue;
    bool ground = a["alt_baro"].is<const char*>();
    if (ground && HIDE_ON_GROUND) continue;
    Plane& p = out[n];
    memset(&p, 0, sizeof(Plane));
    copyStr(p.hex, sizeof p.hex, a["hex"] | "");
    copyStr(p.cs, sizeof p.cs, a["flight"] | "");
    copyStr(p.reg, sizeof p.reg, a["r"] | "");
    copyStr(p.type, sizeof p.type, a["t"] | "");
    copyStr(p.squawk, sizeof p.squawk, a["squawk"] | "");
    p.lat = a["lat"].as<double>();
    p.lon = a["lon"].as<double>();
    p.fx = p.x = mercX(p.lon);
    p.fy = p.y = mercY(p.lat);
    p.tMs = now - (uint32_t)((a["seen_pos"] | 0.0f) * 1000);
    p.hasAlt = ground || a["alt_baro"].is<float>();
    p.alt = ground ? 0 : (int32_t)(a["alt_baro"] | 0.0f);
    p.hasGs = a["gs"].is<float>();
    p.gs = a["gs"] | 0.0f;
    p.hasTrack = a["track"].is<float>();
    p.track = a["track"] | 0.0f;
    p.hasVrate = a["baro_rate"].is<float>();
    p.vrate = (int32_t)(a["baro_rate"] | 0.0f);
    n++;
  }
  return n;
}

void trafficMerge(AppState& s, Plane*& incoming, int n) {
  for (int i = 0; i < n; i++) {
    Plane& p = incoming[i];
    Plane* old = s.find(p.hex);
    if (old) {
      p.trailN = old->trailN;
      p.trailHead = old->trailHead;
      memcpy(p.trail, old->trail, sizeof p.trail);
    }
    p.addTrail(p.fx, p.fy);
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
  for (const char* end : {"origin", "destination"}) {
    fr[end]["iata_code"] = true;
    fr[end]["icao_code"] = true;
    fr[end]["municipality"] = true;
    fr[end]["latitude"] = true;
    fr[end]["longitude"] = true;
  }
}

bool routeParse(JsonDocument& doc, Route& r) {
  JsonObject route = doc["response"]["flightroute"];
  if (route.isNull()) return false;
  copyStr(r.flight, sizeof r.flight, route["callsign_iata"] | "");
  copyStr(r.airline, sizeof r.airline, route["airline"]["name"] | "");
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
  return true;
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
