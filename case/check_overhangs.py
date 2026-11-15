#!/usr/bin/env python3
"""
List every downward-facing surface steeper than 45 degrees that is not on the bed, in the part's PRINT
orientation (the STL as exported), grouped into clusters with their size. A cluster is acceptable without
support only if it is a short bridge (its smaller horizontal extent <= MAX_BRIDGE).

    python3 case/check_overhangs.py lid.stl [--max-bridge 8]

Why (2026-10-26): the lid was declared printable "face down", then "open side down"; "that case
can't be 3D printed" / "it would take loads of supports". This measures it instead of asserting it.
"""
import math
import re
import sys

path = sys.argv[1]
MAX_BRIDGE = float(sys.argv[sys.argv.index("--max-bridge") + 1]) if "--max-bridge" in sys.argv else 8.0
v = [tuple(map(float, m)) for m in re.findall(r"vertex\s+(\S+)\s+(\S+)\s+(\S+)", open(path).read())]
tris = [v[i:i + 3] for i in range(0, len(v), 3)]
zmin = min(p[2] for p in v)
down = []
for a, b, c in tris:
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    area = math.sqrt(nx * nx + ny * ny + nz * nz) / 2
    if area < 1e-6:
        continue
    if nz / (2 * area) < -math.cos(math.radians(45)) - 1e-6 and min(a[2], b[2], c[2]) > zmin + 0.25:
        down.append(((a, b, c), area))
# cluster by shared vertices
key = lambda p: (round(p[0], 3), round(p[1], 3), round(p[2], 3))
par = {}
def find(x):
    while par.setdefault(x, x) != x:
        par[x] = par[par[x]]
        x = par[x]
    return x
for (t, _a) in down:
    k0 = key(t[0])
    for p in t[1:]:
        par[find(key(p))] = find(k0)
cl = {}
for (t, ar) in down:
    r = find(key(t[0]))
    c = cl.setdefault(r, [0.0, [1e9] * 3, [-1e9] * 3])
    c[0] += ar
    for p in t:
        for i in range(3):
            c[1][i] = min(c[1][i], p[i]); c[2][i] = max(c[2][i], p[i])
bad = 0
for r, (ar, lo, hi) in sorted(cl.items(), key=lambda kv: -kv[1][0]):
    span = min(hi[0] - lo[0], hi[1] - lo[1])
    ok = span <= MAX_BRIDGE
    bad += not ok
    print(f"{'ok ' if ok else 'SUPPORT'} z {lo[2]:6.1f}  x {lo[0]:6.1f}..{hi[0]:6.1f}  y {lo[1]:6.1f}..{hi[1]:6.1f}"
          f"  area {ar:7.1f} mm2  bridge {span:4.1f} mm")
print(f"{len(cl)} downward faces > 45 deg off the bed; {bad} need support (bridge > {MAX_BRIDGE} mm)")
sys.exit(1 if bad else 0)
