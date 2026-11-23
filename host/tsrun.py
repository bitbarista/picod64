#!/usr/bin/env python3
"""The 1541 Testsuite (pc64_1, pc64_2) on the board, unattended (rebuilt 2026-11-20 from the 2026-11-13 scratch
tsfull.sh). Its own pass/fail: the tests run on the drive's CPU and the C64 pages a summary ("PASSED:").

Each image: RAM to the power-up pattern, the Ultimate resets the C64, the image in by serial M, LOAD"*",8 and RUN,
"N" to its question, then wait until the board's timed-write count has stood still for 2 looks a minute apart (no C64
memory read while it runs), then page with SPACE until the summary shows.

  python3 -I host/tsrun.py LOG [IMAGE...]        (default pc64_1.d64 pc64_2.d64)
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hatloop as H, twgate as T

DIR = "/Compat/01 1541 testsuite/"


def main():
    path = sys.argv[1]; images = sys.argv[2:] or ["pc64_1.d64", "pc64_2.d64"]
    f = open(path, "a")
    for img in images:
        t0 = time.time()
        H.ser(b"E", 0.5); T.cold(); H.api("PUT", "/machine:reset"); time.sleep(6)
        H.ser(b"z", 0.5); H.ser(b"M" + (DIR + img).encode() + b"\n", 1.0)
        T.keys('LOAD"*",8\n'); time.sleep(1.5); T.quiet(60, 3.0)
        T.keys("RUN\n"); time.sleep(10); T.keys("N")
        prev = -1; same = 0
        while same < 2 and time.time() - t0 < 3 * 3600:
            time.sleep(60); n = T.writes()
            same = same + 1 if n == prev else 0; prev = n
        s = ""
        for _ in range(12):
            s = T.screen()
            if "ASSED:" in s: break
            T.keys(" "); time.sleep(3)
        st = H.status()
        res = " | ".join(l.strip() for l in s.split("\n") if re.search("ASSED|FAIL|RESULT", l)) or "no summary"
        H.log(f, "%s: %s (%.0f min; writes worst %s us; reads %s)" % (img, res, (time.time() - t0) / 60, st.get("wr"), st.get("rd")))


if __name__ == "__main__":
    main()
