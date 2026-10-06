// Full-screen menus: Settings and the on-screen Wi-Fi setup (network list,
// touch keyboard, connecting). Shared by the firmware and the simulator.
#pragma once
#include <Adafruit_GFX.h>
#include "model.h"

struct WifiNet { char ssid[33]; int8_t rssi; bool secure; };

// What the menus need from the device (the simulator provides simulated versions).
struct WifiHooks {
  void (*startScan)();
  int (*scanResults)(WifiNet* out, int max);          // -1 while still searching
  void (*connect)(const char* ssid, const char* pass);
  int (*connectStatus)();                             // 0 trying, 1 connected, -1 failed
  // Current connection: ssid "" when not connected.
  void (*current)(char* ssid, size_t n, char* ip, size_t ipn, int* rssi);
  void (*connected)(const char* ssid, const char* pass);  // success: save it, go live
  void (*forget)();                                   // delete the saved network
  void (*useDemo)();                                  // switch to simulated planes
  void (*saveSettings)();                             // units changed: store them
  // Setting home
  void (*placeSearch)(const char* query);             // start an address search
  int (*placeResults)(Place* out, int max);           // -1 still searching, -2 failed
  void (*placeChosen)(const Place& p);                // show it on the map to fine-tune
  bool (*needHome)();                                 // ask for home after the first Wi-Fi setup?
  void (*homeSkipped)();                              // "Ohita": keep the default, don't ask again
  void (*wakeNet)();                                  // the background task: look at fwUpdate now
  // Finding a flight (the magnifier on the map)
  void (*flightSearch)(const char* query);            // start a search
  int (*flightResults)(Plane* out, int max);          // -1 still searching, -2 failed
  void (*flightChosen)(const Plane& p);               // select it and follow it
};

void uiInit(const WifiHooks* hooks);
bool uiActive();                                      // a menu is covering the map
void uiOpenSettings();
void uiOpenHome(bool firstTime);                      // "Missä koti on?" address search
void uiOpenWifi(const char* note, bool firstRun, bool alert);  // note: shown above the list (red if alert)
void uiOpenUpdate();                                  // "a new version is available": install / later / skip
void uiOpenFlightSearch();                            // the magnifier: type a flight to find
void uiOpenLogbook();                                 // the book: today's and all-time counts
void uiClose();
void uiTap(int x, int y, uint32_t nowMs, AppState& s);
void uiTick(uint32_t nowMs);                          // call often: scans, connecting
void uiRender(Adafruit_GFX& g, AppState& s, uint32_t nowMs);
bool uiBusy();                                        // typing or connecting (don't time out)

// ============================================================================
//  Touch gestures and map actions
// ============================================================================
// Touch gestures and what they do. Shared by the firmware and the PC/browser simulator,
// so both behave exactly the same.
struct TouchPt { int16_t x, y; };

enum EvType : uint8_t { EV_TAP, EV_DRAG, EV_DRAG_END, EV_ZOOM, EV_SCROLL };   // scroll: dy on the side panel
struct Ev { EvType type; int16_t x, y, dx, dy; };

// Turns raw touch points (polled ~60 times a second) into taps, drags and pinches.
class Gestures {
 public:
  // emit is called for each recognised gesture event.
  void update(const TouchPt* p, int n, uint32_t nowMs, void (*emit)(const Ev&));
 private:
  bool down = false, dragging = false, scrolling = false, pinching = false;
  int16_t sx = 0, sy = 0, lx = 0, ly = 0;
  uint32_t t0 = 0;
  float pinchStart = 0;
};

// Actions. Call these with the state locked.
void appGoHome(AppState& s);
void appPan(AppState& s, int dxPx, int dyPx);          // map follows a finger moving by dx, dy
void appScroll(AppState& s, int dyPx);                  // the side panel's plane list follows a finger
void appZoom(AppState& s, int delta);
// Setting home: show the map at a found place with a cross in the middle.
void appStartPickHome(AppState& s, const Place& p);
// Save the place under the cross as home (into cfg) and leave pick mode. The caller
// stores cfg in flash. cancel = leave without changing home.
void appEndPickHome(AppState& s, bool save);
// requestRoute(plane) is called when a plane gets selected (may be null).
// Returns what was tapped (the caller opens Settings for HIT_SETTINGS).
int appTap(AppState& s, int x, int y, uint32_t nowMs, void (*requestRoute)(const Plane&));

// Finding a flight. normalizeFlight: upper case, no spaces (AY 1431 -> AY1431).
void normalizeFlight(const char* in, char* out, size_t n);
// Planes already on the map matching the query: callsign, registration (with or without
// the dash) or flight number (AY1431, once its route is known). Exact matches first, then
// ones starting with it (FIN: every Finnair plane in view). *exact: was any exact.
int appFindFlights(AppState& s, const char* query, Plane* out, int max, bool* exact);
// Show a found plane: put it on the map if it isn't there yet, select it and follow it.
void appShowFlight(AppState& s, const Plane& p, void (*requestRoute)(const Plane&));

// Search the towns built into the map (setting home without internet). Names match from
// the start of the name or of any word, ignoring case. Returns how many were found.
int placeSearchOffline(const char* query, Place* out, int max);
