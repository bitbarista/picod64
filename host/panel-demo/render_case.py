#!/usr/bin/env python3
"""The front panel demo's pictures, from the case model itself (case/picod64-case.scad): the lid from straight above
(img/case-top.png), the knob alone, to turn (img/knob.png), the whole case (img/case-view.png), and where things
are in mm (img/geom.json). Re-run after changing the case. Needs OpenSCAD, Pillow, numpy."""
import json, os, re, subprocess, tempfile
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); CASE = os.path.join(HERE, '..', '..', 'case'); SCAD = os.path.abspath(os.path.join(CASE, 'picod64-case.scad'))
IMG = os.path.join(HERE, 'img'); T = tempfile.mkdtemp()
TOP_PX, KNOB_PX, VIEW_PX = 1880, 420, 760
def scad(body, out, *args):
    f = os.path.join(T, 'w.scad'); open(f, 'w').write('include <%s>\npart="none";\n%s\n' % (SCAD, body))
    r = subprocess.run(['openscad', '--colorscheme=Tomorrow', *args, '-o', out, f], capture_output=True, text=True); assert os.path.exists(out), r.stderr
    return r.stderr
e = scad('echo(str("GEOM ",OUT_W," ",OUT_L," ",cx(SHAFT_X)," ",cy(SHAFT_Y)," ",KNOB_D," ",cx(OLED_X0+OLED_W/2)," ",cy(OLED_Y0+7.35+7.35)," ",cx(LED_PWR[0])," ",cy(LED_PWR[1])," ",cx(LED_ACT[0])," ",cy(LED_ACT[1])));', os.path.join(T, 'g.echo'))
v = [float(x) for x in re.search(r'GEOM ([^"]+)', open(os.path.join(T, 'g.echo')).read()).group(1).split()]
G = dict(zip('w l kx ky kd ox oy px py ax ay'.split(), v)); G.update(oled_w=29.42, oled_h=14.7, knob_box=v[4] + 1.6)   # knurls stand 0.8 proud (module knob)
for p in 'tray lid_front_raw lid_rear_raw knob_raw badges_front_fit letters_front_fit badges_rear_fit letters_rear_fit'.split():
    subprocess.run(['openscad', '-D', 'part="%s"' % p, '-o', os.path.join(T, p + '.stl'), SCAD], capture_output=True, check=True)
imp = lambda c, p: 'color(%s) import("%s");\n' % (c, os.path.join(T, p + '.stl'))
BODY = imp('TRAY_COLOUR', 'tray') + imp('LID_COLOUR', 'lid_front_raw') + imp('LID_REAR_COLOUR', 'lid_rear_raw') + 'color("#1a1a1a") lid_labels();\noled_fitted();\n' \
    + ''.join(imp(c, p) for c, p in [('BADGE_COLOUR', 'badges_front_fit'), ('BADGE_INK', 'letters_front_fit'),
                                      ('BADGE_COLOUR', 'badges_rear_fit'), ('BADGE_INK', 'letters_rear_fit')])   # STYLE c64
LEDS = lambda g, r: '''for (l = [[LED_PWR, "%s"], [LED_ACT, "%s"]]) color(l[1]) translate([cx(l[0][0]), cy(l[0][1]), PCB_TOP + LED_STANDOFF]) {
 cylinder(d = 3.0, h = LED_LENS - 1.5, $fn = 32); translate([0, 0, LED_LENS - 1.5]) sphere(d = 3.0, $fn = 32); }\n''' % (g, r)
LOGO = 'render() intersection() { translate([0, 0, KNOB_H]) vcut() logo_2d(KNOB_LOGO); cylinder(d = KNOB_D, h = KNOB_H, $fn = 72); }'
def key(path):                                    # the background goes: alpha from "is it the backdrop?"
    a = np.asarray(Image.open(path).convert('RGB')).astype(int); bg = a[2, 2]
    return a, (np.abs(a - bg).sum(axis=2) > 18)
def rgba(a, m): return Image.fromarray(np.dstack([a.astype(np.uint8), (m * 255).astype(np.uint8)]), 'RGBA')

# Straight down the renderer's flat light shows no cuts, and the badges are the lid's colour: shade them as the real
# case shows them, by shadow. The badges' gaps, the ribs and the OLED frame groove, in the badge letters' colour.
SHADE = """color(BADGE_INK) {   // (OpenSCAD 2021 draws a translucent colour solid)
  translate([0, 0, FRONT_TOP + 0.7]) linear_extrude(0.01) {
    frame_2d();
    intersection() { offset(delta = -SHOULDER) foot2d(); for (i = [0 : RIB_N - 1]) translate([0, RIB_Y0 + i * (RIB_W + RIB_LAND)]) square([OUT_W, RIB_W]); }
    for (q = [[px_, py_], [ax_, ay_]]) translate([q[0], q[1]]) difference() { lb_outline(LB_L, LB_H); lb_outline(LB_L - 2 * LB_GAP, LB_H - 2 * LB_GAP); }
  }
  translate([pocket_x, pocket_y, REAR_TOP + 0.7]) linear_extrude(0.01) difference() { stadium(POCKET[0], POCKET[1]); stadium(POCKET[0] - 2 * BADGE_GAP, POCKET[1] - 2 * BADGE_GAP); }
}
"""
# the lid, straight down, unlit LEDs (the page lights them). Twice the size, shrunk: smooth edges
p = os.path.join(T, 'top.png')
scad(BODY + SHADE + LEDS('#173d1c', '#4a1411'), p, '--camera=53,45,300,53,45,0', '--projection=o', '--viewall', '--autocenter', '--imgsize=6400,5600')
a, m = key(p)
rows = m.sum(axis=1); full = np.where(rows > rows.max() * 0.9)[0]; y0, y1 = full[0], full[-1] + 1          # (not the USB, proud at the rear)
cols = np.where(m[y0:y1].any(axis=0))[0]; x0, x1 = cols[0], cols[-1] + 1
assert abs((x1 - x0) / (y1 - y0) - G['w'] / G['l']) < 0.01, 'the crop is not the case outline'
# as printed: the renderer's light is dim; scale so the front lid's top is its real colour
mm = (x1 - x0) / G['w']; px = a[y0 + int((G['l'] - 12) * mm), x0 + int(60 * mm)]
gain = np.array([0xAC, 0x93, 0x7D]) / px
lid = rgba(np.clip(a[y0:y1, x0:x1] * gain, 0, 255), m[y0:y1, x0:x1]).resize((TOP_PX, round(TOP_PX * G['l'] / G['w'])), Image.LANCZOS)
lid.save(os.path.join(IMG, 'case-top.png'), optimize=True)
# the knob, alone
p = os.path.join(T, 'knob.png')
scad(imp('KNOB_COLOUR', 'knob_raw') + 'color("#1a1a1a") ' + LOGO, p, '--camera=0,0,100,0,0,0', '--projection=o', '--viewall', '--autocenter', '--imgsize=1600,1600')
a, m = key(p); ys, xs = np.where(m); bx0, bx1, by0, by1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
assert abs((bx1 - bx0) / (by1 - by0) - 1) < 0.01, 'knob not square'
rgba(np.clip(a[by0:by1, bx0:bx1] * gain, 0, 255), m[by0:by1, bx0:bx1]).resize((KNOB_PX, KNOB_PX), Image.LANCZOS).save(os.path.join(IMG, 'knob.png'), optimize=True)
# the whole case
p = os.path.join(T, 'view.png')
scad(BODY + LEDS('#2fbf3a', '#d42020') + 'translate([cx(SHAFT_X), cy(SHAFT_Y), FRONT_TOP + KNOB_CLR]) { ' + imp('KNOB_COLOUR', 'knob_raw') + 'color("#1a1a1a") ' + LOGO + ' }',
     p, '--camera=150,-120,140,53,45,10', '--viewall', '--autocenter', '--imgsize=2400,2000')
a, m = key(p); ys, xs = np.where(m)
c = rgba(a, m).crop((xs.min() - 8, ys.min() - 8, xs.max() + 9, ys.max() + 9))
c.resize((VIEW_PX, round(VIEW_PX * c.height / c.width)), Image.LANCZOS).save(os.path.join(IMG, 'case-view.png'), optimize=True)
json.dump(G, open(os.path.join(IMG, 'geom.json'), 'w'))
print(G, 'gain', gain.round(2))
