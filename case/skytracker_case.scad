// ============================================================================
//  SkyTracker case for the Waveshare ESP32-S3-Touch-LCD-7 (touch version)
//
//  Fully enclosed: the electronics are covered on all sides, and the board's two USB-C
//  ports are reached through openings in the side wall. Parts:
//    "frame"   front bezel that holds the touch glass       print front face down
//    "body"    back body: side rails that clamp the glass,   print front side down
//              walls around the board, and the leg hinges    (hinge tabs up)
//    "back"    back plate with narrow air slots              print outer face down
//    "leg"     kickstand leg: print TWO (one part fits both sides)
//    "assembly" / "assembly_folded"   for looking only
//
//  Hardware (all M3):
//    4 x M3x8 countersunk   front bezel -> body (nuts in the body)
//    2 x M3x12 any head     leg hinges (nuts inside the body)
//    4 x M3x10 pan/button   back plate -> body (they cut their own thread)
//    6 x M3 nuts
//  Units: millimetres.
// ============================================================================

part = "assembly";

// ---- The screen (measure yours: everything else follows from these) ---------------
glass_w     = 192.96;  // touch glass width  (Waveshare drawing: 192.96 x 110.76)
glass_h     = 110.76;  // touch glass height
glass_t     = 1.8;     // touch glass thickness: MEASURE the glass edge
board_depth = 22;      // room behind the glass for the screen module + board: measure
                       // from the back of the glass to the top of the tallest part
module_w    = 165;     // LCD module / board size behind the glass (kept clear)
module_h    = 100;

// ---- Bezel ---------------------------------------------------------------------
clear      = 0.4;    // gap around the glass in the pocket
lip        = 2.0;    // front face thickness
overlap    = 4.0;    // how far the front face covers the glass border
wall_tb    = 3.0;    // top and bottom walls of the bezel
wall_side  = 8.0;    // side walls of the bezel (the front screws go through these)
corner_r   = 5.0;    // outer corner radius
squeeze    = 0.2;    // the body presses the glass by this much (add 1 mm foam if loose)

// ---- Body and back plate --------------------------------------------------------------
rail_t     = 5.0;    // thickness of the side rails that clamp the glass
rail_in    = 9.0;    // how far the rails reach in over the back of the glass
wall_in    = 5.6;    // the body's side walls start this far in from the glass edge
back_t     = 2.5;    // back plate thickness
boss_x     = 60;     // back plate screws: distance from the middle
boss_reach = 8;      // screw posts reach this far in from the outer top/bottom edge
vent_w     = 2.0;    // ventilation slot width
back_cable = false;  // also cut a cable opening in the back plate (normally not needed)
cable_w    = 15;     // size and position of that opening: from the middle, and up
cable_h    = 8;      // from the bottom edge
cable_x    = 0;
cable_y    = 22;

// ---- USB-C ports in the side wall -----------------------------------------------------
// The board has two USB-C ports on one edge: "USB" (the ESP32-S3's own USB: power,
// uploads and the serial monitor) and "UART1" (USB-serial chip: power and uploads).
// Seen from the FRONT they are on the RIGHT edge (on the back of the board they are on
// the left). The openings are big enough for the plug's moulded grip to go in a little.
// MEASURE on your board: height of each port centre above the bottom edge of the glass,
// and depth of the port centre behind the back of the glass.
port_side  = -1;           // -1 = right edge seen from the front, 1 = left edge
port_y     = [49, 73];     // port centres, up from the bottom edge of the glass: USB, UART1
port_depth = 9;            // port centre behind the back of the glass
port_w     = 13.5;         // opening size along the edge
port_h     = 8.0;          // opening size front-to-back

// ---- Kickstand -------------------------------------------------------------------------
tilt       = 20;     // screen leans back this many degrees from vertical
leg_open   = 55;     // leg angle from the screen when open (stop angle)
pivot_y    = 95;     // hinge height above the bottom of the bezel (above the USB ports)
lug_h      = 6;      // hinge axis distance behind the rails
leg_w      = 10;     // leg width (in its swing plane)
leg_th     = 5;      // leg thickness
foot_r     = 5;      // foot radius
foot_in    = 9;      // foot pad reaches this far in past the glass edge
screw_y    = 22;     // front screws: distance from top and bottom
lift       = 12;     // front feet: the rails reach this far below the bezel, so the case
                     // rests on its front edge (steadier than on the back plate)

// ---- M3 hardware -------------------------------------------------------------------
m3_hole    = 3.3;
m3_pilot   = 2.7;    // for screws that cut their own thread
m3_csk_d   = 6.4;
nut_af     = 5.6;
nut_depth  = 2.5;
rail_nut_depth = 3.0;
head_d     = 6.2;
head_depth = 2.5;

$fn = 48;

// ---- Derived -----------------------------------------------------------------------
W   = glass_w + 2 * clear + 2 * wall_side;       // outer width
H   = glass_h + 2 * clear + 2 * wall_tb;         // outer height
D   = lip + glass_t - squeeze;                   // bezel depth (the body starts here)
Z0  = D + rail_t;                                // back of the rails
XE  = glass_w / 2;                               // glass edge (right side)
XR  = XE - rail_in;                              // inner edge of the rails
XW  = XE - wall_in;                              // outer face of the body side walls
ZP  = Z0 + lug_h;                                // hinge axis depth
ZI  = lip + glass_t + board_depth;               // inside of the back plate
ZB  = ZI + back_t;                               // outside of the back plate
Y0  = 0.5;                                       // body top/bottom walls: y Y0..wall_tb
// The screen stands on its lowest corner (the back plate's bottom edge, or the rail
// feet if lifted); the legs are made just long enough to stand it at `tilt`.
function hgt(y, z) = y * cos(tilt) - z * sin(tilt);
CY  = hgt(Y0, ZB) < hgt(-lift, Z0) ? Y0 : -lift;
CZ  = hgt(Y0, ZB) < hgt(-lift, Z0) ? ZB : Z0;
leg_len = ((pivot_y - CY) * cos(tilt) + (CZ - ZP) * sin(tilt) - foot_r) / cos(leg_open - tilt);
heel = (lug_h - leg_w / 2) / sin(leg_open);      // touches the rail exactly at leg_open
POST_Y = (boss_reach + Y0) / 2;                  // back plate screws: y from the edge

// ---- helpers -------------------------------------------------------------------------
module rrect(w, h, r) { translate([r, r]) offset(r = r) square([w - 2 * r, h - 2 * r]); }
module hexagon(af) { circle(d = af / cos(30), $fn = 6); }
module slot_v(l, w) { hull() { translate([0, w / 2]) circle(d = w); translate([0, l - w / 2]) circle(d = w); } }

// ---- Front bezel -----------------------------------------------------------------------
module frame() {
  difference() {
    bezel_body();
    translate([-(XE + clear), wall_tb, lip]) cube([glass_w + 2 * clear, glass_h + 2 * clear, D]);
    translate([-(XE - overlap), wall_tb + clear + overlap, -1])
      linear_extrude(D + 2) rrect(glass_w - 2 * overlap, glass_h - 2 * overlap, 2);
    for (sx = [-1, 1], y = [screw_y, H - screw_y])
      translate([sx * (W / 2 - wall_side / 2), y, 0]) {
        translate([0, 0, -1]) cylinder(d = m3_hole, h = D + 2);
        translate([0, 0, -0.01]) cylinder(d1 = m3_csk_d, d2 = m3_hole, h = (m3_csk_d - m3_hole) / 2);
      }
  }
}
module bezel_body() {                           // with a small chamfer on the front edge
  c = 0.8;
  translate([-W / 2, 0, 0]) hull() {
    translate([c, c, 0]) linear_extrude(0.01) rrect(W - 2 * c, H - 2 * c, corner_r - c);
    translate([0, 0, c]) linear_extrude(D - c) rrect(W, H, corner_r);
  }
}

// ---- Back body ---------------------------------------------------------------------------
module rail_side() {                             // right side; the left is its mirror
  difference() {
    union() {
      intersection() {                          // rail over the bezel edge and the glass strip
        translate([XR, -lift, D]) cube([W / 2 - XR, H + lift, rail_t]);
        translate([-W / 2, -lift, D - 1]) linear_extrude(rail_t + 2) rrect(W, H + lift, corner_r);
      }
      // side wall around the board, standing on the rail
      translate([XR, Y0, Z0 - 0.01]) cube([XW - XR, H - 2 * Y0, ZI - Z0 + 0.01]);
      // hinge lug, joined to the wall
      translate([XE - 0.4, 0, 0]) rotate([0, -90, 0]) linear_extrude(XE - 0.4 - XW + 0.01)
        hull() {
          translate([Z0 - 0.01, pivot_y - 9]) square([0.02, 18]);
          translate([ZP, pivot_y]) circle(r = 5);
        }
    }
    // front screws: nuts in pockets on the back of the rail
    for (y = [screw_y, H - screw_y])
      translate([W / 2 - wall_side / 2, y, 0]) {
        translate([0, 0, D - 1]) cylinder(d = m3_hole, h = rail_t + 2);
        translate([0, 0, Z0 - rail_nut_depth]) linear_extrude(rail_nut_depth + 1) hexagon(nut_af);
      }
    // hinge: bolt from the leg side, nut on the inside face of the wall
    translate([XR - 1, pivot_y, ZP]) rotate([0, 90, 0]) cylinder(d = m3_hole, h = rail_in + 2);
    translate([XR - 1, pivot_y, ZP]) rotate([0, 90, 0]) rotate([0, 0, 30]) linear_extrude(nut_depth + 1) hexagon(nut_af);
  }
}
module body() {
  difference() {
    union() {
      rail_side();
      mirror([1, 0, 0]) rail_side();
      // top and bottom walls (standing on the bezel's top/bottom walls)
      for (y = [Y0, H - wall_tb]) translate([-XW, y, D]) cube([2 * XW, wall_tb - Y0, ZI - D]);
      // posts for the back plate screws
      for (sx = [-1, 1], top = [0, 1])
        translate([sx * boss_x - 4.5, top ? H - boss_reach : Y0, D]) cube([9, boss_reach - Y0, ZI - D]);
    }
    for (sx = [-1, 1], top = [0, 1])
      translate([sx * boss_x, top ? H - POST_Y : POST_Y, ZI - 14]) cylinder(d = m3_pilot, h = 15);
    port_openings();
  }
}
// Openings for the USB-C plugs through the side wall (rounded slots).
PORT_Z = lip + glass_t + port_depth;
module port_openings() {
  for (y = port_y)
    translate([port_side * (XR - 1), wall_tb + clear + y, PORT_Z])
      rotate([0, port_side * 90, 0]) linear_extrude(XE - XR + 2)
        hull() for (dy = [-1, 1]) translate([0, dy * (port_w - port_h) / 2]) circle(d = port_h);
}

// ---- Back plate --------------------------------------------------------------------------
module back_plate() {                            // in place: z from ZI to ZB
  difference() {
    translate([-XW, Y0, ZI]) linear_extrude(back_t) rrect(2 * XW, H - 2 * Y0, 2);
    for (sx = [-1, 1], top = [0, 1])
      translate([sx * boss_x, top ? H - POST_Y : POST_Y, ZI - 1]) cylinder(d = m3_hole, h = back_t + 2);
    // air slots: the low row lets cool air in, the high row lets warm air out
    for (y = [36, H - 32], i = [-8 : 8])
      translate([i * 9, y, ZI - 1]) linear_extrude(back_t + 2) slot_v(18, vent_w);
    // optional cable opening
    if (back_cable)
      translate([cable_x - cable_w / 2, cable_y - cable_h / 2, ZI - 1]) linear_extrude(back_t + 2)
        rrect(cable_w, cable_h, cable_h / 2 - 0.01);
  }
}

// ---- Legs ------------------------------------------------------------------------------------
module leg_profile() {                           // (the 2D x becomes y: along the leg)
  hull() {
    translate([heel, 0]) circle(d = leg_w);
    translate([-leg_len, 0]) circle(d = leg_w);
  }
}
module leg_r_local() {                           // x: 0 = inner face, leg_th = outer face
  difference() {
    union() {
      rotate([90, 0, 90]) linear_extrude(leg_th) leg_profile();
      translate([-foot_in, -leg_len, 0]) rotate([0, 90, 0]) cylinder(r = foot_r, h = foot_in + leg_th);
    }
    translate([-1, 0, 0]) rotate([0, 90, 0]) cylinder(d = m3_hole, h = leg_th + 2);
    translate([leg_th - head_depth, 0, 0]) rotate([0, 90, 0]) cylinder(d = head_d, h = head_depth + 1);
  }
}
module leg_r_placed(a) { translate([XE, pivot_y, ZP]) rotate([-a, 0, 0]) leg_r_local(); }
module leg_print() { rotate([0, 90, 0]) translate([-leg_th, 0, 0]) leg_r_local(); }

// ---- Looking at it -------------------------------------------------------------------------
module dummy_screen() {
  color("#10141c") translate([-XE, wall_tb + clear, lip]) cube([glass_w, glass_h, glass_t]);
  color("#2a3140") translate([-module_w / 2, H / 2 - module_h / 2, lip + glass_t]) cube([module_w, module_h, board_depth - 1]);
  // the two USB-C sockets at the board edge
  color("#b8bcc4") for (y = port_y)
    translate([port_side * (module_w / 2 + 0.01) - (port_side < 0 ? 0 : 7.3), wall_tb + clear + y - 4.5, PORT_Z - 1.6])
      cube([7.3, 9, 3.2]);
}
module assembly(a, screen = true) {
  rotate([90, 0, 0]) rotate([tilt, 0, 0]) {     // stood up: z is up, the screen faces +y
    color("#e9e6df") frame();
    color("#d7d2c8") body();
    color("#cfc9bd") back_plate();
    color("#c9c3b6") { leg_r_placed(a); mirror([1, 0, 0]) leg_r_placed(a); }
    if (screen) dummy_screen();
  }
}

if (part == "frame") frame();
else if (part == "body") translate([0, 0, -D]) body();
else if (part == "back") translate([0, 0, ZB]) mirror([0, 0, 1]) back_plate();
else if (part == "leg") leg_print();
else if (part == "assembly") assembly(leg_open);
else if (part == "assembly_folded") assembly(0);
else if (part == "assembly_bare") assembly(leg_open, false);
else if (part == "echo") echo(W = W, H = H, D = D, ZB = ZB, leg_len = leg_len, heel = heel, contact = [CY, CZ],
                              port_z = PORT_Z, rail_back = Z0, lug_y = [pivot_y - 9, pivot_y + 9]);
