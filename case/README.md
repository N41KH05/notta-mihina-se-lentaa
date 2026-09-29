# Case

A printable case for the Waveshare ESP32-S3-Touch-LCD-7 (touch version). It's closed all round apart from some narrow air slots in the back, two fold-out legs tilt the screen back 20°, and the USB-C ports are reachable through openings in the side wall.

![preview](preview.png)

## Measure first

The model is drawn from Waveshare's outline drawing (glass 192.96 × 110.76 mm) and photos, not from a board on my desk, so check these before you print. Each one is a variable at the top of `skytracker_case.scad`.

1. `glass_t` (1.8 mm): thickness of the touch glass at its edge. The body clamps exactly this much.
2. `board_depth` (22 mm): with the screen face down, the height from the back of the glass to the tallest part on the board. Add a millimetre or two.
3. `module_w` × `module_h` (165 × 100 mm): the screen module and board behind the glass. The walls and screw posts stay outside it.
4. The bare strip of glass at the left and right edges on the back. The side rails sit on it and need about 10 mm with nothing stuck to it.
5. The USB-C ports. Both (USB and UART1) are on the same edge: left when you look at the back of the board, so right when you look at the screen. `port_y` is the height of each port's centre above the bottom edge of the glass (49 and 73 mm, estimated from photos), and `port_depth` is how far the centre sits behind the glass (9 mm).

The port openings are 13.5 × 8 mm, enough for the grip of a normal USB-C plug to go in a little. Very chunky plugs may not reach; a slim or right-angle one is safest.

## Printing

Print `stl/frame.stl` front face down, `stl/body.stl` as exported (rails on the bed), `stl/back.stl` outer face down, and `stl/leg.stl` twice. No supports.

The body is 210 × 130 mm, so you need a bed of roughly 215 × 135 or bigger; an A1 mini is too small. I'd use PETG so it doesn't go soft in a sunny window, but PLA is fine. 0.2 mm layers, 3 walls and 25 % infill work, and the whole thing takes about 130–150 g.

## Hardware

- 4 × M3×8 countersunk (DIN 7991) for the front bezel. Not longer.
- 2 × M3×12, any head, for the leg hinges
- 4 × M3×10 pan or button head for the back plate. These cut their own thread.
- 6 × M3 nuts
- 4 small rubber feet
- Optionally some 1 mm foam tape if the glass rattles

## Assembly

1. Press 4 nuts into the hex pockets on the back of the side rails and 2 into the pockets on the inside of the side walls, level with the hinge tabs. A drop of glue holds them.
2. Put the bezel face down on a cloth and drop the screen in, glass first.
3. Put the body over it so the rails rest on the glass edges, and screw in the 4 countersunk screws from the front. Tighten evenly until the glass is held; if it still moves, add foam tape under the rails.
4. Each leg goes on the outside of its hinge tab with the recessed hole facing out. Put an M3×12 through the leg and tab into the nut, and tighten until the leg stays where you put it.
5. Screw on the back plate. Stop when the screws are snug, or you'll strip the plastic.
6. Stick the rubber feet under the front feet and the leg ends, fold the legs out to their stop and stand it up.
7. Plug the cable into the lower opening on the right (the USB port). That port does power, uploads and the serial monitor. UART1 above it also works for power and uploads.

BOOT, RESET and the SD card slot end up inside. You don't need them normally; updates go in through the side port. If you do, the back plate comes off with 4 screws.

With the legs out, the balance point is about 3 cm behind the front feet and 8 cm in front of the leg feet, so tapping the screen won't tip it over. Let the cable drop straight down beside the case; the legs are well behind the plug.

## Changing it

Everything is a variable at the top of the file: `tilt`, `pivot_y` (where the legs attach), `lift` (front feet), `board_depth`, `port_y` / `port_depth` / `port_side`, `vent_w`, and `back_cable` if you'd rather have a cable hole in the back plate as well. The leg length and stop are recalculated so the screen still stands at `tilt`.

Change a value in [OpenSCAD](https://openscad.org) and export with F6 then F7, or export all parts at once:

```
python export_stl.py glass_t=1.6 board_depth=18
```

Set `part = "assembly"` (or `"assembly_folded"`) to see the whole thing with a dummy screen.
