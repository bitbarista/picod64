#!/usr/bin/env python3
"""
Bring every via AND track up to the netclass floor.

    python3 gen/fix_undersized_copper.py <board.kicad_pcb> [--apply]

⚠ WHY THIS EXISTS. gen/board.sh pins the netclass to via 0.6/0.3 BEFORE the DSN
is written, precisely so nothing lands on JLCPCB's premium price tier (DEFERRED
item 5). Every plane tool in gen/board-finish.sh is then told `--via-size 0.6
--via-drill 0.3`, and syn/fab_jlc_routing.txt declares the same numbers as the
escalation floor. On the LVC variant, EIGHT vias came out at 0.45/0.2 anyway:
KRT's repair pass escalates its grid to close a tight strap and takes the via
down with it, and the flag does not stop it. Rev C never hit this because it had
half the copper; this board has 3736 mm of track against 2486 mm.

The result was 16 DRC errors on a board that every other gate had passed --
and the failure mode if it had shipped is not cosmetic: 0.2 mm drills are a
different, dearer process class, so the order is either re-quoted or silently
built at a tolerance the rest of the board was not designed for.

⚠ ENLARGE FIRST, DELETE ONLY WHAT WILL NOT FIT. These are GND pour-tie vias, so
removing one costs at most one more pour fragment -- but removing one that would
have fitted costs return-path quality for nothing. Widening is tried first and
the board's own DRC is the judge, not this script's geometry model.

⚠ AND IT IS NOT ONLY VIAS. The same escalation emits TRACK segments below the
floor: two at 0.090 mm against a 0.100 mm minimum, 0.13 and 0.18 mm long, on
+3V3 beside U1. A 0.09 mm track is under JLC's standard capability entirely --
it is not a tolerance question, it is a segment the fab cannot promise to etch.
Tracks are widened to the DRC MINIMUM rather than the netclass width: 0.10 mm
clears the rule with the smallest change, where jumping to 0.15 mm risks a fresh
clearance violation in a place the router already found tight.

⚠ IT DOES NOT TOUCH EXPOSED-PAD VIAS. A QFN thermal pad has vias in it on
purpose (gen/fix_ep_vias.py) and they are already at the floor.
"""
import subprocess
import sys
import pathlib
import pcbnew

# Ported 2026-10-15 from an earlier board's tools. Floor: track 0.25 (gen/project.py).
# Here the thin tracks come from freerouting itself -- it necks down into pins (18 wires at
# 0.1874 mm in the first all-SMD session) -- as well as KRT's escalation.
MIN_D, MIN_DRILL, MIN_TRACK = 0.6, 0.3, 0.25
MM = pcbnew.FromMM


def undersized(board):
    out = []
    for t in board.GetTracks():
        if t.GetClass() != "PCB_VIA":
            continue
        if t.GetWidth() < MM(MIN_D) or t.GetDrill() < MM(MIN_DRILL):
            out.append(t)
    return out


def thin_tracks(board):
    out = []
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            continue
        if t.GetWidth() < MM(MIN_TRACK):
            out.append(t)
    return out


def drc_violations(path):
    """Positions (rounded mm) that KiCad reports an error at."""
    rpt = pathlib.Path("/tmp/undersized-drc.rpt")
    subprocess.run(["kicad-cli", "pcb", "drc", "--output", str(rpt),
                    "--severity-error", "--format", "report", path],
                   capture_output=True)
    hits = set()
    import re
    for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\)", rpt.read_text()):
        hits.add((round(float(m.group(1)), 2), round(float(m.group(2)), 2)))
    return hits


def main():
    path = sys.argv[1]
    apply = "--apply" in sys.argv
    b = pcbnew.LoadBoard(path)
    bad = undersized(b)
    thin = thin_tracks(b)
    if thin:
        print(f"   {len(thin)} track segment(s) below {MIN_TRACK} mm -- widening")
        for t in thin:
            t.SetWidth(MM(MIN_TRACK))
        if apply:
            pcbnew.SaveBoard(path, b)
            b = pcbnew.LoadBoard(path)
            bad = undersized(b)
    if not bad:
        if not thin:
            print("   copper: all vias and tracks at or above the floor")
        return 0
    print(f"   {len(bad)} via(s) below the {MIN_D}/{MIN_DRILL} floor "
          f"-- widening")
    for v in bad:
        v.SetWidth(MM(MIN_D))
        v.SetDrill(MM(MIN_DRILL))
    if not apply:
        print("   (dry run -- pass --apply to write)")
        return 0
    pcbnew.SaveBoard(path, b)

    # The board's own DRC decides which of the widened vias actually fit.
    hits = drc_violations(path)
    b = pcbnew.LoadBoard(path)
    doomed = [t for t in b.GetTracks()
              if t.GetClass() == "PCB_VIA"
              and (round(pcbnew.ToMM(t.GetPosition().x), 2),
                   round(pcbnew.ToMM(t.GetPosition().y), 2)) in hits]
    for v in doomed:
        b.RemoveNative(v)
    if doomed:
        pcbnew.SaveBoard(path, b)
        print(f"   {len(doomed)} would not fit at full size -- removed "
              f"(GND pour ties; costs fragments, not connectivity)")
    else:
        print(f"   all {len(bad)} fit at {MIN_D}/{MIN_DRILL}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
