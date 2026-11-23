#!/usr/bin/env python3
"""Transwarp Menu entry 1 on the board, unattended (rebuilt 2026-11-20 from the 2026-11-14 scratch gate2.py).

Each run: the C64's RAM set to a power-up pattern (a soft reset keeps RAM, and leftovers broke later runs, 2026-11-14),
the Ultimate resets the C64 (the board restarts with it), the Menu disk in by serial M, LOAD"*",8,1, then key 1. Each
stage waits for the bus to be quiet QUIET s (the board's timed-write count, serial w): no C64 memory is read during a
load. Pass = "WE FLY HIGH" on the screen. PicoD64 alone on the bus (a looped-through device broke Transwarp even on the
Ultimate's 1541, 2026-11-14).

  python3 -I host/twgate.py LOG [RUNS]        (HAT_SPLIT=1: even runs pins 0.3 us later, serial >>>)
"""
import os, re, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hatloop as H

DISK = "/Compat/02 transwarp/Transwarp-Menu.d64"
QUIET = 1.2
WAIT = float(os.environ.get("TW_WAIT", "180"))     # s after key 1 for "WE FLY HIGH"


def writes():
    m = re.search(r"w (\d+)", H.ser(b"w", 0.4)); return int(m.group(1)) if m else -1


def quiet(maxwait, q=QUIET):
    t0 = time.time(); last = writes(); since = time.time()
    while time.time() - t0 < maxwait:
        time.sleep(0.3); n = writes()
        if n != last: last = n; since = time.time()
        elif time.time() - since >= q: return True
    return False


def keys(t):
    c = bytes(13 if x == "\n" else ord(x) for x in t)
    H.api("PUT", "/machine:writemem?address=0277&data=" + c.hex()); H.api("PUT", "/machine:writemem?address=00C6&data=%02x" % len(c))


def cold():
    pat = bytes((0 if (a // 64) % 2 == 0 else 0xFF) for a in range(0x10000))
    for lo, hi in ((0x0002, 0xD000), (0xE000, 0x10000)):
        H.api("POST", "/machine:writemem?address=%04x" % lo, pat[lo:hi])


def screen():
    d = H.api("GET", "/machine:readmem?address=0400&length=1000")
    ch = lambda c: chr(64 + (c & 127)) if 1 <= (c & 127) <= 26 else chr(c & 127) if 32 <= (c & 127) <= 63 else "."
    return "\n".join("".join(ch(b) for b in d[r * 40:r * 40 + 40]) for r in range(25))


def main():
    path = sys.argv[1]; runs = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    f = open(path, "a"); ok = {}; tot = {}
    for i in range(1, runs + 1):
        t0 = time.time()
        H.ser(b"E", 0.5); cold(); H.api("PUT", "/machine:reset"); time.sleep(6)
        arm = "+0.0"
        if H.SPLIT and i % 2 == 0:
            r = H.ser(b">>>", 1.0); arm = "+0.3" if "pins 45 sysclk after" in r else "?"
        H.ser(b"z", 0.5)
        H.ser(b"M" + DISK.encode() + b"\n", 1.0)
        keys('LOAD"*",8'); time.sleep(0.3); keys(',1\n'); time.sleep(1.5)
        quiet(40)
        keys("1"); time.sleep(1.0)
        t1 = time.time(); s = ""                        # (2026-11-20: one look after the first quiet came before the demo
        while time.time() - t1 < WAIT:                  #  showed anything: look again each time the bus is quiet, up to WAIT s)
            quiet(60, 5.0); s = screen()                # (5 s quiet before each look: no memory read as a load starts)
            if "WE FLY HIGH" in s: break
            time.sleep(10)
        res = "PASS" if "WE FLY HIGH" in s else "FAIL " + " ".join(l.strip() for l in s.split("\n") if "ERROR" in l)
        ok[arm] = ok.get(arm, 0) + (res == "PASS"); tot[arm] = tot.get(arm, 0) + 1
        st = H.status()
        H.log(f, "run %d [pins %s]: %s (%.0f s; writes worst %s us; reads %s)  [%s]" % (
            i, arm, res, time.time() - t0, st.get("wr"), st.get("rd"), ", ".join("%s %d/%d" % (a, ok[a], tot[a]) for a in sorted(tot))))


if __name__ == "__main__":
    main()
