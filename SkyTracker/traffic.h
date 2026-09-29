// Reading the flight-data replies. Shared by the firmware (net.cpp) and the browser
// simulator, which fetches the same services from the browser.
#pragma once
#include <ArduinoJson.h>
#include "model.h"

// Aircraft lists in the readsb/tar1090 "v2" format (adsb.fi, airplanes.live, adsb.lol).
void trafficFilter(JsonDocument& filter);
// Read the aircraft from a reply into out[] (at most MAX_PLANES). Returns how many.
int trafficParse(JsonDocument& doc, Plane* out, uint32_t nowMs);
// Put a new list in place, keeping each plane's trail. `incoming` becomes the spare
// buffer for next time. Call with the state locked.
void trafficMerge(AppState& s, Plane*& incoming, int n);
// Query radius (nm, 5..250) that covers the current view.
int trafficRadiusNm(const AppState& s);

// Routes from adsbdb.com (/v0/callsign/...). Returns false if the reply has no route.
void routeFilter(JsonDocument& filter);
bool routeParse(JsonDocument& doc, Route& r);

// Address search results from OpenStreetMap Nominatim (/search?format=jsonv2).
void nominatimFilter(JsonDocument& filter);
int nominatimParse(JsonDocument& doc, Place* out, int max);
