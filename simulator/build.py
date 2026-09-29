#!/usr/bin/env python3
"""Rebuild the browser simulator from the firmware sources (needs: pip install ziglang).

Usage: python build.py [path/to/Adafruit_GFX_Library] [path/to/ArduinoJson/src]"""
import base64, gzip, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SK = os.path.join(HERE, "..", "SkyTracker")
# Library folders (defaults: where the Arduino IDE installs them)
LIBS = os.path.expanduser("~/Documents/Arduino/libraries")
GFX = sys.argv[1] if len(sys.argv) > 1 else os.path.join(LIBS, "Adafruit_GFX_Library")
JSON = sys.argv[2] if len(sys.argv) > 2 else os.path.join(LIBS, "ArduinoJson", "src")
wasm = os.path.join(HERE, "sim.wasm")
qr_obj = os.path.join(HERE, "qr.o")
subprocess.check_call([sys.executable, "-m", "ziglang", "cc", "-target", "wasm32-wasi", "-O2", "-w", "-c",
    os.path.join(SK, "qr.c"), "-o", qr_obj])
subprocess.check_call([sys.executable, "-m", "ziglang", "c++", "-target", "wasm32-wasi", "-O2",
    "-fno-exceptions", "-fno-rtti", "-std=gnu++17", "-DARDUINO=100", "-w",
    "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
    "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0", "-DARDUINOJSON_ENABLE_PROGMEM=0",
    "-I" + os.path.join(HERE, "shim"), "-I" + GFX, "-I" + JSON, "-I" + SK, "-mexec-model=reactor", "-Wl,--no-entry",
    "-g0", "-Wl,--strip-all",                    # no debug info: a much smaller page
    os.path.join(HERE, "sim.cpp"), *[os.path.join(SK, f) for f in ("render.cpp", "text.cpp", "demo.cpp", "app.cpp", "ui.cpp", "places.cpp", "traffic.cpp", "mapdata.cpp")],
    os.path.join(GFX, "Adafruit_GFX.cpp"), qr_obj, "-o", wasm])
b64 = base64.b64encode(gzip.compress(open(wasm, "rb").read(), 9)).decode()
page = open(os.path.join(HERE, "page.html"), encoding="utf-8").read().replace("__WASM__", b64)
open(os.path.join(HERE, "SkyTracker Simulator.html"), "w", encoding="utf-8").write(page)
os.remove(wasm)
os.remove(qr_obj)
print("Wrote simulator/SkyTracker Simulator.html")
