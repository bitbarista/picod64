#!/usr/bin/env python3
"""G1 (hardware/IEC-TIMING-DECISION.md): serial O, the line timing, N times in a row, unattended (2026-11-22).
Each run: no disk in (serial E), the Ultimate runs fw/linetime/linetime.prg (no card needed), serial O. One line per
run with each edge's d50 and the release lags, then the spread.
  python3 -I host/ltrepeat.py LOG [N]
"""
import os, re, sys, time, statistics
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hatloop as H

PRG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fw", "linetime", "linetime.prg")


def main():
    path = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    f = open(path, "a"); prg = open(PRG, "rb").read(); rows = []
    for i in range(1, n + 1):
        H.ser(b"E", 0.5)
        H.api("POST", "/runners:run_prg", prg); time.sleep(4)
        out = H.ser(b"O", 60.0)
        d = {(m[0], m[1].strip()): int(m[2]) for m in re.findall(r"(CLK|DATA) (pull   |release): d50 (-?\d+) ns", out)}
        lag = {l: int(v) for l, v in re.findall(r"(CLK|DATA): a release reaches the C64 (-?\d+) ns", out)}
        bad = "not trusted" in out or "no C64 program" in out or "stopped" in out
        if len(d) == 4 and len(lag) == 2:
            rows.append((lag["CLK"], lag["DATA"]))
            H.log(f, "run %d: CLK pull %d release %d lag %d | DATA pull %d release %d lag %d%s" % (
                i, d[("CLK", "pull")], d[("CLK", "release")], lag["CLK"], d[("DATA", "pull")], d[("DATA", "release")], lag["DATA"],
                "  (a curve not trusted)" if bad else ""))
        else:
            H.log(f, "run %d: no result: %s" % (i, " / ".join(l.strip() for l in out.splitlines() if l.strip())[-200:]))
        time.sleep(2)
    if rows:
        for k, nm in ((0, "CLK"), (1, "DATA")):
            v = [r[k] for r in rows]
            H.log(f, "%s lag over %d runs: min %d, max %d, mean %.1f, sd %.1f ns" % (nm, len(v), min(v), max(v), statistics.mean(v),
                                                                                    statistics.pstdev(v)))


if __name__ == "__main__":
    main()
