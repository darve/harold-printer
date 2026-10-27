// harold-printer case: Raspberry Pi Pico 2 (or Pico 2 W) plus a USB-A
// screw-terminal breakout, with a push button in the lid.
//
// Layout, seen from above: the Pico sits at the left with its micro-USB
// pointing out of the left wall; the breakout sits at the right with its
// USB-A socket pointing out of the right wall. Wires run between them.
//
// Render one part at a time:
//   openscad -D 'part="base"' -o base.stl harold_case.scad
//   openscad -D 'part="lid"'  -o lid.stl  harold_case.scad
// part="both" shows them side by side, part="assembly" shows them stacked.
//
// All sizes are mm. MEASURE YOUR BREAKOUT and update the breakout_* values
// before printing; the defaults are the Adafruit 3629 board (50 x 20 x 11.1).

part = "both";  // base | lid | both | assembly

/* [Print tuning] */
wall      = 2.0;   // side wall thickness
floor_t   = 2.0;   // base floor thickness
lid_t     = 2.0;   // lid top thickness
clearance = 0.4;   // gap around boards and between lid lip and walls
$fn       = 48;

/* [Pico 2 / Pico 2 W] */
pico_l      = 51.0;
pico_w      = 21.0;
pico_pcb_t  = 1.0;
pico_hole_dx = 47.0;   // hole spacing along the length
pico_hole_dy = 11.4;   // hole spacing across the width
standoff_h  = 4.0;     // PCB underside above the floor
standoff_d  = 5.0;
screw_pilot = 1.8;     // for M2 self-tapping screws
usb_micro_w = 12.0;    // cutout for the micro-USB plug (connector is 8 mm)
usb_micro_h = 8.0;

/* [USB-A screw-terminal breakout] */
breakout_l = 50.0;     // along the case length, socket end included
breakout_w = 20.0;
breakout_h = 11.1;     // overall height including the socket
breakout_pcb_t = 1.6;
usb_a_w = 15.0;        // cutout for the USB-A plug (socket is 13.1 x 5.7)
usb_a_h = 8.5;
usb_a_z = 1.6;         // socket bottom above the breakout's resting surface
cradle_h = 4.0;        // height of the walls that hold the breakout in place

/* [Lid] */
button_d = 12.4;       // hole for a 12 mm panel-mount push button
lid_lip  = 3.0;        // depth of the lip that plugs into the base
led_hole_d = 3.0;      // over the Pico's onboard LED; 0 to disable

/* [Layout] */
gap = 18.0;            // space between the boards: wiring, and the button body hangs here

inner_l = clearance + pico_l + gap + breakout_l + clearance;
inner_w = max(pico_w, breakout_w) + 2 * clearance + 4;
inner_h = standoff_h + max(pico_pcb_t + 4, breakout_h) + 8;  // room for wires and button body

outer_l = inner_l + 2 * wall;
outer_w = inner_w + 2 * wall;
outer_h = floor_t + inner_h;

pico_x0 = wall + clearance;                    // left edge of the Pico PCB
pico_y0 = wall + (inner_w - pico_w) / 2;
pico_z0 = floor_t + standoff_h;                // underside of the Pico PCB

brk_x0 = outer_l - wall - clearance - breakout_l;
brk_y0 = wall + (inner_w - breakout_w) / 2;
brk_z0 = floor_t;                              // breakout rests on the floor

// Pico mounting hole centres, relative to the PCB corner
pico_holes = [
  [(pico_l - pico_hole_dx) / 2,                (pico_w - pico_hole_dy) / 2],
  [(pico_l - pico_hole_dx) / 2,                (pico_w + pico_hole_dy) / 2],
  [(pico_l + pico_hole_dx) / 2,                (pico_w - pico_hole_dy) / 2],
  [(pico_l + pico_hole_dx) / 2,                (pico_w + pico_hole_dy) / 2],
];

module rounded_box(l, w, h, r = 2) {
  hull() for (x = [r, l - r], y = [r, w - r]) translate([x, y, 0]) cylinder(r = r, h = h);
}

module base() {
  difference() {
    rounded_box(outer_l, outer_w, outer_h);

    // hollow
    translate([wall, wall, floor_t]) cube([inner_l, inner_w, inner_h + 1]);

    // micro-USB opening, centred on the Pico's USB connector (on top of the PCB)
    translate([-1, pico_y0 + pico_w / 2 - usb_micro_w / 2,
               pico_z0 + pico_pcb_t + 1.5 - usb_micro_h / 2])
      cube([wall + 2, usb_micro_w, usb_micro_h]);

    // USB-A opening for the printer cable
    translate([outer_l - wall - 1, brk_y0 + breakout_w / 2 - usb_a_w / 2, brk_z0 + usb_a_z])
      cube([wall + 2, usb_a_w, usb_a_h]);
  }

  // Pico standoffs
  for (h = pico_holes)
    translate([pico_x0 + h[0], pico_y0 + h[1], floor_t - 0.01])
      difference() {
        cylinder(d = standoff_d, h = standoff_h);
        cylinder(d = screw_pilot, h = standoff_h + 1);
      }

  // Breakout cradle: low walls on three sides; the socket end sits against
  // the case wall. Push-fit, so no screws are needed.
  cradle_t = 1.6;
  translate([brk_x0 - clearance - cradle_t, brk_y0 - clearance - cradle_t, floor_t - 0.01])
    difference() {
      cube([breakout_l + clearance + cradle_t + 0.01,
            breakout_w + 2 * clearance + 2 * cradle_t, cradle_h]);
      translate([cradle_t, cradle_t, -1])
        cube([breakout_l + clearance + 1, breakout_w + 2 * clearance, cradle_h + 2]);
    }
}

module lid() {
  difference() {
    union() {
      rounded_box(outer_l, outer_w, lid_t);
      // lip that plugs into the base
      translate([wall + clearance / 2, wall + clearance / 2, lid_t - 0.01])
        difference() {
          cube([inner_l - clearance, inner_w - clearance, lid_lip]);
          translate([1.6, 1.6, -1]) cube([inner_l - clearance - 3.2, inner_w - clearance - 3.2, lid_lip + 2]);
        }
    }

    // push button, centred over the gap between the boards
    translate([pico_x0 + pico_l + gap / 2, outer_w / 2, -1]) cylinder(d = button_d, h = lid_t + lid_lip + 2);

    // LED window over the Pico's LED (near the USB end, offset from centre).
    // The lid prints upside down, so y is mirrored here.
    if (led_hole_d > 0)
      translate([pico_x0 + 4.5, outer_w - (pico_y0 + pico_w / 2 + 3.5), -1]) cylinder(d = led_hole_d, h = lid_t + 2);
  }
}

if (part == "base") base();
else if (part == "lid") lid();
else if (part == "assembly") {
  base();
  translate([0, outer_w, outer_h + lid_t]) rotate([180, 0, 0]) lid();
} else {
  base();
  translate([0, outer_w + 10, 0]) lid();
}

echo(str("Outer size: ", outer_l, " x ", outer_w, " x ", outer_h + lid_t, " mm"));
