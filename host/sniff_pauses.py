#!/usr/bin/env python3
"""Whole sector transfers, compared (host/hw_sniff.py recordings): python3 host/sniff_pauses.py REF.txt TEST.txt
In both recordings, pieces that add up to a sector (1044 edges) are joined (the C64 itself pauses mid-sector in some
parts). A TEST sector whose payload (slots 8-1042) REF never sent is listed with its pauses. A pause of ~200 ms (one
revolution) inside a sector is the drive re-reading it."""
import sys
exec(open(__file__.replace("sniff_pauses.py", "sniff_cmp.py")).read().split("\ndef main")[0])


def sectors(cs):
    out = []; i = 0
    while i < len(cs):
        ms, gap, s = cs[i]; j = i; tot = len(s); parts = [(gap, len(s))]
        while tot < 1044 and j + 1 < len(cs) and tot + len(cs[j + 1][2]) <= 1044 and len(s) > 2:
            j += 1; tot += len(cs[j][2]); parts.append((cs[j][1], len(cs[j][2])))
        if tot == 1044:
            out.append((ms, parts, "".join(cs[k][2] for k in range(i, j + 1)))); i = j + 1
        else:
            i += 1
    return out


ref = sectors(chunks(sys.argv[1])); test = sectors(chunks(sys.argv[2]))
R = {tuple(slots(s)[0][8:1043]) for _, _, s in ref}
t0 = test[0][0] if test else 0; bad = 0
for ms, parts, s in test:
    if tuple(slots(s)[0][8:1043]) not in R:
        bad += 1
        print(f"{(ms - t0) / 1000:6.1f} s: payload REF never sent; pieces {[(round(g * 0.4 / 1000, 1), n) for g, n in parts][:6]} (pause ms, edges)")
print(f"REF {len(ref)} sectors, TEST {len(test)}; {bad} TEST sectors with a payload REF never sent")
