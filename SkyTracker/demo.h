#pragma once
#include "model.h"
void demoInit(AppState& s, uint32_t nowMs);
// clockMin: local time as minutes past midnight (used for the simulated timetables).
void demoStep(AppState& s, uint32_t nowMs, int clockMin = -1);
void demoRelabel(AppState& s);   // the language changed: re-name the simulated routes' cities
