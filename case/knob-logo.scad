/* Knob-face logo concepts (2026-11-01): a disk, Commodore-themed, bold enough to print.
 * Theme: the 1541's own medium, the 5¼" disk (jacket, hub hole, head slot, index hole, write-protect notch),
 * drawn in big square pixels like a PETSCII / C64 character cell. No Commodore trademarks.
 * CHECKED (check_logo.py, same limits as the lid labels: islands >= 1.2, plastic >= 0.9, cuts >= 0.65):
 *   D passes. A and C FAIL: 0.75 mm pixels leave 0.75 islands (hub centre, index pillar) and 0.75 strips, and
 *   1.2 mm pixels would leave only 10 across the face, too coarse for hub + slot + notch. Kept for reference only.
 * The knob prints top face down, so the logo is debossed 0.6 into the bed face, as the lid labels are.
 *
 *   logo = "A" pixel outline | "C" pixel solid (the features stand up) | "D" smooth outline
 *   part = "tokens": three Ø18 discs, A C D, 2 mm, letter on the back, printed face down
 *          "2d": the chosen logo flat (for knob() and renders)
 */
logo = "D"; part = "tokens";
PX = 0.75;                 // pixel: 15 px = 11.25 mm, corners inside Ø16 (the knob is Ø18 with ribs)
DEEP = 0.8; T = 2.0;      // as the lid labels

// '#' = debossed. Row 0 is the top (the label end); the head slot is at the bottom.
A = ["###############",
     "#.............#",
     "#.....###...###",
     "#....#####..#..",
     "#....##.##..###",
     "#....#####....#",
     "#.....###.....#",
     "#.............#",
     "#......#..#...#",
     "#.....###.....#",
     "#.....###.....#",
     "#.....###.....#",
     "#......#......#",
     "#.............#",
     "###############"];
C = ["###############",
     "###############",
     "######...######",
     "#####.....###..",
     "#####..#..#####",
     "#####.....#####",
     "######...######",
     "###############",
     "#######.##.####",
     "######...######",
     "######...######",
     "######...######",
     "#######.#######",
     "###############",
     "###############"];   // A inverted inside the jacket (generated from A)

module pixels(m) {
    n = len(m);
    translate([-n * PX / 2, n * PX / 2]) for (r = [0 : n - 1], c = [0 : len(m[r]) - 1])
        if (m[r][c] == "#") translate([c * PX, -(r + 1) * PX]) square(PX + 0.001);
}
module jacket(S) difference() {     // rounded square less the write-protect notch (right edge, near the top)
    offset(r = 0.6, $fn = 24) offset(delta = -0.6) square(S, center = true);
    translate([S / 2 - 1.0, S / 2 - 3.0]) square([2, 1.0]);
}
module smooth() {       // D: the same disk drawn smooth, 0.8 mm strokes
    S = 11.25; W = 0.8;
    difference() { jacket(S); offset(delta = -W) jacket(S); }
    translate([0, 1.6]) difference() { circle(d = 4.2, $fn = 64); circle(d = 1.5, $fn = 32); }   // hub ring
    translate([0, -3.0]) hull() for (y = [-0.9, 0.9]) translate([0, y]) circle(d = 1.3, $fn = 24);   // head slot
    translate([2.3, -1.8]) circle(d = 1.0, $fn = 20);                                       // index hole
}
module logo_2d(l = logo) {
    if (l == "A") pixels(A); else if (l == "C") pixels(C); else smooth();
}

module token(l) difference() {
    cylinder(d = 18, h = T, $fn = 96);
    translate([0, 0, -1]) linear_extrude(1 + DEEP) mirror([1, 0, 0]) logo_2d(l);        // bed face: reads turned over
    translate([0, 0, T - 0.4]) linear_extrude(1) text(l, size = 7, font = "Liberation Sans:style=Bold", halign = "center", valign = "center");
}
if (part == "tokens") token("D");
else logo_2d();
