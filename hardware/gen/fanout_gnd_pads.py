#!/usr/bin/env python3
"""
Give every surface-mount GND pad its own locked escape via BEFORE the signals are routed.

    python3 gen/fanout_gnd_pads.py <board.kicad_pcb>      (floorplan stage, gen/board.sh)

Ported 2026-10-15 from an earlier board's tools: the
method that fixed cut-off ground pads there. Changes, nothing else:
  * the stub goes on the PAD'S OWN layer -- Rev B has SMD parts on BOTH sides (U2-U4 and
    their 0805s under the Pico). The sibling only had F-side SMD.
  * the courtyard rule looks only at parts on the pad's own side (else nothing under the Pico
    could escape: its courtyard covers the area).
  * SKIP = the Pico (its GND castellations get gen_pcb.py's locked escapes) and the edge
    fingers. Its QFN-exposed-pad and DIRECTION_OVERRIDE rules are the sibling's, kept as found
    (no override entries: those were measured on the sibling's board).

⚠ WHY: TWO REVISIONS, THE SAME CAPACITOR. On two layers with `connect_pads
thru_hole_only`, an SMD GND pad reaches ground only through its own stub, and
freerouting never routes GND -- it is a pour net -- so nothing stops it walling a
ground pad in with signal tracks. Rev C shipped seven pads cut off that way (ERRATA
F-14). Rev D's first route did it again to C12.2: MA1 down its right and across its
top, signals down its left, its own +3V3 pad below -- the only way out was a via IN
the pad, which F-2 forbids and gen/remove_vias_in_pads.py rightly refused.

The fix is to take the question away from the router. Each SMD GND pad gets a short
F.Cu stub and a 0.6/0.3 via, placed now, outside every pad and courtyard, LOCKED so
the DSN carries them as protected wiring. freerouting must then route around the
escape, and every pad has a guaranteed path to the B.Cu pour whatever the route does.

Placement per pad: try distances 0.9..2.4 mm in eight directions plus the four axes,
outward from the part's centre first; accept the first site whose via clears every other-net pad by
CLR, every hole by HOLE_CLR, the board edge by EDGE, every OTHER footprint's
courtyard, and whose stub clears every other-net pad. A pad with no site FAILS the
build, naming it: that is a floorplan problem, and the floorplan is where to fix it.
"""
import math
import sys

import pcbnew

FM, TM = pcbnew.FromMM, pcbnew.ToMM
CLR, HOLE_CLR, EDGE = FM(0.20), FM(0.30), FM(0.60)
W, VD, VH = FM(0.25), FM(0.6), FM(0.3)
F, B = pcbnew.F_Cu, pcbnew.B_Cu


# ⚠ PER-PAD DIRECTION OVERRIDES, BY MEASUREMENT, NOT BY TASTE. An escape via is a fixed
# obstacle, and two of them landed in a lane the router needs:
#   C4.2 / C6.2 escaping UPWARD sat at y=35.12 between U1.23 (37.75, 32.94) and R20.2
#   (42.51, 37.50) -- WE_N then failed to route on every attempt. Sideways clears the lane.
# Add an entry only with the net it unblocked written beside it.
DIRECTION_OVERRIDE = {
    "C1.2": 0.0,          # +x: up is its label spot (above, centred); -x is U1 (central-post layout)
}
SKIP = {"A1"}   # PicoD64: the Pico (locked escapes in gen_pcb.py); no edge fingers here


def main(path):
    b = pcbnew.LoadBoard(path)
    gnd = b.GetNetcodeFromNetname("GND")
    edge = b.GetBoardEdgesBoundingBox()
    fps = list(b.GetFootprints())
    pads = [(fp, p) for fp in fps for p in fp.Pads()]
    holes = [(p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y) / 2)
             for _, p in pads if p.GetDrillSize().x > 0]
    holes += [(t.GetPosition(), t.GetDrillValue() / 2) for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
    placed, failed = [], []

    def courtyard(fp):
        return fp.GetCourtyard(F) if fp.GetLayer() == F else fp.GetCourtyard(B)

    for fp, pad in pads:
        if pad.GetNetCode() != gnd or pad.GetDrillSize().x > 0 or fp.GetReference() in SKIP:
            continue
        PL = F if pad.IsOnLayer(F) else B          # the pad's own copper layer
        if TM(pad.GetSizeX()) > 3.0 and TM(pad.GetSizeY()) > 3.0:
            continue                                   # QFN exposed pad: gen/fix_ep_vias.py
        P, C = pad.GetPosition(), fp.GetPosition()
        out = math.atan2(P.y - C.y, P.x - C.x) if (P.x, P.y) != (C.x, C.y) else 0.0
        # ⚠ THE FOUR AXIS DIRECTIONS TOO. Angles measured only from the part's centre never
        # point straight along a fine-pitch pad, so on U2's SSOP-48 (0.635 mm pitch) every
        # stub cut across the neighbouring pin and all four inner GND pins had "no site".
        cand = [out + k * math.pi / 4 for k in range(8)] + [k * math.pi / 2 for k in range(4)]
        # ⚠ INWARD FOR ICs. Outward from an 8+ pin part is exactly where its signals fan out:
        # U2's right-column GND escapes sat in the U2 -> U1 bus lanes and freerouting left
        # D7_SKT and MA2 unrouted. Under the package, between the pad rows, is empty.
        prefer = out + math.pi if len(list(fp.Pads())) >= 8 else out
        if f"{fp.GetReference()}.{pad.GetNumber()}" in DIRECTION_OVERRIDE:
            prefer = DIRECTION_OVERRIDE[f"{fp.GetReference()}.{pad.GetNumber()}"]
        angles = sorted(cand, key=lambda a: abs(math.remainder(a - prefer, 2 * math.pi)))
        # ported: an override is a DIRECTION, not a preference. Distance is the outer loop, so a
        # mere preference loses to any other angle that fits at a nearer distance (C1.2 kept
        # escaping up, onto its label spot, because +x needs 1.4 mm to clear its own pad).
        if f"{fp.GetReference()}.{pad.GetNumber()}" in DIRECTION_OVERRIDE:
            angles = [prefer]
        # ⚠ NEAREST SITE, HUGGING THE PAD -- AND "ROOMIEST" WAS TRIED AND REVERTED. Choosing
        # the site with the most space around it put the escape vias out in open board, which
        # is where the router needs its lanes: SIX routing attempts in a row then failed with
        # +5V_SKT1 open. A via tight against its own pad costs the router least.
        site = None
        for d in (0.9, 1.0, 1.1, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.4):
            ring = []
            for a in angles:
                v = pcbnew.VECTOR2I(int(P.x + FM(d) * math.cos(a)), int(P.y + FM(d) * math.sin(a)))
                if not (edge.GetLeft() + EDGE < v.x < edge.GetRight() - EDGE and
                        edge.GetTop() + EDGE < v.y < edge.GetBottom() - EDGE):
                    continue
                via = pcbnew.PCB_VIA(b)
                via.SetPosition(v); via.SetWidth(VD); via.SetDrill(VH)
                via.SetLayerPair(F, B); via.SetNetCode(gnd)
                tr = pcbnew.PCB_TRACK(b)
                tr.SetStart(P); tr.SetEnd(v); tr.SetWidth(W); tr.SetLayer(PL); tr.SetNetCode(gnd)
                vs = {L: via.GetEffectiveShape(L) for L in (F, B)}
                ts = tr.GetEffectiveShape(PL)
                bad = False
                for fp2, p2 in pads:
                    same_pad = p2 is pad
                    for L in (F, B):
                        if not p2.IsOnLayer(L):
                            continue
                        ps = p2.GetEffectiveShape(L)
                        # the via may never touch any pad, its own included (F-2)
                        if ps.Collide(vs[L], CLR if not same_pad else FM(0.10)):
                            bad = True
                            break
                        if L == PL and p2.GetNetCode() != gnd and ps.Collide(ts, CLR):
                            bad = True
                            break
                    if bad:
                        break
                if bad:
                    continue
                if any(math.hypot(v.x - h.x, v.y - h.y) - r - VH / 2 < HOLE_CLR for h, r in holes):
                    continue
                # ported: only parts on the PAD'S side -- the Pico's F courtyard covers everything
                # under it, and a via there is normal (its TP keep-outs are zones: DRC checks them)
                if any(fp2 is not fp and fp2.GetLayer() == fp.GetLayer() and courtyard(fp2).Collide(v, VD // 2)
                       for fp2 in fps):
                    continue
                ring.append((0, v, via, tr, d))
                break                       # first (nearest-angle) site at this distance wins
            if ring:
                _, v, via, tr, d = ring[0]
                site = (v, via, tr, d)
                break
        lab = f"{fp.GetReference()}.{pad.GetNumber()}"
        if not site:
            failed.append(lab)
            continue
        v, via, tr, d = site
        via.SetLocked(True); tr.SetLocked(True)
        b.Add(via); b.Add(tr)
        holes.append((v, VH / 2))
        placed.append((lab, TM(v.x), TM(v.y), d))
    # ⚠ SPINE TRACKS BETWEEN AN IC'S ESCAPE VIAS -- REINSTATED, AND HERE IS THE HISTORY.
    # Removed once because a locked run under U2 left D3_SKT, MA7_R and VPROG unrouted;
    # put back because WITHOUT it U2.8 ends up landlocked: its escape via is under the
    # package, the address bus crosses above and below it on both layers, and no
    # post-route repair can reach it (measured, Rev D: every straight, one-bend and
    # two-bend path on F.Cu and B.Cu blocked). The routing pressure it adds is now
    # survivable because board.sh retries the route (ROUTE_TRIES) and keeps an attempt
    # only if every signal net closed.
    spines = 0
    by_fp = {}
    for lab, x, y, _ in placed:
        by_fp.setdefault(lab.split(".")[0], []).append((x, y))
    for ref, pts in by_fp.items():
        fpx = b.FindFootprintByReference(ref)
        # ⚠ SPINES OFF: with the clearance pinned correctly on every attempt the board would
        # not close (CRESET_N and MA7_R open on all of them) until these five locked tracks
        # under U2 came out. U2.8 is handled after routing by --reconnect-all instead.
        if True or len(list(fpx.Pads())) < 8 or len(pts) < 2:
            continue
        joined, pending = {pts[0]}, set(pts[1:])
        while pending:
            _, a, c = min(((math.hypot(a[0] - c[0], a[1] - c[1]), a, c)
                           for a in joined for c in pending), key=lambda z: z[0])
            pending.discard(c); joined.add(c)
            tr = pcbnew.PCB_TRACK(b)
            tr.SetStart(pcbnew.VECTOR2I(FM(a[0]), FM(a[1])))
            tr.SetEnd(pcbnew.VECTOR2I(FM(c[0]), FM(c[1])))
            tr.SetWidth(W); tr.SetLayer(F); tr.SetNetCode(gnd)
            ts = tr.GetEffectiveShape(F)
            if any(p2.IsOnLayer(F) and p2.GetNetCode() != gnd and p2.GetEffectiveShape(F).Collide(ts, CLR)
                   for _, p2 in pads):
                continue
            tr.SetLocked(True)
            b.Add(tr)
            spines += 1
    print(f"   GND spine tracks under ICs: {spines}")
    pcbnew.SaveBoard(path, b)
    print(f"   GND escape vias: {len(placed)} placed and locked" +
          (f"; *** NO SITE for {', '.join(failed)}" if failed else ""))
    for lab, x, y, d in placed:
        print(f"      {lab:7s} via at ({x:.2f}, {y:.2f}), {d:.1f} mm")
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
