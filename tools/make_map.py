#!/usr/bin/env python3
"""
Bake the map into the firmware: writes SkyTracker/mapdata.cpp.

  * a detailed region around home (coastlines, lakes, borders, towns,
    airports, runways) for zooming in, and
  * a simple whole-world layer for zooming right out.

Usage:  python make_map.py 60.1699 24.9384      (lat lon of home)
Data:   Natural Earth 1:10m and OurAirports, downloaded from GitHub once.
"""
import csv
import json
import math
import os
import sys
import urllib.request

import numpy as np

from geo import clip_ring, decimate, split_line, to_merc
import finnish_names

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "raw")
OUT = os.path.join(HERE, "..", "SkyTracker", "mapdata.cpp")

NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/"
OA = "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/"
FILES = {
    "land": NE + "ne_10m_land.geojson", "islands": NE + "ne_10m_minor_islands.geojson",
    "lakes": NE + "ne_10m_lakes.geojson", "coast": NE + "ne_10m_coastline.geojson",
    "borders": NE + "ne_10m_admin_0_boundary_lines_land.geojson",
    "places": NE + "ne_10m_populated_places_simple.geojson",
    "airports": OA + "airports.csv", "runways": OA + "runways.csv",
    "countries": NE + "ne_10m_admin_0_countries.geojson",
}

REGION_KM = 1000                 # detailed area: this far around home
REGION_UNIT = 80                 # metres per coordinate step in the region layer
WORLD_UNIT = 612                 # metres per step in the world layer
WORLD_M = 20037508.34
LODS = {"fine": 90.0, "coarse": 2500.0}
WORLD_DECIMATE = 15000.0
TILE = 600_000


def fetch(name):
    os.makedirs(RAW, exist_ok=True)
    url = FILES[name]
    path = os.path.join(RAW, os.path.basename(url))
    if not os.path.exists(path):
        print("  downloading", os.path.basename(url), flush=True)
        req = urllib.request.Request(url, headers={"User-Agent": "SkyTracker map builder"})
        with urllib.request.urlopen(req, timeout=180) as r, open(path + ".part", "wb") as f:
            f.write(r.read())
        os.replace(path + ".part", path)
    return path


def features(name):
    with open(fetch(name), encoding="utf-8") as f:
        return json.load(f)["features"]


def proj(coords):
    a = np.asarray(coords, dtype=np.float64)[:, :2]
    x, y = to_merc(a[:, 0], a[:, 1])
    return np.column_stack([x, y])


def rings(feats):
    for f in feats:
        g = f.get("geometry") or {}
        if g.get("type") == "Polygon":
            polys = [g["coordinates"]]
        elif g.get("type") == "MultiPolygon":
            polys = g["coordinates"]
        else:
            continue
        for poly in polys:
            for i, ring in enumerate(poly):
                if len(ring) >= 4:
                    yield proj(ring), i == 0


def lines(feats):
    for f in feats:
        g = f.get("geometry") or {}
        parts = ([g["coordinates"]] if g.get("type") == "LineString"
                 else g.get("coordinates", []) if g.get("type") == "MultiLineString" else [])
        for p in parts:
            if len(p) >= 2:
                yield proj(p)


def tiles(ring):
    x0, y0 = ring.min(axis=0)
    x1, y1 = ring.max(axis=0)
    if x1 - x0 <= TILE and y1 - y0 <= TILE:
        return [ring]
    out = []
    for tx in np.arange(math.floor(x0 / TILE) * TILE, x1, TILE):
        for ty in np.arange(math.floor(y0 / TILE) * TILE, y1, TILE):
            piece = clip_ring(ring, tx, ty, tx + TILE, ty + TILE)
            if len(piece) >= 4:
                out.append(piece)
    return out


class Layer:
    """Shapes stored as int16 points relative to an origin, with bounding boxes."""

    def __init__(self, ox, oy, unit):
        self.ox, self.oy, self.unit = ox, oy, unit
        self.pts = []
        self.groups = {}

    def q(self, arr):
        q = np.round((arr - (self.ox, self.oy)) / self.unit).astype(np.int64)
        q = np.clip(q, -32768, 32767).astype(np.int16)
        # drop repeated points created by rounding
        keep = np.ones(len(q), bool)
        keep[1:] = np.any(q[1:] != q[:-1], axis=1)
        return q[keep]

    def add(self, group, arr, flags=0, closed=False):
        q = self.q(arr)
        if len(q) < (4 if closed else 2):
            return
        start = sum(len(p) for p in self.pts)
        self.pts.append(q)
        b = (q[:, 0].min(), q[:, 1].min(), q[:, 0].max(), q[:, 1].max())
        self.groups.setdefault(group, []).append((start, len(q), *b, flags))


def main():
    if len(sys.argv) >= 3:
        lat, lon = float(sys.argv[1]), float(sys.argv[2])
    else:
        lat, lon = 60.1699, 24.9384
    hx, hy = map(float, to_merc(lon, lat))
    ext = REGION_KM * 1000 / math.cos(math.radians(lat))
    unit = max(REGION_UNIT, math.ceil(2 * ext / 65000))
    box = (hx - ext, hy - ext, hx + ext, hy + ext)
    reg = Layer(hx, hy, unit)
    wld = Layer(0.0, 0.0, WORLD_UNIT)
    wbox = (-WORLD_M, -WORLD_M * 0.999, WORLD_M, WORLD_M * 0.999)
    print(f"Map around {lat:.4f}, {lon:.4f}: detailed within {REGION_KM} km, plus the world")

    print("Land and lakes...")
    land = features("land") + features("islands")
    for ring, outer in rings(land):
        c = clip_ring(ring, *box)
        if len(c) >= 4:
            for lod, d in LODS.items():
                dd = decimate(c, d, closed=True)
                if len(dd) >= 4:
                    for t in (tiles(dd) if lod == "fine" else [dd]):
                        reg.add("fill_" + lod, t, 0, closed=True)
        w = decimate(clip_ring(ring, *wbox), WORLD_DECIMATE, closed=True)
        if len(w) >= 4:
            wld.add("fill", w, 0, closed=True)
    for ring, outer in rings(features("lakes")):
        c = clip_ring(ring, *box)
        if len(c) >= 4:
            for lod, d in LODS.items():
                dd = decimate(c, d, closed=True)
                if len(dd) >= 4:
                    reg.add("fill_" + lod, dd, 1, closed=True)   # flag 1: lake outline
        w = decimate(clip_ring(ring, *wbox), WORLD_DECIMATE, closed=True)
        if len(w) >= 4 and (w.max(0) - w.min(0)).max() > 60000:
            wld.add("fill", w, 1, closed=True)

    print("Coastlines and borders...")
    isl = [{"geometry": {"type": "LineString", "coordinates": r}}
           for f in features("islands")
           for poly in ([f["geometry"]["coordinates"]] if f["geometry"]["type"] == "Polygon"
                        else f["geometry"]["coordinates"]) for r in poly[:1]]
    for name, feats in (("coast", features("coast") + isl), ("border", features("borders"))):
        for ln in lines(feats):
            for run in split_line(ln, *box):
                for lod, d in LODS.items():
                    dd = decimate(run, d)
                    for i in range(0, len(dd) - 1, 300):
                        reg.add(f"{name}_{lod}", dd[i:i + 301])
            w = decimate(ln, WORLD_DECIMATE)
            if len(w) >= 2:
                wld.add(name, w)

    print("Towns, airports, runways...")
    places = []
    for f in features("places"):
        p = f["properties"]
        x, y = map(float, to_merc(p["longitude"], p["latitude"]))
        mz = float(p.get("min_zoom") or 10)
        inside = box[0] <= x <= box[2] and box[1] <= y <= box[3]
        if inside or mz <= 5:
            places.append((int(x), int(y), int(round(mz * 10)), int(p.get("pop_max") or 0),
                           (screen_name(p["name"], p.get("nameascii"))[:24],
                            fold(p["name"] or p.get("nameascii") or "")[:24])))
    places.sort(key=lambda p: -p[3])

    print("Countries, seas and lakes (Finnish and English names)...")
    labels = []    # (x, y, kind 0 country / 1 water, min zoom*10, max zoom*10, name)
    for f in features("countries"):
        p = f["properties"]
        if p.get("LABEL_X") is None:
            continue
        iso = p.get("ISO_A2_EH") if p.get("ISO_A2_EH") not in (None, "-99") else p.get("ISO_A2")
        if iso in (None, "-99") and p.get("NAME") not in finnish_names.COUNTRY_BY_ENGLISH:
            continue                          # disputed zones, reefs, bases...
        name = finnish_country(p).split(" (")[0]
        if len(name) > 24:
            continue                          # long names of tiny territories
        x, y = map(float, to_merc(p["LABEL_X"], p["LABEL_Y"]))
        name = fold(name).upper()
        english = fold(p.get("NAME_EN") or p.get("NAME") or "").split(" (")[0].upper()
        english = ENGLISH_SHORT.get(english, english)
        labels.append((int(x), int(y), 0, int(round(float(p.get("MIN_LABEL") or 5) * 10)),
                       int(round(float(p.get("MAX_LABEL") or 9) * 10)), (name[:28], english[:28])))
    for name, english, la, lo, zmin, zmax in finnish_names.WATERS:
        x, y = map(float, to_merc(lo, la))
        labels.append((int(x), int(y), 1, zmin * 10, zmax * 10, (fold(name), english)))

    airports, ids = [], {}
    with open(fetch("airports"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            t = r["type"]
            if t not in ("large_airport", "medium_airport", "small_airport"):
                continue
            if t == "small_airport" and r["scheduled_service"] != "yes":
                continue
            try:
                x, y = map(float, to_merc(float(r["longitude_deg"]), float(r["latitude_deg"])))
            except ValueError:
                continue
            if box[0] <= x <= box[2] and box[1] <= y <= box[3]:
                ids[r["id"]] = True
                def short(n):
                    return n.replace(" Airport", "").replace(" International", "")[:26]
                airports.append((int(x), int(y), r["iata_code"][:3], (r["icao_code"] or r["ident"])[:4],
                                 1 if t == "large_airport" else 0,
                                 (short(screen_name(r["name"])), short(fold(r["name"])))))
    runways = []
    with open(fetch("runways"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["airport_ref"] not in ids or r["closed"] == "1":
                continue
            try:
                x1, y1 = to_merc(float(r["le_longitude_deg"]), float(r["le_latitude_deg"]))
                x2, y2 = to_merc(float(r["he_longitude_deg"]), float(r["he_latitude_deg"]))
            except ValueError:
                continue
            runways.append(tuple(int(v) for v in (x1, y1, x2, y2)))

    write(reg, wld, places, airports, runways, labels, lat, lon)


# Shorter English country names for the map.
ENGLISH_SHORT = {
    "PEOPLE'S REPUBLIC OF CHINA": "CHINA", "UNITED STATES OF AMERICA": "UNITED STATES",
    "DEMOCRATIC REPUBLIC OF THE CONGO": "DR CONGO", "REPUBLIC OF THE CONGO": "CONGO",
}

# Letters the screen fonts can show besides plain ASCII.
FONT_EXTRA = set("äöåÄÖÅšŠžŽ")


def fold(text):
    """Keep letters the fonts have; turn others into their plain form (é -> e)."""
    import unicodedata
    out = []
    for c in text:
        if ord(c) < 128 or c in FONT_EXTRA:
            out.append(c)
        else:
            out.append(unicodedata.normalize("NFKD", c).encode("ascii", "ignore").decode()
                       or {"ø": "o", "Ø": "O", "æ": "ae", "Æ": "Ae", "ß": "ss", "ł": "l", "Ł": "L",
                           "đ": "d", "ı": "i"}.get(c, ""))
    return " ".join("".join(out).split())


def screen_name(name, ascii_name=None):
    """Finnish name if there is one (Tukholma, Pietari), else the local spelling."""
    for key in (name, ascii_name):
        if key and key in finnish_names.PLACES:
            return finnish_names.PLACES[key]
    return fold(name or ascii_name or "")


def finnish_country(props):
    iso = props.get("ISO_A2_EH") if props.get("ISO_A2_EH") not in (None, "-99") else props.get("ISO_A2")
    if iso in finnish_names.COUNTRY_OVERRIDES:
        return finnish_names.COUNTRY_OVERRIDES[iso]
    if props.get("NAME") in finnish_names.COUNTRY_BY_ENGLISH:
        return finnish_names.COUNTRY_BY_ENGLISH[props["NAME"]]
    try:
        import gettext
        import pycountry
        c = pycountry.countries.get(alpha_2=iso)
        if c:
            fi = gettext.translation("iso3166-1", pycountry.LOCALES_DIR, languages=["fi"])
            return fi.gettext(getattr(c, "common_name", c.name))
    except Exception:
        pass
    return props.get("NAME", "")


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_layer(out, prefix, layer, groups):
    pts = np.concatenate(layer.pts) if layer.pts else np.zeros((0, 2), np.int16)
    out.append(f"const int16_t {prefix}_PTS[] = {{")
    flat = pts.ravel()
    for i in range(0, len(flat), 32):
        out.append(",".join(str(int(v)) for v in flat[i:i + 32]) + ",")
    out.append("0};")
    for g in groups:
        shapes = layer.groups.get(g, [])
        out.append(f"const MapShape {prefix}_{g.upper()}[] = {{")
        for s in shapes:
            out.append("{%d,%d,%d,%d,%d,%d,%d}," % tuple(int(v) for v in s))
        out.append("{0,0,0,0,0,0,0}};")
        out.append(f"const uint32_t {prefix}_{g.upper()}_N = {len(shapes)};")
    return len(flat) * 2


def write(reg, wld, places, airports, runways, labels, lat, lon):
    out = ["// Generated by tools/make_map.py - do not edit by hand.",
           "// Map data: Natural Earth (public domain), OurAirports (public domain).",
           '#include "mapdata.h"', "",
           f"const double MAP_HOME_LAT = {lat!r};", f"const double MAP_HOME_LON = {lon!r};",
           f"const float REG_OX = {reg.ox:.1f}f;", f"const float REG_OY = {reg.oy:.1f}f;",
           f"const float REG_UNIT = {reg.unit}.0f;", f"const float WLD_UNIT = {WORLD_UNIT}.0f;", ""]
    size = emit_layer(out, "REG", reg, ["fill_fine", "fill_coarse", "coast_fine", "coast_coarse",
                                         "border_fine", "border_coarse"])
    size += emit_layer(out, "WLD", wld, ["fill", "coast", "border"])

    # Each name is stored as "Finnish\0English\0"; the English part is empty
    # when both are the same (see mapName() in mapdata.h).
    names = bytearray()

    def add_name(pair):
        fi, en = pair
        start = len(names)
        names.extend(fi.encode("utf-8") + b"\0" + (b"" if en == fi else en.encode("utf-8")) + b"\0")
        return start
    out.append("const MapPlace PLACES[] = {")
    for x, y, mz, pop, name in places:
        out.append("{%d,%d,%d,%d,%d}," % (x, y, min(mz, 255), 1 if pop > 500000 else 0, add_name(name)))
    out.append("{0,0,0,0,0}};")
    out.append(f"const uint32_t PLACES_N = {len(places)};")
    out.append("const MapAirport AIRPORTS[] = {")
    for x, y, iata, icao, big, name in airports:
        out.append("{%d,%d,%s,%s,%d,%d}," % (x, y, c_str(iata), c_str(icao), big, add_name(name)))
    out.append('{0,0,"","",0,0}};')
    out.append(f"const uint32_t AIRPORTS_N = {len(airports)};")
    out.append("const MapRunway RUNWAYS[] = {")
    for r in runways:
        out.append("{%d,%d,%d,%d}," % r)
    out.append("{0,0,0,0}};")
    out.append(f"const uint32_t RUNWAYS_N = {len(runways)};")
    out.append("const MapLabel MAP_LABELS[] = {")
    for x, y, kind, zmin, zmax, name in labels:
        out.append("{%d,%d,%d,%d,%d,%d}," % (x, y, kind, min(zmin, 255), min(zmax, 255), add_name(name)))
    out.append("{0,0,0,0,0,0}};")
    out.append(f"const uint32_t MAP_LABELS_N = {len(labels)};")
    # A string literal with octal escapes works whether char is signed or unsigned.
    def lit(chunk):
        return '"' + "".join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\?' else "\\%03o" % b
                             for b in chunk) + '"'
    out.append("const char MAP_NAMES[] =")
    for i in range(0, len(names), 60):
        out.append("  " + lit(names[i:i + 60]))
    out.append(";")
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    total = size + len(names) + len(places) * 12 + len(airports) * 20 + len(runways) * 16
    print(f"Wrote {os.path.normpath(OUT)}: ~{total / 1024:.0f} KB of map data, "
          f"{len(places)} towns, {len(airports)} airports, {len(runways)} runways")


if __name__ == "__main__":
    main()
