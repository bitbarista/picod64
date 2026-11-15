#!/usr/bin/env python3
"""
⚠ Ported 2026-09-23 from an earlier board's tools, verbatim below this note.
The "WHY" is the sibling's history, kept because it is the reason the gate
exists; this board's own proof is gen/test_check_pad_connectivity.py.

Every pad must reach the rest of its net IN COPPER. Fails naming each pad that
does not, and the nearest pad it should have reached.

    python3 gen/check_pad_connectivity.py <board.kicad_pcb>

⚠ WHY THIS EXISTS: SEVEN RELIABLY-DEAD REV C BOARDS. The ordered board
(md5 f438dcc0) had seven GND pads sitting on isolated pour fragments -- U8.2,
the 1V2 regulator's ground, and U5.3, the CAS buffer's, among them. All five
boards came back with no core rail. KiCad reported it, as "Found 7 unconnected
items", but every item reads "Zone [GND] <-> Zone [GND]": the report names the
two pieces of pour and NEVER the pad riding on the small one. verify.sh read
that as "pour-only -- every pad still reaches copper -- not a blocker". A pad on
a fragment does reach copper. It does not reach ground.

So the question is asked of the copper directly, and not of the DRC's wording:
build every pad, via, track, arc and filled-zone island of a net as a polygon
on each copper layer, join the ones whose copper overlaps, and require one
connected piece per net holding every pad.

⚠ NOT CONNECTIVITY_DATA.GetConnectedPads. Tried first: it returned zero
connected pads for EVERY pad on the board, so a gate built on it would have
failed all 72 nets, or -- written the other way round -- passed all of them.

⚠ AND IT MUST AGREE WITH KICAD. The number of extra pieces found here, summed
over nets, is compared with KiCad's own unconnected count. A mismatch FAILS: a
disagreement means one of the two models is wrong about this board, and the
direction that bit us was a checker being kinder than the copper.

Uses the fills SAVED in the file, because those are what the Gerbers plot and
what was fabricated. It never refills.
"""
import math
import sys

import pcbnew

EPS = pcbnew.FromMM(0.005)       # grow each shape 5 um so abutting copper counts
MAXERR = pcbnew.FromMM(0.005)


def analyse(b):
    """-> (isolated, extra_pieces, piece): piece[(ref, pad)] = (net name, piece key).

    The piece key identifies which connected piece of copper the pad sits on; the
    board-level simulation builds its copper netlist from it (sim/board/).
    """
    layers = [l for l in b.GetEnabledLayers().CuStack()]

    def poly(item, layer):
        s = pcbnew.SHAPE_POLY_SET()
        item.TransformShapeToPolygon(s, layer, EPS, MAXERR, pcbnew.ERROR_OUTSIDE)
        s.Simplify()
        return s

    # nodes[net] = list of (node_id, layer, SHAPE_POLY_SET, BOX2I)
    nodes, pads, label = {}, {}, {}

    def add(net, nid, layer, s):
        if s.OutlineCount() == 0:
            return
        nodes.setdefault(net, []).append((nid, layer, s, s.BBox()))

    for fp in b.GetFootprints():
        for p in fp.Pads():
            net = p.GetNetCode()
            if net <= 0:
                continue
            nid = ("pad", fp.GetReference(), p.GetNumber(), id(p))
            pads.setdefault(net, []).append((nid, p))
            label[nid] = f"{fp.GetReference()}.{p.GetNumber()}"
            for L in layers:
                if p.IsOnLayer(L) and p.FlashLayer(L):
                    add(net, nid, L, poly(p, L))
    for t in b.GetTracks():
        net = t.GetNetCode()
        if net <= 0:
            continue
        nid = ("trk", t.m_Uuid.AsString())
        if t.Type() == pcbnew.PCB_VIA_T:
            for L in layers:
                if t.IsOnLayer(L) and t.FlashLayer(L):
                    add(net, nid, L, poly(t, L))
        else:
            add(net, nid, t.GetLayer(), poly(t, t.GetLayer()))
    for zi, z in enumerate(b.Zones()):
        net = z.GetNetCode()
        if net <= 0 or z.GetIsRuleArea():
            continue
        for L in layers:
            if not z.IsOnLayer(L):
                continue
            fill = z.GetFilledPolysList(L)
            for k in range(fill.OutlineCount()):
                island = fill.UnitSet(k)
                add(net, ("zone", zi, L, k), L, island)

    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, c):
        parent[find(a)] = find(c)

    def touch(a, c):
        x = pcbnew.SHAPE_POLY_SET(a)
        x.BooleanIntersection(c)
        return x.OutlineCount() > 0 and x.Area() > 0

    isolated, extra_pieces, piece = [], 0, {}
    for net, items in nodes.items():
        for nid, *_ in items:
            find(nid)
        for L in layers:
            on = [it for it in items if it[1] == L]
            for i in range(len(on)):
                for j in range(i + 1, len(on)):
                    if find(on[i][0]) == find(on[j][0]):
                        continue
                    if not on[i][3].Intersects(on[j][3]):
                        continue
                    if touch(on[i][2], on[j][2]):
                        union(on[i][0], on[j][0])
        # KiCad counts a piece if it holds a pad or pour; bare track stubs
        # are "dangling track" warnings to it, not unconnected items.
        pieces = {}
        for nid, *_ in items:
            if nid[0] in ("pad", "zone"):
                pieces.setdefault(find(nid), set()).add(nid)
        for nid, _ in pads.get(net, []):          # pads with no copper at all
            pieces.setdefault(find(nid), set()).add(nid)
        extra_pieces += max(0, len(pieces) - 1)
        padgroups = {}
        for nid, p in pads.get(net, []):
            padgroups.setdefault(find(nid), []).append((nid, p))
            piece[(nid[1], nid[2])] = (p.GetNetname(), find(nid))
        if len(padgroups) > 1:
            main = max(padgroups.values(), key=len)
            for grp in padgroups.values():
                if grp is main:
                    continue
                for nid, p in grp:
                    P = p.GetPosition()
                    near = min(main, key=lambda m: math.hypot(
                        m[1].GetPosition().x - P.x, m[1].GetPosition().y - P.y))
                    d = pcbnew.ToMM(math.hypot(near[1].GetPosition().x - P.x,
                                               near[1].GetPosition().y - P.y))
                    isolated.append((p.GetNetname(), label[nid],
                                     pcbnew.ToMM(P.x), pcbnew.ToMM(P.y),
                                     label[near[0]], d, len(grp), len(main)))

    return isolated, extra_pieces, piece


def main(path):
    b = pcbnew.LoadBoard(path)
    isolated, extra_pieces, _ = analyse(b)
    b.BuildConnectivity()
    kicad = b.GetConnectivity().GetUnconnectedCount(False)

    print(f"   pads cut off from their net   : {len(isolated)}   <- FATAL if non-zero")
    for net, pad, x, y, near, d, g, m in sorted(isolated):
        print(f"      {pad:8s} [{net}] at ({x:.2f}, {y:.2f}) -- nearest connected "
              f"pad {near}, {d:.1f} mm  (a piece of {g} pad(s) vs the net's {m})")
    print(f"   copper pieces beyond one/net  : {extra_pieces}   KiCad unconnected: {kicad}")
    bad = False
    if isolated:
        bad = True
    if extra_pieces != kicad:
        print("   *** this check and KiCad disagree about how many pieces the board is in"
              " -- one model is wrong about this board, so neither verdict stands")
        bad = True
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
