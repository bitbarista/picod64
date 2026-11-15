#!/usr/bin/env python3
"""A disk filled to 0 blocks free with self-checking programs (fill.asm), for the fast loader and the drive:
    python3 host/filltest/make.py OUTDIR
writes OUTDIR/FILLTEST.D64 (FILL 1-4: 154 blocks each, $0801-$9FFF; FILL 5: the last 48 blocks) and the same five as
PRGs in OUTDIR/FILLTEST/. RUN one: "FILL TEST FILE n: OK, $count" or "BAD AT $address". Needs acme and c1541."""
import os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1]; os.makedirs(out + "/FILLTEST", exist_ok=True)


def lfsr(seed, n):
    s, o = seed, bytearray()
    for _ in range(n):
        c = s & 1; s >>= 1
        if c: s ^= 0xB400
        o.append(s & 0xFF)
    return bytes(o)


def build(fileno, seed, filelen, tmp):
    """A PRG of exactly filelen bytes (load address included)."""
    for attempt in (0, 1):
        n = 1 if attempt == 0 else filelen - code_len
        open(tmp + "/fill.dat", "wb").write(lfsr(seed, n))
        subprocess.run(["acme", "-f", "cbm", f"-DSEED={seed}", f"-DFILENO={fileno}", "-o", tmp + "/f.prg", HERE + "/fill.asm"],
                       cwd=tmp, check=True)
        prg = open(tmp + "/f.prg", "rb").read()
        if attempt == 0: code_len = len(prg) - 1
    assert len(prg) == filelen, (len(prg), filelen)
    return prg


with tempfile.TemporaryDirectory() as tmp:
    d64 = out + "/FILLTEST.D64"
    subprocess.run(["c1541", "-format", "picod64 fill,pf", "d64", d64], check=True, capture_output=True)
    sizes = [0x9FFF - 0x0801 + 1 + 2] * 4 + [48 * 254]       # 154 blocks x 4, then 48: 664
    for i, size in enumerate(sizes, 1):
        prg = build(i, (0xACE1 + i * 0x1111) & 0xFFFF or 1, size, tmp)
        p = f"{out}/FILLTEST/fill {i}.prg"
        open(p, "wb").write(prg)
        subprocess.run(["c1541", d64, "-write", p, f"fill {i}"], check=True, capture_output=True)
        print(f"fill {i}: {len(prg)} bytes, {-(-len(prg) // 254)} blocks, ${0x0801:04X}-${0x0801 + len(prg) - 3:04X}")
    ls = subprocess.run(["c1541", "-attach", d64, "-list"], capture_output=True, text=True).stdout
    print(next(l for l in ls.splitlines() if "blocks free" in l.lower()))
