#!/bin/bash
# Gate: every lid label, as cut (bolded, stretched, spaced), must keep each glyph a separate body
# with >= MIN mm of lid between bodies (2026-11-01: "IEC" printed with I run into E; measured
# 0.27 mm at spacing 1.1), and every island inside a letter >= 0.9 mm. Also catches the "PicoD64" i-dot trim drifting off the i.
#   bash case/check_labels.sh        (needs python3 + shapely, and the Eurostile font)
set -e; cd "$(dirname "$0")"; T=$(mktemp -d); MIN=${MIN:-0.6}; bad=0
for spec in 'label_2d("IEC",LABEL_SIZE):3' 'label_2d("PWR",LABEL_SIZE):3' 'label_2d("ACT",LABEL_SIZE):3' \
            'boot_sym_2d():2' 'label_2d("USB",LABEL_SIZE):3' 'name_2d():-'; do
    call=${spec%:*}; want=${spec##*:}
    printf 'include <%s/picod64-case.scad>\npart = "none";\n%s;\n' "$PWD" "$call" > $T/l.scad
    openscad -o $T/l.svg $T/l.scad 2>/dev/null
    python3 - $T/l.svg "$call" $want $MIN <<'PY' || bad=1
import re, sys
from shapely.geometry import Polygon
from shapely.ops import unary_union
svg, name, want, mn = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])   # want '-': any count (NAME is a Customizer value)
P = []
for d in re.findall(r'd="([^"]+)"', open(svg).read()):
    for sub in re.split(r'[Mm]', d):
        pts = [tuple(map(float, p.split(','))) for p in re.findall(r'-?[\d.]+,-?[\d.]+', sub)]
        if len(pts) > 2: P.append(Polygon(pts).buffer(0))
u = P[0]
for p in P[1:]: u = u.symmetric_difference(p)          # even-odd, so the letters' holes stay holes
g = list(getattr(u, 'geoms', [u]))
def inscribed(pg):                                      # thickest point (diameter)
    lo, hi = 0, 10
    for _ in range(16):
        m = (lo + hi) / 2
        lo, hi = (m, hi) if not pg.buffer(-m / 2).is_empty else (lo, m)
    return lo
isl = [inscribed(Polygon(h)) for a in g for h in a.interiors]
ISL = 0.9   # islands of lid inside letters stand alone on the first layer: >= two Classic lines (no brim!)
gap = min(a.distance(b) for i, a in enumerate(g) for b in g[i + 1:])
x0, y0, x1, y1 = u.bounds
mi = min(isl) if isl else 99
ok = (want == '-' or len(g) == int(want)) and gap >= mn and mi >= ISL
print(f"{'ok ' if ok else 'BAD'} {name}: {len(g)} bodies (want {want}), min gap {gap:.2f} (>= {mn}), smallest island " +
      (f"{mi:.2f} (>= {ISL})" if isl else "none") + f", {x1 - x0:.1f} x {y1 - y0:.1f} mm")
sys.exit(0 if ok else 1)
PY
done
rm -rf $T; exit $bad
