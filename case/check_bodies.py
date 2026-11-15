#!/usr/bin/env python3
"""
Fail if a printed part is not ONE solid sitting on the bed (z >= 0).

    python3 case/check_bodies.py part.stl [...]      (STLs from openscad -D 'part="lid"' ...)
    python3 case/check_bodies.py spacers.stl:4       (a part that is meant to be N separate pieces)

Why (AUDIT D6, 2026-10-26): the BOOTSEL tube stood 2.6 mm proud of the lid's roof -- a separate
body BELOW the bed once the lid is turned face down -- and the light-pipe tubes 3.8 mm proud.
OpenSCAD only hinted ("Volumes: 3"). Counts connected bodies by shared vertices.
"""
import re
import sys

bad = 0
for arg in sys.argv[1:]:
    path, _, want = arg.partition(":")
    want = int(want or 1)
    vs = re.findall(r"vertex\s+(\S+)\s+(\S+)\s+(\S+)", open(path).read())
    idx, par = {}, []

    def find(a):
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for i in range(0, len(vs), 3):
        t = []
        for v in vs[i:i + 3]:
            k = tuple(round(float(x), 4) for x in v)
            if k not in idx:
                idx[k] = len(par)
                par.append(len(par))
            t.append(idx[k])
        for b in t[1:]:
            par[find(b)] = find(t[0])
    n = len({find(i) for i in idx.values()})
    zmin = min(k[2] for k in idx)
    ok = n == want and zmin >= -1e-3
    bad += not ok
    print(f"{'ok ' if ok else 'BAD'} {path}: {n} bod{'y' if n == 1 else 'ies'} (want {want}), lowest z {zmin:.2f}")
sys.exit(1 if bad else 0)
