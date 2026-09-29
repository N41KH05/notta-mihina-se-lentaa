#!/usr/bin/env python3
"""Export every printable part of skytracker_case.scad to STL (needs OpenSCAD).
Extra settings are passed through, e.g.:  python export_stl.py glass_t=1.6 lift=30"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
defs = []
for a in sys.argv[1:]:
    defs += ["-D", a]
os.makedirs(os.path.join(HERE, "stl"), exist_ok=True)
for part in ["frame", "body", "back", "leg"]:
    out = os.path.join(HERE, "stl", f"{part}.stl")
    subprocess.check_call(["openscad", "-o", out, "-D", f'part="{part}"', *defs,
                           os.path.join(HERE, "skytracker_case.scad")])
    print("wrote", out)
