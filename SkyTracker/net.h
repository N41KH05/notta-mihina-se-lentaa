#pragma once
#include "model.h"

// Called with the state locked/unlocked by the caller as noted.
void netInit();
// Fetch planes for the current view and merge them in. Takes the lock itself.
void netFetchPlanes(AppState& s, void* lock);
// Resolve one pending route lookup, if any. Takes the lock itself.
void netLookupRoute(AppState& s, void* lock);
// Firmware updates from GitHub releases (UPDATE_REPO). netLatestFirmware returns the newest
// build number (0 on failure, with err set) and the download address of its SkyTracker.bin.
int netLatestFirmware(char* url, size_t urlLen, uint32_t* size, char* notes, size_t notesLen,
                      char* err, size_t errLen);
// Downloads it into the other app slot; true when it's ready to start (then restart).
bool netInstallFirmware(const char* url, uint32_t size, void (*progress)(int pct), char* err, size_t errLen);
// Fetch the photo for the selected plane, if one was requested. Takes the lock itself.
void netFetchPhoto(void* lock);
// Fetch the selected plane's flight path, if one was requested. Takes the lock itself.
void netFetchPath(void* lock);
// Look up departure/arrival times for a flight that asked for them (needs AIRLABS_KEY).
// Takes the lock itself.
void netLookupTimes(AppState& s, void* lock);
// AirLabs requests made today (for the status on the phone settings page).
int netAirlabsCallsToday();
// Address search (OpenStreetMap Nominatim). Returns how many were found, or -1 if the
// search failed (no connection etc.). Blocking: call from a background task.
int netSearchPlaces(const char* query, Place* out, int max);

// ============================================================================
//  Settings page for a phone (web.cpp)
// ============================================================================
// The settings page for a phone or computer on the same Wi-Fi (http://<address>/).
// The address is shown on the device under Asetukset.
void webInit(AppState* state, void* lock);
void webLoop();          // call often from the main loop; starts once Wi-Fi is up
