#pragma once
#include "model.h"

// Called with the state locked/unlocked by the caller as noted.
void netInit();
// Fetch planes for the current view and merge them in. Takes the lock itself.
void netFetchPlanes(AppState& s, void* lock);
// Resolve one pending route lookup, if any. Takes the lock itself.
void netLookupRoute(AppState& s, void* lock);
// Fetch the photo for the selected plane, if one was requested. Takes the lock itself.
void netFetchPhoto(void* lock);
// Look up departure/arrival times for a flight that asked for them (needs AIRLABS_KEY).
// Takes the lock itself.
void netLookupTimes(AppState& s, void* lock);
// AirLabs requests made today (for the status on the phone settings page).
int netAirlabsCallsToday();
// Address search (OpenStreetMap Nominatim). Returns how many were found, or -1 if the
// search failed (no connection etc.). Blocking: call from a background task.
int netSearchPlaces(const char* query, Place* out, int max);
