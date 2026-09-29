"""Small geometry helpers shared by the map builder and the app."""
import math

import numpy as np

R_EARTH = 6378137.0
WORLD_M = 2 * math.pi * R_EARTH          # width of the Web Mercator world, metres


def to_merc(lon, lat):
    """lon/lat degrees (scalars or arrays) -> Web Mercator metres."""
    lon = np.asarray(lon, dtype=np.float64)
    lat = np.clip(np.asarray(lat, dtype=np.float64), -85.0, 85.0)
    x = np.radians(lon) * R_EARTH
    y = np.log(np.tan(np.pi / 4 + np.radians(lat) / 2)) * R_EARTH
    return x, y


def to_merc_inv(x, y):
    """Web Mercator metres -> lon/lat degrees."""
    lon = np.degrees(np.asarray(x, dtype=np.float64) / R_EARTH)
    lat = np.degrees(2 * np.arctan(np.exp(np.asarray(y, dtype=np.float64) / R_EARTH)) - np.pi / 2)
    return float(lon), float(lat)


def from_merc(x, y):
    lon = np.degrees(np.asarray(x) / R_EARTH)
    lat = np.degrees(2 * np.arctan(np.exp(np.asarray(y) / R_EARTH)) - np.pi / 2)
    return lon, lat


def metres_per_px(zoom):
    """Mercator metres per screen pixel at a (possibly fractional) zoom."""
    return WORLD_M / (256 * 2 ** zoom)


def haversine_km(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 6371.0 * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


def bearing_deg(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(lon2 - lon1)
    y = math.sin(dl) * math.cos(p2)
    x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
    return (math.degrees(math.atan2(y, x)) + 360) % 360


def compass(deg):
    return ["N", "NE", "E", "SE", "S", "SW", "W", "NW"][int((deg + 22.5) // 45) % 8]


def clip_ring(pts, xmin, ymin, xmax, ymax):
    """Sutherland-Hodgman clip of a closed polygon ring (N x 2) to a rectangle.
    Vectorised per clip edge, so it stays fast for big coastlines."""
    for axis, bound, keep_greater in ((0, xmin, True), (0, xmax, False),
                                      (1, ymin, True), (1, ymax, False)):
        n = len(pts)
        if n == 0:
            return pts
        cur, nxt = pts, np.roll(pts, -1, axis=0)
        cin = cur[:, axis] >= bound if keep_greater else cur[:, axis] <= bound
        nin = nxt[:, axis] >= bound if keep_greater else nxt[:, axis] <= bound
        if cin.all():
            continue
        if not cin.any():
            return pts[:0]
        cross = cin != nin
        d = nxt[:, axis] - cur[:, axis]
        with np.errstate(divide="ignore", invalid="ignore"):
            t = np.where(d != 0, (bound - cur[:, axis]) / d, 0.0)
        inter = cur + (nxt - cur) * t[:, None]
        inter[:, axis] = bound
        # Each edge emits [cur if inside] then [intersection if crossing].
        out = np.empty((n * 2, 2))
        out[0::2] = cur
        out[1::2] = inter
        mask = np.empty(n * 2, dtype=bool)
        mask[0::2] = cin
        mask[1::2] = cross
        pts = out[mask]
    return pts


def split_line(pts, xmin, ymin, xmax, ymax):
    """Split a polyline into the runs that fall inside a rectangle
    (keeping one point either side so lines run off the edge cleanly)."""
    inside = ((pts[:, 0] >= xmin) & (pts[:, 0] <= xmax) &
              (pts[:, 1] >= ymin) & (pts[:, 1] <= ymax))
    if inside.all():
        return [pts]
    if not inside.any():
        return []
    keep = inside.copy()
    keep[1:] |= inside[:-1]
    keep[:-1] |= inside[1:]
    runs, start = [], None
    for i, k in enumerate(keep):
        if k and start is None:
            start = i
        elif not k and start is not None:
            runs.append(pts[start:i])
            start = None
    if start is not None:
        runs.append(pts[start:])
    return [r for r in runs if len(r) >= 2]


def decimate(pts, min_dist, closed=False):
    """Drop points closer than min_dist to the last kept one (cheap simplify)."""
    if len(pts) <= 3 or min_dist <= 0:
        return pts
    keep = [0]
    last = pts[0]
    for i in range(1, len(pts) - 1):
        if abs(pts[i, 0] - last[0]) + abs(pts[i, 1] - last[1]) >= min_dist:
            keep.append(i)
            last = pts[i]
    keep.append(len(pts) - 1)
    out = pts[keep]
    if closed and len(out) < 4:
        return out[:0]
    return out
