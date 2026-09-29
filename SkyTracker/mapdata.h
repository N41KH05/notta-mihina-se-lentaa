// Map data layout (the data itself is generated into mapdata.cpp by tools/make_map.py).
#pragma once
#include <stdint.h>

struct MapShape {          // one ring (filled area) or polyline
  uint32_t start;          // where its points start in the layer's _PTS bytes (see MapPoints)
  uint32_t count;          // number of points
  int16_t x0, y0, x1, y1;  // bounding box in layer units
  uint8_t flags;           // fills: 1 = lake (outline it when zoomed in)
};
struct MapPlace { int32_t x, y; uint8_t minz10; uint8_t big; uint32_t name; };
struct MapAirport { int32_t x, y; char iata[4]; char icao[5]; uint8_t big; uint32_t name; };
struct MapRunway { int32_t x1, y1, x2, y2; };   // web-mercator metres
// Country, sea and lake names. kind: 0 = country, 1 = water. Shown between zooms.
struct MapLabel { int32_t x, y; uint8_t kind, minz10, maxz10; uint32_t name; };

extern const double MAP_HOME_LAT, MAP_HOME_LON;
// Region layer: point = REG_O + value * REG_UNIT. World layer: point = value * WLD_UNIT.
extern const float REG_OX, REG_OY, REG_UNIT, WLD_UNIT;
extern const uint8_t REG_PTS[], WLD_PTS[];
// Region layer at three levels of detail (fine: zoom 9-11, mid: 7-8, coarse: further out).
extern const MapShape REG_FILL_FINE[], REG_FILL_MID[], REG_FILL_COARSE[], REG_COAST_FINE[], REG_COAST_MID[],
    REG_COAST_COARSE[], REG_BORDER_FINE[], REG_BORDER_MID[], REG_BORDER_COARSE[], WLD_FILL[], WLD_COAST[], WLD_BORDER[];
// Grid index of the region layer: REG_GRID x REG_GRID cells over the int16 coordinate
// range. For cell c, the shapes touching it are _IDX[_CELLS[c]] .. _IDX[_CELLS[c + 1] - 1].
extern const int REG_GRID;
extern const uint32_t REG_FILL_FINE_CELLS[], REG_FILL_MID_CELLS[], REG_FILL_COARSE_CELLS[], REG_COAST_FINE_CELLS[],
    REG_COAST_MID_CELLS[], REG_COAST_COARSE_CELLS[], REG_BORDER_FINE_CELLS[], REG_BORDER_MID_CELLS[],
    REG_BORDER_COARSE_CELLS[];
extern const uint32_t REG_FILL_FINE_IDX[], REG_FILL_MID_IDX[], REG_FILL_COARSE_IDX[], REG_COAST_FINE_IDX[],
    REG_COAST_MID_IDX[], REG_COAST_COARSE_IDX[], REG_BORDER_FINE_IDX[], REG_BORDER_MID_IDX[],
    REG_BORDER_COARSE_IDX[];
extern const uint32_t REG_FILL_FINE_N, REG_FILL_MID_N, REG_FILL_COARSE_N, REG_COAST_FINE_N, REG_COAST_MID_N,
    REG_COAST_COARSE_N, REG_BORDER_FINE_N, REG_BORDER_MID_N, REG_BORDER_COARSE_N, WLD_FILL_N, WLD_COAST_N, WLD_BORDER_N;
extern const MapPlace PLACES[];
extern const uint32_t PLACES_N;
extern const MapAirport AIRPORTS[];
extern const uint32_t AIRPORTS_N;
extern const MapRunway RUNWAYS[];
extern const uint32_t RUNWAYS_N;
extern const MapLabel MAP_LABELS[];
extern const uint32_t MAP_LABELS_N;
extern const char MAP_NAMES[];

// MAP_NAMES holds each name as "Finnish\0English\0"; the English part is empty when
// both are the same. mapName() returns the one for the chosen language.
inline const char* mapNameFi(uint32_t off) { return MAP_NAMES + off; }
inline const char* mapNameEn(uint32_t off) {
  const char* fi = MAP_NAMES + off;
  const char* en = fi;
  while (*en) en++;
  en++;
  return *en ? en : fi;
}
inline const char* mapName(uint32_t off, bool english) { return english ? mapNameEn(off) : mapNameFi(off); }

// Reads a shape's points in order. The first point is stored as two int16, each
// following one as the step from the previous point: two int8, or 0x80 and two int16
// for a long step. About half the size of storing every point in full.
struct MapPoints {
  const uint8_t* p;
  int x, y;                                  // current point, in layer units
  explicit MapPoints(const uint8_t* s)
      : p(s + 4), x((int16_t)(s[0] | s[1] << 8)), y((int16_t)(s[2] | s[3] << 8)) {}
  void next() {
    if (p[0] == 0x80) {
      x = (int16_t)(x + (p[1] | p[2] << 8));   // 16-bit wrap-around, as stored
      y = (int16_t)(y + (p[3] | p[4] << 8));
      p += 5;
    } else {
      x += (int8_t)p[0];
      y += (int8_t)p[1];
      p += 2;
    }
  }
};
