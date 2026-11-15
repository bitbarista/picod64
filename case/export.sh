#!/bin/bash
# Export every printed part to case/stl/ from picod64-case.scad, then run the checks. Print as exported, NO BRIM.
#   bash case/export.sh
set -e
cd "$(dirname "$0")"
SC="openscad -q picod64-case.scad"
mkdir -p stl stl/options stl/inlay stl/plain
x() { echo "  $1"; $SC "${@:2}" -o "stl/$1"; case $1 in *lid*) python3 weld.py "stl/$1" >/dev/null;; esac; }   # weld the lids: sub-0.0001 mm slivers where cuts cross facets
x picod64-tray.stl                -D 'part="tray"'
x picod64-lid-front.stl           -D 'part="lid_front"'
x picod64-lid-rear.stl            -D 'part="lid_rear"'
x options/picod64-lid-front-insert.stl -D 'part="lid_front"' -D 'FIXING="insert"'   # M3 heat-set inserts (BUILD-NOTES, step 7)
x options/picod64-lid-rear-insert.stl  -D 'part="lid_rear"'  -D 'FIXING="insert"'
x picod64-knob.stl                -D 'part="knob"'
x picod64-knob-seater.stl         -D 'part="knob_seater"'                        # the knob's first push, on the bench (BUILD-NOTES)
x picod64-led-standoffs.stl       -D 'part="led_standoffs"'
x picod64-oled-pillars.stl        -D 'part="oled_pillars"'                       # 4, screw-free (BUILD-NOTES: OLED)
# badges (STYLE c64): letters up, the letters a second colour from their first layer (BUILD-NOTES, steps 6 and 7)
x picod64-badge-pwr.stl           -D 'part="badge_pwr"'
x picod64-badge-act.stl           -D 'part="badge_act"'
x picod64-badge-name.stl          -D 'part="badge_name"'
# the first case's lids, labels cut in (STYLE plain)
x plain/picod64-lid-front.stl     -D 'part="lid_front"' -D 'STYLE="plain"'
x plain/picod64-lid-rear.stl      -D 'part="lid_rear"'  -D 'STYLE="plain"'
# two-colour inlay set, plain style only (parked: needs an AMS or a mid-layer colour change; CASE.md)
y() { echo "  inlay/$1"; $SC -D INLAY=true -D 'STYLE="plain"' "${@:2}" -o "stl/inlay/$1"; }
y lid_front_inlay.stl -D 'part="lid_front"'; y labels_front.stl -D 'part="labels_front"'
y lid_rear_inlay.stl  -D 'part="lid_rear"';  y labels_rear.stl  -D 'part="labels_rear"'
y knob_inlay.stl      -D 'part="knob"';      y knob_logo.stl    -D 'part="knob_logo"'

echo "checks:"
for f in stl/picod64-*.stl stl/options/*.stl stl/plain/*.stl stl/inlay/*.stl; do
    n=1; case $f in *standoffs*) n=2;; *pillars*) n=4;; *labels_front*) n=6;; *labels_rear*) n=16;; *knob_logo*) n=4;; esac
    python3 check_bodies.py "$f:$n" | tail -1; python3 check_manifold.py "$f" | tail -1
done
# Overhangs. The tray's 4 "need support" are the ring-shaped roofs of the foot recesses, judged by their 10.6 mm
# outer size; the ring itself is narrow. They were in the tray already printed (30 Oct), same triangles.
# The knob's 1 is the logo's square outline (11.2 by its outside, 0.94 across: CASE.md).
# The front lid's 1 (c64 style) is the OLED frame groove, judged by its outer size; it is 0.9 wide.
for f in stl/picod64-lid-*.stl stl/options/*.stl stl/plain/*.stl stl/picod64-tray.stl stl/picod64-knob.stl stl/picod64-knob-seater.stl stl/picod64-badge-*.stl; do echo "$f: $(python3 check_overhangs.py "$f" | tail -1)"; done
bash check_labels.sh
python3 check_logo.py knob-logo.scad D
