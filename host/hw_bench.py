#!/usr/bin/env python3
"""PicoD64's on-board speed benchmark, over USB serial (fw/pico_main.c bench_run):
    python3 host/hw_bench.py [flash]      (flash: put fw/build/picod64.uf2 on first)
Mounts The Hat's disk and ejects it (so a disk is in the drive model), then runs 'b' (the booted DOS) and 'k' (a
loader-like BIT $1800 / STA $1800 / JMP loop): sysclk per drive cycle, off the bus. Real time is 150 at 150 MHz."""
import glob, os, select, shutil, subprocess, sys, termios, time

DISK = "/Demos/05 the hat - flt & gp/the-hat-7a825b1-a.d64"


def port():
    for _ in range(60):
        p = glob.glob("/dev/ttyACM*")
        if p:
            return p[0]
        time.sleep(0.5)
    sys.exit("no board")


def flash():
    fd = os.open(port(), os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd); a[4] = a[5] = termios.B1200; termios.tcsetattr(fd, termios.TCSANOW, a); os.close(fd)
    for _ in range(80):
        if os.path.exists("/run/media/carl/RP2350/INFO_UF2.TXT"):
            break
        time.sleep(0.5)
    shutil.copy(os.path.join(os.path.dirname(__file__), "..", "fw", "build", "picod64.uf2"), "/run/media/carl/RP2350/")
    subprocess.run(["sync"])
    time.sleep(4)


def main():
    if "flash" in sys.argv[1:]:
        flash()
    fd = os.open(port(), os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd); a[3] &= ~(termios.ICANON | termios.ECHO); a[0] = 0; a[1] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)

    def cmd(s, wait):
        os.write(fd, s.encode()); out, end = b"", time.time() + wait
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                out += os.read(fd, 65536)
        return out.decode(errors="replace")
    cmd("E", 1)
    cmd("M" + DISK + "\n", 3)
    cmd("E", 2)
    for k in (sys.argv[2] if len(sys.argv) > 2 else "bkKnN"):
        print([l for l in cmd(k, 12).splitlines() if l.startswith("bench")])


main()
