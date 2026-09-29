#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include "render.h"
#include "photo.h"

void Gestures::update(const TouchPt* p, int n, uint32_t now, void (*emit)(const Ev&)) {
  if (n >= 2) {                                        // two fingers: pinch to zoom
    float d = hypotf(p[0].x - p[1].x, p[0].y - p[1].y);
    if (!pinching) {
      pinching = true;
      pinchStart = d;
      if (dragging) emit(Ev{EV_DRAG_END, 0, 0, 0, 0});
      dragging = false;
    } else if (pinchStart > 20 && (d > pinchStart * 1.45f || d < pinchStart / 1.45f)) {
      emit(Ev{EV_ZOOM, 0, 0, (int16_t)(d > pinchStart ? 1 : -1), 0});
      pinchStart = d;
    }
    down = true;
  } else if (n == 1) {
    if (!down) {
      down = true; dragging = false; pinching = false;
      sx = lx = p[0].x; sy = ly = p[0].y;
      t0 = now;
    } else if (!pinching) {
      if (!dragging && (abs(p[0].x - sx) > 10 || abs(p[0].y - sy) > 10) && sx < MAP_W) dragging = true;
      if (dragging && (p[0].x != lx || p[0].y != ly))
        emit(Ev{EV_DRAG, p[0].x, p[0].y, (int16_t)(p[0].x - lx), (int16_t)(p[0].y - ly)});
    }
    lx = p[0].x; ly = p[0].y;
  } else if (down) {                                   // finger lifted
    if (dragging) emit(Ev{EV_DRAG_END, 0, 0, 0, 0});
    else if (!pinching && now - t0 < 600) emit(Ev{EV_TAP, sx, sy, 0, 0});
    down = dragging = pinching = false;
  }
}

void appGoHome(AppState& s) {
  s.cx = mercX(cfg.homeLon);
  s.cy = mercY(cfg.homeLat);
  s.zoom = START_ZOOM;
  s.selHex[0] = 0;
  s.follow = false;
}

void appStartPickHome(AppState& s, const Place& p) {
  s.pickHome = true;
  snprintf(s.pickName, sizeof s.pickName, "%s", p.name);
  s.cx = s.pickX = mercX(p.lon);
  s.cy = s.pickY = mercY(p.lat);
  s.zoom = 10;
  s.selHex[0] = 0;
  s.follow = false;
}

void appEndPickHome(AppState& s, bool save) {
  if (save) {
    cfg.homeLat = latFromY(s.cy);
    cfg.homeLon = lonFromX(s.cx);
  }
  s.pickHome = false;
  appGoHome(s);
}

void appPan(AppState& s, int dx, int dy) {
  float m = metresPerPx(s.zoom);
  s.cx = fmaxf(-2.0e7f, fminf(2.0e7f, s.cx - dx * m));
  s.cy = fmaxf(-1.6e7f, fminf(1.6e7f, s.cy + dy * m));
  s.follow = false;
}

void appZoom(AppState& s, int d) {
  s.zoom += d;
  if (s.zoom < MIN_ZOOM) s.zoom = MIN_ZOOM;
  if (s.zoom > MAX_ZOOM) s.zoom = MAX_ZOOM;
}

static void selectPlane(AppState& s, const Plane& p, void (*requestRoute)(const char*)) {
  snprintf(s.selHex, sizeof s.selHex, "%s", p.hex);
  if (requestRoute) requestRoute(p.cs);
}

int appTap(AppState& s, int x, int y, uint32_t nowMs, void (*requestRoute)(const char*)) {
  int row = -1;
  UiHit hit = uiHitTest(x, y, s, &row);
  if (s.pickHome) {                         // setting home: only moving the map (the caller saves)
    if (hit == HIT_ZOOM_IN) appZoom(s, 1);
    else if (hit == HIT_ZOOM_OUT) appZoom(s, -1);
    else if (hit == HIT_HOME) { s.cx = s.pickX; s.cy = s.pickY; }
    return hit;
  }
  switch (hit) {
    case HIT_ZOOM_IN:  appZoom(s, 1); break;
    case HIT_ZOOM_OUT: appZoom(s, -1); break;
    case HIT_HOME:     appGoHome(s); break;
    case HIT_FOLLOW:   s.follow = !s.follow; break;
    case HIT_CLOSE:    s.selHex[0] = 0; s.follow = false; break;
    case HIT_PHOTO:    photo.hidden = true; break;          // tap the photo card to hide it
    case HIT_LIST_ROW: {
      const char* hex = listRowHex(row);
      Plane* p = hex ? s.find(hex) : nullptr;
      if (p) selectPlane(s, *p, requestRoute);
      break;
    }
    case HIT_MAP: {
      s.advance(nowMs);
      int i = planeAt(s, x, y, 30);
      if (i >= 0) selectPlane(s, s.planes[i], requestRoute);
      else if (s.selHex[0]) { s.selHex[0] = 0; s.follow = false; }   // empty map: back to list
      break;
    }
    default: break;
  }
  return hit;
}
