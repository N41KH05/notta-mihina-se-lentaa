// Demo traffic: simulated planes around home, for trying the device without internet
// (and for the browser simulator).
#pragma once
#include "model.h"

void demoInit(AppState& s, uint32_t nowMs);
// clockMin: local time as minutes past midnight (used for the simulated timetables).
void demoStep(AppState& s, uint32_t nowMs, int clockMin = -1);
void demoRelabel(AppState& s);   // the language changed: re-name the simulated routes' cities
// A made-up flight path for a demo plane (the board fetches real ones for live planes).
void demoPath(AppState& s, const Plane& p, FlightPath& out);
