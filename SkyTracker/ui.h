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
};

void uiInit(const WifiHooks* hooks);
bool uiActive();                                      // a menu is covering the map
void uiOpenSettings();
void uiOpenHome(bool firstTime);                      // "Missä koti on?" address search
void uiOpenWifi(const char* note, bool firstRun, bool alert);  // note: shown above the list (red if alert)
void uiClose();
void uiTap(int x, int y, uint32_t nowMs, AppState& s);
void uiTick(uint32_t nowMs);                          // call often: scans, connecting
void uiRender(Adafruit_GFX& g, AppState& s, uint32_t nowMs);
bool uiBusy();                                        // typing or connecting (don't time out)
