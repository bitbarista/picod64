#!/usr/bin/env python3
"""Run a disk's loader on the real C64 and PicoD64, unattended, and score how far it gets.

    python3 host/hw_loader_test.py [runs] [d64 path on the card] [cmd byte addr in drive RAM, hex]

The C64 is driven through an Ultimate II+L's REST API (192.168.8.110; its drive A must be OFF, so it
doesn't answer as device 8 too): reset, then LOAD"*",8 and RUN typed into the keyboard buffer. PicoD64 is
driven over its USB serial: E (eject), C/z (clear its logs), M<path> (insert), and c, its event log of the
loader's command byte and the head's half-track. Nothing reads the C64 while the program runs (DMA can upset
its timing). A run ends after 600 s, or when no command has come for 45 s (VICE's longest gap between loads
is 39 s), which counts as a crash; its score is how many commands the drive received ("next file", The Hat's
$10). VICE's drive gets ~15 in the first 120 s; at 150 MHz the board stops after 2 (2026-11-07).
"""
import glob, os, select, sys, termios, time, urllib.request

ULT = "http://192.168.8.110"
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
DISK = sys.argv[2] if len(sys.argv) > 2 else "/Demos/05 the hat - flt & gp/the-hat-7a825b1-a.d64"
QUIET_S, MAX_S, LOAD_S = 60, 600, 25   # stalled = no command for 60 s (VICE's longest gap between loads: 39 s)


def api(method, path):
    req = urllib.request.Request(ULT + path, method=method)
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read().decode(errors="replace")


def type_keys(text):
    """Into the KERNAL's keyboard buffer ($0277, count in $C6): at most 10 characters at a time."""
    codes = bytes(13 if c == "\n" else ord(c) for c in text)
    assert len(codes) <= 10
    api("PUT", f"/v1/machine:writemem?address=0277&data={codes.hex()}")
    api("PUT", f"/v1/machine:writemem?address=00C6&data={len(codes):02x}")


class Board:
    def __init__(self):
        self.fd = os.open(glob.glob("/dev/ttyACM*")[0], os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd)
        a[3] &= ~(termios.ICANON | termios.ECHO); a[0] = 0; a[1] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def cmd(self, s, wait=1.0):
        self.fd and os.write(self.fd, s.encode())
        out, end = b"", time.time() + wait
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if r:
                out += os.read(self.fd, 65536)
                end = max(end, time.time() + 0.3)
        return out.decode(errors="replace")

    def events(self):
        return [l.split() for l in self.cmd("c", 1.5).splitlines() if l[:1].isdigit() and len(l.split()) == 3]


def screen():
    """After a run (no DMA during one): the border colour and the text screen at $0400, as ASCII-ish."""
    try:
        border = api_bin(f"/v1/machine:readmem?address=D020&length=1")[0] & 15
        scr = api_bin(f"/v1/machine:readmem?address=0400&length=1000")
    except Exception as e:
        return f"(screen not read: {e})"
    rows = []
    for r in range(25):
        line = "".join(chr(c + 64) if 1 <= c <= 26 else chr(c) if 32 <= c < 64 else "." for c in (x & 0x7F for x in scr[r * 40:r * 40 + 40]))
        rows.append(line.rstrip("."))
    text = "\n".join("   |" + l for l in rows if l.strip(". "))
    return f"border {border}; screen at $0400:\n" + (text or "   (empty)")


def api_bin(path):
    with urllib.request.urlopen(urllib.request.Request(ULT + path, method="GET"), timeout=10) as r:
        return r.read()


def drive_a_on():
    import json
    try: return json.loads(api("GET", "/v1/drives"))["drives"][0]["a"]["enabled"]
    except Exception: return False


def run_once(b):
    # the Ultimate's drive A is on by default when it powers up: a second device 8, and the run means nothing
    assert not drive_a_on(), "Ultimate drive A is ON (device 8 twice): PUT /v1/drives/a:off first"
    b.cmd("E", 1.0)
    api("PUT", "/v1/machine:reset")
    time.sleep(4)
    b.cmd("C"); b.cmd("z")
    b.cmd("M" + DISK + "\n", 2.0)
    time.sleep(1)
    type_keys('LOAD"*",8\n')
    time.sleep(LOAD_S)
    type_keys("RUN\n")
    t0 = last_t = time.time(); seen = 0; why = "time limit"
    while time.time() - t0 < MAX_S:
        time.sleep(3)
        ev = b.events()
        ev = [e for e in ev if e[1] == "cmd-byte" and e[2] == "10"]
        if len(ev) != seen:
            seen, last_t = len(ev), time.time()
        elif time.time() - last_t > QUIET_S:
            why = f"stalled {QUIET_S} s after the last command, {last_t - t0:.0f} s in"
            break
    ev = b.events()
    cmds = [e for e in ev if e[1] == "cmd-byte" and e[2] == "10"]
    stats = [l for l in b.cmd("s", 1.5).splitlines() if l.startswith(("bus writes", "late", "drive now"))]
    return len(cmds), ev, [why] + stats + [screen()]


if __name__ == "__main__":
    b = Board()
    scores = []
    for i in range(RUNS):
        n, ev, stats = run_once(b)
        scores.append(n)
        tail = " ".join(f"{e[0]}:{'c' if e[1] == 'cmd-byte' else 'h'}{e[2]}" for e in ev[-8:])
        print(f"run {i + 1}: {n} commands; " + " | ".join(x for x in stats[:-1] if not x.startswith("bus writes")) + f"\n   last events (ms:c=cmd byte,h=half-track): {tail}", flush=True)
    print("scores:", scores)
