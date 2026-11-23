#!/usr/bin/env python3
"""Overnight cycle test on the board: The Hat disk 1, over and over, unattended.

Each cycle: the Ultimate resets the C64 (PicoD64 restarts with it; the knob's disk stays in),
LOAD"*",8,1 and RUN are typed at READY, then only serial is watched: no C64 memory is read
while the demo loads (it would hang the load). A pass is the drive reaching The Hat's later
code, in zero page (PC below $0100), after PASS_AFTER: the PC twin is there from about 7
minutes on, and every crash on the board was earlier, in the loader at $03xx (some early code
is in zero page too, so a look before PASS_AFTER doesn't count). A crash: not there by LIMIT. One line per cycle in the log.

  python3 -I host/hatloop.py LOG [CYCLES]        (kill it by its PID to stop)
"""
import os, sys, time, select, termios, urllib.request, re

ULT = "http://192.168.8.110/v1"
DISK = "/Demos/05 the hat - flt & gp/the-hat-7a825b1-a.d64"
LIMIT = 15 * 60          # s from RUN to reach the later code
PASS_AFTER = 450         # s: zero page only counts from here
POLL = 20                # s between serial looks
SPLIT = os.environ.get("HAT_SPLIT") == "1"
WATCH = 300              # s of edge watch from RUN (every crash so far was before this)

def api(m, path, data=None):
    # (2026-11-19: the network went at 05:21 the night before, after an address clash at 04:44, and the first failed call
    #  ended the run; now a call waits through an outage, retrying every 15 s for up to an hour)
    for i in range(240):
        try:
            return urllib.request.urlopen(urllib.request.Request(ULT + path, method=m, data=data), timeout=10).read()
        except OSError as e:
            if i == 0: print(time.strftime("%H:%M:%S"), "Ultimate API failed (%s): retrying" % e, flush=True)
            time.sleep(15)
    raise SystemExit("Ultimate API unreachable for an hour")

def ser_open():
    for _ in range(100):
        try:
            fd = os.open("/dev/ttyACM%d" % (_ % 2), os.O_RDWR | os.O_NOCTTY)   # (after a reboot it can come back as ACM1)
            a = termios.tcgetattr(fd); a[4] = a[5] = termios.B115200; a[3] &= ~(termios.ICANON | termios.ECHO)
            termios.tcsetattr(fd, termios.TCSANOW, a); return fd
        except OSError:
            time.sleep(0.2)
    raise SystemExit("no serial")

def ser(cmd, t=1.0):
    fd = ser_open()
    try:
        os.write(fd, cmd); out = b""; t0 = time.time()
        while time.time() - t0 < t:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r: out += os.read(fd, 8192)
        return out.decode(errors="replace")
    finally:
        os.close(fd)

def status():
    s = ser(b"s", 1.0); d = {"raw": s}
    m = re.search(r"drive now: PC \$([0-9A-F]{4}), half-track (\d+), motor (\d)", s)
    if m: d["pc"] = int(m.group(1), 16); d["ht"] = int(m.group(2)); d["motor"] = m.group(3) == "1"
    m = re.search(r'mounted "([^"]*)"', s); d["mounted"] = m.group(1) if m else None
    m = re.search(r"pins late: .*?worst \d+ sysclk \(([\d.]+) us\)", s); d["wr"] = m.group(1) if m else "?"
    m = re.search(r"\nlate: worst \d+ sysclk \(([\d.]+) us\).*?over 1 us (\d+), over 10 us (\d+)", s)
    d["rd"] = "%s us, %s over 1, %s over 10" % m.groups() if m else "?"
    return d

def screen():
    d = api("GET", "/machine:readmem?address=0400&length=1000")
    def c(b):
        b &= 0x7F
        return "@" if b == 0 else chr(64 + b) if b <= 26 else chr(b) if 32 <= b <= 63 else "."
    return "".join(c(x) for x in d)

def typ(text):
    for i in range(0, len(text), 10):
        chunk = text[i:i + 10].encode()
        api("POST", "/machine:writemem?address=0277", chunk)
        api("PUT", "/machine:writemem?address=00C6&data=%02X" % len(chunk))
        time.sleep(0.6)

def wait_load(limit):
    t0 = time.time(); seen = False; off = None
    while time.time() - t0 < limit:
        m = status().get("motor", False)
        if m: seen = True; off = None
        elif seen:
            off = off or time.time()
            if time.time() - off > 4: return True
        time.sleep(0.5)
    return False

def log(f, s):
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + s
    print(line, flush=True); f.write(line + "\n"); f.flush()

def main():
    path = sys.argv[1]; cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    f = open(path, "a")
    if status().get("mounted") != DISK:
        ser(b"M" + DISK.encode() + b"\n", 2.0)
    passes = crashes = 0
    for n in range(1, cycles + 1):
        api("PUT", "/machine:reset")
        time.sleep(6)                                 # PicoD64 restarts (~0.9 s), the C64 to READY
        st = status()
        if st.get("mounted") != DISK:
            log(f, "cycle %d: disk not in after the reset (%s): mounting" % (n, st.get("mounted")))
            ser(b"M" + DISK.encode() + b"\n", 2.0); time.sleep(1)
        if "READY." not in screen():                  # (idle: the drive isn't loading)
            log(f, "cycle %d: no READY after the reset; again" % n); continue
        ser(b"z", 0.5)
        arm = os.environ.get("HAT_A", "+0.3")           # A/B (2026-11-18): even cycles' pins 0.3 us earlier (serial <, 0.1 us
        # each; the board reboots on every reset, back to its build's default). HAT_B: the even cycles' serial keys;
        # HAT_A_KEYS: the odd cycles' (2026-11-21: "+++++" = 25 sysclk and "+++++++++" = 45 on the +0.0 fine-step build).
        # Each cycle's line gives the pins as the board reported them back
        keys = os.environ.get("HAT_B", "<<<") if SPLIT and n % 2 == 0 else os.environ.get("HAT_A_KEYS", "") if SPLIT else ""
        if keys:
            r = ser(keys.encode(), 1.0)
            m = re.findall(r"pins (-?\d+) sysclk after", r)
            arm = "%d sysclk %+.3f" % (int(m[-1]), int(m[-1]) / 150) if m else "?(" + (r.strip().splitlines() or ["no reply"])[-1] + ")"
            rl = re.findall(r"releases (-?\d+) sysclk before pulls", r)   # (G4, 2026-11-22: '}' raises the release lead 7 sysclk)
            if rl: arm += ", releases %d early" % int(rl[-1])
        typ('LOAD"*",8,1\r')
        if not wait_load(120):
            log(f, "cycle %d: LOAD didn't finish" % n); crashes += 1; continue
        typ("RUN\r")
        t0 = time.time(); ok = False; last = None
        # the edge watch (serial e, 2026-11-19) over the first WATCH s: how long the bus takes to follow our CLK/DATA
        # outputs (the 2N3904s' release, cold vs warm); serial is busy while it runs
        fd = ser_open(); out = b""
        try:
            os.write(fd, b"a")   # (the edge watch, e, reads a 460 ns floor not yet explained: back to the answer watch, 2026-11-19)
            while time.time() - t0 < WATCH:
                r, _, _ = select.select([fd], [], [], 1.0)
                if r: out += os.read(fd, 8192)
            os.write(fd, b"\n"); t1 = time.time()
            while time.time() - t1 < 4:
                r, _, _ = select.select([fd], [], [], 0.2)
                if r: out += os.read(fd, 8192)
        finally:
            os.close(fd)
        w = out.decode(errors="replace")
        with open("%s.watch%d" % (path, n), "w") as d: d.write(w)
        ws = [int(x) for x in re.findall(r"worst: margin (-?\d+) ns[^\n]*drive PC \$03", w)]   # (the summary's min loses a sign under 1 us)
        wmin = "margin min %.2f us" % (min(ws) / 1000) if ws else "margin ?"
        mk = re.search(r"margin < 0 by edge: ([^;]*);", w)       # (G3 builds, 2026-11-21: which edges were late)
        if mk: wmin += " (late: " + mk.group(1) + ")"
        while time.time() - t0 < LIMIT:
            time.sleep(POLL)
            st = status(); last = st
            if time.time() - t0 >= PASS_AFTER and st.get("pc", 0xFFFF) < 0x0100: ok = True; break
        el = time.time() - t0
        if ok:
            passes += 1
            log(f, "cycle %d [pins %s]: PASS at %.0f s; %s; writes worst %s us; reads %s  [%d pass, %d crash]" % (n, arm, el, wmin, last["wr"], last["rd"], passes, crashes))
        else:
            crashes += 1
            dump = "%s.crash%d" % (path, n)            # (TRACE firmware: G the line ring with our pulls, D the drive; the C64's
            with open(dump, "w") as d:                 #  RAM after: its load is dead, so reading it hangs nothing)
                d.write(ser(b"G", 4.0)); d.write(ser(b"D", 2.0))
            with open(dump + ".c64", "wb") as d:
                for a in range(0, 0x10000, 0x1000): d.write(api("GET", "/machine:readmem?address=%04X&length=4096" % a))
            log(f, "cycle %d [pins %s]: CRASH (not at the later code by %d s): %s; drive PC $%04X track %.1f motor %d; writes worst %s us; reads %s  [%d pass, %d crash]"
                % (n, arm, LIMIT, wmin, last.get("pc", 0), last.get("ht", 0) / 2 + 1, last.get("motor", 0), last["wr"], last["rd"], passes, crashes))

if __name__ == "__main__":
    main()
