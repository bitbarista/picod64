#!/usr/bin/env python3
"""Fail if an STL is not closed 2-manifold: every edge shared by exactly two triangles (OpenSCAD's
'may not be a valid 2-manifold' warning, made a gate)."""
import re, sys, collections
bad = 0
for path in sys.argv[1:]:
    v = [tuple(round(float(x), 4) for x in m) for m in re.findall(r"vertex\s+(\S+)\s+(\S+)\s+(\S+)", open(path).read())]
    e = collections.Counter()
    for i in range(0, len(v), 3):
        t = v[i:i + 3]
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            e[tuple(sorted((a, b)))] += 1
    n = sum(1 for c in e.values() if c != 2)
    bad += n > 0
    print(f"{'ok ' if not n else 'BAD'} {path}: {n} edges not shared by exactly 2 faces")
sys.exit(1 if bad else 0)
