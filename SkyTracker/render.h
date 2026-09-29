// Draws the 800x480 picture onto any Adafruit_GFX surface: the LCD frame buffer
// on the ESP32, or an image on a computer for testing.
//
// The map background (land, water, towns, airports...) only changes when the view
// moves, so it is drawn separately and reused; planes and the side panel are drawn
// on top of a copy of it every frame.
#pragma once
#include <Adafruit_GFX.h>
#include <time.h>
#include "model.h"

void* renderAlloc(size_t bytes);                 // big scratch buffers (PSRAM on the ESP32)

void renderBase(Adafruit_GFX& g, float cx, float cy, int zoom);
void renderOverlay(Adafruit_GFX& g, AppState& s, uint32_t nowMs, const struct tm* now,
                   const struct tm* updated);
void renderMessage(Adafruit_GFX& g, const char* big, const char* small);

// What is under a touch at (x, y)?
enum UiHit { HIT_NONE, HIT_MAP, HIT_ZOOM_IN, HIT_ZOOM_OUT, HIT_HOME, HIT_FOLLOW, HIT_CLOSE,
             HIT_LIST_ROW, HIT_PANEL, HIT_SETTINGS, HIT_PHOTO,
             HIT_PICK_SAVE, HIT_PICK_CANCEL };           // setting home (AppState::pickHome)
UiHit uiHitTest(int x, int y, const AppState& s, int* row);
const char* listRowHex(int row);                 // plane shown in that panel row last frame

// Nearest plane to a screen point, within maxPx. Returns index or -1.
int planeAt(AppState& s, int x, int y, int maxPx);

double haversineKm(double lat1, double lon1, double lat2, double lon2);
const char* typeName(const char* icaoType);
// QR code for a link (up to 78 characters), m pixels per module, top-left at x, y.
// Returns its width/height in pixels (0 if the link is too long).
int drawQr(Adafruit_GFX& g, int x, int y, const char* link, int m = 2);
