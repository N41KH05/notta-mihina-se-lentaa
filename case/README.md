# Enclosure with kickstand

A 3D-printable, fully enclosed case for the Waveshare ESP32-S3-Touch-LCD-7 (touch version). Two fold-out legs hold the screen at a 20° tilt. The electronics are completely covered: the back plate has only narrow 2 mm ventilation slots, and the board's two USB-C ports are reached through openings in the side wall, so the cable plugs straight in from the side.

![preview](preview.png)

## Verify before printing

The model is based on Waveshare's published outline dimensions (glass 192.96 × 110.76 mm). Check the following on your board with calipers or a ruler; each one is a parameter at the top of `skytracker_case.scad`.

1. **Glass thickness** (`glass_t`, set to 1.8 mm). Measure the edge of the touch glass only. The body clamps the glass by exactly this much.
2. **Depth behind the glass** (`board_depth`, set to 22 mm). Lay the screen face down and measure from the back of the glass to the top of the tallest part: the screw terminals, connectors or the ESP32 module. Add 1–2 mm.
3. **Size of the screen module and board behind the glass** (`module_w` 165, `module_h` 100). The walls and screw posts stay outside this area.
4. **The strip at the left and right edges.** On the back, the glass sticks out past the screen module. The body's side rails need at least **10 mm** of free glass there, with nothing glued or cabled on it.
5. **Where the USB-C ports are.** Both ports (**USB** and **UART1**) are on one edge of the board: on the left when you look at the back of the board, so on the **right when you look at the screen**. The side wall has an opening for each. Measure, with the glass facing down:
   - `port_y`: the height of each port's centre above the bottom edge of the glass (set to 49 mm for USB and 73 mm for UART1, estimated from Waveshare's photos);
   - `port_depth`: how far the port's centre sits behind the back of the glass (set to 9 mm).

   The openings are 13.5 × 8 mm, so the moulded grip of an ordinary USB-C plug can go a little way in. A plug with a very bulky grip may not reach far enough; a slim one or a **right-angle (90°) plug** pointing down is neatest.

## Parts to print

| File | How many | On the bed |
|---|---|---|
| `stl/frame.stl` | 1 | front face down (the smooth side) |
| `stl/body.stl` | 1 | as exported: rails flat on the bed, hinge tabs and walls up |
| `stl/back.stl` | 1 | outer face down |
| `stl/leg.stl` | **2** | as exported (the same leg fits both sides) |

- **Printer:** the body is 210 × 130 mm, so it needs a bed of at least about 215 × 135 mm. Most full-size printers are fine (Prusa, Ender 3, Bambu A1/P1/X1). A Bambu A1 mini is too small.
- **Material:** PETG is best because it's tougher and doesn't soften in a sunny window. PLA works too.
- **Settings:**
  - 0.2 mm layers
  - 3 walls (perimeters)
  - 25 % infill
  - no supports needed
- **Filament:** about 130–150 g in total.

## Hardware

- 4 × **M3×8 countersunk** screws (DIN 7991), for the front. Not longer.
- 2 × **M3×12** screws with any head, for the leg hinges
- 4 × **M3×10** pan or button head screws, for the back plate. They cut their own thread in the plastic.
- 6 × **M3 nuts**
- 4 **rubber feet**, under the two front feet and the two leg feet. They stop it sliding if the table gets bumped.
- Optional: a strip of 1 mm foam tape, if the glass rattles

## Assembly

1. Push the nuts into the body:
   - 4 into the hex pockets on the back of the side rails (for the front screws)
   - 2 into the hex pockets on the **inside** of the side walls, level with the hinge tabs
   A dab of glue keeps them in place.
2. Lay the bezel face down on a soft cloth. Drop the screen in, glass first. 
3. Put the body over the screen. Its rails rest on the strip of glass beside the screen. Screw the 4 countersunk screws in from the front.
4. Tighten evenly until the glass is held firmly, but don't force them. If the glass still moves, add foam tape under the rails.
5. Fit the legs: each goes on the outside of its hinge tab, with the recessed hole facing out. Screw an M3×12 through the leg and tab into the nut inside the wall. Tighten until the leg stays where you put it.
6. Screw the back plate on with the 4 M3×10 screws. Stop as soon as they're snug: they cut their own thread, and overtightening strips it.
7. Stick the rubber feet on, fold the legs out until they stop (at 55°), and stand it up.
8. Plug the USB-C cable into the **USB** opening (the lower one) on the right side. It powers the device, and the same port is used for firmware updates and the serial monitor. The **UART1** port above it also works for power and uploads.

The BOOT and RESET buttons and the memory card slot end up inside the case. They aren't needed day to day. Firmware updates go through the side ports, and the case doesn't need opening. If you ever need them, take off the back plate (4 screws).

## Placement and durability

- **Cable routing:** the cable leaves the right side; let it drop to the table and run it along behind the device. The legs sit behind the ports and stay clear of the plug. A braided USB-C cable or a cable guard is more durable than a standard cable.
- **Stability:** the case rests on its front feet and the leg feet. The centre of mass is about 3 cm inside the front support and 8 cm inside the rear support, so normal touch input will not tip it. Rubber feet prevent it from sliding.
- **No loose parts:** all screws are secured with nuts or thread into the plastic.

## Changing the design

Every size is a setting at the top of `skytracker_case.scad`:

- `tilt`: how far the screen leans back
- `pivot_y`: where the legs are attached
- `lift`: height of the front feet
- `board_depth`: depth behind the glass
- `port_y`, `port_depth`, `port_side`: where the USB-C openings are
- `back_cable`: also cut the old cable opening in the back plate (off by default)
- `vent_w`: width of the air slots

The leg length and the stop are recalculated automatically, so the screen always stands at the chosen angle.

1. Open the file in [OpenSCAD](https://openscad.org) (free), change a number, then press F6 and F7 to export an STL.
2. Or, with OpenSCAD installed, export all parts at once:
   ```
   python export_stl.py glass_t=1.6 board_depth=18
   ```
3. `part = "assembly"` shows the whole thing standing up with a dummy screen, and `part = "assembly_folded"` shows it with the legs folded.
