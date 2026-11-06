// =====================================================================================
//  AUDIO PLAYER - rugged industrial enclosure for the 2.8" ESP32-S3 touch display audio player
//  Overall size follows your sketch: 180 (W) x 80 (H) x 40 (D) mm, 2 x 40 mm speakers,
//  2.8" display (69.2 x 50 mm module) in the middle, 3 keys on top, USB-C + microSD on the back.
//
//  Coordinate frame used in this file (viewed from the FRONT):
//     X = width  (0 .. W, left -> right)
//     Y = height (0 .. H, bottom -> top)
//     Z = depth  (0 = front face, D = back face)
//
//  Parts (set PART below, or pass -D 'PART="front"' on the command line):
//     "front"    front plate  (print front face DOWN, no supports needed)
//     "shell"    rear shell   (print back face DOWN, supports only for the tiny pad chamfers)
//     "bracket"  key bracket  (holds 3 tact switches under the top wall)
//     "keys"     3 key caps   (power, vol+, vol-)
//     "cover"    2 honeycomb speaker guards (print back face down, rim up)
//     "assembly" everything together with ghost PCB / speakers / battery
//     "exploded" exploded view
//     "section"  cut-away through the middle key (check the stack-up)
// =====================================================================================

PART = "assembly";
GHOST = false;
$fn = 40;

// ---------------- overall -----------------
W = 180;  H = 80;  D = 40;
PLATE = 4;            // front plate thickness (white face plate + orange trim are glued on in front of it)
OC = 10;              // corner chamfer of the octagon outline
OVERLAY_T = 1.6;  TRIM_T = 1.2;  LET_T = 0.8;  OV_INSET = 3.5;
LIP   = 2;            // solid front lip in front of display / speakers
WALL  = 3.5;          // shell wall
R     = 6;            // corner radius
CH    = 1.6;          // outer chamfer (front plate front edge and shell rear edge)
SEAM  = 0.6;          // chamfer at the parting line (makes the V-groove "panel gap")
TOL   = 0.15;         // fit clearance

// ---------------- display module (ES3C28P / FNK0104B) -----------------
DISP_W = 69.2;  DISP_H = 50.0;        // glass / LCD outline in landscape
DISP_CX = 90;   DISP_CY = 40;
WIN_W = 69.40;  WIN_H = 50.15;        // screen opening (the display module slides in from behind)
WIN_CHAMFER = 0;                      // optional chamfer on the front edge of the opening (e.g. 0.8)
GLASS_RECESS = 1.5;                   // how far the glass sits behind the front surface
PANEL_OUT = 3.0;  PANEL_IN = 2.2;     // engraved frame line around the screen (outer / inner offset from the opening)
STACK = 4.3;                          // glass front -> PCB front (1.0 TP + 0.5 + 2.3 LCD + 0.5)
PCB_L = 86;  PCB_W = 50;              // PCB outline
PCB_HOLE_X = 78;  PCB_HOLE_Y = 42;    // mounting holes (dia 3.2)

// ---------------- speakers -----------------
SPK_D   = 40;   SPK_CLR = 0.6;        // speaker cone / press-fit clearance
// SQ-157E 1.5" (40 mm) speaker: front rim ~42 mm (estimated from the photo), ear holes 49 mm apart (55 mm over the ears),
// ear plane 6 mm behind the front face, basket 37 mm, magnet 23 mm, total depth 20 mm. Ears point UP / DOWN (no room sideways).
SPK_RIM_D = 42;  SPK_HOLE = 49;  SPK_EAR_DZ = 6;  SPK_DEPTH = 20;  SPK_POST_D = 6;  SPK_PILOT = 2.2;   // M2.5 self-tapping
SPK_CY  = 40;
SPK_STYLE = "cover";                  // "cover" = bolt-on honeycomb guard (like the sample photo), "mesh" = diamond mesh moulded in the plate
COVER_R_OUT = 21;  COVER_R_IN = 17.5; // guard: outer radius of the rim / radius of the honeycomb area
COVER_H = 4;  COVER_T = 1.6;          // guard: rim + lug height / honeycomb floor thickness
EAR_R = 24;  EAR_LUG = 4.2;           // guard: bolt circle radius (4 lugs on the diagonals) / lug radius
HEX_R = 2.0;  HEX_WEB = 0.95;         // honeycomb: hole corner radius / wall between holes
// equal spacing: gap(plate edge -> guard) == gap(guard -> screen frame) on both sides
FRAME_L = DISP_CX - WIN_W/2 - PANEL_OUT;
SPK_CX  = [FRAME_L / 2, W - FRAME_L / 2];
COLLAR_OD = 44;  COLLAR_H = 6;
MESH_R = 18;  MESH_BAR = 0.9;  MESH_PITCH = 3.4;  MESH_T = 1.2;   // diamond wire-mesh grille

// ---------------- name plate (recessed panel with letters flush to the front = embossed look) -----------------

// ---------------- screws (M3 socket head, visible on the front) -----------------
SCREWS = [for (x = [16, 48, 132, 164], y = [9.5, 70.5]) [x, y]];   // 8 real M3 button-head (ISO 7380) bolts, none above the screen

// ---------------- keys on top (left -> right: POWER, VOL+, VOL-) -----------------
KX = [75, 90, 105];  KZ = 14;
KEY_HOLE = 7.4;  CHAMBER = 10;

// ---------------- rear panel -----------------
USB_X = 72;  SD_X = 108;  PORT_Y = 10;      // PORT_Y was 13: moved down 3 mm to make room for the larger battery
PAD_T = 1.5;                         // mounting pad on the inside of the back wall (wall = 5 mm locally, extension flange sits on it)
PORT_RECESS = 3.5;                   // outside recess depth: leaves a 1.5 mm wall so the plug / card reaches the socket
// USB-C panel-mount extension (sample photo: ear holes 17 mm apart)
USB_HOLE_SPACING = 17;  USB_HOLE_D = 3.2;  USB_W = 10.0;  USB_H = 3.8;
// microSD (TF) extension cable with ears (sample drawing: 29.5 mm over the ears, 3 mm thick body)
SD_W = 19.5;  SD_H = 3.1;     // microSD slot (no screw holes, extension is held by the support shelf)

// ---------------- battery bay (3.7V 2000mAh pouch cell, adjust to your cell) -----------------
// Cell 55.05 x 67.38 x 9.19 mm INCLUDING its wire; the wire leaves on a short (55.05 mm) edge, so the 67.38 mm length runs along X.
// BAT_CY keeps ~1.9 mm of room above (top wall) and below (USB-C pad / microSD slot).
BAT_X = 67.38;  BAT_Y = 55.05;  BAT_Z = 9.19;  BAT_CX = 90;  BAT_CY = 46.5;

// =====================================================================================
//  helpers
// =====================================================================================
module rr(w, h, r) { translate([r, r]) offset(r = r) square([w - 2*r, h - 2*r]); }
module oct_poly() { polygon([[OC,0],[W-OC,0],[W,OC],[W,H-OC],[W-OC,H],[OC,H],[0,H-OC],[0,OC]]); }
module inset_rr(i) { offset(delta = -i) oct_poly(); }
module slice(z, i) { translate([0, 0, z]) linear_extrude(0.01) inset_rr(i); }
module cslab(z0, z1, c0, c1) {         // rounded slab with chamfers c0 (at z0) and c1 (at z1)
  hull() {
    slice(z0, c0);
    slice(z0 + c0, 0);
    slice(z1 - c1 - 0.01, 0);
    slice(z1 - 0.01, c1);
  }
}
module tear_y(x, y, z, d, h) {            // teardrop hole (apex towards -z = up when the shell is printed back-down)
  translate([x, y, z]) rotate([-90, 0, 0]) linear_extrude(h) hull() { circle(d = d, $fn = 40); translate([0, d/2*1.4142]) square(0.01, center = true); }
}
module cyl_y(x, y, z, d, h) { translate([x, y, z]) rotate([-90, 0, 0]) cylinder(d = d, h = h); }   // axis +Y

// =====================================================================================
//  FRONT PLATE
// =====================================================================================
module mesh_bars2d() {                    // 45 degree crossing bars (SPK_STYLE = "mesh")
  intersection() {
    circle(r = MESH_R + 0.5, $fn = 64);
    union() for (a = [45, -45]) rotate(a) for (i = [-7 : 7])
      translate([i * MESH_PITCH - MESH_BAR / 2, -MESH_R - 1]) square([MESH_BAR, 2 * MESH_R + 2]);
  }
}

module window_cut() {
  translate([DISP_CX - WIN_W/2, DISP_CY - WIN_H/2, -0.01]) cube([WIN_W, WIN_H, PLATE + 0.02]);
  if (WIN_CHAMFER > 0) hull() {
    translate([DISP_CX - WIN_W/2 - WIN_CHAMFER, DISP_CY - WIN_H/2 - WIN_CHAMFER, -0.01]) cube([WIN_W + 2*WIN_CHAMFER, WIN_H + 2*WIN_CHAMFER, 0.01]);
    translate([DISP_CX - WIN_W/2, DISP_CY - WIN_H/2, WIN_CHAMFER]) cube([WIN_W, WIN_H, 0.01]);
  }
}

module nameplate_cut() {                  // recessed panel
  translate([NP_CX - NP_W/2, NP_CY - NP_H/2, -0.01]) linear_extrude(NP_DEPTH + 0.01) rr(NP_W, NP_H, 0.8);
}
module nameplate_emboss() {               // letters + hex rivets, flush with the front face
  translate([NP_CX, NP_CY, 0]) linear_extrude(NP_DEPTH + 0.02) {
    resize([NP_TEXT_W, 0], auto = true) text(NP_TEXT, size = 5, halign = "center", valign = "center", font = "DejaVu Sans:style=Bold");
    for (s = [-1, 1]) translate([s * (NP_W/2 - 3), 0]) circle(d = 2.6, $fn = 6);
  }
}

// ---- honeycomb speaker guard: separate bolt-on part (like the sample photo) -----------------------------
module hex_holes2d(r_area) {              // flat-top honeycomb
  dy = 2 * HEX_R * cos(30) + HEX_WEB;
  dx = dy * cos(30);
  for (i = [-6 : 6], j = [-6 : 6]) {
    x = i * dx;  y = j * dy + abs(i % 2) * dy / 2;
    if (norm([x, y]) + HEX_R <= r_area - 0.4) translate([x, y]) circle(r = HEX_R, $fn = 6);
  }
}

module cover() {                          // local frame: z = 0 is the back face (sits on the plate), +z points outwards
  difference() {
    union() {
      rotate_extrude($fn = 96) polygon([[COVER_R_IN, 0], [COVER_R_OUT, 0], [COVER_R_OUT, COVER_H - 0.8],
                                        [COVER_R_OUT - 0.8, COVER_H], [COVER_R_IN + 0.4, COVER_H], [COVER_R_IN, COVER_H - 0.4]]);
      linear_extrude(COVER_T) difference() { circle(r = COVER_R_IN + 0.01, $fn = 96); hex_holes2d(COVER_R_IN); }
      for (a = [45, 135, 225, 315]) rotate(a) linear_extrude(COVER_H) hull() {         // mounting lugs
        translate([EAR_R, 0]) circle(r = EAR_LUG, $fn = 32);
        translate([COVER_R_OUT - 1, 0]) circle(r = 2.2, $fn = 32);
      }
      for (a = [22.5 : 45 : 360]) rotate(a)                                           // rivet heads on the rim
        translate([(COVER_R_IN + COVER_R_OUT) / 2, 0, COVER_H - 0.01]) {
          cylinder(d = 2.2, h = 0.4, $fn = 24);
          translate([0, 0, 0.4]) cylinder(d1 = 2.2, d2 = 1.4, h = 0.4, $fn = 24);
        }
    }
    for (a = [45, 135, 225, 315]) rotate(a) translate([EAR_R, 0, -0.01]) {            // M3 socket-head screws
      cylinder(d = 3.4, h = COVER_H + 0.1, $fn = 24);
      translate([0, 0, COVER_H - 3.0]) cylinder(d = 6.2, h = 3.1, $fn = 24);
    }
  }
}

module collar_arcs(cx) {                  // four press-fit clips around the speaker (leave room for the PCB beside it)
  for (a = [45, 135, 225, 315]) intersection() {
    translate([cx, SPK_CY, PLATE - 0.01]) cylinder(d = COLLAR_OD, h = COLLAR_H + 0.01, $fn = 64);
    translate([cx, SPK_CY, PLATE - 1]) linear_extrude(COLLAR_H + 2)
      polygon([[0, 0], [40 * cos(a - 20), 40 * sin(a - 20)], [40 * cos(a + 20), 40 * sin(a + 20)]]);
  }
}

module front_plate() {
  union() {
    difference() {
      union() {
        cslab(0, PLATE, CH, SEAM);
        // tongue that locks into the shell
        translate([0, 0, PLATE - 0.01]) linear_extrude(2.01)
          difference() { inset_rr(1.8 + TOL); inset_rr(WALL); }
        // two screw posts per speaker for the mounting ears (top face = ear plane, 6 mm behind the speaker front)
        for (cx = SPK_CX, s = [-1, 1]) translate([cx, SPK_CY + s * SPK_HOLE/2, PLATE - 0.01]) cylinder(d = SPK_POST_D, h = LIP + SPK_EAR_DZ - PLATE + 0.01, $fn = 32);
        // PCB standoffs
        for (sx = [-1, 1], sy = [-1, 1])
          translate([DISP_CX + sx * PCB_HOLE_X / 2, DISP_CY + sy * PCB_HOLE_Y / 2, PLATE - 0.01])
            cylinder(d = 7, h = GLASS_RECESS + STACK - PLATE + 0.01);
      }
      window_cut();
      for (cx = SPK_CX) {
        translate([cx, SPK_CY, LIP]) cylinder(d = SPK_RIM_D + SPK_CLR, h = COLLAR_H + PLATE, $fn = 96);   // speaker rim pocket (rim rests on the 2 mm lip)
        for (s = [-1, 1]) translate([cx, SPK_CY + s * SPK_HOLE/2, LIP + SPK_EAR_DZ - 5]) cylinder(d = SPK_PILOT, h = 5.1, $fn = 20);   // ear screw pilot holes
        translate([cx, SPK_CY, -0.01]) cylinder(r = COVER_R_IN, h = LIP + 0.02, $fn = 96);            // open speaker window
      }
      // PCB standoff holes (M3 self-tapping)
      for (sx = [-1, 1], sy = [-1, 1])
        translate([DISP_CX + sx * PCB_HOLE_X / 2, DISP_CY + sy * PCB_HOLE_Y / 2, 2.6])
          cylinder(d = 2.6, h = STACK + 1);
      // alignment holes for the white face plate pins
      for (p = OVL_PINS) translate([p[0], p[1], -0.01]) cylinder(d = PIN_D + 0.4, h = 1.5, $fn = 24);
      // plain M3 clearance holes (button-head bolts sit on the white face plate)
      for (p = SCREWS) translate([p[0], p[1], -0.01]) cylinder(d = 3.4, h = PLATE + 0.1, $fn = 24);
    }
  }
}

// =====================================================================================
//  REAR SHELL
// =====================================================================================
module boss(p) {                         // full-height post standing on the back wall (no overhangs when printed back-down)
  x = p[0]; y = p[1];
  yw = (y < H/2) ? WALL - 0.5 : H - WALL + 0.5;
  translate([0, 0, PLATE + 2.01]) linear_extrude(D - WALL - PLATE - 2) hull() {
    translate([x, y]) circle(d = 8, $fn = 32);
    translate([x - 4, yw - 0.005]) square([8, 0.01]);
  }
}

module key_pad() {                       // thick pad under the top wall (guide + chamber + bracket screws)
  hull() {
    translate([64, H - WALL - 3, 7]) cube([52, 3.01, 11]);
    translate([64, H - WALL - 0.1, 7]) cube([52, 0.11, 14]);
  }
}

module end_rib(y) {
  hull() {
    translate([-0.5, y - 2.5, 8]) cube([1, 5, D - 8]);
    translate([-2.0, y - 1.5, 8]) cube([1, 3, D - 8]);
  }
}

module top_rail(x) {
  hull() {
    translate([x, H - 1, 8]) cube([4, 1.5, D - 8]);
    translate([x + 1, H + 2.9, 9]) cube([2, 0.1, D - 9]);
  }
}

module bat_cradle() {
  x0 = BAT_CX - BAT_X/2 - 0.6;  x1 = BAT_CX + BAT_X/2 + 0.6;
  y0 = BAT_CY - BAT_Y/2 - 0.6;  y1 = BAT_CY + BAT_Y/2 + 0.6;
  zb = D - WALL;  zt = zb - BAT_Z - 1;
  for (cx = [0, 1], cy = [0, 1]) {
    px = cx ? x1 : x0;  py = cy ? y1 : y0;
    sx = cx ? 1 : -1;   sy = cy ? 1 : -1;
    // L shaped corner bracket, arms 10 mm
    la = (sy < 0) ? 3 : 2;   // short arms: keep the port area (bottom) and the key bracket (top) free
    translate([min(px, px + sx*1.6), min(py, py + sy*la), zt]) cube([1.6, la, zb - zt + 0.01]);
    translate([min(px, px + sx*10), min(py, py + sy*1.6), zt]) cube([10, 1.6, zb - zt + 0.01]);
  }
}

module sd_frame() {                      // pocket for a microSD extension PCB on the inside of the back wall
  translate([SD_X - 10, PORT_Y - 7, D - WALL - 4.5]) difference() {
    cube([20, 14, 4.6]);
    translate([2, 1.5, -0.1]) cube([16, 11, 5]);
  }
}

// mounting pads for the USB-C and microSD extension flanges (inside of the back wall)
USB_PAD = [30, 13];  SD_PAD = [36, 13];
module port_pads() {
  for (q = [[USB_X, USB_PAD]])
    translate([q[0] - q[1][0]/2, PORT_Y - q[1][1]/2, D - WALL - PAD_T]) cube([q[1][0], q[1][1], PAD_T + 0.01]);
}
// support shelf under the microSD extension cable (from your sketch): block standing on the bottom wall, against the pad,
// top touches the underside of the flat cable; width = 19.5 mm body between the ears, ears stay free for the screws
SD_SUP_W = 19.5;  SD_SUP_L = 18;
module sd_support() {
  y0 = WALL - 0.01;  y1 = PORT_Y - SD_H/2;      // top flush with the lower edge of the slot
  translate([SD_X - SD_SUP_W/2, y0, D - WALL - SD_SUP_L]) cube([SD_SUP_W, y1 - y0, SD_SUP_L + 0.01]);
}
module rear_shell() {
  difference() {
    union() {
      difference() {
        cslab(PLATE, D, SEAM, CH);
        translate([0, 0, PLATE - 0.01]) linear_extrude(D - WALL - PLATE + 0.01) inset_rr(WALL);   // octagon cavity (was a rounded rectangle that broke through the chamfered corners)
        translate([0, 0, PLATE - 0.01]) linear_extrude(2.01) inset_rr(1.8);             // rabbet for the plate tongue
      }
      // armor ribs / roll rails
      for (y = [25, 40, 55]) { end_rib(y); translate([W, 0, 0]) mirror([1, 0, 0]) end_rib(y); }
      top_rail(12);  top_rail(W - 16);
      // internals
      for (p = SCREWS) boss(p);
      key_pad();
      bat_cradle();
      port_pads();
      sd_support();
    }
    // heat-set insert holes (M3)
    for (p = SCREWS) translate([p[0], p[1], PLATE - 0.01]) cylinder(d = 4.2, h = 6.5, $fn = 24);
    // key holes, chambers, bracket screw holes
    for (kx = KX) {
      cyl_y(kx, H - WALL - 4, KZ, KEY_HOLE, 9);            // round key holes (top arc is a short 7.4 mm bridge)
      cyl_y(kx, H - WALL - 3.01, KZ, CHAMBER, 1.6);
    }
    for (sx = [67.5, 112.5]) cyl_y(sx, H - WALL - 3.1, KZ, 1.9, 4.7);
    // rear: USB-C panel-mount extension: slot, 17 mm ear holes (through), captive M3 nut pockets on the pad, outside recess
    hull() for (sx = [-1, 1]) translate([USB_X + sx * (USB_W - USB_H) / 2, PORT_Y, D - WALL - PAD_T - 1]) cylinder(d = USB_H, h = WALL + PAD_T + 2, $fn = 24);
    for (sx = [-1, 1]) translate([USB_X + sx * USB_HOLE_SPACING / 2, PORT_Y, D - WALL - PAD_T - 1]) cylinder(d = USB_HOLE_D, h = WALL + PAD_T + 2, $fn = 24);
    for (sx = [-1, 1]) translate([USB_X + sx * USB_HOLE_SPACING / 2, PORT_Y, D - WALL - PAD_T - 0.01]) rotate(30) cylinder(d = 6.7, h = 2.5, $fn = 6);
    translate([USB_X - 5.5, PORT_Y - 3.2, D - PORT_RECESS]) cube([11, 6.4, PORT_RECESS + 1]);
    // rear: microSD slot 19.5 x 3.1 + shallow outside recess
    translate([SD_X - SD_W / 2, PORT_Y - SD_H / 2, D - WALL - 1]) cube([SD_W, SD_H, WALL + 2]);
    translate([SD_X - 11.25, PORT_Y - 2.75, D - 2]) cube([22.5, 5.5, 3]);
    // rear: speaker vent slots
    for (cx = SPK_CX, y = [28, 34, 40, 46, 52]) hull() {
      translate([cx - 9.8, y, D - WALL - 1]) cylinder(d = 2.4, h = WALL + 2, $fn = 16);
      translate([cx + 9.8, y, D - WALL - 1]) cylinder(d = 2.4, h = WALL + 2, $fn = 16);
    }
    // rubber foot recesses (bottom)
    for (p = [[25, 11], [155, 11], [25, 33], [155, 33]]) cyl_y(p[0], -0.01, p[1], 10, 1.01);   // 4 foot recesses, symmetric on the bottom face (z 6..16 and 28..38)
  }
}

// =====================================================================================
//  KEY BRACKET (3 x 6x6x5 tact switches) and KEY CAPS
// =====================================================================================
module key_bracket() {
  y_top = H - WALL - 3;                  // 73.5  (touches the underside of the key pad)
  difference() {
    translate([64, y_top - 6.1, 7]) cube([52, 6.1, 14]);
    for (kx = KX) {
      translate([kx - 3.15, y_top - 4.6, KZ - 3.15]) cube([6.3, 4.7, 6.3]);                  // switch pocket
      for (sx = [-1, 1], sz = [-1, 1])                                                       // leg notches
        translate([kx + sx * 3.9 - 0.75, y_top - 4.6, KZ + sz * 2.25 - 0.7]) cube([1.5, 4.7, 1.4]);
    }
    for (sx = [67.5, 112.5]) {
      cyl_y(sx, y_top - 6.2, KZ, 2.4, 7);
      cyl_y(sx, y_top - 6.2, KZ, 4.4, 2.1);
    }
  }
}

module sym_power() {                     // power symbol, opening pointing to -y
  difference() { circle(r = 2.1, $fn = 32); circle(r = 1.5, $fn = 32); translate([-0.7, -3]) square([1.4, 3]); }
  translate([-0.35, -2.4]) square([0.7, 2.6]);
}
module sym_plus()  { square([3.6, 0.8], center = true); square([0.8, 3.6], center = true); }
module sym_minus() { square([3.6, 0.8], center = true); }

module keycap(k = 0) {
  difference() {
    union() {
      cylinder(d = 9.6, h = 1.2);
      translate([0, 0, 1.2]) cylinder(d = 7.0, h = 5.6);
      translate([0, 0, 6.8]) cylinder(d1 = 7.0, d2 = 6.0, h = 0.5);
    }
    translate([0, 0, 6.9]) linear_extrude(0.5) {
      if (k == 0) sym_power(); else if (k == 1) sym_plus(); else sym_minus();
    }
  }
}

// =====================================================================================
//  GHOST PARTS (preview only)
// =====================================================================================
module ghost_parts() {
  color("green", 0.55) translate([DISP_CX - PCB_L/2, DISP_CY - PCB_W/2, GLASS_RECESS + STACK]) cube([PCB_L, PCB_W, 1.6]);
  color("darkgreen", 0.45) translate([DISP_CX - PCB_L/2 + 6, DISP_CY - PCB_W/2 + 4, GLASS_RECESS + STACK + 1.6]) cube([PCB_L - 12, PCB_W - 8, 4.7]);
  color("black", 0.8) translate([DISP_CX - DISP_W/2, DISP_CY - DISP_H/2, GLASS_RECESS]) cube([DISP_W, DISP_H, STACK]);
  for (cx = SPK_CX) color("silver", 0.8) translate([cx, SPK_CY, LIP]) {
    cylinder(d = SPK_RIM_D, h = 2, $fn = 48);
    cylinder(d = 37, h = SPK_DEPTH - 6, $fn = 48);
    translate([0, 0, SPK_DEPTH - 6]) cylinder(d = 23, h = 6, $fn = 48);
    translate([-5, -27.5, SPK_EAR_DZ - 0.8]) cube([10, 55, 0.8]);
  }
  color("orange", 0.5) translate([BAT_CX - BAT_X/2, BAT_CY - BAT_Y/2, D - WALL - BAT_Z]) cube([BAT_X, BAT_Y, BAT_Z]);
}


ORANGE = "#ff7a00";  WHITE = "#ffffff";
COLORWAY = "orange_white";            // "orange_white" | "black_orange" | "purple_orange"
BODY   = COLORWAY == "black_orange" ? "#262626" : COLORWAY == "purple_orange" ? "#5b2a86" : ORANGE;   // shell, front plate, trim
ACCENT = COLORWAY == "orange_white" ? WHITE : ORANGE;                                                 // face plate, letters, keys
NP_TEXT = "ESP AUDIO PLAYER";  NP_W = 62;  NP_H = 6.6;  NP_CX = 90;  NP_CY = 8.4;  NP_TEXT_W = 46;
RING_IN = 17.6;  RING_OUT = 21.5;  BZ_L = 3.5;  BZ_B = 2.4;  BZ_T = 3.5;

PIN_D = 2.4;  PIN_L = 1.2;
OVL_PINS = [[75, 72], [105, 72], [75, 8], [105, 8]];                       // overlay -> front plate
TRIM_PINS = concat(                                                         // trim -> overlay
  [[DISP_CX - WIN_W/2 - BZ_L/2, DISP_CY], [DISP_CX + WIN_W/2 + BZ_L/2, DISP_CY]],
  [for (cx = SPK_CX, a = [45, 225]) [cx + (RING_IN + RING_OUT)/2 * cos(a), SPK_CY + (RING_IN + RING_OUT)/2 * sin(a)]],
  [[NP_CX - 22, NP_CY], [NP_CX + 22, NP_CY]]);
module hex_perf() {
  intersection() {
    circle(r = 17.5, $fn = 64);
    union() for (i = [-6 : 6], j = [-6 : 6]) { x = i * 3.6 + abs(j % 2) * 1.8;  y = j * 3.12;
      if (norm([x, y]) <= 15.4) translate([x, y]) circle(d = 2.4, $fn = 16); }
  }
}
// white face plate (1.6 mm): screen window, perforated speaker grilles, 8 bolt holes
module overlay_part() {
  translate([0, 0, -OVERLAY_T]) linear_extrude(OVERLAY_T) difference() {
    offset(delta = -OV_INSET) oct_poly();
    translate([DISP_CX - WIN_W/2, DISP_CY - WIN_H/2]) square([WIN_W, WIN_H]);
    for (cx = SPK_CX) translate([cx, SPK_CY]) hex_perf();
    for (p = SCREWS) translate(p) circle(d = 3.4, $fn = 24);
    for (p = TRIM_PINS) translate(p) circle(d = PIN_D + 0.4, $fn = 24);
  }
  for (p = OVL_PINS) translate([p[0], p[1], -0.01]) cylinder(d = PIN_D, h = PIN_L + 0.01, $fn = 24);   // locating pins on the back
}
module nameplate2d() { translate([NP_CX - NP_W/2, NP_CY - NP_H/2]) rr(NP_W, NP_H, 1.2); }
// orange trim (1.2 mm): screen bezel, 2 speaker rings, nameplate base
module trim_part() {
  translate([0, 0, -OVERLAY_T - TRIM_T]) linear_extrude(TRIM_T) {
    difference() {
      translate([DISP_CX - WIN_W/2 - BZ_L, DISP_CY - WIN_H/2 - BZ_B]) square([WIN_W + 2*BZ_L, WIN_H + BZ_B + BZ_T]);
      translate([DISP_CX - WIN_W/2, DISP_CY - WIN_H/2]) square([WIN_W, WIN_H]);
    }
    for (cx = SPK_CX) translate([cx, SPK_CY]) difference() { circle(r = RING_OUT, $fn = 96); circle(r = RING_IN, $fn = 96); }
    nameplate2d();
  }
  for (p = TRIM_PINS) translate([p[0], p[1], -OVERLAY_T - 0.01]) cylinder(d = PIN_D, h = PIN_L + 0.01, $fn = 24);   // locating pins on the back
}
// white raised letters (0.8 mm) on the nameplate
module letters2d() { translate([NP_CX, NP_CY]) offset(delta = 0.08) resize([NP_TEXT_W, 0], auto = true) text(NP_TEXT, size = 3.6, halign = "center", valign = "center", font = "DejaVu Sans:style=Bold"); }
// LETTER JIG: lies over the nameplate, letters drop into letter-shaped windows, glue, lift the jig off.
// Walls hug the left, right and bottom edges of the nameplate (no top wall - the screen bezel is there).
JIG_T = 0.6;  JIG_WALL = 1.5;  JIG_CLR = 0.15;
module letter_jig() {        // design frame: nameplate front at z = -OVERLAY_T - TRIM_T
  zf = -OVERLAY_T - TRIM_T;
  x0 = NP_CX - NP_W/2 - JIG_CLR - JIG_WALL;  x1 = NP_CX + NP_W/2 + JIG_CLR + JIG_WALL;
  y0 = NP_CY - NP_H/2 - JIG_CLR - JIG_WALL;  y1 = NP_CY + NP_H/2 + 2;
  difference() {
    union() {
      translate([x0, y0, zf - JIG_T]) cube([x1 - x0, y1 - y0, JIG_T]);                                  // top sheet
      translate([x0, y0, zf - 0.01]) difference() {                                                      // walls, left / right / bottom
        cube([x1 - x0, NP_H + 2*JIG_CLR + JIG_WALL + 0.5, TRIM_T - 0.2]);
        translate([JIG_WALL, JIG_WALL, -1]) cube([NP_W + 2*JIG_CLR, 20, 5]);
      }
    }
    translate([0, 0, zf - JIG_T - 1]) linear_extrude(JIG_T + 2) offset(delta = JIG_CLR) letters2d();     // letter windows
    for (s = [-1, 1]) translate([NP_CX + s * (NP_W/2 - 3.2), NP_CY, zf - JIG_T - 1]) cylinder(d = 4.4, h = JIG_T + 2, $fn = 32);   // clear the bolt heads
    translate([NP_CX - 4, y1 - 3, zf - JIG_T - 1]) cube([8, 4, JIG_T + 2]);                                // finger notch
  }
}
module letters_part() { translate([0, 0, -OVERLAY_T - TRIM_T - LET_T]) linear_extrude(LET_T + 0.01) letters2d(); }

module faux_head(d = 3.6, h = 1.1, sd = 0.5) {   // domed round head with hex socket, flat base at z = 0, dome towards +z
  R = d / 2;
  difference() {
    rotate_extrude($fn = 40) polygon(concat([[0, 0], [R, 0], [R, 0.3*h]], [for (t = [10 : 10 : 90]) [R*cos(t), 0.3*h + 0.7*h*sin(t)]]));
    translate([0, 0, h - sd]) cylinder(d = d * 0.42, h = sd + 0.1, $fn = 6);
  }
}
HEAD_POS = concat(
  [for (cx = SPK_CX, a = [0, 90, 180, 270]) [cx + (RING_IN + RING_OUT)/2 * cos(a), SPK_CY + (RING_IN + RING_OUT)/2 * sin(a)]],
  [for (s = [-1, 1]) [NP_CX + s * (NP_W/2 - 3.2), NP_CY]]);
module heads_in_place() { color("silver") for (p = HEAD_POS) translate([p[0], p[1], -OVERLAY_T - TRIM_T]) mirror([0, 0, 1]) faux_head(); }
module bolts_in_place() { color("silver") for (p = SCREWS) translate([p[0], p[1], -OVERLAY_T]) mirror([0, 0, 1]) faux_head(5.7, 1.65, 0.8); }

module caps_in_place() {
  for (i = [0 : 2]) color(ACCENT) translate([KX[i], H - WALL - 3 + 0.4, KZ]) rotate([-90, 0, 0]) keycap(i);
}

// =====================================================================================
//  OUTPUT
// =====================================================================================
// The model is written as seen from the FRONT with X to the right; phys() mirrors in X so the exported parts are the real thing.
module phys() { translate([W, 0, 0]) mirror([1, 0, 0]) children(); }

module print_front()   { phys() front_plate(); }
module print_overlay() { translate([0, 0, OVERLAY_T]) phys() overlay_part(); }
module print_trim()    { translate([0, 0, OVERLAY_T + TRIM_T]) phys() trim_part(); }
module print_jig()     { translate([0, 0, OVERLAY_T + TRIM_T + JIG_T]) phys() letter_jig(); }
module print_letters() { translate([0, 0, OVERLAY_T + TRIM_T + LET_T]) phys() letters_part(); }
module print_heads_small() { for (i = [0 : 9]) translate([(i % 5) * 6, floor(i / 5) * 6, 0]) faux_head(3.6, 1.1, 0.5); }      // 8 speaker-ring + 2 nameplate
module print_heads_big()   { for (i = [0 : 7]) translate([(i % 4) * 9, floor(i / 4) * 9, 0]) faux_head(5.7, 1.65, 0.8); }         // 8 front corner bolts (printed alternative to real M3 button-head screws)
module print_shell()   { translate([0, H, D]) rotate([180, 0, 0]) phys() rear_shell(); }
module print_bracket() { translate([-64, 21, -(H - WALL - 3 - 6.1)]) rotate([90, 0, 0]) phys() key_bracket(); }
module print_keys()    { for (i = [0 : 2]) translate([i * 14, 0, 0]) keycap(i); }

if (PART == "front")        print_front();
else if (PART == "overlay") print_overlay();
else if (PART == "trim")    print_trim();
else if (PART == "letters") print_letters();
else if (PART == "jig")     print_jig();
else if (PART == "heads_small") print_heads_small();
else if (PART == "heads_big")   print_heads_big();
else if (PART == "shell")   print_shell();
else if (PART == "bracket") print_bracket();
else if (PART == "keys")    print_keys();
else if (PART == "section") phys() difference() {
  union() { color(BODY) front_plate(); color(ACCENT) overlay_part(); color(BODY) trim_part(); color(BODY) rear_shell(); color("peru") key_bracket(); caps_in_place(); ghost_parts(); }
  translate([90, -5, -5]) cube([100, 100, 100]);
}
else phys() {                            // assembly
  color(BODY) front_plate();
  color(ACCENT) overlay_part();
  color(BODY) trim_part();
  color(ACCENT) letters_part();
  heads_in_place();  bolts_in_place();
  color(BODY) rear_shell();
  color("peru") key_bracket();
  caps_in_place();
  if (GHOST) ghost_parts();
}
