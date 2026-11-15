#!/usr/bin/env python3
"""
# Ported 2026-10-15, unchanged, from an earlier board's tools, where it
# replaced gen/fix_via_in_pad.py after that "damaged the board on every attempt".
Take every GND via out of a small pad (ERRATA F-2) WITHOUT cutting the pad off (ERRATA F-14).

    python3 gen/remove_vias_in_pads.py <board.kicad_pcb>

⚠ THIS REPLACES TWO INLINE BLOCKS IN gen/board-finish.sh THAT SHIPPED A DEAD BOARD.
They removed every GND via sitting in a pad on the reasoning "a GND stitch inside
a pad is redundant among ~220 others". On a two-layer board with
`connect_pads thru_hole_only`, a surface-mount pad reaches ground ONLY through its
own stub, and for some pads the via in the pad WAS the stub's only way down. Rev C
lost seven that way -- U8.2 (the 1V2 regulator) and U5.3 (the CAS buffer) among
them -- and all five fabricated boards came back with no core rail. U8.2's copper
still shows it: a B.Cu route to a good via starting inside the pad, and nothing
left joining the pad to it.

So:
  1. remove the in-pad GND vias (QFN exposed pads excepted: those are intended) --
     in a SEPARATE PROCESS, see strip()
  2. refill, and ask gen/check_pad_connectivity.analyse which pads are now cut
  3. for each: FIRST a straight/one-bend F.Cu track to copper already on the net (an
     existing via or pad); failing that, a via OUTSIDE every pad, either dropped into the pad's own pour
     fragment or joined to the pad by a straight / one-bend F.Cu track, clear of
     all other-net copper by CLR and of every hole by HOLE_CLR, taken only if a
     refill + analyse shows the pad joined its net
  4. anything still cut: EXIT 1, naming it. The build stops there; nobody gets a
     board file with a pad on an island.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcbnew                                  # noqa: E402
import check_pad_connectivity as cpc           # noqa: E402

FM, TM = pcbnew.FromMM, pcbnew.ToMM
CLR = FM(0.12)          # 0.10 is the fab floor (syn/fab_jlc_routing.txt)
HOLE_CLR = FM(0.25)     # min_hole_to_hole
W = FM(0.2)
VD, VH = FM(0.6), FM(0.3)
RADIUS = 40             # x 0.1 mm
F, B = pcbnew.F_Cu, pcbnew.B_Cu


def big_pad(p):
    return TM(p.GetSizeX()) > 3.0 and TM(p.GetSizeY()) > 3.0


def strip(path, before_file):
    """Stage 1, its own process: note what is already cut, remove the in-pad vias, save.

    ⚠ ITS OWN PROCESS BECAUSE board.Remove() OF AN ITEM LOADED FROM THE FILE CORRUPTS
    pcbnew's SWIG registry for the rest of the process (gen/strip_artwork.py records it):
    the next GetEnabledLayers() came back as a bare SwigPyObject and the connectivity
    check crashed -- the first version of this tool died exactly there."""
    b = pcbnew.LoadBoard(path)
    gnd = b.GetNetcodeFromNetname("GND")
    pads = [p for f in b.GetFootprints() for p in f.Pads()]
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    before = sorted(x[1] for x in cpc.analyse(b)[0])
    doomed = []
    for t in b.GetTracks():
        if t.GetClass() != "PCB_VIA" or t.GetNetCode() != gnd:
            continue
        hit = [p for p in pads if p.HitTest(t.GetPosition())]
        if hit and not any(big_pad(p) for p in hit):
            doomed.append(t)
    for t in doomed:
        b.Remove(t)
    pcbnew.SaveBoard(path, b)
    open(before_file, "w").write("\n".join(before))
    print(f"   removed {len(doomed)} GND via(s) sitting in a pad")
    return 0


RECONNECT_ALL = False


def main(path):
    import subprocess, tempfile
    if RECONNECT_ALL:
        before = set()
    else:
        before_file = tempfile.mktemp(suffix=".cut")
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--strip", path, before_file],
                           capture_output=True, text=True)
        sys.stdout.write("".join(l + "\n" for l in r.stdout.splitlines() if "Debug" not in l))
        if r.returncode:
            sys.stderr.write(r.stderr)
            return r.returncode
        before = set(filter(None, open(before_file).read().split("\n")))
        os.remove(before_file)

    b = pcbnew.LoadBoard(path)
    gnd = b.GetNetcodeFromNetname("GND")
    pads = [p for f in b.GetFootprints() for p in f.Pads()]
    fill = lambda: pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    fill()
    iso, _, _ = cpc.analyse(b)
    cut = [x[1] for x in iso if x[1] not in before]
    print(f"   that cut off {len(cut)} pad(s)" + (f": {', '.join(cut)}" if cut else ""))

    others = {L: [] for L in (F, B)}
    holes = []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetDrillSize().x > 0:
                holes.append((p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y) / 2))
            for L in (F, B):
                if p.IsOnLayer(L) and p.GetNetCode() != gnd:
                    others[L].append(p.GetEffectiveShape(L))
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            holes.append((t.GetPosition(), t.GetDrillValue() / 2))
        for L in (F, B):
            if t.IsOnLayer(L) and t.GetNetCode() != gnd:
                others[L].append(t.GetEffectiveShape(L))

    def clear(item, layers):
        return all(not o.Collide(item.GetEffectiveShape(L), CLR) for L in layers for o in others[L])

    still = []
    for lab in cut:
        ref, num = lab.split(".")
        pad = [p for p in b.FindFootprintByReference(ref).Pads() if p.GetNumber() == num][0]
        net = pad.GetNetCode()
        P = pad.GetPosition()
        done = None
        # FIRST, AND CHEAPEST: a straight or one-bend track to copper ALREADY on the net.
        # ⚠ FROM THE PAD *OR* FROM ITS ESCAPE VIA, ON EITHER LAYER. An SMD pad is F.Cu only,
        # but the piece that is cut off usually includes the pad's escape via -- and after the
        # thermal-relief step re-carves the pour it is the VIA's B.Cu side that is stranded
        # (U2.8, Rev D). Starting only from the pad on F.Cu could not fix that.
        # -- an existing via or pad. On Rev D's first route C12.2's stub ran on B.Cu from the
        # deleted in-pad via to a good via 0.9 mm away, with open board between: no new via
        # was needed, and the new-via search alone reported it unfixable.
        targets = []
        for t2 in b.GetTracks():
            if t2.GetClass() == "PCB_VIA" and t2.GetNetCode() == net:
                targets.append(t2.GetPosition())
        for f2 in b.GetFootprints():
            for p2 in f2.Pads():
                if p2.GetNetCode() == net and p2.IsOnLayer(F) and f"{f2.GetReference()}.{p2.GetNumber()}" != lab:
                    targets.append(p2.GetPosition())
        targets = sorted((tg for tg in targets if math.hypot(tg.x - P.x, tg.y - P.y) <= FM(6.0)),
                         key=lambda tg: math.hypot(tg.x - P.x, tg.y - P.y))
        starts = [(P, F)]
        for t2 in b.GetTracks():
            if t2.GetClass() == "PCB_VIA" and t2.GetNetCode() == net and \
               math.hypot(t2.GetPosition().x - P.x, t2.GetPosition().y - P.y) <= FM(3.0):
                starts += [(t2.GetPosition(), F), (t2.GetPosition(), B)]
        for S, L in starts:
            for T in targets:
                dx, dy = T.x - S.x, T.y - S.y
                if dx == 0 and dy == 0:
                    continue
                s = min(abs(dx), abs(dy))
                sg = lambda a: (a > 0) - (a < 0)
                # one bend, then two: a Z round a bus track is often the only way out from
                # under a package (U2.8, Rev D -- every straight run blocked by MA lines)
                paths = [[S, T],
                         [S, pcbnew.VECTOR2I(T.x, S.y), T],
                         [S, pcbnew.VECTOR2I(S.x, T.y), T],
                         [S, pcbnew.VECTOR2I(S.x + sg(dx) * s, S.y + sg(dy) * s), T]]
                for k in range(1, 8):
                    mx = S.x + (T.x - S.x) * k // 8
                    my = S.y + (T.y - S.y) * k // 8
                    paths.append([S, pcbnew.VECTOR2I(mx, S.y), pcbnew.VECTOR2I(mx, T.y), T])
                    paths.append([S, pcbnew.VECTOR2I(S.x, my), pcbnew.VECTOR2I(T.x, my), T])
                for pts in paths:
                    segs = []
                    for a1, a2 in zip(pts, pts[1:]):
                        if a1.x == a2.x and a1.y == a2.y:
                            continue
                        tr = pcbnew.PCB_TRACK(b)
                        tr.SetStart(a1); tr.SetEnd(a2); tr.SetWidth(W); tr.SetLayer(L); tr.SetNetCode(net)
                        segs.append(tr)
                    if not segs or not all(clear(tr, (L,)) for tr in segs):
                        continue
                    for tr in segs:
                        b.Add(tr)
                    fill()
                    if not any(x[1] == lab for x in cpc.analyse(b)[0]):
                        done = ("track", T, len(segs))
                        break
                    for tr in segs:
                        b.Remove(tr)
                if done:
                    break
            if done:
                break
        if done:
            print(f"      {lab}: reconnected by {done[2]} F.Cu track segment(s) to existing copper at "
                  f"({TM(done[1].x):.2f}, {TM(done[1].y):.2f})")
            continue
        sites = sorted(((math.hypot(ix, iy), ix, iy) for ix in range(-RADIUS, RADIUS + 1)
                        for iy in range(-RADIUS, RADIUS + 1)), key=lambda s: s[0])
        done = None
        for _, ix, iy in sites:
            v = pcbnew.VECTOR2I(P.x + FM(0.1) * ix, P.y + FM(0.1) * iy)
            if any(p.HitTest(v) or p.GetEffectiveShape(F).Collide(v, VD // 2 + CLR) for p in pads
                   if p.IsOnLayer(F) or p.IsOnLayer(B)):
                continue
            if any(math.hypot(v.x - h.x, v.y - h.y) - r - VH / 2 < HOLE_CLR for h, r in holes):
                continue
            via = pcbnew.PCB_VIA(b)
            via.SetPosition(v); via.SetWidth(VD); via.SetDrill(VH)
            via.SetLayerPair(F, B); via.SetNetCode(net)
            if not clear(via, (F, B)):
                continue
            dx, dy = v.x - P.x, v.y - P.y
            s = min(abs(dx), abs(dy))
            sg = lambda a: (a > 0) - (a < 0)
            options = [[]]                                  # a bare via, into the fragment
            for c in (None, pcbnew.VECTOR2I(v.x, P.y), pcbnew.VECTOR2I(P.x, v.y),
                      pcbnew.VECTOR2I(P.x + sg(dx) * s, P.y + sg(dy) * s)):
                pts = [P, v] if c is None else [P, c, v]
                segs = []
                for a1, a2 in zip(pts, pts[1:]):
                    if a1.x == a2.x and a1.y == a2.y:
                        continue
                    tr = pcbnew.PCB_TRACK(b)
                    tr.SetStart(a1); tr.SetEnd(a2); tr.SetWidth(W); tr.SetLayer(F); tr.SetNetCode(net)
                    segs.append(tr)
                if segs and all(clear(tr, (F,)) for tr in segs):
                    options.append(segs)
            for segs in options:
                b.Add(via)
                for tr in segs:
                    b.Add(tr)
                fill()
                if not any(x[1] == lab for x in cpc.analyse(b)[0]):
                    done = (v, len(segs))
                    break
                for tr in segs:
                    b.Remove(tr)
                b.Remove(via)
            if done:
                break
        if done:
            v, n = done
            print(f"      {lab}: reconnected by a via at ({TM(v.x):.2f}, {TM(v.y):.2f})"
                  + (f" and {n} track segment(s)" if n else ", dropped into its own pour fragment"))
        else:
            still.append(lab)
    fill()
    pcbnew.SaveBoard(path, b)
    if still:
        print(f"   *** {len(still)} pad(s) CUT OFF and not reconnectable here: {', '.join(still)} -- "
              "the build stops; this is a routing problem to solve, not a via to delete")
        return 1
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--strip":
        sys.exit(strip(sys.argv[2], sys.argv[3]))
    if len(sys.argv) == 3 and sys.argv[1] == "--reconnect-all":
        # the LAST line of defence in board-finish.sh: after every plane tool has had its
        # turn, try to reconnect ANY cut pad by the same rules, and fail naming what is left
        RECONNECT_ALL = True
        sys.exit(main(sys.argv[2]))
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
