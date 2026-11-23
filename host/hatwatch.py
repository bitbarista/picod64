#!/usr/bin/env python3
"""The answer watch (serial a) over one launch of The Hat disk 1: how long the drive takes to answer each of the C64's
ATN edges, and the margin left before the C64 reads the lines, timed from the bus lines alone. The same rule times
either drive, so the two can be compared:
  pico: PicoD64 is device 8 with The Hat's disk in (the knob's disk).
  ult:  the Ultimate's drive A as its launcher runs it (device 9) with The Hat's disk 1 in; PicoD64 off the bus (serial X said "lets go").
The C64 is reset, LOAD"*",8,1 and RUN typed; the watch runs from RUN for SECS (default 450, past every crash seen),
then a key stops it and its report is printed and appended to LOG. No C64 memory is read during the load.
  python3 -I host/hatwatch.py pico|ult LOG [SECS]
"""
import os, sys, time, select
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hatloop import api, ser, ser_open, status, screen, typ, DISK
ULT_DISK = "/USB0/Demos/05 the hat - flt & gp/the-hat-7a825b1-a.d64"

def main():
    who, path = sys.argv[1], sys.argv[2]; secs = int(sys.argv[3]) if len(sys.argv) > 3 else 450
    assert who in ("pico", "ult")
    if who == "pico" and status().get("mounted") != DISK:
        ser(b"M" + DISK.encode() + b"\n", 2.0)
    if who == "ult":                                    # the reference alone on the bus: PicoD64's disk out, then X
        import urllib.parse
        api("PUT", "/drives/a:mount?image=" + urllib.parse.quote(ULT_DISK))
        api("PUT", "/machine:reset"); time.sleep(3)    # (the last run's loader out of the drive: a disk comes out only when it idles)
        ser(b"E", 1.0); time.sleep(1); r = ser(b"X", 1.5)
        if "lets go" not in r: raise SystemExit("PicoD64 didn't leave the bus: " + r.strip())
    api("PUT", "/machine:reset"); time.sleep(6)
    if "READY." not in screen(): raise SystemExit("no READY after the reset")
    typ('LOAD"*",%d,1\r' % (8 if who == "pico" else 9))   # (ult: drive A as the launcher runs it, device 9)
    time.sleep(60 if who == "pico" else 90)             # (no serial look at the Ultimate's drive: a fixed wait)
    if who == "pico":
        while status().get("motor"): time.sleep(1)
    sc = screen()
    if "LOADING" not in sc or "READY." not in sc[sc.index("LOADING"):]: raise SystemExit("the LOAD didn't end at READY")
    typ("RUN\r")
    fd = ser_open(); out = b""
    try:
        os.write(fd, b"a"); t0 = time.time()
        while time.time() - t0 < secs:
            r, _, _ = select.select([fd], [], [], 1.0)
            if r: out += os.read(fd, 8192)
        os.write(fd, b"\n"); t0 = time.time()
        while time.time() - t0 < 5:
            r, _, _ = select.select([fd], [], [], 0.2)
            if r: out += os.read(fd, 8192)
    finally:
        os.close(fd)
    rep = time.strftime("%Y-%m-%d %H:%M:%S ") + who + "\n" + out.decode(errors="replace")
    print(rep); open(path, "a").write(rep + "\n")

main()
