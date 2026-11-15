#!/usr/bin/env python3
"""python3 case/weld.py part.stl   (export.sh runs it on the lids)
OpenSCAD leaves a few vertices under 0.0001 mm apart where a cut crosses a facet. Weld them as check_manifold.py
rounds them, and drop the triangles that collapse to nothing. Moves nothing by more than 0.00005 mm."""
import re, sys
path = sys.argv[1]
v = [tuple(round(float(x), 4) + 0.0 for x in m) for m in re.findall(r"vertex\s+(\S+)\s+(\S+)\s+(\S+)", open(path).read())]
keep = dropped = 0; out = ['solid weld']
for i in range(0, len(v), 3):
    a, b, c = v[i:i + 3]
    if a == b or b == c or c == a: dropped += 1; continue
    keep += 1; out += ['facet normal 0 0 0', ' outer loop'] + [f'  vertex {p[0]} {p[1]} {p[2]}' for p in (a, b, c)] + [' endloop', 'endfacet']
open(path, 'w').write('\n'.join(out + ['endsolid weld', '']))
print(f'{keep} triangles kept, {dropped} collapsed')
