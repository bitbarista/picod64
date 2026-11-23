#!/usr/bin/env python3
"""The compat set on the board, one title at a time, with someone watching the C64 (2026-11-22).
  NORESET=1 ... go TITLE                   the same after a power cycle by hand (no RAM fill, no reset)
  python3 -I host/compat.py go TITLE      RAM to the power-up pattern, the Ultimate resets the C64 (the board restarts),
                                          disk 1 in by serial M, LOAD"*",8,1, then RUN at READY
  python3 -I host/compat.py disk TITLE N  disk N in (as the knob would), when the title asks for it
  python3 -I host/compat.py stat          the board's worst write and read lateness since the launch
Waits only on the board's timed-write count (serial w): no C64 memory is read during a load.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hatloop as H, twgate as T

C, D = "/Compat/", "/Demos/"
TITLES = {
    "coma":      [C + "06 coma light 13/side1.d64", C + "06 coma light 13/side2.d64"],
    "dutch":     [C + "10 dutch breeze/black mail - Dutch Breeze-1.d64", C + "10 dutch breeze/black mail - Dutch Breeze-2.d64"],
    "eod":       [D + "booze_design-edge_of_disgrace/edge_of_disgrace-booze_design_0.d64",
                  D + "booze_design-edge_of_disgrace/edge_of_disgrace-booze_design_1a.d64",
                  D + "booze_design-edge_of_disgrace/edge_of_disgrace-booze_design_1b.d64"],
    "lunatico":  [D + "lft-lunatico/lft-lunatico-side1.d64", D + "lft-lunatico/lft-lunatico-side2.d64"],
    "mafio":     [C + "03 mafiosino/Mafio-DOS_V2.1.d64"],
    "math":      [D + "math/MATH1.D64", D + "math/MATH2.D64"],
    "memento":   [C + "05 memento mori/MementoMori-GP-Disk%d.d64" % i for i in (1, 2, 3)],
    "p7s":       [C + "11 primitive 7 sins/PRIMITIVE.D64"],
    "reanim8ed": [C + "12 reanim8ed/Reanim8ed.S1.D64", C + "12 reanim8ed/Reanim8ed.S2.D64"],
    "shards":    [C + "07 shards of fancy/lft_shards_of_fancy.d64"],
    "hat":       [D + "05 the hat - flt & gp/the-hat-7a825b1-a.d64", D + "05 the hat - flt & gp/the-hat-7a825b1-b.d64"],
    "bench":     [C + "02 transwarp/Transwarp-Benchmark.d64"],
    "uncensored": [D + "Uncensored/Uncensored_1.d64", D + "Uncensored/Uncensored_2.d64"],
    "wonderland": [D + "Wonderland_14-Censor/wonderland_xiv_s%d.d64" % i for i in (1, 2, 3, 4)],
    "galactic":  ["/D64/G/Galactic Controller (198x)(Commodore).d64"],
    "summer":    ["/D64/S/Summer Games II (1985)(Epyx)(Side A).d64"],
    "rainbow":   ["/D64/R/Rainbow Islands (1990)(Taito).d64"],
    "adventure": ["/D64/A/Adventure #01 - Adventureland (1984)(Adams, Scott).d64"],
}


def mount(path):
    out = H.ser(b"M" + path.encode() + b"\n", 1.5)
    for _ in range(8):                      # a swap opens the door first: the disk is out for a moment
        st = H.status()
        if st.get("mounted") == path: break
        time.sleep(1)
    print("mounted:", st.get("mounted"))
    if st.get("mounted") != path: sys.exit("*** not mounted: " + out.strip()[-200:])


def main():
    cmd = sys.argv[1]
    if cmd == "go":
        disks = TITLES[sys.argv[2]]
        H.ser(b"E", 0.5)
        if os.environ.get("NORESET") != "1":  # NORESET=1: the C64 has just been power-cycled by hand; no RAM fill, no reset
            T.cold(); H.api("PUT", "/machine:reset"); time.sleep(6)
        mount(disks[0])
        for k in os.environ.get("SET", ""):  # timing steps after the reset (the board restarts with defaults), e.g. SET='{{'
            print(H.ser(k.encode(), 0.5).strip())
        H.ser(b"z", 0.5)
        T.keys('LOAD"*",8,1\n'); time.sleep(1.5)
        print("load quiet:", T.quiet(120, 3.0))
        T.keys("RUN\n"); print("RUN typed")
    elif cmd == "disk":
        mount(TITLES[sys.argv[2]][int(sys.argv[3]) - 1])
    st = H.status(); print("writes late, worst:", st["wr"], "us; reads:", st["rd"])


if __name__ == "__main__":
    main()
