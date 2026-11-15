#!/usr/bin/env python3
"""
No via may touch a component pad. Fix what the routers left, then (--check) prove it.

    python3 gen/fix_via_in_pad.py <board.kicad_pcb>            fix, save
    python3 gen/fix_via_in_pad.py <board.kicad_pcb> --check    report, exit 1 if any

⚠ WHY (an earlier board shipped with 14): an open via barrel in a pad
wicks solder down the hole and starves the joint. Here the pads are the Pico's
castellations, soldered by hand -- the joint IS the board. The routers have no option to
avoid it, so a check must fail loudly. Found 2026-10-11 by checking a sentence in ORDER.md
("none in a pad") that had been written without being measured: three were.

What touched pads, and what is done:
  * a via with NO track, in a pad of its own net -- stitch.py's lattice (the pour floods a
    solid-connected pad, so "both pours filled here" was true inside it; stitch.py now
    excludes pads). The pour still reaches the pad, so it is deleted.
  * a via WITH tracks (route_planes' GND tap in A1.28, KRT's /D0 close beside A1.1) --
    slid along its own track on one layer until it clears the pad by CLEAR, with a jog on
    the other layer to reach it. ⚠ NOT DELETED: the first version deleted every GND via
    in a GND pad as "redundant beside the locked escape via" -- A1.28's was that pad's
    ONLY path to ground, and check_pad_connectivity named it at once.
  The caller's gates (check_pad_connectivity, DRC) then judge the result.
Anything else touching a pad is reported and left: the script exits 1 rather than guess.
(A different-net via in a pad is handled the same way, but KiCad re-assigns such a via to
the pad's net on load, so it is not expected to occur; DRC owns shorts.)
"""
import math
import sys

import pcbnew

MM, mm = pcbnew.FromMM, pcbnew.ToMM
CLEAR = 0.2          # via ring to pad copper, after a move: a mask web, as pad_to_mask_clearance is 0
                     # and the fab adds its own expansion [A -- figure not on hand]


SEARCH = 4.0         # mm, farthest a via is moved
JOG_W = 0.25         # mm, the board's signal width (gen/project.py)
GAP = 0.22           # mm, other-net clearance: gen/fab_floor.txt, above DRC's 0.2
HOLE_GAP = 0.25 + 0.05   # mm, hole edge to hole edge: board minimum + margin


def clear_of_others(b, v, q, jog_layers):
    """Would a via at q, and a jog to it on each of jog_layers, clear every other-net item?"""
    r = v.GetWidth(pcbnew.F_Cu) // 2
    net = v.GetNetCode()
    pt = v.GetPosition()
    probe = pcbnew.PCB_VIA(b)
    probe.SetPosition(q); probe.SetWidth(v.GetWidth(pcbnew.F_Cu)); probe.SetDrill(v.GetDrillValue())
    probe.SetNet(v.GetNet())
    jogs = []
    for lay in jog_layers:
        j = pcbnew.PCB_TRACK(b); j.SetStart(pt); j.SetEnd(q); j.SetWidth(MM(JOG_W)); j.SetLayer(lay)
        jogs.append(j)
    items = [t for t in b.GetTracks() if t.GetNetCode() != net and t is not v]
    items += [pp for f in b.GetFootprints() for pp in f.Pads() if pp.GetNetCode() != net]
    for it in items:
        for L in (pcbnew.F_Cu, pcbnew.B_Cu):
            if not it.IsOnLayer(L):
                continue
            sh = it.GetEffectiveShape(L)
            if probe.GetEffectiveShape(L).Collide(sh, MM(GAP)):
                return False
            for j in jogs:
                if j.GetLayer() == L and j.GetEffectiveShape(L).Collide(sh, MM(GAP)):
                    return False
    # holes: every other via and drilled pad
    d0 = v.GetDrillValue() / 2
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T and t is not v:
            if (t.GetPosition() - q).EuclideanNorm() < d0 + t.GetDrillValue() / 2 + MM(HOLE_GAP):
                return False
    for f in b.GetFootprints():
        for pp in f.Pads():
            if pp.GetDrillSize().x > 0:
                if (pp.GetPosition() - q).EuclideanNorm() < d0 + max(pp.GetDrillSize().x, pp.GetDrillSize().y) / 2 + MM(HOLE_GAP):
                    return False
    return True


def new_home(b, v, p):
    """Nearest site that clears pad p by CLEAR, every pad's copper, and every other net."""
    pt, r = v.GetPosition(), v.GetWidth(pcbnew.F_Cu) // 2
    layers = sorted({t.GetLayer() for t in tracks_at(b, pt, v.GetNetCode())})
    pads = [(pp, L, pad_poly(pp, L)) for f in b.GetFootprints() for pp in f.Pads()
            for L in (pcbnew.F_Cu, pcbnew.B_Cu) if pp.IsOnLayer(L)]
    cands = []
    step = MM(0.05)
    for k in range(1, int(MM(SEARCH) / step) + 1):
        rad = k * step
        n = max(8, int(2 * math.pi * rad / step))
        for a in range(n):
            ang = 2 * math.pi * a / n
            cands.append((rad, pcbnew.VECTOR2I(int(pt.x + rad * math.cos(ang)), int(pt.y + rad * math.sin(ang)))))
    for rad, q in cands:
        if any(poly.Contains(q) or poly.Collide(q, r + MM(CLEAR)) for pp, L, poly in pads):
            continue
        if clear_of_others(b, v, q, layers):
            return q
    return None


def pad_poly(p, layer):
    s = pcbnew.SHAPE_POLY_SET()
    p.TransformShapeToPolygon(s, layer, 0, MM(0.005), pcbnew.ERROR_OUTSIDE)
    return s


def touching(b):
    """[(via, pad, layer, inside?)] for every via whose ring touches a pad's copper."""
    out = []
    for v in b.GetTracks():
        if v.Type() != pcbnew.PCB_VIA_T:
            continue
        r = v.GetWidth(pcbnew.F_Cu) // 2
        for f in b.GetFootprints():
            for p in f.Pads():
                for L in (pcbnew.F_Cu, pcbnew.B_Cu):
                    if not p.IsOnLayer(L):
                        continue
                    s = pad_poly(p, L)
                    c = v.GetPosition()
                    if s.Contains(c) or s.Collide(c, r):
                        out.append((v, p, L, s.Contains(c)))
                        break
    return out


def tracks_at(b, pt, net):
    return [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetCode() == net
            and min((t.GetStart() - pt).EuclideanNorm(), (t.GetEnd() - pt).EuclideanNorm()) <= MM(0.01)]


def far_end(t, pt):
    return t.GetEnd() if (t.GetStart() - pt).EuclideanNorm() <= MM(0.01) else t.GetStart()


def describe(v, p):
    return (f"{v.GetNetname()} via at ({mm(v.GetPosition().x):.3f}, {mm(v.GetPosition().y):.3f}) "
            f"on {p.GetParentFootprint().GetReference()}.{p.GetNumber()} ({p.GetNetname()})")


def fix(b):
    done, left = [], []
    for v, p, L, inside in touching(b):
        what = describe(v, p)                # before anything is deleted
        if v.GetNetCode() == p.GetNetCode() and not tracks_at(b, v.GetPosition(), v.GetNetCode()):
            pt, net = v.GetPosition(), v.GetNetCode()
            gone = 0
            # tracks that ended at this via and now end nowhere: walk the chain away
            frontier = [pt]
            b.Delete(v)
            while frontier:
                q = frontier.pop()
                for t in tracks_at(b, q, net):
                    e = far_end(t, q)
                    others = [o for o in tracks_at(b, e, net) if o is not t]
                    on_pad = any(pp.GetNetCode() == net and pp.IsOnLayer(t.GetLayer()) and pp.HitTest(e)
                                 for f in b.GetFootprints() for pp in f.Pads())
                    via_at = any(o.Type() == pcbnew.PCB_VIA_T and (o.GetPosition() - e).EuclideanNorm() <= MM(0.01)
                                 for o in b.GetTracks())
                    on_q_pad = p.IsOnLayer(t.GetLayer()) and pad_poly(p, t.GetLayer()).Contains(e)
                    if on_q_pad or not (others or on_pad or via_at):
                        b.Delete(t)
                        gone += 1
                        if not on_q_pad:
                            frontier.append(e)
            done.append(f"deleted {what} ({'inside' if inside else 'ring on'} the pad)"
                        f" and {gone} track(s) left dangling")
        elif v.GetNetCode() == p.GetNetCode():
            q = new_home(b, v, p)
            if q is None:
                left.append(f"{what}: no position within {SEARCH} mm clears the pad and every "
                            f"other net")
                continue
            pt = v.GetPosition()
            layers = sorted({t.GetLayer() for t in tracks_at(b, pt, v.GetNetCode())})
            for lay in layers:                          # reach the new site on every layer used
                jog = pcbnew.PCB_TRACK(b)
                jog.SetStart(pt); jog.SetEnd(q)
                jog.SetWidth(MM(JOG_W)); jog.SetLayer(lay); jog.SetNet(v.GetNet())
                b.Add(jog)
            v.SetPosition(q)
            done.append(f"moved {what} {mm((q - pt).EuclideanNorm()):.3f} mm to "
                        f"({mm(q.x):.3f}, {mm(q.y):.3f}), jog on "
                        + ", ".join(b.GetLayerName(l) for l in layers))
        else:
            left.append(f"{what}: a DIFFERENT net -- that is a short, DRC's to report")
    return done, left


def main():
    path = sys.argv[1]
    b = pcbnew.LoadBoard(path)
    if "--check" in sys.argv:
        t = touching(b)
        for v, p, L, inside in t:
            print(f"   VIA IN PAD: {describe(v, p)} ({'inside' if inside else 'ring on'})")
        print(f"   vias touching a pad: {len(t)}")
        return 1 if t else 0
    done, left = fix(b)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(path, b)
    for d in done:
        print(f"   {d}")
    for x in left:
        print(f"   *** LEFT: {x}")
    print(f"   via-in-pad: {len(done)} fixed, {len(left)} left")
    return 1 if left else 0


if __name__ == "__main__":
    sys.exit(main())
