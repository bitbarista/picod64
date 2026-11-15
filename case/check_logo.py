#!/usr/bin/env python3
# Gate for the knob logo (knob-logo.scad), same limits as the lid labels (first lid print, 2026-11-01):
#   python3 case/check_logo.py case/knob-logo.scad D
# usage: logocheck.py SCAD [logos...]: the knob face (d 18) less the logo, as printed on the bed.
#  cut  = the logo (recess): thinnest >= 0.65
#  plastic strips: thinnest >= 0.9 (two Classic lines); islands (plastic not joined to the rim) >= 1.2 thick
import re, sys, subprocess, os
from shapely.geometry import Polygon, Point
import tempfile; S = tempfile.mkdtemp(); scad = os.path.abspath(sys.argv[1])
def load(f):
    P=[]
    for d in re.findall(r'd="([^"]+)"', open(f).read()):
        for sub in re.split(r'[Mm]', d):
            pts=[tuple(map(float,p.split(','))) for p in re.findall(r'-?[\d.]+,-?[\d.]+', sub)]
            if len(pts)>2: P.append(Polygon(pts).buffer(0))
    u=P[0]
    for p in P[1:]: u=u.symmetric_difference(p)
    return u
def thin(x, frac=0.995):
    lo,hi=0,3
    for _ in range(14):
        m=(lo+hi)/2
        if x.buffer(-m/2, join_style=2).buffer(m/2, join_style=2).area >= frac*x.area: lo=m
        else: hi=m
    return lo
def inscribed(pg):
    lo,hi=0,10
    for _ in range(16):
        m=(lo+hi)/2
        lo,hi=(m,hi) if not pg.buffer(-m/2).is_empty else (lo,m)
    return lo
bad=0
for l in sys.argv[2:]:
    subprocess.run(["openscad","-q","-D",f'logo="{l}"',"-D",'part="2d"',"-o",f"{S}/lg.svg",scad],stderr=subprocess.DEVNULL)
    cut=load(f"{S}/lg.svg"); face=Point(0,0).buffer(9,resolution=64)
    plastic=face.difference(cut); parts=list(getattr(plastic,'geoms',[plastic]))
    rim=max(parts,key=lambda p:p.exterior.distance(Point(0,0)) if False else p.bounds[2]-p.bounds[0])
    isl=[p for p in parts if p is not rim]
    tc=thin(cut); tp=min(thin(p) for p in parts); mi=min([inscribed(p) for p in isl] or [99])
    ok = tc>=0.65 and tp>=0.9 and mi>=1.2; bad|=not ok
    print(f"{'ok ' if ok else 'BAD'} logo {l}: thinnest cut {tc:.2f} (>=0.65), thinnest plastic {tp:.2f} (>=0.9), "
          f"{len(isl)} islands, smallest {mi if isl else float('nan'):.2f} (>=1.2)")
sys.exit(bad)
