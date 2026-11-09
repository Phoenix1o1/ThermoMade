// =============================================================================
// TermoMade base station enclosure — 2 parts on an Italian 503 wall box
// -----------------------------------------------------------------------------
//  FRONT : shell with display window, CYD screwed to 4 standoffs, sensor
//          chamber (DS18B20) below the screen, top lip + 2 lock screws
//  BASE  : plate screwed to the 503 box + closed mains pocket that sits inside
//          the 503 box (PSU, relay module, terminal block)
//
//  Assembly: base on the 503 box -> wire -> CYD in the front -> plug LV harness
//            -> front held 3.5 mm high, pushed to the wall, slid down (top lip
//            hooks behind the plate) -> 2 x M3x16 self-tapping from below.
//
//  Coordinates: X right, Y up (seen from the room), Z out of the wall.
//  z = 0 is the wall surface, origin at the 503 box centre. Units mm.
//  OpenSCAD 2021.01+. Use the Customizer for live tweaking.
//
//  CYD data (ESP32-2432S028): Sunton dimension drawing + community STEP model
//  (github.com/witnessmenow/ESP32-Cheap-Yellow-Display, Havenview CAD).
//  VERIFY with calipers on your 2-USB board: hole spacing, view-area offset,
//  USB-C / micro-USB positions (only used if usb_slot = true).
// =============================================================================

/* [View] */
// what to show / export
part = "assembly"; // [assembly, exploded, section, front, base, front_print, base_print, fit_test]
show_ghosts = true;
// wall + 503 box context in the assembly view
show_wall = true;
explode = 45;
// section plane X (part = "section")
section_x = 41.5;
// stand the model up (Y up) for screenshots; leave false for printing/export
upright = false;

/* [Box] */
box_w = 122;
box_h = 86;
// side walls of the front shell
wall = 2.4;
// front face thickness
front_t = 2.0;
// vertical edge radius (keep large)
edge_r = 6;
// 45 deg chamfer on the front edge
front_chamfer = 1.5;
// general fit clearance
clr = 0.3;

/* [Base plate & joint] */
plate_t = 5;
// how far the front is lifted to hook the top lip
slide = 3.5;
lip_t = 1.6;
// lip reach below the inner top wall
lip_len = 3.3;
lip_w = 24;
lip_x = [-30, 30];
lock_x = [-40, 40];
// boss for the M3 lock screw [x, y, z]
lock_boss = [12, 6, 6];
// M3 self-tapping pilot
lock_pilot = 2.6;
// hole in the plate that receives the screw tip
lock_pin_d = 3.4;
lock_pin_depth = 4;

/* [503 wall box] */
// fixing-screw spacing (Italian standard 503)
b503_screw_dx = 83.5;
b503_hole_d = 3.8;
// +- slot along X
b503_slot = 2;
// ghost only: approx inner [w, h, depth]
b503_inner = [96, 50, 45];

/* [Mains pocket (inside the 503 box)] */
pocket_w = 72;
pocket_h = 42;
pocket_d = 30;
pocket_wall = 2.0;
// component [x, y, depth] and centre [x, y] (plate coords)
psu_size = [34, 15, 20];
psu_pos = [-15, 10];
relay_size = [50, 19, 26];
relay_pos = [-6, -9];
term_size = [25, 10, 15];
term_pos = [20, 11];
cradle_h = 6;
cradle_t = 1.6;
// low-voltage wire passage pocket -> front cavity
lv_hole_pos = [27, -12];
lv_hole_d = 7;
lv_tube_l = 12;

/* [CYD ESP32-2432S028] */
cyd_w = 86;
cyd_h = 50;
cyd_t = 1.6;
cyd_r = 3;
cyd_hole_dx = 78;
cyd_hole_dy = 42;
cyd_hole_d = 3.2;
// display module on the front
lcd_w = 69;
lcd_h = 50;
lcd_t = 3.6;
// visible area and its X offset from the PCB centre
view_w = 59;
view_h = 45;
view_dx = -2.5;
// tallest part on the back of the PCB
back_comp_h = 4.7;
// plate front face -> PCB back
cyd_back_clear = 7;
// display glass -> inside of the front face
glass_gap = 0.3;
// PCB top edge -> inner top wall
cyd_top_gap = 1.5;
// grow (+) / shrink (-) the window
window_extra = 0;
window_chamfer = 1.0;
standoff_d = 6.5;
// M2.5 self-tapping pilot for the CYD screws
cyd_pilot = 2.2;
// material left in front of the pilot hole
pilot_skin = 1.2;

/* [Openings] */
// small hole in front of the CYD light sensor (position unverified)
ldr_hole = false;
ldr_pos = [-39.5, -14];
ldr_d = 2.5;
// side slot for a USB plug (right wall), PCB-relative Y
usb_slot = false;
usb_slot_y = -9.3;
usb_slot_size = [13, 8];

/* [Sensor chamber] */
// free space below the PCB for the side-entry JST plugs
plug_zone = 10;
divider_t = 2.0;
vent_w = 2.0;
vent_pitch = 5;
front_vent_x = 46;
// solid band in the grille that carries the sensor cradle
vent_skip = 6;
bottom_vent_x = 30;
top_vents = true;
top_vent_x = 45;
sensor = "TO92"; // [TO92, probe6]
wire_notch = [5, 3];
wire_notch_x = -10;

/* [Hidden] */
$fn = 48;
eps = 0.01;

// ----------------------------------------------------------------- derived
in_w = box_w - 2*wall;
in_h = box_h - 2*wall;
in_top = box_h/2 - wall;
in_bot = -box_h/2 + wall;
in_r = max(edge_r - wall, 0.5);

z_pcb_b = plate_t + cyd_back_clear;
z_pcb_t = z_pcb_b + cyd_t;
z_glass = z_pcb_t + lcd_t;
z_fin = z_glass + glass_gap;          // inside of the front face
box_d = z_fin + front_t;              // total depth above the wall

cyd_cx = -view_dx;                    // window centred on the box
cyd_cy = in_top - cyd_top_gap - cyd_h/2;
win_cx = cyd_cx + view_dx;
div_top = cyd_cy - cyd_h/2 - plug_zone;
div_bot = div_top - divider_t;
ch_cy = (div_bot + in_bot)/2;         // sensor chamber centre
ch_h = div_bot - in_bot;

plate_top = in_top - clr;
plate_bot = in_bot + slide + clr;
notch_top = in_bot + lock_boss[1] + slide + clr;

standoff_h = z_fin - z_pcb_t;
pilot_depth = standoff_h + front_t - pilot_skin;

echo(str("TermoMade box: ", box_w, " x ", box_h, " x ", box_d, " mm above the wall, pocket ",
         pocket_w, " x ", pocket_h, " x ", pocket_d, " mm into the 503 box"));
echo(str("CYD centre (", cyd_cx, ", ", cyd_cy, "), sensor chamber height ", ch_h, " mm"));
assert(pocket_w <= b503_screw_dx - 8 - 2, "pocket collides with the 503 screw bosses");
assert(div_bot - in_bot >= 12, "sensor chamber too small");

// ----------------------------------------------------------------- helpers
module rrect(w, h, r) {
    rr = min(r, w/2 - eps, h/2 - eps);
    if (rr > 0) offset(r = rr) square([w - 2*rr, h - 2*rr], center = true);
    else square([w, h], center = true);
}
module slot2d(len, w) { hull() for (s = [-1, 1]) translate([0, s*(len - w)/2]) circle(d = w); }

// ----------------------------------------------------------------- FRONT
module front_outer() {
    hull() {
        linear_extrude(box_d - front_chamfer) rrect(box_w, box_h, edge_r);
        translate([0, 0, box_d - eps]) linear_extrude(eps)
            rrect(box_w - 2*front_chamfer, box_h - 2*front_chamfer, edge_r - front_chamfer);
    }
}

module cyd_holes_xy() {
    for (sx = [-1, 1], sy = [-1, 1])
        translate([cyd_cx + sx*cyd_hole_dx/2, cyd_cy + sy*cyd_hole_dy/2]) children();
}

module standoffs() {
    cyd_holes_xy() translate([0, 0, z_pcb_t]) cylinder(d = standoff_d, h = standoff_h + eps);
}

module divider() {
    difference() {
        translate([-in_w/2, div_bot, plate_t + clr]) cube([in_w, divider_t, z_fin - plate_t - clr + eps]);
        // wire passage for the DS18B20 lead, close to the plate
        translate([wire_notch_x - wire_notch[0]/2, div_bot - 1, plate_t + clr - 1])
            cube([wire_notch[0], divider_t + 2, wire_notch[1] + 1]);
    }
}

module top_lips() {
    // hook behind the plate's top edge; underside 45 deg after a short ledge
    for (x = lip_x) translate([x - lip_w/2, 0, 0])
        rotate([90, 0, 90]) linear_extrude(lip_w)
            polygon([[in_top + eps, 0], [in_top - lip_len, 0],
                     [in_top - lip_len + lip_t, lip_t], [in_top + eps, lip_t]]);
}

module lock_bosses() {
    for (x = lock_x) {
        translate([x - lock_boss[0]/2, in_bot - eps, 0]) cube([lock_boss[0], lock_boss[1] + eps, lock_boss[2]]);
        // 45 deg gusset towards the front face (printable face-down)
        translate([x - lock_boss[0]/2, 0, 0]) rotate([90, 0, 90]) linear_extrude(lock_boss[0])
            polygon([[in_bot - eps, lock_boss[2] - eps], [in_bot + lock_boss[1], lock_boss[2] - eps],
                     [in_bot - eps, lock_boss[2] + lock_boss[1]]]);
    }
}

module sensor_cradle() {
    zc = z_fin + eps;
    if (sensor == "TO92") {
        // two prongs gripping the flat TO-92 body (3.6-3.9 mm), leads towards the wall
        for (s = [-1, 1]) translate([-3, ch_cy + s*(1.9 + 0.8) - 0.8, zc - 8]) cube([6, 1.6, 8]);
    } else {
        translate([0, ch_cy, zc - 8]) difference() {
            cylinder(d = 6.3 + 3.2, h = 8);
            translate([0, 0, -1]) cylinder(d = 6.3, h = 10);
            translate([-1.5, -10, -1]) cube([3, 10, 10]);
        }
    }
}

module window_cut() {
    w = view_w + window_extra; h = view_h + window_extra;
    translate([win_cx, cyd_cy, z_fin - 1]) linear_extrude(front_t + 2) rrect(w, h, 1);
    translate([win_cx, cyd_cy, 0]) hull() {
        translate([0, 0, box_d - window_chamfer]) linear_extrude(eps) rrect(w, h, 1);
        translate([0, 0, box_d + eps]) linear_extrude(eps) rrect(w + 2*window_chamfer, h + 2*window_chamfer, 1 + window_chamfer);
    }
}

module vents_cut() {
    // front grille of the sensor chamber
    n = floor(front_vent_x / vent_pitch);
    for (i = [-n:n]) {
        x = i * vent_pitch;
        if (abs(x) > vent_skip)
            translate([x, ch_cy, z_fin - 1]) linear_extrude(front_t + 2) slot2d(ch_h - 6, vent_w);
    }
    // bottom intake slots (run front-to-back, printable)
    m = floor(bottom_vent_x / vent_pitch);
    for (i = [-m:m]) translate([i*vent_pitch - vent_w/2, -box_h/2 - 1, plate_t + 1.5])
        cube([vent_w, wall + 2, z_fin - plate_t - 3]);
    // top exhaust slots above the CYD
    if (top_vents) {
        k = floor(top_vent_x / vent_pitch);
        for (i = [-k:k]) translate([i*vent_pitch - vent_w/2, in_top - 1, plate_t + 1.5])
            cube([vent_w, wall + 2, z_fin - plate_t - 3]);
    }
}

module front() {
    difference() {
        union() {
            difference() {
                front_outer();
                translate([0, 0, -1]) linear_extrude(z_fin + 1) rrect(in_w, in_h, in_r);
            }
            intersection() {
                translate([0, 0, -eps]) linear_extrude(z_fin + 2*eps) rrect(in_w + 2*eps, in_h + 2*eps, in_r);
                union() { standoffs(); divider(); top_lips(); lock_bosses(); sensor_cradle(); }
            }
        }
        window_cut();
        vents_cut();
        // CYD screw pilots (M2.5 self-tapping, from the back)
        cyd_holes_xy() translate([0, 0, z_pcb_t - eps]) cylinder(d = cyd_pilot, h = pilot_depth);
        // lock screws from below: pilot through bottom wall + boss, plus head seat
        for (x = lock_x) translate([x, -box_h/2 - 1, plate_t/2]) rotate([-90, 0, 0])
            cylinder(d = lock_pilot, h = wall + lock_boss[1] + 2);
        if (ldr_hole) translate([cyd_cx + ldr_pos[0], cyd_cy + ldr_pos[1], z_fin - 1]) cylinder(d = ldr_d, h = front_t + 2);
        if (usb_slot) translate([box_w/2 - wall - 1, cyd_cy + usb_slot_y, z_pcb_b - 2])
            rotate([0, 90, 0]) linear_extrude(wall + 2) rrect(usb_slot_size[1], usb_slot_size[0], 2);
    }
}

// ----------------------------------------------------------------- BASE
module cradle(size, pos) {
    // rectangular frame with gaps in the middle of each side (wires, fingers)
    translate([pos[0], pos[1], -cradle_h]) linear_extrude(cradle_h + eps) difference() {
        rrect(size[0] + 2*(clr + cradle_t), size[1] + 2*(clr + cradle_t), 1);
        rrect(size[0] + 2*clr, size[1] + 2*clr, 0.5);
        square([size[0]*0.4, size[1] + 10], center = true);
        square([size[0] + 10, size[1]*0.4], center = true);
    }
}

module pocket() {
    translate([0, 0, -pocket_d]) difference() {
        linear_extrude(pocket_d + eps) difference() {
            rrect(pocket_w, pocket_h, 3);
            rrect(pocket_w - 2*pocket_wall, pocket_h - 2*pocket_wall, 1.5);
        }
        // cable-tie slots in the long walls
        for (x = [-20, 20], y = [-1, 1]) translate([x - 2, y*pocket_h/2 - 2, pocket_d*0.45]) cube([4, 4, 2.4]);
    }
    cradle(psu_size, psu_pos);
    cradle(relay_size, relay_pos);
    cradle(term_size, term_pos);
    // low-voltage sleeve: keeps the LV wires apart from the mains side
    translate([lv_hole_pos[0], lv_hole_pos[1], -lv_tube_l]) cylinder(d = lv_hole_d + 3.2, h = lv_tube_l + eps);
}

module base() {
    difference() {
        union() {
            translate([0, (plate_top + plate_bot)/2, 0]) linear_extrude(plate_t)
                rrect(in_w - 2*clr, plate_top - plate_bot, max(in_r - clr, 0.5));
            pocket();
        }
        // 503 fixing screws (slotted)
        for (s = [-1, 1]) translate([s*b503_screw_dx/2, 0, -1]) linear_extrude(plate_t + 2)
            hull() for (d = [-b503_slot, b503_slot]) translate([d, 0]) circle(d = b503_hole_d);
        // rebates for the front's top lips (wall side)
        for (x = lip_x) translate([x - lip_w/2 - clr, in_top - lip_len - clr, -1])
            cube([lip_w + 2*clr, lip_len + 10, lip_t + clr + 1]);
        // notches for the lock bosses + holes that receive the screw tips
        for (x = lock_x) {
            translate([x - lock_boss[0]/2 - clr, plate_bot - 1, -1])
                cube([lock_boss[0] + 2*clr, notch_top - plate_bot + 1, plate_t + 2]);
            translate([x, notch_top - eps, plate_t/2]) rotate([-90, 0, 0]) cylinder(d = lock_pin_d, h = lock_pin_depth);
        }
        // LV passage
        translate([lv_hole_pos[0], lv_hole_pos[1], -lv_tube_l - 1]) cylinder(d = lv_hole_d, h = lv_tube_l + plate_t + 2);
    }
}

// ----------------------------------------------------------------- ghosts
// back-side parts of the CYD [x0, x1, y0, y1, height], PCB coords (from the STEP model)
cyd_back_parts = [
    [-43.0, -17.6, -9.0, 9.0, 3.0],    // ESP32-WROOM module (antenna at x = -43)
    [-32.8, -22.8, -24.9, -20.6, 3.5], // JST connector (side entry)
    [-16.8, -6.8, -24.9, -20.6, 3.5],  // JST connector (side entry)
    [11.4, 19.0, 20.6, 23.8, 4.6],     // connector
    [38.1, 43.6, -13.2, -5.4, 2.5],    // micro-USB (original board)
    [37.5, 42.0, 4.0, 14.0, 3.5],      // connector / USB-C on 2-USB boards (verify)
    [-15.0, -10.0, -16.5, -11.5, 1.6], // IC
    [-31.4, -27.9, 17.2, 23.2, 4.7],   // button
    [-35.4, -31.9, 17.2, 23.2, 4.7],   // button
    [-2.6, 12.4, -24.9, -9.9, 1.8]     // microSD socket (card from the bottom edge)
];

module cyd_ghost() {
    translate([cyd_cx, cyd_cy, z_pcb_b]) {
        color("darkgreen") difference() {
            linear_extrude(cyd_t) rrect(cyd_w, cyd_h, cyd_r);
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx*cyd_hole_dx/2, sy*cyd_hole_dy/2, -1]) cylinder(d = cyd_hole_d, h = 5);
        }
        color("silver") translate([-lcd_w/2, -lcd_h/2, cyd_t]) cube([lcd_w, lcd_h, lcd_t - 0.1]);
        color("black") translate([view_dx - view_w/2, -view_h/2, cyd_t + lcd_t - 0.1]) cube([view_w, view_h, 0.1]);
        color("dimgray") for (p = cyd_back_parts) translate([p[0], p[2], -p[4]]) cube([p[1] - p[0], p[3] - p[2], p[4]]);
    }
}

module pocket_ghosts() {
    color("royalblue") translate([psu_pos[0] - psu_size[0]/2, psu_pos[1] - psu_size[1]/2, -psu_size[2]]) cube(psu_size);
    color("steelblue") translate([relay_pos[0] - relay_size[0]/2, relay_pos[1] - relay_size[1]/2, -relay_size[2]]) cube(relay_size);
    color("lightgreen") translate([term_pos[0] - term_size[0]/2, term_pos[1] - term_size[1]/2, -term_size[2]]) cube(term_size);
}

module sensor_ghost() {
    color("black") translate([0, ch_cy, z_fin - 6]) cylinder(d = 4.6, h = 4.5);
}

module wall_ghost() {
    color([0.85, 0.85, 0.8, 0.25]) difference() {
        translate([-90, -70, -3]) cube([180, 140, 3]);
        translate([-b503_inner[0]/2, -b503_inner[1]/2, -5]) cube([b503_inner[0], b503_inner[1], 10]);
    }
    color([0.6, 0.6, 0.6, 0.25]) translate([0, 0, -b503_inner[2]/2]) difference() {
        cube([b503_inner[0] + 4, b503_inner[1] + 4, b503_inner[2]], center = true);
        translate([0, 0, 2]) cube([b503_inner[0], b503_inner[1], b503_inner[2]], center = true);
    }
}

module ghosts() { cyd_ghost(); pocket_ghosts(); sensor_ghost(); }

// ----------------------------------------------------------------- output
module front_c() { color("whitesmoke") front(); }
module base_c()  { color("lightgray") base(); }

module show() { if (upright) rotate([90, 0, 0]) children(); else children(); }

show() if (part == "assembly") {
    front_c(); base_c();
    if (show_ghosts) ghosts();
    if (show_wall) wall_ghost();
} else if (part == "exploded") {
    translate([0, 0, explode]) { front_c(); if (show_ghosts) { cyd_ghost(); sensor_ghost(); } }
    base_c();
    if (show_ghosts) pocket_ghosts();
} else if (part == "section") {
    intersection() {
        union() { front_c(); base_c(); if (show_ghosts) ghosts(); }
        translate([section_x, -100, -100]) cube([200, 200, 200]);
    }
} else if (part == "ghost_cyd") {
    cyd_ghost(); sensor_ghost();
} else if (part == "ghost_pocket") {
    pocket_ghosts();
} else if (part == "front") {
    front_c();
} else if (part == "base") {
    base_c();
} else if (part == "front_print") {
    // face down on the bed
    translate([0, 0, box_d]) rotate([180, 0, 0]) front();
} else if (part == "base_print") {
    // plate front face down, pocket pointing up
    translate([0, 0, plate_t]) rotate([180, 0, 0]) base();
} else if (part == "fit_test") {
    // 15-minute print: front face around the display + standoffs.
    // Lay the CYD on it: holes on the standoffs, screen centred in the window.
    translate([0, 0, box_d]) rotate([180, 0, 0]) intersection() {
        front();
        translate([cyd_cx - cyd_w/2 - 5, cyd_cy - cyd_h/2 - 5, z_pcb_t]) cube([cyd_w + 10, cyd_h + 10, 50]);
    }
}
