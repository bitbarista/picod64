#!/usr/bin/env python3
"""
Put every footprint graphic back on the layer the floorplan gave it.
    python3 hardware/gen/restore_fp_layers.py BOARD.kicad_pcb FLOORPLAN.kicad_pcb [--check]

WHY (rev 1, 2026-10-29): KRT's route_planes.py rewrites the board and moved JP3's copper
bridge -- the fp_poly on F.Cu that makes the solder jumper CLOSED as made -- to F.SilkS. DRC
saw only "silk clipped by mask"; the board would have shipped with the jumper OPEN. Rev 0
had no copper in a footprint's graphics, so it never showed. The uuids survive KRT, so the
floorplan (saved before any router runs) is the reference, graphic by graphic.
--check: change nothing, exit 1 if any graphic differs.
"""
import sys

import pcbnew

board, ref = pcbnew.LoadBoard(sys.argv[1]), pcbnew.LoadBoard(sys.argv[2])
check = "--check" in sys.argv
want = {}
for f in ref.GetFootprints():
    for g in f.GraphicalItems():
        want[g.m_Uuid.AsString()] = (f.GetReference(), g.GetLayer())
moved, seen = [], 0
for f in board.GetFootprints():
    for g in f.GraphicalItems():
        w = want.get(g.m_Uuid.AsString())
        if w is None:
            continue
        seen += 1
        if g.GetLayer() != w[1]:
            moved.append(f"{w[0]}: {board.GetLayerName(g.GetLayer())} -> {board.GetLayerName(w[1])}")
            if not check:
                g.SetLayer(w[1])
if not check and moved:
    pcbnew.SaveBoard(sys.argv[1], board)
print(f"   footprint graphics checked: {seen}; {'wrong layer' if check else 'restored'}: {len(moved)}"
      + "".join(f"\n      {m}" for m in moved))
if seen != len(want):
    print(f"   *** {len(want) - seen} floorplan graphics not found on the board")
    sys.exit(1)
sys.exit(1 if (check and moved) else 0)
