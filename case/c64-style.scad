// The case's C64-style details (STYLE = "c64", the default). Included by picod64-case.scad; uses its sizes.
// From the feasibility study (case/study/c64-cues/, 2026-11-05), printed and approved the same day.
// No Commodore logo or name is used. Every cut is on the outside only: nothing reaches the lid's inside.
//   front lid: 45-degree shoulders, a band of ribs, a dark frame groove round the OLED window, and a recess
//              round each LED for a printed PWR/ACT badge (breadbin POWER badge)
//   rear lid:  shoulders, V ribs on the step's walls, and a recess with round ends for the name badge
// Bed-face rules (CASE.md): cuts >= 0.65, what's left between them >= 0.9.

/* Name badge: a recess with round ends in the rear lid, as the breadbin has, taking a printed badge with raised
   letters (or a blank with a vinyl label). Proportion from the overall size of a C64 badge backing (199 x 20 mm on a
   machine of about 404 mm: roughly half its width); none of its design is used. */
POCKET = [50, 8];          // 47% of the case's width. 8 wide: the pocket's floor prints as an 8 mm bridge (clean, 1st print)
POCKET_D = 1.0;            // leaves 1.0 of roof, the labels' minimum. The lid's name is not cut in this style
BADGE_GAP = 0.2;           // each side
BADGE_T = 0.8;             // 0.2 below the lid's face: room for thin double-sided tape
LETTER_H = 0.6; LETTER_S = 5.2;
pocket_x = cx((IEC_X[0] + IEC_X[1]) / 2); pocket_y = cy(STEP_Y) + 7.0;   // where the name was

/* PWR/ACT badges: a badge round each LED, the hole at its left end over the LED's lens */
LB_H = 5.1;                // the LEDs are 6 apart on the PCB: leaves 0.9 of lid between the recesses
LB_L = 22.4;               // ends clear of the knob
LB_GAP = 0.15; LB_HOLE = 3.3; LB_T = 0.8; LB_D = 1.0; LB_S = 3.2;
/* Printed top down, each recess's floor is a bridge with the LED hole in it: the slicer then prints the hole's edge in
   mid-air and picks the bridge direction badly (first print, 2026-11-05: a mess; the rear recess, no hole, was clean).
   So the hole is closed in steps: the layers just over the recess leave out a strip as wide as the hole, straight across
   the recess (a plain bridge), the next ones a square (short bridges onto that), and only then is the hole round.
   Second print: clean (2026-11-05). */
LB_STEP = 0.32;            // two 0.16 layers, 1.6 at 0.2
LB_SQ = LED_HOLE + 0.2;    // strip and square: 0.1 clear of the round hole each side (touching it breaks the mesh)

/* Ribs, shoulders, frame */
RIB_W = 0.8; RIB_LAND = 1.0; RIB_D = 0.4;   // grooves in the top, on the bed
RIB_Y0 = 47.5; RIB_N = 9;                   // the band, front lid: clear of PWR/ACT and the step
SHOULDER = 2.0;                             // 45-degree top edges, as the 1541's shoulders
V = 0.8;                                    // V grooves on walls: 45 degrees, so no support
FRAME_W = 0.9; FRAME_D = 0.4; FRAME_GAP = 0.9; // frame groove round the OLED window (Datasette's); 0.9 wide clears the LED recesses

module stadium(l, w) hull() for (s = [-1, 1]) translate([s * (l - w) / 2, 0]) circle(d = w, $fn = 64);
module lb_outline(l, h) translate([l / 2 - h / 2, 0]) stadium(l, h);  // origin: the round left end's centre, the LED
px_ = cx(LED_PWR[0]); py_ = cy(LED_PWR[1]); ax_ = cx(LED_ACT[0]); ay_ = cy(LED_ACT[1]);
module led_pockets() for (p = [[px_, py_], [ax_, ay_]]) translate([p[0], p[1], FRONT_TOP - LB_D]) {
    linear_extrude(LB_D + 1) lb_outline(LB_L, LB_H);
    translate([0, 0, -LB_STEP]) linear_extrude(LB_STEP + EPS) intersection() { lb_outline(LB_L, LB_H); translate([-LB_SQ / 2, -LB_H]) square([LB_SQ, 2 * LB_H]); }   // strip, inside the recess
    translate([-LB_SQ / 2, -LB_SQ / 2, -2 * LB_STEP]) cube([LB_SQ, LB_SQ, LB_STEP + EPS]);     // square
}
// the badges: the back on the bed, a small chamfer there so the first layer's squash doesn't foul the recess
module led_badge_plate() { l = LB_L - 2 * LB_GAP; h = LB_H - 2 * LB_GAP;
    difference() {
        hull() { linear_extrude(EPS) lb_outline(l - 0.4, h - 0.4); translate([0, 0, 0.2]) linear_extrude(LB_T - 0.2) lb_outline(l, h); }
        translate([0, 0, -1]) cylinder(d = LB_HOLE, h = 5, $fn = 48);
    }
}
module lb_text(t) translate([(LB_HOLE / 2 + 0.8 + LB_L - LB_H / 2 - 1.6) / 2 , 0]) label_2d(t, LB_S);   // between the hole and the right-hand end
module led_badge_letters(t) translate([0, 0, LB_T - EPS]) linear_extrude(0.6 + EPS) lb_text(t);
module led_badge(t) { led_badge_plate(); led_badge_letters(t); }

module pocket_cut() translate([pocket_x, pocket_y, REAR_TOP - POCKET_D]) linear_extrude(POCKET_D + 1) stadium(POCKET[0], POCKET[1]);
module badge_plate() { l = POCKET[0] - 2 * BADGE_GAP; w = POCKET[1] - 2 * BADGE_GAP;
    hull() { linear_extrude(EPS) stadium(l - 0.6, w - 0.6); translate([0, 0, 0.3]) linear_extrude(BADGE_T - 0.3) stadium(l, w); } }
module badge_letters() translate([0, 0, BADGE_T - EPS]) linear_extrude(LETTER_H + EPS) label_2d(NAME, LETTER_S);
module badge() { badge_plate(); badge_letters(); }

win_x = cx(OLED_X0 + OLED_W / 2); win_y = cy(OLED_Y0 + OLED_D / 2 + OLED_WIN_DY) - (ROOF - 0.5) / 2;
win_w = OLED_WIN_W + 2 * ROOF;    win_d = OLED_WIN_D + ROOF + 0.5;
module frame_2d() difference() {
    translate([win_x, win_y]) square([win_w + 2 * (FRAME_GAP + FRAME_W), win_d + 2 * (FRAME_GAP + FRAME_W)], center = true);
    translate([win_x, win_y]) square([win_w + 2 * FRAME_GAP, win_d + 2 * FRAME_GAP], center = true);
}
module frame_cut() translate([0, 0, FRONT_TOP - FRAME_D]) linear_extrude(FRAME_D + 1) frame_2d();
module foot2d() hull() for (x = [R_OUT, OUT_W - R_OUT], y = [R_OUT, OUT_L - R_OUT]) translate([x, y]) circle(r = R_OUT, $fn = 48);   // facet for facet as rounded_box()
module ribs_top() for (i = [0 : RIB_N - 1]) translate([-1, RIB_Y0 + i * (RIB_W + RIB_LAND), FRONT_TOP - RIB_D]) cube([OUT_W + 2, RIB_W, RIB_D + 1]);
// a V groove along x or y (taller than wide: its faces stay well inside 45 degrees in the print position; each stops
// short of the corner it would meet, so the walls' grooves don't touch at a line)
module vx(y, z) translate([V, y, z]) scale([1, 1, 1.3]) rotate([45, 0, 0]) translate([0, -V / 2, -V / 2]) cube([OUT_W - 2 * V, V, V]);
module vy(x, z) translate([x, YS + V, z]) scale([1, 1, 1.3]) rotate([0, 45, 0]) translate([-V / 2, 0, -V / 2]) cube([V, OUT_L - YS - R_OUT - V, V]);
module ribs_rear() for (i = [0 : 2]) { z = FRONT_TOP + 0.9 + i * 1.6;
    vx(YS, z); vy(0, z); vy(OUT_W, z); }
// shoulders: the top edges cut at 45. A cutter, not an intersection: its plane crosses the lid's walls cleanly and
// it touches nothing below the shoulder, so the lid stays facet for facet what it is. M: how far it runs past the cut
M = 0.5;
module shoulder_cut(top, foot_y0, y1) intersection() {
    difference() {
        translate([-5, -5, top - SHOULDER - M]) cube([OUT_W + 10, OUT_L + 10, SHOULDER + 2 * M + 1]);
        hull() {
            translate([0, 0, top - SHOULDER - M]) linear_extrude(EPS) offset(delta = M) intersection() { foot2d(); translate([-1, foot_y0]) square([OUT_W + 2, 500]); }
            translate([0, 0, top + M]) linear_extrude(EPS) offset(delta = -SHOULDER - M) intersection() { foot2d(); translate([-1, foot_y0]) square([OUT_W + 2, 500]); }
        }
    }
    translate([-6, y1[0], top - 5]) cube([OUT_W + 12, y1[1] - y1[0], 10]);
}
// the front: its sides and front edge; it runs on under the step, where the rear part covers it
module c64_front_cuts() { shoulder_cut(FRONT_TOP, -100, [-5, YS + 0.5]); frame_cut(); ribs_top(); led_pockets(); }
module c64_rear_cuts() { shoulder_cut(REAR_TOP, YS - 1, [YS - 5, OUT_L + 5]); ribs_rear(); pocket_cut(); }
// the badges as fitted, each 0.2 below the face (tape)
module badges_fit(letters = false, front = true, rear = true) {
    if (front) for (q = [[px_, py_, "PWR"], [ax_, ay_, "ACT"]]) translate([q[0], q[1], FRONT_TOP - LB_D + 0.2])
        if (letters) led_badge_letters(q[2]); else led_badge_plate();
    if (rear) translate([pocket_x, pocket_y, REAR_TOP - POCKET_D + 0.2]) if (letters) badge_letters(); else badge_plate();
}
