#!/usr/bin/env python3
"""
Keep VIAS off every designator's spot before routing -- so silk.py finds the same spots after it.

    python3 hardware/gen/label_keepouts.py BOARD.kicad_pcb        (board.sh, before the DSN)

Why (Rev B, 2026-10-15): three builds in a row lost a label to router vias -- U1 and C1, then
SW1, then U3 -- each time a different one, so pinning them one by one was the wrong fix. The
spots are decided by silk.py from the PLACEMENT alone (tracks are not obstacles, vias are), so:
run silk.py on a copy of the unrouted board, read back where each designator went, and add a
vias-only rule area over each one (text box + silk.py's 0.25 mm clearance). Tracks and the pour
may still cross it. After routing silk.py meets the same obstacles, so it picks the same spots.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import pcbnew

CLEAR = 0.25          # = silk.py CLEAR
path = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
with tempfile.TemporaryDirectory() as td:
    tmp = os.path.join(td, "probe.kicad_pcb")
    shutil.copy(path, tmp)
    r = subprocess.run([sys.executable, os.path.join(here, "silk.py"), tmp], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout.strip())
        sys.exit("   *** silk.py cannot place every label on the UNROUTED board -- a floorplan problem")
    probe = pcbnew.LoadBoard(tmp)
    boxes = []
    for f in probe.GetFootprints():
        t = f.Reference()
        if t.IsVisible() and t.GetLayer() == pcbnew.F_SilkS:
            bb = t.GetBoundingBox()
            boxes.append((f.GetReference(), bb))
b = pcbnew.LoadBoard(path)
for ref, bb in boxes:
    z = pcbnew.ZONE(b)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowVias(True); z.SetDoNotAllowTracks(False); z.SetDoNotAllowPads(False)
    z.SetDoNotAllowCopperPour(False); z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu); z.SetLayerSet(ls)
    z.SetZoneName(f"label {ref}: vias out")
    g = pcbnew.FromMM(CLEAR)
    o = z.Outline(); o.NewOutline()
    for x, y in ((bb.GetLeft() - g, bb.GetTop() - g), (bb.GetRight() + g, bb.GetTop() - g),
                 (bb.GetRight() + g, bb.GetBottom() + g), (bb.GetLeft() - g, bb.GetBottom() + g)):
        o.Append(int(x), int(y))
    b.Add(z)
pcbnew.SaveBoard(path, b)
print(f"   label spots kept clear of vias: {len(boxes)} ({', '.join(r for r, _ in boxes)})")
