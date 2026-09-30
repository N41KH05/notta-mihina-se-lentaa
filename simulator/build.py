#!/usr/bin/env python3
"""Rebuild the browser simulator (docs/sim.wasm) from the firmware sources (needs: pip install ziglang).

Usage: python build.py [path/to/Adafruit_GFX_Library]"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SK = os.path.join(HERE, "..", "SkyTracker")
# Adafruit GFX (default: where the Arduino IDE installs it)
LIBS = os.path.expanduser("~/Documents/Arduino/libraries")
GFX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(LIBS, "Adafruit_GFX_Library")
wasm = os.path.join(HERE, "..", "docs", "sim.wasm")
qr_obj = os.path.join(HERE, "qr.o")
subprocess.check_call([sys.executable, "-m", "ziglang", "cc", "-target", "wasm32-wasi", "-O2", "-w", "-c",
    os.path.join(SK, "qr.c"), "-o", qr_obj])
subprocess.check_call([sys.executable, "-m", "ziglang", "c++", "-target", "wasm32-wasi", "-O2",
    "-fno-exceptions", "-fno-rtti", "-std=gnu++17", "-DARDUINO=100", "-w",
    "-I" + os.path.join(HERE, "shim"), "-I" + GFX, "-I" + SK, "-mexec-model=reactor", "-Wl,--no-entry",
    "-g0", "-Wl,--strip-all",                    # no debug info: a much smaller download
    os.path.join(HERE, "sim.cpp"), *[os.path.join(SK, f) for f in ("render.cpp", "ui.cpp", "demo.cpp", "mapdata.cpp")],
    os.path.join(GFX, "Adafruit_GFX.cpp"), qr_obj, "-o", wasm])
os.remove(qr_obj)
print("Wrote docs/sim.wasm (the page itself is docs/simulator.html, .css and .js)")
