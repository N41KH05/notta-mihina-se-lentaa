"""Builds the case for the Waveshare ESP32-S3-Touch-LCD-7 (touch version).

Three parts:
  frame  front bezel and side walls in one piece; the screen drops in from the back
  back   back plate: holds the screen by its brass standoffs, carries the USB bay
  leg    fold-out kickstand, hinged on top of the frame

Usage:
  pip install cadquery-ocp
  python build_case.py            writes stl/ and step/ next to this file

Units are mm. Axes: x to the right seen from the front, z up, y towards the back.
The front face of the bezel is at y = -3.2 and the back of the walls at y = 23.5.
"""
import math
import os
import sys

from OCP.BRep import BRep_Builder
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism
from OCP.ShapeUpgrade import ShapeUpgrade_UnifySameDomain
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCP.StlAPI import StlAPI_Writer
from OCP.TopoDS import TopoDS_Compound
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec


# ---- small helpers ---------------------------------------------------------------

def box(x0, y0, z0, x1, y1, z1):
    return BRepPrimAPI_MakeBox(gp_Pnt(min(x0, x1), min(y0, y1), min(z0, z1)),
                               gp_Pnt(max(x0, x1), max(y0, y1), max(z0, z1))).Shape()

def cyl_y(x, z, r, y0, y1):
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y0, z), gp_Dir(0, 1, 0)), r, y1 - y0).Shape()

def cyl_x(y, z, r, x0, x1):
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x0, y, z), gp_Dir(1, 0, 0)), r, x1 - x0).Shape()

def prism_x(points_yz, x0, x1):
    poly = BRepBuilderAPI_MakePolygon()
    for y, z in points_yz:
        poly.Add(gp_Pnt(x0, y, z))
    poly.Close()
    face = BRepBuilderAPI_MakeFace(poly.Wire()).Face()
    return BRepPrimAPI_MakePrism(face, gp_Vec(x1 - x0, 0, 0)).Shape()

def add(a, b):
    return BRepAlgoAPI_Fuse(a, b).Shape()

def cut(a, b):
    return BRepAlgoAPI_Cut(a, b).Shape()

def rounded_rect(half_w, half_h, r, y0, y1):
    s = add(box(-half_w + r, y0, -half_h, half_w - r, y1, half_h),
            box(-half_w, y0, -half_h + r, half_w, y1, half_h - r))
    for sx in (-1, 1):
        for sz in (-1, 1):
            s = add(s, cyl_y(sx * (half_w - r), sz * (half_h - r), r, y0, y1))
    return s

def tidy(shape):
    u = ShapeUpgrade_UnifySameDomain(shape, True, True, True)
    u.Build()
    return u.Shape()


# ---- dimensions ------------------------------------------------------------------

# Screen (Waveshare drawing): glass 192.96 x 110.76 with R7.88 corners, visible
# area 184.96 x 102.76 through the bezel. The glass is about 1 mm thick; the pocket
# is deeper and the back plate pushes the screen forward against the bezel lip.
OUT_W, OUT_H, OUT_R = 104.88, 58.78, 5.0      # outer half width / half height / corner
WALL = 2.5
DEPTH = 23.5                                   # back of the bezel to the back plate
BACK = 2.5                                     # back plate thickness
WINDOW = (92.48, 51.38)                        # half sizes of the opening in the bezel
POCKET = (96.88, 55.78, 1.8)                   # glass pocket: half sizes and depth

# Back plate screws go into posts in the four corners of the frame, outside the
# glass corners so the screen can still go in from the back.
CORNERS = [(sx * 99.6, sz * 53.5) for sx in (-1, 1) for sz in (-1, 1)]

# Brass standoffs on the back of the screen (126.2 x 65.65, M3). The pattern is not
# centred on the glass. Seen from the front, the USB ports are on the left (x < 0).
STANDOFFS = [(x, z) for x in (-61.57, 64.63) for z in (33.80, -31.85)]
STANDOFF_TOP = 11.2                            # y of the standoff tops
SQUEEZE = 0.2                                  # posts press the screen forward this much

# USB bay. Both ports sit 41 mm in from the left edge of the glass, about 9.5 mm
# behind the bezel. The bay is a little tub on the back plate with a window in its
# inner wall, so the plugs reach the ports but the board stays out of sight.
BAY_HALF = 20.0                                # bay height / 2
BAY_FLOOR = 5.4                                # y of the bay floor
BAY_INNER = -58.0                              # inner wall of the bay (x)
PORT_WINDOW = 9.0                              # window height in that wall (along y)

# Kickstand: hinge axis along x on top of the frame. Opened 26 degrees the leg holds
# the screen leaning back 20 degrees; a lug on the leg lands on the top of the frame.
HINGE_Y, HINGE_Z = 19.5, 63.3
LEG_HALF_W = 20.0
KNUCKLE_X = (20.5, 32.0)
LEG_END_Z = -55.4
OPEN_DEG = 26.0
LUG = (4.52 - math.cos(math.radians(OPEN_DEG))) / math.sin(math.radians(OPEN_DEG))


# ---- frame -----------------------------------------------------------------------

bezel = rounded_rect(OUT_W, OUT_H, OUT_R, -3.2, 0)
bezel = cut(bezel, box(-WINDOW[0], -4, -WINDOW[1], WINDOW[0], 1, WINDOW[1]))
bezel = cut(bezel, box(-POCKET[0], -POCKET[2], -POCKET[1], POCKET[0], 0.01, POCKET[1]))
walls = cut(rounded_rect(OUT_W, OUT_H, OUT_R, 0, DEPTH),
            rounded_rect(OUT_W - WALL, OUT_H - WALL, OUT_R - WALL, -1, DEPTH + 1))
frame = add(bezel, walls)

for x, z in CORNERS:
    frame = add(frame, cyl_y(x, z, 3.0, 0, DEPTH))
    frame = cut(frame, cyl_y(x, z, 1.25, DEPTH - 10, DEPTH + 1))     # M3 self-tapping

# opening in the left wall where the bay slides in (cut well past the wall)
frame = cut(frame, box(-150, BAY_FLOOR, -BAY_HALF, -OUT_W + WALL + 1.5, 40, BAY_HALF))

# hinge knuckles; the 45 degree wedge underneath lets the frame print face down
# without supports
for sx in (-1, 1):
    x0, x1 = sorted((sx * KNUCKLE_X[0], sx * KNUCKLE_X[1]))
    k = add(cyl_x(HINGE_Y, HINGE_Z, 4.0, x0, x1), box(x0, HINGE_Y - 4, OUT_H, x1, HINGE_Y + 4, HINGE_Z))
    k = add(k, prism_x([(HINGE_Y - 4, OUT_H), (HINGE_Y - 4, HINGE_Z + 4),
                        (HINGE_Y - 4 - (HINGE_Z + 4 - OUT_H), OUT_H)], x0, x1))
    k = cut(k, cyl_x(HINGE_Y, HINGE_Z, 1.7, x0 - 1, x1 + 1))
    frame = add(frame, k)
frame = tidy(frame)


# ---- back plate ------------------------------------------------------------------

back = rounded_rect(OUT_W, OUT_H, OUT_R, DEPTH, DEPTH + BACK)
for x, z in CORNERS:
    back = cut(back, cyl_y(x, z, 1.7, DEPTH - 1, DEPTH + BACK + 1))

for x, z in STANDOFFS:
    back = add(back, cyl_y(x, z, 4.0, STANDOFF_TOP - SQUEEZE, DEPTH))
    back = cut(back, cyl_y(x, z, 2.25, 10, DEPTH + BACK + 1))         # 4.5 mm, some play for M3

tub_x = -(OUT_W - WALL) + 0.3
back = add(back, box(tub_x, 4.0, -BAY_HALF - 2, BAY_INNER + 2, DEPTH + BACK, BAY_HALF + 2))
back = cut(back, box(-150, BAY_FLOOR, -BAY_HALF, BAY_INNER, 40, BAY_HALF))
back = cut(back, box(BAY_INNER - 1, BAY_FLOOR, -BAY_HALF, BAY_INNER + 3, BAY_FLOOR + PORT_WINDOW, BAY_HALF))

for x in (-52, -47, -42, -37, -32, -27, 27, 32, 37, 42, 47, 52):    # vents, clear of the leg
    back = cut(back, box(x - 1.25, DEPTH - 1, -12, x + 1.25, DEPTH + BACK + 1, 32))
back = tidy(back)


# ---- kickstand -------------------------------------------------------------------

leg = add(cyl_x(HINGE_Y, HINGE_Z, 4.0, -LEG_HALF_W, LEG_HALF_W),
          box(-LEG_HALF_W, HINGE_Y, OUT_H + 0.52, LEG_HALF_W, 30.5, HINGE_Z + 4))
leg = add(leg, box(-LEG_HALF_W, 26.5, LEG_END_Z, LEG_HALF_W, 30.5, HINGE_Z + 4))
leg = add(leg, box(-LEG_HALF_W, HINGE_Y - LUG, HINGE_Z - 1, LEG_HALF_W, HINGE_Y, HINGE_Z + 4))
leg = cut(leg, cyl_x(HINGE_Y, HINGE_Z, 1.25, -LEG_HALF_W - 1, LEG_HALF_W + 1))   # hinge screws
leg = cut(leg, box(-LEG_HALF_W + 8, 26, -30, LEG_HALF_W - 8, 31, 40))
leg = tidy(leg)


# ---- export ----------------------------------------------------------------------

def write_step(shape, path):
    w = STEPControl_Writer()
    w.Transfer(shape, STEPControl_AsIs)
    w.Write(path)

def write_stl(shape, path):
    BRepMesh_IncrementalMesh(shape, 0.05, False, 0.3, True)
    StlAPI_Writer().Write(shape, path)

here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else here
os.makedirs(os.path.join(out, "stl"), exist_ok=True)
os.makedirs(os.path.join(out, "step"), exist_ok=True)

parts = {"frame": frame, "back": back, "leg": leg}
for name, shape in parts.items():
    write_stl(shape, os.path.join(out, "stl", name + ".stl"))

everything = TopoDS_Compound()
builder = BRep_Builder()
builder.MakeCompound(everything)
for shape in parts.values():
    builder.Add(everything, shape)
write_step(everything, os.path.join(out, "step", "case.step"))
print("done:", ", ".join(parts))
