#!/usr/bin/env python3
"""The Masque at a given pin timing and release lead, unattended except for SPACE (2026-11-20, revised 2026-11-22).

Facts this rests on (runs watched by hand, 2026-11-20 to 22):
- it stops for SPACE at exactly 284,108 timed writes and reads the keyboard directly, not the KERNAL buffer (a SPACE
  put there sat unread for 20 s while it waited, 2026-11-22): pressed by hand;
- at pins +0.0 it crashed 3/3 at a screen transition after 322,164 writes (a long quiet stretch there is normal);
- its last part runs on the drive's CPU: drive PC in RAM $04AB-$07EC, ~2,940 timed writes/s steady, half-track 16
  (25 sysclk, 2026-11-22, the rotating vector on screen). A run passes only on reaching that steady state.
Only the board's serial is read (write count, drive PC): no C64 memory.

  python3 -I host/masque.py LOG SPEC [RUNS]     SPEC: e.g. s25r35 = pins 25 sysclk, releases 35 sysclk early
                                                (the fine-step build: '+' 5 sysclk from 0, '}' 7 from 21); several
                                                SPECs comma-separated alternate run by run
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hatloop as H, twgate as T

DISK = "/Compat/09 the masque/the-masque.d64"
SPACE_AT, SPACE_WAIT = 284108, 2 * 3600       # writes at the SPACE stop; how long to wait for the press by hand (s)
STALL = 360                                   # s with no writes elsewhere = crashed
LAST_RATE, LAST_FOR = 2000, 30                # the last part: writes/s at least this, for this long, drive PC in RAM


def keys_for(spec):
    m = re.fullmatch(r"s(\d+)(?:r(\d+))?", spec)
    if not m: raise SystemExit("SPEC like s25r35")
    k = b"+" * (int(m.group(1)) // 5)
    if m.group(2): k += b"}" * ((int(m.group(2)) - 21) // 7)
    return k


def main():
    path, specs = sys.argv[1], sys.argv[2].split(","); runs = int(sys.argv[3]) if len(sys.argv) > 3 else len(specs)
    f = open(path, "a")
    for n in range(1, runs + 1):
        spec = specs[(n - 1) % len(specs)]
        H.ser(b"E", 0.5); T.cold(); H.api("PUT", "/machine:reset"); time.sleep(6)
        H.ser(b"z", 0.5)
        r = H.ser(keys_for(spec), 1.0)
        p = re.findall(r"pins (-?\d+) sysclk after", r); rl = re.findall(r"releases (-?\d+) sysclk before", r)
        got = "pins %s, releases %s early" % (p[-1] if p else "0", rl[-1] if rl else "21")
        H.ser(b"M" + DISK.encode() + b"\n", 1.0)
        T.keys('LOAD"*",8,1\n'); time.sleep(2); T.quiet(200, 3.0)
        T.keys("RUN\n"); t0 = time.time()
        last, since, res, t_space, fast_since = -1, time.time(), None, None, None
        while res is None:
            time.sleep(5); w = T.writes(); now = time.time()
            rate = (w - last) / 5 if last >= 0 else 0
            if w != last: last, since = w, now
            if w == SPACE_AT:                             # waiting for SPACE by hand
                t_space = t_space or now
                if now - t_space > SPACE_WAIT: res = "NO SPACE (waited %d min)" % (SPACE_WAIT // 60)
                since = now
                continue
            if w > SPACE_AT and rate >= LAST_RATE:
                pc = H.status().get("pc", 0)
                if 0x0400 <= pc < 0x0800:
                    fast_since = fast_since or now
                    if now - fast_since >= LAST_FOR: res = "END (last part reached)"
                else:
                    fast_since = None
            else:
                fast_since = None
            if res is None and now - since > STALL: res = "CRASH (no writes for %d s)" % STALL
            if res is None and now - t0 > 3 * 3600: res = "TIMEOUT"
        s = H.status()
        H.log(f, "run %d [%s; board: %s]: %s at %d writes, %.0f s after RUN; drive PC $%04X; writes worst %s us; reads %s" % (
            n, spec, got, res, last, time.time() - t0, s.get("pc", 0), s.get("wr"), s.get("rd")))


if __name__ == "__main__":
    main()
