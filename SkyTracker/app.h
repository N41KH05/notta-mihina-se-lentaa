// Touch gestures and what they do. Shared by the firmware and the PC/browser simulator,
// so both behave exactly the same.
#pragma once
#include <stdint.h>
#include "model.h"

struct TouchPt { int16_t x, y; };

enum EvType : uint8_t { EV_TAP, EV_DRAG, EV_DRAG_END, EV_ZOOM };
struct Ev { EvType type; int16_t x, y, dx, dy; };

// Turns raw touch points (polled ~60 times a second) into taps, drags and pinches.
class Gestures {
 public:
  // emit is called for each recognised gesture event.
  void update(const TouchPt* p, int n, uint32_t nowMs, void (*emit)(const Ev&));
 private:
  bool down = false, dragging = false, pinching = false;
  int16_t sx = 0, sy = 0, lx = 0, ly = 0;
  uint32_t t0 = 0;
  float pinchStart = 0;
};

// Actions. Call these with the state locked.
void appGoHome(AppState& s);
void appPan(AppState& s, int dxPx, int dyPx);          // map follows a finger moving by dx, dy
void appZoom(AppState& s, int delta);
// Setting home: show the map at a found place with a cross in the middle.
void appStartPickHome(AppState& s, const Place& p);
// Save the place under the cross as home (into cfg) and leave pick mode. The caller
// stores cfg in flash. cancel = leave without changing home.
void appEndPickHome(AppState& s, bool save);
// requestRoute(callsign) is called when a plane gets selected (may be null).
// Returns what was tapped (the caller opens Settings for HIT_SETTINGS).
int appTap(AppState& s, int x, int y, uint32_t nowMs, void (*requestRoute)(const char*));
