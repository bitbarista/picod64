#!/bin/bash
# Re-make every render in case/render/ and hardware/render/ from the current sources, and nothing else.
#   bash case/render.sh        (after hardware/gen/board.sh + fab.py, and case/export.sh for the knob face)
# case_true_size.png is drawn at 8.43 px/mm: calibrated on a laptop at Loupe 100 % (2026-10-26).
set -e
cd "$(dirname "$0")"
R=render; H=../hardware/render; TMP=$(mktemp -d)
mkdir -p "$R" "$H"; rm -f "$R"/*.png                      # case/render/ is this script's alone
rm -f "$H"/board_top.png "$H"/board_bottom.png "$H"/gerber_top.png
                                                                     # (not made here; once deleted by this line, 2026-11-01)
SC="openscad --colorscheme=Tomorrow picod64-case.scad"

# ---- board
python3 render_hero.py --board-views          # top and bottom, parts in their real colours (as the README picture)
G=../hardware/fab/gerbers
gerbv -x png -D 160 -b '#FFFFFF' -o $H/gerber_top.png -f '#20208080' $(ls $G/*-F_Cu.gtl) -f '#00800060' $(ls $G/*-F_Mask.gts) \
    -f '#000000FF' $(ls $G/*-F_Silkscreen.gto) -f '#FF0000FF' $(ls $G/*-PTH.drl) -f '#0000FFFF' $(ls $G/*-NPTH.drl) \
    -f '#000000FF' $(ls $G/*-Edge_Cuts.gm1) 2>/dev/null

# ---- case views
$SC -D 'part="view"' --camera=53,45,160,53,45,0 --projection=o --viewall --autocenter --imgsize=1400,1200 -o $TMP/v_top.png 2>/dev/null
$SC -D 'part="view"' --camera=150,-90,120,53,45,10 --viewall --autocenter --imgsize=1400,1200 -o $TMP/v_front.png 2>/dev/null
$SC -D 'part="view"' --camera=-50,200,110,53,45,10 --viewall --autocenter --imgsize=1400,1200 -o $TMP/v_rear.png 2>/dev/null
$SC -D 'part="view"' --camera=110,-70,95,40,40,15 --imgsize=1400,1100 -o $TMP/o_front.png 2>/dev/null
$SC -D 'part="section"' --camera=200,40,20,40,40,18 --projection=o --imgsize=1600,900 -o $TMP/o_section.png 2>/dev/null
$SC -D 'part="view"' --camera=53,45,300,53,45,0 --projection=o --viewall --autocenter --imgsize=3000,2600 -o $TMP/scale_src.png 2>/dev/null
# exploded, in the printed colours: each part rendered to a mesh first, then coloured and previewed
# (OpenSCAD 2021's --render drops colours, and its CSG preview streaks the lid's coincident faces)
for p in tray lid_front_raw lid_rear_raw knob_raw badges_front_fit letters_front_fit badges_rear_fit letters_rear_fit; do $SC -D "part=\"$p\"" -o $TMP/m_$p.stl 2>/dev/null; done
cat > $TMP/exploded.scad <<EOS
include <$PWD/picod64-case.scad>
part = "none";
color(TRAY_COLOUR) import("$TMP/m_tray.stl");
translate([0, -12, 25]) color(LID_COLOUR) import("$TMP/m_lid_front_raw.stl");
translate([0, 12, 25]) color(LID_REAR_COLOUR) import("$TMP/m_lid_rear_raw.stl");
translate([cx(SHAFT_X), cy(SHAFT_Y) - 12, 25 + FRONT_TOP + KNOB_CLR + 4]) color(KNOB_COLOUR) import("$TMP/m_knob_raw.stl");
translate([0, -12, 25]) { color(BADGE_COLOUR) import("$TMP/m_badges_front_fit.stl"); color(BADGE_INK) import("$TMP/m_letters_front_fit.stl"); }
translate([0, 12, 25]) { color(BADGE_COLOUR) import("$TMP/m_badges_rear_fit.stl"); color(BADGE_INK) import("$TMP/m_letters_rear_fit.stl"); }
EOS
openscad --colorscheme=Tomorrow $TMP/exploded.scad --camera=150,-120,140,53,45,20 --viewall --autocenter --imgsize=1400,1200 -o $TMP/exploded.png 2>/dev/null
python3 -c "
from PIL import Image, ImageChops
im = Image.open('$TMP/exploded.png').convert('RGB'); b = ImageChops.difference(im, Image.new('RGB', im.size, im.getpixel((0, 0)))).getbbox()
im.crop((max(0, b[0] - 40), max(0, b[1] - 40), min(im.width, b[2] + 40), min(im.height, b[3] + 40))).save('$R/exploded.png')"   # README picture: no empty margin
$SC -D 'part="knob_section"' --camera=200,0,0,0,0,0 --projection=o --viewall --autocenter --imgsize=1200,900 -o $R/knob_section.png 2>/dev/null
for p in lid_front lid_rear; do   # as printed: top face down, no supports
    $SC -D "part=\"$p\"" --camera=120,-140,110,50,45,8 --viewall --autocenter --imgsize=1000,800 -o $TMP/print_$p.png 2>/dev/null; done
# the knob's face as it prints (the bed layer, seen from below: the logo upside down, not mirrored)
printf 'mirror([1,0,0]) projection(cut=true) translate([0,0,-0.3]) import("%s/stl/picod64-knob.stl");\n' "$PWD" > $TMP/kf.scad
openscad --colorscheme=Tomorrow --camera=0,0,0,0,0,0,40 --projection=o --imgsize=600,600 -o $R/knob_face.png $TMP/kf.scad 2>/dev/null

python3 - "$TMP" <<'EOF'
import sys
from PIL import Image, ImageDraw, ImageFont, ImageChops
T = sys.argv[1]
F = lambda s: ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', s)
# 1. case sheet
ims = [Image.open(f'{T}/v_{n}.png').convert('RGB') for n in ('top', 'front', 'rear')]
ims = [i.crop((150, 150, i.width - 150, i.height - 100)) for i in ims]
W = sum(i.width for i in ims); s = Image.new('RGB', (W, max(i.height for i in ims) + 70), 'white'); d = ImageDraw.Draw(s)
x = 0
for im, t in zip(ims, ['Top (front of case at the bottom)', 'Front: OLED, LEDs, knob, microSD slot', 'Rear: IEC x2, USB']):
    s.paste(im, (x, 70)); d.text((x + 20, 15), t, fill='black', font=F(38)); x += im.width
s.save('render/case_sheet.png')
# 2. OLED fitted sheet
top = Image.open(f'{T}/v_top.png').convert('RGB').crop((230, 190, 1170, 1010))
front = Image.open(f'{T}/o_front.png').convert('RGB').crop((0, 120, 1400, 1040))
sec = Image.open(f'{T}/o_section.png').convert('RGB').crop((200, 200, 1560, 730))
h = 820; fit = lambda im: im.resize((int(im.width * h / im.height), h))
top, front = fit(top), fit(front)
sec = sec.resize((top.width + front.width, int(sec.height * (top.width + front.width) / sec.width)))
s = Image.new('RGB', (top.width + front.width, 70 + h + 70 + sec.height), 'white'); d = ImageDraw.Draw(s)
s.paste(top, (0, 70)); s.paste(front, (top.width, 70)); s.paste(sec, (0, 70 + h + 70))
d.text((20, 15), 'Top, OLED fitted (front at the bottom)', fill='black', font=F(36))
d.text((top.width + 20, 15), 'Front, low angle', fill='black', font=F(36))
d.text((20, 70 + h + 15), 'Section through the right-hand OLED pillars (front left, rear right)', fill='black', font=F(36))
s.save('render/oled_fitted_sheet.png')
# 3. true size (8.43 px/mm calibrated on this screen; measured case aspect checked)
PXMM, OUT_W, OUT_L = 9.694 / 1.15, 105.8, 90.8
im = Image.open(f'{T}/scale_src.png').convert('RGB')
bg = Image.new('RGB', im.size, im.getpixel((5, 5)))
px = ImageChops.difference(im, bg).convert('L').point(lambda v: 255 if v > 12 else 0).load()
w, hh = im.size
rows = [(sum(1 for x in range(0, w, 2) if px[x, y]), y) for y in range(0, hh, 2)]
full = [y for n, y in rows if n > max(r[0] for r in rows) * 0.9]
cols = [x for x in range(w) if any(px[x, y] for y in range(full[0], full[-1], 4))]
x0, x1, y0, y1 = min(cols), max(cols), full[0], full[-1]
assert abs((x1 - x0) / (y1 - y0) - OUT_W / OUT_L) < 0.01, 'true-size crop is not the case outline'
k = OUT_W * PXMM / (x1 - x0)
case = im.crop((x0, y0, x1, y1)).resize((round((x1 - x0) * k), round((y1 - y0) * k)), Image.LANCZOS)
m = lambda v: round(v * PXMM)
c = Image.new('RGB', (case.width + m(20), case.height + m(30)), 'white'); d = ImageDraw.Draw(c)
c.paste(case, (m(10), m(5)))
rx, ry = m(10), m(5) + case.height + m(12)
d.line((rx, ry, rx + m(100), ry), fill='black', width=2)
for i in range(101):
    t = m(5) if i % 10 == 0 else (m(3.5) if i % 5 == 0 else m(2))
    d.line((rx + m(i), ry, rx + m(i), ry - t), fill='black', width=2 if i % 10 == 0 else 1)
    if i % 10 == 0:
        d.text((rx + m(i) - m(1.5), ry + m(1)), str(i), fill='black', font=F(m(3.2)))
c.save('render/case_true_size.png')
# 4. both lid parts as printed
ims = [Image.open(f'{T}/print_{n}.png').convert('RGB') for n in ('lid_front', 'lid_rear')]
s = Image.new('RGB', (sum(i.width for i in ims), ims[0].height + 70), 'white'); d = ImageDraw.Draw(s); x = 0
for im, t in zip(ims, ['Front lid part, as printed (top face down)', 'Rear lid part, as printed (top face down)']):
    s.paste(im, (x, 70)); d.text((x + 20, 15), t, fill='black', font=F(34)); x += im.width
s.save('render/lid_print.png')
EOF
rm -rf "$TMP"
ls -1 $H $R
# the README picture: board + exploded case, same viewpoint, transparent (docs/img/hero.png)
python3 render_hero.py
