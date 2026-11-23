#!/usr/bin/env python3
"""Reanim8ed on the board, unattended, both sides (2026-11-20). Helpers from host/hatloop.py.

Each cycle: S1 in, the Ultimate resets the C64, LOAD"*",8,1 and RUN typed at READY. The block log (serial W + U: a hash
of each decoded sector at the drive's $0702 while the loader is in its send loop, $04A5-$04E7) is read every POLL s and each hash looked up in
the two images (host/blockcheck.py's hash): a hash in neither is a block read wrong. Side 1 done = all the side-1
sectors a clean run sends (S1_N) and the drive quiet IDLE1 s (or nearly all and quiet IDLE_SLOW s): S2 goes in, as the
knob would. Side 2 done = S2_N and quiet END_IDLE s: the C64's memory is read then (the loads are over, so a read can't
hang one) and saved; with REF (a dump of a run seen to end on the credits) the stable pages are compared. A cycle with
no new block for STALL s before that is a stall. Never reads C64 memory while the demo loads.

  python3 -I host/r8loop.py LOG [CYCLES] [REF.bin]        (HAT_SPLIT=1: even cycles pins 0.3 us earlier; kill by PID)
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hatloop as H

CARD = "/Compat/12 reanim8ed/Reanim8ed.%s.D64"
IMG = os.path.expanduser("~/PicoD64-compat/reanim8ed/x/Reanim8ed.%s.D64")
S1_N, S2_N = 588, 656          # distinct sectors a clean run sent, per side (2026-11-13, block log)
W1, W2 = 681591, 1240000      # without the block log: timed bus writes at side 1's "turn disk" (681,591 exactly, 2026-11-13 and -20) and by the credits (1,242,179)
BLOCKLOG = os.environ.get("BLOCKLOG") == "1"   # (2026-11-20: with it on the demo hung at 102 sectors, all correct; off it ran)
SAME_MIN = 200                 # of 240 pages like the reference (231 stay the same over 5 s on the credits)
IDLE1, IDLE_SLOW, END_IDLE, STALL = 20, 120, 60, 300
POLL = 10
SPT = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def fnv(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def known():
    k = {}
    for side in ("S1", "S2"):
        d = open(IMG % side, "rb").read(); o = 0
        for t, n in enumerate(SPT, 1):
            for s in range(n):
                k.setdefault(fnv(d[o + 2:o + 256]), (side, t, s)); o += 256
    return k


def blocks():
    """(total so far, the newest hashes the board still holds, oldest first)"""
    r = H.ser(b"v", 2.0)
    m = re.search(r"block log: (\d+) blocks", r)
    return (int(m.group(1)) if m else -1), [int(x, 16) for x in re.findall(r"^B \d+ ([0-9A-F]{8})", r, re.M)]


def dump():
    d = bytearray()
    for a in range(0, 0x10000, 0x1000):
        d += bytes(0x1000) if a == 0xD000 else H.api("GET", "/machine:readmem?address=%04X&length=4096" % a)
    return bytes(d)


def main():
    path = sys.argv[1]; cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    ref = open(sys.argv[3], "rb").read() if len(sys.argv) > 3 else None
    K = known(); f = open(path, "a"); passes = fails = 0
    for n in range(1, cycles + 1):
        H.ser(b"M" + (CARD % "S1").encode() + b"\n", 2.0); time.sleep(1)
        H.api("PUT", "/machine:reset"); time.sleep(6)
        st = H.status()
        if st.get("mounted") != CARD % "S1":
            H.log(f, "cycle %d: S1 not in after the reset (%s)" % (n, st.get("mounted"))); fails += 1; continue
        if "READY." not in H.screen():
            H.log(f, "cycle %d: no READY after the reset" % n); fails += 1; continue
        H.ser(b"z", 0.5)
        arm = "+0.0"
        if H.SPLIT and n % 2 == 0:
            r = H.ser(b">>>", 1.0); arm = "+0.3" if "pins 45 sysclk after" in r else "?"
        if BLOCKLOG:
            H.ser(b"W04A504E7", 1.0)                    # the loader's send loop (2026-11-13)
            r = H.ser(b"U", 1.0)                          # (U toggles; the reset restarted the board, so it was off)
            if ") on:" not in r:
                H.log(f, "cycle %d: block log didn't come on (%s)" % (n, r.strip())); fails += 1; continue
        H.typ('LOAD"*",8,1\r')
        if not H.wait_load(120):
            H.log(f, "cycle %d [pins %s]: LOAD didn't finish" % (n, arm)); fails += 1; continue
        H.typ("RUN\r")
        t0 = time.time(); seen_n = 0; wn = 0; got = {"S1": set(), "S2": set()}; bad = 0; side = 1; last_new = time.time()
        result = None
        while result is None:
            time.sleep(POLL)
            m = re.search(r"w (\d+)", H.ser(b"w", 1.0))
            if m and int(m.group(1)) > wn: wn = int(m.group(1)); last_new = time.time()
            tot, hs = blocks() if BLOCKLOG else (0, [])
            if tot > seen_n:
                new = hs[-(tot - seen_n):] if tot - seen_n <= len(hs) else hs
                lost = max(0, tot - seen_n - len(hs))
                for h in new:
                    if h in K: got[K[h][0]].add(K[h][1:])
                    else: bad += 1
                seen_n = tot; last_new = time.time()
                if lost: H.log(f, "cycle %d: %d blocks fell out of the board's ring between looks" % (n, lost))
            q = time.time() - last_new
            s1 = len(got["S1"]) if BLOCKLOG else wn * S1_N // W1
            s2 = len(got["S2"]) if BLOCKLOG else (S2_N if wn >= W2 else 0)
            if side == 1 and ((s1 >= S1_N and q >= IDLE1) or (s1 >= S1_N - 30 and q >= IDLE_SLOW)):
                H.ser(b"M" + (CARD % "S2").encode() + b"\n", 2.0); side = 2; last_new = time.time()
                t_turn = time.time() - t0
            elif side == 2 and s2 >= S2_N and q >= END_IDLE:
                result = "end"
            elif q >= STALL or time.time() - t0 > 45 * 60:
                result = "stall"
        st = H.status(); el = time.time() - t0
        tail = ("side %d, S1 %d/%d S2 %d/%d sectors, %d bad blocks" % (side, len(got["S1"]), S1_N, len(got["S2"]), S2_N, bad)
                if BLOCKLOG else "side %d, %d timed writes" % (side, wn)) + "; writes worst %s us; reads %s" % (st.get("wr"), st.get("rd"))
        d = dump(); dn = "%s.end%d.bin" % (path, n); open(dn, "wb").write(d)
        cmp = ""; same = None
        if ref:
            pages = [p for p in range(256) if not 0xD0 <= p <= 0xDF]
            same = sum(d[p * 256:(p + 1) * 256] == ref[p * 256:(p + 1) * 256] for p in pages)
            cmp = "; memory vs the reference: %d of %d pages the same" % (same, len(pages))
        ok = result == "end" and bad == 0 and (same is None or same >= SAME_MIN)
        passes += ok; fails += not ok
        H.log(f, "cycle %d [pins %s]: %s at %.0f s%s; %s%s  [%d ok, %d not]" % (
            n, arm, "END" if result == "end" else "STALL", el,
            " (disk turned at %.0f s)" % t_turn if side == 2 else "", tail, cmp, passes, fails))
        if BLOCKLOG: H.ser(b"U", 1.0)                     # block log off


if __name__ == "__main__":
    main()
