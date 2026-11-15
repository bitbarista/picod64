#!/usr/bin/env python3
"""
Post-route finishing for the PCB (pcbnew API only -- no KRT).

  1. Remove dangling stubs shorter than 0.5 mm.
  2. Fill the zones.
  3. Stitch the F.Cu and B.Cu GND pours with vias on a lattice -- placing a via
     ONLY where KiCad itself reports BOTH pours filled, at the site and on a ring
     round it. That is an earlier board's method, chosen
     there because a tool's own fill model disagreed with the board's. Asking the
     board means keep-outs (Pico test points, the case post), thermal reliefs and
     other-net clearances are all respected without modelling any of them.
  4. Refill, and report the count AND the spread -- a count is not coverage
     (there: 129 vias "placed", 87 of them in two cells).
Usage: stitch.py <board.kicad_pcb> [--pitch 2.54]
"""
import math
import sys

import pcbnew

VIA_D, VIA_DRILL = 0.6, 0.3
RING = VIA_D / 2 + 0.25        # fill must exist this far out, all round the site
PITCH = float(sys.argv[sys.argv.index("--pitch") + 1]) if "--pitch" in sys.argv else 3.81
mm, MM = pcbnew.ToMM, pcbnew.FromMM

path = sys.argv[1]
b = pcbnew.LoadBoard(path)
gnd = b.FindNet("GND")


def fill():
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())


# 1. dangling stubs
def touches(p, t):
    for o in b.GetTracks():
        if o is t or o.GetNetCode() != t.GetNetCode():
            continue
        if o.Type() == pcbnew.PCB_VIA_T:
            if (o.GetPosition() - p).EuclideanNorm() <= o.GetWidth(pcbnew.F_Cu) / 2:
                return True
        elif o.IsOnLayer(t.GetLayer()) and min((o.GetStart() - p).EuclideanNorm(),
                                               (o.GetEnd() - p).EuclideanNorm()) <= MM(0.01):
            return True
    for f in b.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetCode() == t.GetNetCode() and pad.IsOnLayer(t.GetLayer()) and pad.HitTest(p):
                return True
    return False


removed = 0
for t in list(b.GetTracks()):
    if t.Type() == pcbnew.PCB_TRACE_T and t.GetLength() < MM(0.5) and \
            not (touches(t.GetStart(), t) and touches(t.GetEnd(), t)):
        b.Delete(t)
        removed += 1

# 1b. dangling vias: freerouting leaves vias with copper on ONE layer only --
# dead ends on nets that are already complete. Remove the via, then any track
# chain left with a free end, then let DRC confirm no net was opened.
def layers_used(v):
    used = set()
    for o in b.GetTracks():
        if o.Type() == pcbnew.PCB_TRACE_T and o.GetNetCode() == v.GetNetCode():
            # ⚠ WITHIN THE VIA, not equal to its centre: KRT's ends sit a few nm off
            # centre, and an exact test called /D0's live via "one-layer" and
            # would have deleted it -- opening D0.
            r = v.GetWidth(pcbnew.F_Cu) / 2
            if min((o.GetStart() - v.GetPosition()).EuclideanNorm(),
                   (o.GetEnd() - v.GetPosition()).EuclideanNorm()) <= r:
                used.add(o.GetLayer())
    for f in b.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetCode() == v.GetNetCode() and pad.HitTest(v.GetPosition()):
                used.update(l for l in (pcbnew.F_Cu, pcbnew.B_Cu) if pad.IsOnLayer(l))
    return used


dead_vias = 0
for v in list(b.GetTracks()):
    if v.Type() == pcbnew.PCB_VIA_T and v.GetNetname() != "GND" and len(layers_used(v)) < 2:
        b.Delete(v)
        dead_vias += 1
changed = True
while changed:
    changed = False
    for t in list(b.GetTracks()):
        if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() != "GND" and \
                not (touches(t.GetStart(), t) and touches(t.GetEnd(), t)):
            b.Delete(t)
            removed += 1
            changed = True
            break          # the list is stale after a delete: rescan

# 2-3. fill, then stitch where both pours really are
fill()
zones = {z.GetLayer(): z for z in b.Zones() if z.GetNetname() == "GND" and not z.GetIsRuleArea()}
if set(zones) != {pcbnew.F_Cu, pcbnew.B_Cu}:
    sys.exit("stitch: need one GND pour on each of F.Cu and B.Cu")


HOLE_GAP = 0.25 + 0.2                       # min hole-to-hole + margin, edge to edge
drills = [(v.GetPosition(), v.GetDrillValue()) for v in b.GetTracks() if v.Type() == pcbnew.PCB_VIA_T] + \
         [(p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y))
          for f in b.GetFootprints() for p in f.Pads() if p.GetDrillSize().x > 0]


def clear_of_holes(x, y):
    # ⚠ fill-based placement cannot see this: two GND vias share one pour. A
    # stitch landed beside a pad-tap via and DRC flagged hole_to_hole.
    pt = pcbnew.VECTOR2I(MM(x), MM(y))
    return all((pt - c).EuclideanNorm() >= MM(VIA_DRILL / 2 + HOLE_GAP) + d / 2 for c, d in drills)


# ⚠ NOT IN A PAD. A GND pad connected SOLIDLY is flooded by its own pour, so "both pours
# filled here" is true inside it -- and this lattice put a via in Pico pad A1.8, open
# under a hand-soldered castellation (found 2026-10-11; gen/fix_via_in_pad.py).
_pads = [(p, L) for f in b.GetFootprints() for p in f.Pads()
         for L in (pcbnew.F_Cu, pcbnew.B_Cu) if p.IsOnLayer(L)]


def clear_of_pads(x, y):
    pt = pcbnew.VECTOR2I(MM(x), MM(y))
    for p, L in _pads:
        sh = pcbnew.SHAPE_POLY_SET()
        p.TransformShapeToPolygon(sh, L, 0, MM(0.005), pcbnew.ERROR_OUTSIDE)
        if sh.Contains(pt) or sh.Collide(pt, MM(RING)):
            return False
    return True


# ⚠ NOT IN A "NO VIAS" RULE AREA (Rev B, 2026-10-15). The designator spots (label_keepouts.py)
# and the board text allow the pour, so "both pours filled" is true inside them -- and this
# lattice put a stitch on C4's label spot after routing had respected it.
_novia = [z for z in b.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]


def clear_of_rule_areas(x, y):
    pt = pcbnew.VECTOR2I(MM(x), MM(y))
    for z in _novia:
        if z.Outline().Contains(pt) or z.Outline().Collide(pt, MM(VIA_D / 2)):
            return False
    return True


def filled_both(x, y):
    for z in zones.values():
        for a in range(12):
            r = 0 if a == 0 else RING
            ang = 2 * math.pi * a / 11
            pt = pcbnew.VECTOR2I(MM(x + r * math.cos(ang)), MM(y + r * math.sin(ang)))
            if not z.HitTestFilledArea(z.GetLayer(), pt):
                return False
    return True


# Stitching IS allowed under the Pico. It was excluded once, and the F.Cu pour
# under the module -- cut into fragments by the Pico's own tracks -- was left
# joined by a single via each: DRC reported them unconnected. Vias are tented
# (gen_pcb.py setup), and filled_both() keeps them out of the test-point keep-outs.

bb = b.GetBoardEdgesBoundingBox()
x0, y0, x1, y1 = mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())
placed = []
y = y0 + PITCH / 2
while y < y1:
    x = x0 + PITCH / 2
    while x < x1:
        if clear_of_holes(x, y) and clear_of_pads(x, y) and clear_of_rule_areas(x, y) and filled_both(x, y):
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(pcbnew.VECTOR2I(MM(round(x, 3)), MM(round(y, 3))))
            v.SetWidth(MM(VIA_D))
            v.SetDrill(MM(VIA_DRILL))
            v.SetNet(gnd)
            b.Add(v)
            placed.append((x, y))
            drills.append((v.GetPosition(), MM(VIA_DRILL)))
        x += PITCH
    y += PITCH

# 4. refill and report spread: occupied 10 x 10 mm cells
fill()
pcbnew.SaveBoard(path, b)
cells = {(int((x - x0) // 10), int((y - y0) // 10)) for x, y in placed}
total = math.ceil((x1 - x0) / 10) * math.ceil((y1 - y0) / 10)
print(f"stitch: removed {dead_vias} one-layer via(s) and {removed} dangling track(s); {len(placed)} GND stitching vias at "
      f"{PITCH} mm pitch, spread over {len(cells)} of {total} 10 mm cells")
