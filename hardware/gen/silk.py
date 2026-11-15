#!/usr/bin/env python3
"""
Place PicoD64's silkscreen designators -- deliberately, not by formula.

"Use some logic, as you have been known to place them in odd places and
rotations in the past." The past failures (an earlier board): designators printed on
their neighbours while the placer reported success, because its text-width model
was 40% short. So:

  * EVERY extent comes from KiCad itself (GetBoundingBox), never an estimate.
  * All text is HORIZONTAL, reading upright with the fingers at the bottom -- the
    way the board is held and the way it sits in the case.
  * A designator goes BESIDE its part, never under it: once the Pico or the DIP
    is fitted, anything beneath it is invisible.
  * Each part has an ORDERED list of preferred sides, chosen for where a person
    looks (U1's label by its pin-1 end, D1's by its cathode, ...). The first
    candidate clear of every obstacle wins; if none is, the part is REPORTED and
    the script exits 1 -- it never forces a label onto something.
  * Obstacles: pads (+mask margin), vias, holes, other silkscreen, other parts'
    courtyards, and the board edge.

The mounting holes H1-H8 get no designator: a hole needs no name. Nor do the test pads
TP1-6 (rev 1): gen_pcb.py letters each one with its signal instead.
Usage: silk.py <board.kicad_pcb>
"""
import sys

import pcbnew

mm, MM = pcbnew.ToMM, pcbnew.FromMM
SIZE, THICK = 1.0, 0.15          # mm -- JLCPCB: text >= 1.0 mm, line >= 0.15 mm (capabilities page, 2026-10-26)
GAP = 0.5                        # text to its own part
CLEAR = 0.25                     # text to any obstacle
EDGE = 0.6                       # text to the board outline

# part -> ordered sides. PicoD64: every footprint except the holes, the default order
# below; parts a person looks at while fitting get their own order.
DEFAULT = ["above", "below", "left", "right"]
OWN = {
    "A1": ["left", "below"],            # beside its GP0-15 column, away from the USB end
    "J1": ["below"], "J2": ["below"],   # in front of the socket body
    "J3": ["above", "left"],            # behind the card slot
    "J4": ["below", "above"],
    "SW1": ["left", "below"],
    "JP1": ["right"], "JP2": ["right"], "J6": ["below", "right"], "R10": ["below", "right"],   # beside their own header: below/above read as the NEXT one (2026-10-25)
    "C1": ["left", "below", "above"], "C4": ["right", "left"], "C5": ["right", "left"],
    "R14": ["above", "below", "right", "left"],
    "RN2": ["right", "left", "above", "below"],
    "RN3": ["right", "below", "above", "left"],   # rev 1: its left is RN2's only free side
    "R18": ["above", "below"],   # standing at the left edge, by R15
    "J7": ["right", "above"],    # its below is R18's label spot
    "R15": ["left", "below"],    # by its own body, not slid down beside R17
}
path = sys.argv[1]
b = pcbnew.LoadBoard(path)
PREFS = {f.GetReference(): OWN.get(f.GetReference(), DEFAULT)
         for f in b.GetFootprints() if not f.GetReference().startswith(("H", "TP"))}
edge = b.GetBoardEdgesBoundingBox()


def box(x0, y0, x1, y1):
    return (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))


def bb(item):
    r = item.GetBoundingBox()
    return box(mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))


def grow(r, d):
    return (r[0] - d, r[1] - d, r[2] + d, r[3] + d)


def hit(a, c):
    return a[0] < c[2] and c[0] < a[2] and a[1] < c[3] and c[1] < a[3]


def obstacles(exclude_ref):
    obs = []
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.IsOnLayer(pcbnew.F_Cu) or p.IsOnLayer(pcbnew.F_Mask) or p.GetDrillSize().x > 0:
                obs.append(("pad " + f.GetReference(), grow(bb(p), 0.1)))
        for g in f.GraphicalItems():
            if g.GetLayer() == pcbnew.F_SilkS:
                obs.append(("silk " + f.GetReference(), bb(g)))
        if f.GetReference() != exclude_ref:
            cy = f.GetCourtyard(pcbnew.F_CrtYd)
            if cy.OutlineCount():
                r = cy.BBox()
                obs.append(("courtyard " + f.GetReference(),
                            box(mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))))
        if f.GetReference() not in (exclude_ref,) and f.Reference().IsVisible():
            obs.append(("designator " + f.GetReference(), bb(f.Reference())))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            obs.append(("via", bb(t)))
    for d in b.GetDrawings():
        if d.GetLayer() == pcbnew.F_SilkS:
            obs.append(("board silk", bb(d)))
    return obs


def own_box(f):
    cy = f.GetCourtyard(pcbnew.F_CrtYd)
    r = cy.BBox() if cy.OutlineCount() else f.GetBoundingBox(False, False)
    return box(mm(r.GetLeft()), mm(r.GetTop()), mm(r.GetRight()), mm(r.GetBottom()))


def candidates(own, side, w, h, centred=False):
    x0, y0, x1, y1 = own
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    slides = [0] if centred else [0, -1, 1, -2, 2, -3, 3, -4, 4, -6, 6, -8, 8]
    # A centred label may sit CLOSER to its own part rather than slide off-centre:
    # U1's centred spot is 0.13 mm into C1's courtyard at a 0.5 mm gap. Silkscreen
    # never moves a component (an earlier board's lesson), so the gap gives instead.
    gaps = [GAP, 0.35, 0.2] if centred else [GAP]
    for g in gaps:
        yield from _cands(x0, y0, x1, y1, cx, cy, side, w, h, slides, g)


def _cands(x0, y0, x1, y1, cx, cy, side, w, h, slides, GAP):
    for s in slides:
        if side == "left":
            yield (x0 - GAP - w / 2, cy + s)
        elif side == "right":
            yield (x1 + GAP + w / 2, cy + s)
        elif side == "above":
            yield (cx + s, y0 - GAP - h / 2)
        elif side == "below":
            yield (cx + s, y1 + GAP + h / 2)


# Parts whose label must be CENTRED on the part (no sliding along the side).
CENTRED = set()
STITCH_VIA_OK_TO_REMOVE = True    # a GND stitching via in the way of a centred label
                                  # is removed (one of ~100 redundant stitches);
                                  # nothing else is ever moved or removed

failed, report = [], []
for ref, sides in PREFS.items():
    f = b.FindFootprintByReference(ref)
    t = f.Reference()
    t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(MM(SIZE), MM(SIZE)))
    t.SetTextThickness(MM(THICK))
    t.SetTextAngleDegrees(0)          # absolute: upright whatever the footprint's rotation
    t.SetKeepUpright(True)
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
    t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    t.SetVisible(True)
    obs = obstacles(ref)
    own = own_box(f)
    placed = None
    t.SetTextPos(pcbnew.VECTOR2I(MM(0), MM(0)))
    w0 = bb(t)
    w, h = w0[2] - w0[0], w0[3] - w0[1]              # KiCad's own extent of this text
    for side in sides:
        for (x, y) in candidates(own, side, w, h, ref in CENTRED):
            t.SetTextPos(pcbnew.VECTOR2I(MM(round(x, 3)), MM(round(y, 3))))
            tb = bb(t)
            inside = (tb[0] >= mm(edge.GetLeft()) + EDGE and tb[2] <= mm(edge.GetRight()) - EDGE and
                      tb[1] >= mm(edge.GetTop()) + EDGE and tb[3] <= mm(edge.GetBottom()) - EDGE)
            clash = [n for n, o in obs if hit(grow(tb, CLEAR), o)]
            if ref in CENTRED and inside and clash and STITCH_VIA_OK_TO_REMOVE and \
                    set(clash) == {"via"}:
                gone = [v for v in b.GetTracks() if v.Type() == pcbnew.PCB_VIA_T and
                        v.GetNetname() == "GND" and not v.IsLocked() and hit(grow(tb, CLEAR), bb(v))]
                if gone and all(hit(grow(tb, CLEAR), bb(v)) for v in gone) and \
                        len(gone) == sum(1 for n in clash if n == "via"):
                    for v in gone:
                        b.Delete(v)
                    report.append(f"   ({ref}: removed {len(gone)} GND stitching via(s) under its centred label)")
                    obs = obstacles(ref)
                    clash = [n for n, o in obs if hit(grow(tb, CLEAR), o)]
            if inside and not clash:
                placed = (side, x, y)
                break
        if placed:
            break
    if placed:
        report.append(f"   {ref:3} {placed[0]:6} at ({placed[1]:.2f}, {placed[2]:.2f})")
    else:
        t.SetVisible(False)
        failed.append(ref)

pcbnew.SaveBoard(path, b)
print("\n".join(report))
if failed:
    print(f"   *** NO CLEAR POSITION for {failed} -- not forcing a label onto anything")
    sys.exit(1)
print(f"silk: {len(report)} designators placed, all horizontal, none under a part or on copper")
