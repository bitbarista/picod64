#!/usr/bin/env python3
"""
Spoke room round every through-hole GND pad, for the ROUTE only.

    gnd_pad_rings.py BOARD add      before the DSN export
    gnd_pad_rings.py BOARD remove   after the SES import

Why (2026-10-25): freerouting runs tracks at bare clearance past GND pads; the thermal relief
then has no room for a spoke on that side, or its spoke reaches a sliver of pour boxed in by the
track -- KiCad's starved_thermal. Builds stopped on J1.2, Q2.1, D1.1, SW1.C and C1.2 in turn,
and re-routing only moved it. A spoke needs: relief gap 0.3 + pour min width 0.25 + clearance
0.2 = 0.75 mm of free board between the pad edge and the nearest track.

So each THT GND pad gets a tracks-only keep-out ring (KiCad exports rule areas to the DSN as
keep-outs): radius = pad radius + 0.75, but never reaching another net's pad (stops 0.05 short
of it -- no track fits through such a gap anyway). Named "spoke room", so 'remove' finds
exactly these. SMD GND pads (Pico castellations, microSD pad 6) are left alone: their
neighbours are too close for a ring, and the pour reaches them from outside the row.
"""
import math
import sys

import pcbnew

MM, mm = pcbnew.FromMM, pcbnew.ToMM
ROOM = 0.75
NAME = "spoke room"
path, op = sys.argv[1], sys.argv[2]
b = pcbnew.LoadBoard(path)

if op == "remove":
    gone = [z for z in b.Zones() if z.GetIsRuleArea() and z.GetZoneName() == NAME]
    for z in gone:
        b.Remove(z)
    pcbnew.SaveBoard(path, b)
    print(f"   spoke-room rings removed: {len(gone)}")
    sys.exit(0)

pads = [p for f in b.GetFootprints() for p in f.Pads()]
others = [p for p in pads if p.GetNetname() != "GND" and p.GetNetCode() > 0]
added = 0
for p in pads:
    if p.GetNetname() != "GND" or p.GetAttribute() != pcbnew.PAD_ATTRIB_PTH:
        continue
    c = p.GetPosition()
    r_pad = max(p.GetSize().x, p.GetSize().y) / 2
    r = r_pad + MM(ROOM)
    for o in others:
        sh = pcbnew.SHAPE_POLY_SET()
        o.TransformShapeToPolygon(sh, pcbnew.F_Cu if o.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu,
                                  0, MM(0.005), pcbnew.ERROR_OUTSIDE)
        # distance from c to the other pad's copper
        for k in range(sh.OutlineCount()):
            ol = sh.Outline(k)
            for i in range(ol.PointCount()):
                q = ol.CPoint(i)
                r = min(r, (q - c).EuclideanNorm() - MM(0.05))
    if r <= r_pad:
        continue
    z = pcbnew.ZONE(b)
    z.SetIsRuleArea(True)
    z.SetZoneName(NAME)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(False)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowCopperPour(False)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    ol = z.Outline()
    ol.NewOutline()
    for i in range(24):
        a = 2 * math.pi * i / 24
        ol.Append(int(c.x + r * math.cos(a)), int(c.y + r * math.sin(a)))
    b.Add(z)
    added += 1
pcbnew.SaveBoard(path, b)
print(f"   spoke-room rings added round {added} THT GND pads")
