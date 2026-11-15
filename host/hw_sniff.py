#!/usr/bin/env python3
"""Like-for-like loader runs on the real C64, with PicoD64's bus sniffer recording every ATN edge (fw/iec_sniff.pio):

    python3 host/hw_sniff.py ult  OUT.txt [seconds]   the Ultimate II+'s drive B (device 9) has the bus, PicoD64's is off
    python3 host/hw_sniff.py pico OUT.txt [seconds]   PicoD64 (device 8) has the bus, the Ultimate's drives are off

Both: reset the C64, LOAD"*",dev, RUN (Ultimate REST API), then record PicoD64's serial until `seconds` (default 600)
or 60 s with no edge. Each sniffer line is "S<ms> <gap ticks> <chars>": one char per ATN edge, '0' + 6 bits (the lines
just before the edge, then ~13 us after: ATN CLK DATA, 1 = released). host/sniff_cmp.py compares two recordings.
The D64 must already be on drive B (ult: POST /v1/drives/b:mount) or the card (pico: mounted here by path).
"""
import glob, json, os, select, sys, termios, time, urllib.request

ULT = "http://192.168.8.110"
DISK = os.environ.get("SNIFF_DISK", "/Demos/05 the hat - flt & gp/the-hat-7a825b1-a.d64")   # (a path on the card)
NAME = os.environ.get("SNIFF_NAME", "*")         # SNIFF_NAME=MD SNIFF_ONE=1 SNIFF_NORUN=1: LOAD"MD",dev,1 and no RUN
ONE = ",1" if os.environ.get("SNIFF_ONE") else ""
NORUN = bool(os.environ.get("SNIFF_NORUN"))
QUIET_S, LOAD_S = 60, 25


def api(method, path):
    with urllib.request.urlopen(urllib.request.Request(ULT + path, method=method), timeout=10) as r:
        return r.read()


def drives():
    return {k: v["enabled"] for d in json.loads(api("GET", "/v1/drives"))["drives"] for k, v in d.items() if k in ("a", "b")}


def type_keys(text):
    """Into the KERNAL's 10-key buffer, 8 at a time."""
    for i in range(0, len(text), 8):
        c = bytes(13 if ch == "\n" else ord(ch) for ch in text[i:i + 8])
        api("PUT", f"/v1/machine:writemem?address=0277&data={c.hex()}")
        api("PUT", f"/v1/machine:writemem?address=00C6&data={len(c):02x}")
        time.sleep(0.6)


class Board:
    def __init__(self):
        self.fd = os.open(glob.glob("/dev/ttyACM*")[0], os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd); a[3] &= ~(termios.ICANON | termios.ECHO); a[0] = 0; a[1] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.buf = b""

    def send(self, s):
        os.write(self.fd, s.encode())

    def lines(self, wait):
        """Whole lines read within `wait` seconds."""
        end = time.time() + wait
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                self.buf += os.read(self.fd, 1 << 16)
        *done, self.buf = self.buf.split(b"\n")
        return [l.decode(errors="replace").rstrip("\r") for l in done]

    def cmd(self, s, wait=1.0):
        self.send(s); return self.lines(wait)


def main():
    mode, out = sys.argv[1], sys.argv[2]
    secs = int(sys.argv[3]) if len(sys.argv) > 3 else 600
    b = Board()
    b.cmd("E", 1.0)
    if mode == "ult":
        api("PUT", "/v1/drives/a:off"); api("PUT", "/v1/drives/b:on"); api("PUT", "/v1/drives/a:off")
        # the same D64, from the card's copy on this PC, every run (the Ultimate drops it on a reboot, and once unasked)
        img = open(os.path.expanduser("~/PicoD64-card") + DISK, "rb").read()
        req = urllib.request.Request(ULT + "/v1/drives/b:mount?type=d64&mode=readonly", data=img, method="POST")
        urllib.request.urlopen(req, timeout=20).read()
        assert json.loads(api("GET", "/v1/drives"))["drives"][1]["b"]["image_file"], "drive B: no disk"
        r = b.cmd("X", 1.0)
        if not any("bus off" in l for l in r):
            r = b.cmd("X", 1.0)                      # it was off already: the first X put it on
        dev = 9
    else:
        api("PUT", "/v1/drives/a:off"); api("PUT", "/v1/drives/b:off")
        r = b.cmd("X", 1.0)
        if not any(l.strip() == "bus on" for l in r):
            r = b.cmd("X", 1.0)
        dev = 8
    print("drives:", drives(), "| PicoD64:", [l for l in r if l.startswith("bus")], flush=True)
    d = drives()
    assert not d["a"] and d["b"] == (mode == "ult"), d
    api("PUT", "/v1/machine:reset"); time.sleep(4)
    b.cmd("C"); b.cmd("z")
    if mode == "pico":
        b.cmd("M" + DISK + "\n", 2.0); time.sleep(1)
    r = b.cmd("S", 0.5)
    if not any("sniffer on" in l for l in r):        # it was on (an interrupted run): that S stopped it
        r = b.cmd("S", 0.5)
    assert any("sniffer on" in l for l in r), r
    f = open(out, "w")
    f.write(f"# {mode} {time.strftime('%Y-%m-%d %H:%M:%S')} disk {DISK}\n")
    type_keys(f'LOAD"{NAME}",{dev}{ONE}\n')
    t_end = time.time() + LOAD_S
    while time.time() < t_end:
        for l in b.lines(0.2): f.write(l + "\n")
    if not NORUN:
        type_keys("RUN\n")
        f.write("# RUN\n")
    t0 = last = time.time(); edges = 0; ovf = 0
    while time.time() - t0 < secs and time.time() - last < QUIET_S:
        for l in b.lines(0.2):
            f.write(l + "\n")
            if l.startswith("S") and " " in l:
                p = l.split(" ", 2)
                if len(p) == 3 and p[2]:
                    edges += len(p[2]); last = time.time()
            if "SOVF" in l:
                ovf += 1
    why = "time limit" if time.time() - t0 >= secs else f"no edge for {QUIET_S} s, {last - t0:.0f} s in"
    for l in b.cmd("S", 1.0): f.write(l + "\n")
    tail = b.cmd("c", 2.0) + b.cmd("s", 1.0)
    for l in tail: f.write("# " + l + "\n")
    f.close()
    print(f"{mode}: {edges} edges, {ovf} overflows, ended: {why}")
    for l in tail:
        if l.startswith(("late", "drive now", "pins late")): print("  ", l)


main()
