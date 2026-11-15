#!/usr/bin/env python3
"""
Tracks laid by hand, LOCKED, before the DSN -- so neither router touches them.
    python3 hardware/gen/fixed_tracks.py BOARD.kicad_pcb        (board.sh, after the GND fanout)

+5V_J6, J6.1 -> JP3.1 (rev 1, 2026-10-29). Freerouting never entered JP3's pads in 8 attempts
(the bridged jumper's copper bar lies across both), and the KRT close then put a via in the pad.
Two short F.Cu runs round J6.2 (GND), on its side away from the board edge.
+5V_EXT, JP3.2 -> D3.2 (anode), the jumper's other side: later builds left THAT one to the KRT
close instead, with the same via in the pad. Down from JP3.2, right under the OLED standoff
hole H7, up left of the microSD socket, across under D3 to its anode.
"""
import sys

import pcbnew

MM = pcbnew.FromMM
b = pcbnew.LoadBoard(sys.argv[1])


def pad(ref, num):
    return b.FindFootprintByReference(ref).FindPadByNumber(num)




def run(netname, pts):
    a = pts[0]
    n = 0
    net = b.FindNet(netname)
    for p0, p1 in zip(pts, pts[1:]):
        if p0 == p1:
            continue
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(p0); t.SetEnd(p1); t.SetWidth(MM(0.5)); t.SetLayer(pcbnew.F_Cu)
        t.SetNet(net); t.SetLocked(True)
        b.Add(t); n += 1
    return n


V = pcbnew.VECTOR2I
j61, jp1, jp2, d3a = pad("J6", "1"), pad("JP3", "1"), pad("JP3", "2"), pad("D3", "2")
assert j61.GetNetname() == jp1.GetNetname() == "/+5V_J6", (j61.GetNetname(), jp1.GetNetname())
assert jp2.GetNetname() == d3a.GetNetname() == "/+5V_EXT", (jp2.GetNetname(), d3a.GetNetname())
assert jp1.GetPosition().y < jp2.GetPosition().y, "JP3 pad 1 must face J6 (rotation 270)"
pa, pz = j61.GetPosition(), jp1.GetPosition()
side = pa.x + MM(2.0)                         # 2.0 right of the J6 column, clear of J6.2's pad
n1 = run("/+5V_J6", [pa, V(side, pa.y), V(side, pz.y), pz])
q, d = jp2.GetPosition(), d3a.GetPosition()
x_up, y_x = q.x + MM(9.4), d.y + MM(2.5)      # x 12 (left of J3 at 15); y 64 (below D3's courtyard)
y_low = q.y + MM(1.95)                        # y 72.6: under the OLED standoff hole H7 (8.8, 70.0, r 1.35)
n2 = run("/+5V_EXT", [q, V(q.x, y_low), V(x_up, y_low), V(x_up, y_x), V(d.x, y_x), d])
pcbnew.SaveBoard(sys.argv[1], b)
print(f"   fixed tracks: +5V_J6 J6.1 -> JP3.1 ({n1} segments), +5V_EXT JP3.2 -> D3.2 ({n2}), locked")
