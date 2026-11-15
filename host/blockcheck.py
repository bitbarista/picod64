#!/usr/bin/env python3
"""Were the blocks a loader sent read right? (2026-11-13, Reanim8ed)
    python3 host/blockcheck.py LOG IMAGE.d64...
LOG: the board's serial 'v' output after 'U' (the block log of decoded sectors: "B <half-track> <hash>", FNV-1a over
bytes 2-255 of the sector at the drive's $0702 (Reanim8ed's loader decodes in place: 0-1 are leftovers) while it ran the loader's send code, serial W). Each hash should be one of the
images' sectors; one that is none of them is a block the drive read wrong (or a buffer the loader built itself)."""
import sys


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


SPT = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def sectors(img):
    d = open(img, "rb").read()
    o = 0
    for t, n in enumerate(SPT, 1):
        for s in range(n):
            yield t, s, d[o:o + 256]
            o += 256


known = {}
for img in sys.argv[2:]:
    for t, s, b in sectors(img):
        known.setdefault(fnv(b[2:]), (img.split("/")[-1], t, s))
seen = bad = 0
for line in open(sys.argv[1], errors="replace"):
    f = line.split()
    if len(f) == 3 and f[0] == "B":
        seen += 1
        h = int(f[2], 16)
        if h in known:
            continue
        bad += 1
        print("NOT A SECTOR: half-track %s (track %g) hash %08X" % (f[1], int(f[1]) / 2 + 1, h))
print("%d blocks, %d not on the disk" % (seen, bad))
