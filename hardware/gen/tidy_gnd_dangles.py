#!/usr/bin/env python3
"""
Delete track copper that KiCad itself says goes nowhere -- and nothing else.

    python3 hardware/gen/tidy_gnd_dangles.py BOARD.kicad_pcb

The plane tools (KRT route_planes / repair_planes) and remove_vias_in_pads.py can leave GND
stubs hanging: Rev B, 2026-10-15, three B.Cu ends of 0.1-0.2 mm and a split F/B pair at Pico
pad 8, where a via in the pad had been removed. GND reaches its pads through the pours and the
locked escapes, so a GND track with a dead end carries nothing. And a SIGNAL stub with a dead
end carries nothing either: KRT's close left one on /A5_P (2026-10-15). A dangling segment is by
definition not on the path between two pads, so removing it cannot open a net -- and board.sh's
unconnected and pad-connectivity gates afterwards would say so if it did.

Method: refill, ask KiCad's DRC for track_dangling items, delete only UNLOCKED GND tracks at
those spots, and repeat until none are left (removing one dead end can expose the next).
Locked copper (the escapes) is never touched. board.sh's pad-connectivity gate afterwards
proves no pad lost its ground.
"""
import json
import re
import subprocess
import sys
import tempfile

import pcbnew

path = sys.argv[1]
removed = 0
for rnd in range(10):
    b = pcbnew.LoadBoard(path)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(path, b)
    with tempfile.NamedTemporaryFile(suffix=".json") as tf:
        subprocess.run(["kicad-cli", "pcb", "drc", "--severity-all", "--format", "json", "-o", tf.name, path],
                       capture_output=True)
        d = json.load(open(tf.name))
    # ⚠ PicoD64 2026-10-25: this used to delete ANY unlocked track passing within 0.05 mm of
    # a dangling point -- any net, either layer. A live /SD_DAT1 segment crossing a GND stub's
    # end was deleted and RN1.6 opened (pad-connectivity gate caught it). Now: the reported
    # track's own NET and LAYER, and only a track whose END is the dangling point.
    spots = []
    for v in d.get("violations", []):
        if v["type"] != "track_dangling":
            continue
        for it in v["items"]:
            m = re.match(r"Track \[([^\]]*)\] on (\S+)", it["description"])
            if m:
                spots.append((it["pos"]["x"], it["pos"]["y"], m.group(1), m.group(2).rstrip(",")))
    if not spots:
        break
    gone = 0
    for t in list(b.GetTracks()):
        if t.Type() == pcbnew.PCB_VIA_T or t.IsLocked():
            continue
        for x, y, net, layer in spots:
            if t.GetNetname().lstrip("/") != net.lstrip("/") or b.GetLayerName(t.GetLayer()) != layer:
                continue
            p = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
            if min((t.GetStart() - p).EuclideanNorm(), (t.GetEnd() - p).EuclideanNorm()) <= pcbnew.FromMM(0.05):
                b.Delete(t)
                gone += 1
                break
    if not gone:
        print(f"   *** {len(spots)} dangling end(s) KiCad reports, none of them deletable (locked?)")
        sys.exit(1)
    removed += gone
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(path, b)
print(f"   dangling tracks removed: {removed}")
