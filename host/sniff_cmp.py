#!/usr/bin/env python3
"""Compare two bus-sniffer recordings of the same loader (host/hw_sniff.py; fw/iec_sniff.pio):

    python3 host/sniff_cmp.py REF.txt TEST.txt

A recording is chunks of ATN edges (a chunk ends at a gap of 2 ms). Per edge: the lines just before the edge and ~13 us
after (ATN, CLK, DATA; 1 = released). A slot's data is CLK and DATA as held just before the next edge (a drive answers
once per edge, 6-13 us after it; the chunk's last slot: 13 us after). "after 13 us": slots whose answer came after the
13 us sample; a real 1541 does that for about a third of them (2026-11-07: it answers right on the C64's read).
  - every TEST chunk is looked up in REF; a chunk REF never sent is shown against REF's closest of the same length,
    slot by slot, as bytes (4 slots a byte, from the chunk's start: the alignment is a guess, the slots are not).
Sectors come in rotation order, so chunks are compared as a set, not in order."""
import collections, sys


def chunks(path):
    out = []
    for l in open(path):
        if not l.startswith("S") or l.startswith("SOVF"):
            continue
        p = l.rstrip("\n").split(" ", 2)
        if len(p) < 3 or not p[2]:
            continue
        ms, gap, s = int(p[0][1:]), int(p[1]), p[2]
        if gap == 0 and out:
            out[-1][2] += s
        else:
            out.append([ms, gap, s])
    return out


def slots(s):
    """Per edge: (data held at the next edge, data 13 us after): CLK, DATA as 2 bits."""
    v = [ord(c) - 48 for c in s]
    post = [(x >> 1) & 3 for x in v]
    pre = [(x >> 4) & 3 for x in v]
    return [pre[i + 1] if i + 1 < len(v) else post[i] for i in range(len(v))], post


def as_bytes(d):
    return bytes((d[i] << 6 | d[i + 1] << 4 | d[i + 2] << 2 | d[i + 3]) for i in range(0, len(d) - 3, 4))


def summary(name, cs):
    late = sum(1 for _, _, s in cs for a, b in zip(*slots(s)) if a != b)
    n = sum(len(s) for _, _, s in cs)
    # a "before" sample showing ATN already at its new level (as 13 us after) wasn't before the edge
    bad = sum(1 for _, _, s in cs for c in s if ((ord(c) - 48) >> 3) & 1 == (ord(c) - 48) & 1)
    print(f"{name}: {len(cs)} chunks, {n} edges ({bad} 'before' samples not before), {late} slots answered after 13 us;"
          f" sizes {collections.Counter(len(s) for _, _, s in cs).most_common(6)}")


def main():
    ref, test = chunks(sys.argv[1]), chunks(sys.argv[2])
    summary("REF ", ref); summary("TEST", test)
    rset = collections.Counter(tuple(slots(s)[0]) for _, _, s in ref)
    bylen = collections.defaultdict(list)
    for _, _, s in ref:
        bylen[len(s)].append(slots(s)[0])
    miss = 0; t0 = test[0][0] if test else 0
    for i, (ms, gap, s) in enumerate(test):
        d = slots(s)[0]
        if tuple(d) in rset:
            continue
        miss += 1
        cand = bylen.get(len(d), [])
        if not cand:
            print(f"#{i} at {(ms - t0) / 1000:.1f} s: {len(d)} edges, no REF chunk of that length")
            continue
        best = min(cand, key=lambda r: sum(a != b for a, b in zip(r, d)))
        diff = [k for k in range(len(d)) if best[k] != d[k]]
        print(f"#{i} at {(ms - t0) / 1000:.1f} s: {len(d)} edges, {len(diff)} slots differ from REF's closest, at slots"
              f" {diff[:16]}{' ...' if len(diff) > 16 else ''}")
        if len(diff) <= 16:
            rb, tb = as_bytes(best), as_bytes(d)
            for k in sorted({x // 4 for x in diff}):
                print(f"     byte {k}: REF {rb[k]:02X} TEST {tb[k]:02X}   (context REF {rb[max(0, k - 4):k + 5].hex()} TEST {tb[max(0, k - 4):k + 5].hex()})")
    print(f"{miss} of {len(test)} TEST chunks are not in REF")


main()
