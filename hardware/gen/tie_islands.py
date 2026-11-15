#!/usr/bin/env python3
"""
Tie every GND pour fragment that has no via of its own to the other layer -- pcbnew API only.

Why (2026-10-25): thermal relief (thermal_relief.sh) carves the pours; a fragment boxed in by
tracks that touches ONE pad is reached only through that pad. KiCad calls it a starved thermal
("spokes connected to isolated island"), and KRT's repair_planes calls the zone "fully
connected" (the pad joins it), so nothing fixed it: three builds in a row stopped on J1.2,
SW1.C and C1.2. A via inside such a fragment, where BOTH pours fill, makes it real ground.

Sites are found the way stitch.py finds them: ask the board's own fill (both layers, at the
site and on a ring round it), clear of holes, pads and no-via rule areas. A fragment with no
site is reported, not forced.
Usage: tie_islands.py <board.kicad_pcb>
"""
import math
import sys

import pcbnew

VIA_D, VIA_DRILL = 0.6, 0.3          # = stitch.py
RING = VIA_D / 2 + 0.25
HOLE_GAP = 0.25 + 0.2
STEP = 0.1
mm, MM = pcbnew.ToMM, pcbnew.FromMM

path = sys.argv[1]
b = pcbnew.LoadBoard(path)
gnd = b.FindNet("GND")


def fill():
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())


zones = {z.GetLayer(): z for z in b.Zones() if z.GetNetname() == "GND" and not z.GetIsRuleArea()}
if set(zones) != {pcbnew.F_Cu, pcbnew.B_Cu}:
    sys.exit("tie_islands: need one GND pour on each of F.Cu and B.Cu")
_pads = [(p, L) for f in b.GetFootprints() for p in f.Pads()
         for L in (pcbnew.F_Cu, pcbnew.B_Cu) if p.IsOnLayer(L)]
_novia = [z for z in b.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]


def drills():
    return [(v.GetPosition(), v.GetDrillValue()) for v in b.GetTracks() if v.Type() == pcbnew.PCB_VIA_T] + \
           [(p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y))
            for f in b.GetFootprints() for p in f.Pads() if p.GetDrillSize().x > 0]


def site_ok(x, y, holes):
    pt = pcbnew.VECTOR2I(MM(x), MM(y))
    if any((pt - c).EuclideanNorm() < MM(VIA_DRILL / 2 + HOLE_GAP) + d / 2 for c, d in holes):
        return False
    for p, L in _pads:
        sh = pcbnew.SHAPE_POLY_SET()
        p.TransformShapeToPolygon(sh, L, 0, MM(0.005), pcbnew.ERROR_OUTSIDE)
        if sh.Contains(pt) or sh.Collide(pt, MM(RING)):
            return False
    for z in _novia:
        if z.Outline().Contains(pt) or z.Outline().Collide(pt, MM(VIA_D / 2)):
            return False
    for z in zones.values():
        for a in range(12):
            r = 0 if a == 0 else RING
            ang = 2 * math.pi * a / 11
            q = pcbnew.VECTOR2I(MM(x + r * math.cos(ang)), MM(y + r * math.sin(ang)))
            if not z.HitTestFilledArea(z.GetLayer(), q):
                return False
    return True


fill()
gvias = [v.GetPosition() for v in b.GetTracks() if v.Type() == pcbnew.PCB_VIA_T and v.GetNetname() == "GND"]
placed, stuck = [], []
for L, z in zones.items():
    polys = z.GetFilledPolysList(L)
    for i in range(polys.OutlineCount()):
        ol = polys.Outline(i)
        if any(ol.PointInside(v) for v in gvias):
            continue
        bb = ol.BBox()
        holes = drills()
        site = None
        y = mm(bb.GetTop())
        while site is None and y <= mm(bb.GetBottom()):
            x = mm(bb.GetLeft())
            while x <= mm(bb.GetRight()):
                if ol.PointInside(pcbnew.VECTOR2I(MM(x), MM(y))) and site_ok(x, y, holes):
                    site = (round(x, 3), round(y, 3))
                    break
                x += STEP
            y += STEP
        where = f"{b.GetLayerName(L)} fragment at ({mm(bb.Centre().x):.2f}, {mm(bb.Centre().y):.2f})"
        if site is None:
            stuck.append(where)
            continue
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(pcbnew.VECTOR2I(MM(site[0]), MM(site[1])))
        v.SetWidth(MM(VIA_D))
        v.SetDrill(MM(VIA_DRILL))
        v.SetNet(gnd)
        b.Add(v)
        gvias.append(v.GetPosition())
        placed.append(where)

fill()
pcbnew.SaveBoard(path, b)
print(f"tie islands: {len(placed)} via-less GND fragment(s) tied to the other layer")
for w in placed:
    print(f"   tied   {w}")
for w in stuck:
    print(f"   NO SITE {w} (left; DRC will say if it starves a pad)")
