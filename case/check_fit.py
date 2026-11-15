#!/usr/bin/env python3
"""
Assembled fit check (AUDIT D11): does the populated board fit the case?

    python3 case/check_fit.py            (needs KiCad 9's kicad-cli + pcbnew, OpenSCAD; ~10 min)
    python3 case/check_fit.py --keep D   (keep the per-part exports in D and reuse them next time)
    python3 case/check_fit.py --prove    (plants a 30 mm pillar under the front roof: the check must catch it)

Every footprint with a 3D model (optional/DNP parts included: they must fit too) becomes a box measured
from its own KiCad model: one box over the board and, if the model reaches past the board's edge, a second
box for just that part (so the Pico's USB connector is checked where it is, and the Pico is not one box
through the rear wall: the false alarm in the first run of D11). Datasheet proxies replace models that are
missing or wrong:
  J1/J2 IEC DIN (board_dims), J3 microSD + a latched card, SW1 EC11E (body, collar, shaft to 24.5),
  C1 at its real 11 mm (KiCad's model: 5), D1/D2 on their LED standoffs with the lens up through the roof.
Plus the OLED as fitted (picod64-case.scad's oled_fitted: module, glass, pillars, J4), every plated lead
trimmed to 2 mm under the board (the OLED pillars' snap barbs are in oled_fitted). All of it is intersected with the tray and
each lid part as assembled; any overlap is listed with the parts it touches. Exit 1 on any overlap.
"""
import os, re, subprocess, sys, tempfile

CASE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.join(os.path.dirname(CASE), "hardware")
PCB = os.path.join(HW, "picod64.kicad_pcb")
OVR_H = {"C1": 11.0}               # [D] 5 x 11 mm electrolytic; the KiCad model is 5 mm tall
OWN_PROXY = {"SW1", "D1", "D2", "J1", "J2", "J3"}   # modelled from datasheets below, not from KiCad's model
LEAD = 2.0                          # trimmed leads under the board (BUILD-NOTES)


def stl_verts(p):
    return set((round(float(a), 3), round(float(b), 3), round(float(c), 3))
               for a, b, c in re.findall(r"vertex\s+(\S+)\s+(\S+)\s+(\S+)", open(p).read()))


def export(args, out):
    subprocess.run(["kicad-cli", "pcb", "export", "stl", "--user-origin", "100x100mm", "--subst-models", "-f",
                    "-o", out, *args, PCB], capture_output=True, check=True)


def dims():
    s = open(os.path.join(CASE, "board_dims.scad")).read()
    return {k: eval(v) for k, v in re.findall(r"^(\w+) = (.+);", s, re.M)}


def main():
    keep = sys.argv[sys.argv.index("--keep") + 1] if "--keep" in sys.argv else None
    W = keep or tempfile.mkdtemp()
    os.makedirs(W, exist_ok=True)
    import pcbnew
    D = dims()
    BW, BL = D["BOARD_W"], D["BOARD_L"]

    # ---- boxes from each part's own model (board coords: x right, y to the front; z above the PCB top)
    bo = os.path.join(W, "board_only.stl")
    if not os.path.exists(bo):
        export(["--board-only"], bo)
    B = stl_verts(bo)
    TOP = max(v[2] for v in B)
    board = pcbnew.LoadBoard(PCB)
    boxes, leads = [], []
    for f in board.GetFootprints():
        r = f.GetReference()
        for p in f.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                leads.append((pcbnew.ToMM(p.GetPosition().x) - 100, pcbnew.ToMM(p.GetPosition().y) - 100))
        if r.startswith("H") or r in OWN_PROXY or not list(f.Models()):
            continue
        out = os.path.join(W, f"c_{r}.stl")
        if not os.path.exists(out):
            export(["--component-filter", r], out)
        V = [(x, -y, z - TOP) for x, y, z in stl_verts(out) - B]
        if not V:
            print(f"  {r}: model has no geometry, NOT CHECKED")
            continue
        on = [v for v in V if -0.01 <= v[0] <= BW + 0.01 and -0.01 <= v[1] <= BL + 0.01]
        off = [v for v in V if v not in set(on)]
        for tag, vs in (("", on), (" (past the board edge)", off)):
            if not vs:
                continue
            h = OVR_H.get(r, max(v[2] for v in vs))
            boxes.append((r + tag, min(v[0] for v in vs), max(v[0] for v in vs),
                          min(v[1] for v in vs), max(v[1] for v in vs), max(0.0, min(v[2] for v in vs)), h))
    if "--prove" in sys.argv:          # the check can fail: a part that must hit the front roof
        boxes.append(("PROVE pillar", 40.0, 42.0, 60.0, 62.0, 0.0, 30.0))
    print(f"{len(boxes)} model boxes, {len(leads)} plated leads (models cached in {W})")

    # ---- the scad: the case file itself (part "none"), so the check always uses the case as it is now
    L = ['include <%s/picod64-case.scad>' % CASE, 'part = "none";', 'which = "tray";',
         'module pbox(x0, x1, y0, y1, z0, z1) translate([cx(x0), cy(y1), z0]) cube([x1 - x0, y1 - y0, z1 - z0]);',
         'BOXES = [' + ",\n  ".join('["%s", %.3f, %.3f, %.3f, %.3f, %.3f, %.3f]' % b for b in boxes) + '];',
         'LEADS = [' + ", ".join("[%.3f, %.3f]" % l for l in leads) + '];',
         'LEAD = %s;' % LEAD, r'''
module parts() {
    for (b = BOXES) pbox(b[1], b[2], b[3], b[4], PCB_TOP + b[5], PCB_TOP + b[6]);
    // J1/J2 IEC DIN sockets [D: 21.4 wide x 21.4 high, 18.9 deep], face at IEC_FACE_Y
    for (x = IEC_X) pbox(x - IEC_BODY_W / 2, x + IEC_BODY_W / 2, IEC_FACE_Y, IEC_BODY_BACK_Y, PCB_TOP, PCB_TOP + IEC_BODY_H);
    // J3 microSD [D: GCT MEM2075 15.05 x 14.5 x 1.40] and a latched card (11 wide, 2.5 proud, 1 thick)
    pbox(SD_X - 7.525, SD_X + 7.525, BOARD_L - 14.5, BOARD_L, PCB_TOP, PCB_TOP + 1.40);
    pbox(SD_CARD_X - 5.5, SD_CARD_X + 5.5, BOARD_L - 12.0, BOARD_L + 2.5, PCB_TOP + 0.2, PCB_TOP + 1.2);
    // SW1 Alps EC11E [D: body 11.7 wide to 4.5; collar to 11.5 (Ø7 [A]); shaft Ø6 to SHAFT_TIP]
    pbox(SHAFT_X - 6.0, SHAFT_X + 6.0, SHAFT_Y - 7.5, SHAFT_Y + 7.0, PCB_TOP, PCB_TOP + 4.5);
    translate([cx(SHAFT_X), cy(SHAFT_Y), PCB_TOP]) { cylinder(d = 7.0, h = 11.5, $fn = 32); cylinder(d = 6.0, h = SHAFT_TIP, $fn = 32); }
    // D1/D2 as fitted: on the LED standoff, flange (Ø3.8 x 1 [A]) on top, lens Ø3 up through the roof
    for (p = [LED_PWR, LED_ACT]) translate([cx(p[0]), cy(p[1]), PCB_TOP]) {
        led_standoff();
        translate([0, 0, LED_STANDOFF]) { cylinder(d = 3.8, h = 1.0, $fn = 32); cylinder(d = 3.0, h = LED_LENS, $fn = 32); }
    }
    oled_fitted();
    // under the board: every plated lead trimmed to LEAD
    for (l = LEADS) translate([cx(l[0]), cy(l[1]), PCB_BOT - LEAD]) cylinder(d = 1.0, h = LEAD, $fn = 8);
}
// the LED standoffs against the board's parts (not D1/D2 themselves): the first ones hit R12/R13's leads
module others() for (b = BOXES) if (b[0] != "D1" && b[0] != "D2") pbox(b[1], b[2], b[3], b[4], PCB_TOP + b[5], PCB_TOP + b[6]);
if (which == "standoffs") intersection() {
    for (p = [LED_PWR, LED_ACT]) translate([cx(p[0]), cy(p[1]), PCB_TOP]) led_standoff();
    others();
}
else intersection() {
    if (which == "tray") tray(); else if (which == "lid_front") lid_front(); else lid_rear();
    parts();
}
''']
    scad = os.path.join(W, "fit.scad")
    open(scad, "w").write("\n".join(L))

    # names and board-coord footprints of everything, to say what an overlap touches
    named = [(b[0], b[1], b[2], b[3], b[4]) for b in boxes]
    named += [(f"J{i + 1} (IEC)", x - D["IEC_BODY_W"] / 2, x + D["IEC_BODY_W"] / 2, D["IEC_FACE_Y"], D["IEC_BODY_BACK_Y"])
              for i, x in enumerate(D["IEC_X"])]
    named += [("J3 + card", D["SD_X"] - 7.525, D["SD_X"] + 7.525, BL - 14.5, BL + 2.5),
              ("SW1", D["SHAFT_X"] - 6, D["SHAFT_X"] + 6, D["SHAFT_Y"] - 7.5, D["SHAFT_Y"] + 7),
              ("D1 + standoff", D["LED_PWR"][0] - 2.5, D["LED_PWR"][0] + 2.5, D["LED_PWR"][1] - 1.9, D["LED_PWR"][1] + 1.9),
              ("D2 + standoff", D["LED_ACT"][0] - 2.5, D["LED_ACT"][0] + 2.5, D["LED_ACT"][1] - 1.9, D["LED_ACT"][1] + 1.9),
              ("OLED", D["OLED_X0"], D["OLED_X0"] + D["OLED_W"], D["OLED_Y0"], D["OLED_Y0"] + D["OLED_D"])]
    named += [("lead", x - 0.5, x + 0.5, y - 0.5, y + 0.5) for x, y in leads]

    WALL, CLR = 2.4, 0.5      # only to turn the overlap's case coords back into board coords for the report
    m = re.search(r"^WALL = ([\d.]+);", open(os.path.join(CASE, "picod64-case.scad")).read(), re.M)
    if m:
        WALL = float(m.group(1))
    bad = 0
    for which in ("tray", "lid_front", "lid_rear", "standoffs"):
        out = os.path.join(W, f"fit_{which}.stl")
        if os.path.exists(out):
            os.remove(out)
        r = subprocess.run(["openscad", "-D", f'which="{which}"', "-o", out, scad], capture_output=True, text=True)
        if "Current top level object is empty" in r.stderr or not os.path.exists(out) or os.path.getsize(out) == 0:
            print(f"{which}: clear")
            continue
        if r.returncode:
            print(f"{which}: OpenSCAD failed\n{r.stderr[-800:]}")
            bad = 1
            continue
        for g in components(out):
            xs = [p[0] - WALL - CLR for p in g]; ys = [BL - (p[1] - WALL - CLR) for p in g]; zs = [p[2] for p in g]
            if max(xs) - min(xs) < 0.05 or max(ys) - min(ys) < 0.05 or max(zs) - min(zs) < 0.05:
                continue          # faces touching, no volume
            hit = sorted({n.split(" (")[0] if n != "lead" else "lead" for n, x0, x1, y0, y1 in named
                          if x0 < max(xs) and x1 > min(xs) and y0 < max(ys) and y1 > min(ys)})
            print(f"{which}: OVERLAP board x {min(xs):.1f}..{max(xs):.1f} y {min(ys):.1f}..{max(ys):.1f} case z "
                  f"{min(zs):.1f}..{max(zs):.1f} ({max(xs) - min(xs):.1f} x {max(ys) - min(ys):.1f} x "
                  f"{max(zs) - min(zs):.1f}), near: {', '.join(hit) or '?'}")
            bad = 1
        if not bad:
            print(f"{which}: clear")
    if "--prove" in sys.argv:
        print("PROVE: the planted pillar was caught" if bad else "PROVE FAILED: the planted pillar was NOT caught")
        sys.exit(0 if bad else 1)
    print("FIT: OK" if not bad else "FIT: OVERLAPS (above)")
    sys.exit(bad)


def components(path):
    """Connected pieces of an STL, as lists of vertices."""
    vs = re.findall(r"vertex\s+(\S+)\s+(\S+)\s+(\S+)", open(path).read())
    idx, par = {}, []

    def find(a):
        while par[a] != a:
            par[a] = par[par[a]]; a = par[a]
        return a
    for i in range(0, len(vs), 3):
        t = []
        for v in vs[i:i + 3]:
            k = tuple(round(float(x), 3) for x in v)
            if k not in idx:
                idx[k] = len(par); par.append(len(par))
            t.append(idx[k])
        for q in t[1:]:
            par[find(q)] = find(t[0])
    groups = {}
    for k, i in idx.items():
        groups.setdefault(find(i), []).append(k)
    return list(groups.values())


if __name__ == "__main__":
    main()
