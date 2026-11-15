#!/usr/bin/env python3
"""The browser in VICE, on a folder of the PC as drive 8:  sh host/try-vice.sh FOLDER

VICE's folder device can't mount a D64, so this watches the C64's memory (MBOX in browser.asm)
through VICE's monitor: asked for a D64, it swaps drive 8 for a real 1541 with that disk in it,
and back. Everything then loads as it would from a 1541, fast loaders included, but there is
no PicoD64 turbo. The twin (host/fwtwin.sh) tests the real firmware.

Enter in this terminal is the knob's long press: the next disk of the set.

--test STEP...  runs minimised: keys for the C64 (as the monitor's keybuf),
w<seconds>, shot:FILE, next, warp or nowarp. Used to check this by hand.
"""
import fnmatch, os, re, select, shutil, signal, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOS = os.environ.get("DOS", os.path.expanduser("~/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin"))
MBOX = 0x03F0
PROMPT = re.compile(rb"\(C:\$[0-9a-f]{4}\) $")


class Monitor:
    def __init__(self, port):
        self.port = port

    def do(self, *cmds):
        """Stops the C64 while it is connected. Returns all the text."""
        for _ in range(60):
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=5)
                break
            except OSError:
                time.sleep(0.5)
        else:
            raise SystemExit("VICE's monitor didn't answer")
        out = b""
        s.settimeout(0.2)
        trace = os.environ.get("TRYVICE_TRACE")
        def until_prompt(need=b""):
            nonlocal out
            buf = b""
            t = time.time()
            while time.time() - t < 8:
                if need in buf and PROMPT.search(buf):
                    break
                try:
                    d = s.recv(65536)
                except socket.timeout:
                    continue
                if not d:
                    break
                buf += d
            out += buf
        # No prompt comes until something is typed, and then one comes ahead of the answer: r lines it up
        s.sendall(b"r\n")
        until_prompt(b"STOPWATCH")
        for c in cmds:
            s.sendall((c + "\n").encode())
            n = len(out)
            until_prompt()
            if trace:
                print(">", c, "->", out[n:][-160:], flush=True)
        s.sendall(b"x\n")
        s.close()
        return out.decode("latin-1")

    def mem(self, a, n):
        if n <= 0:
            return b""
        t = self.do("m %04x %04x" % (a, a + n - 1))
        got = {}
        for m in re.finditer(r">C:([0-9a-f]{4})((?:\s+[0-9a-f]{2})+)", t):
            at = int(m.group(1), 16)
            for i, h in enumerate(m.group(2).split()):
                got[at + i] = int(h, 16)
        return bytes(got.get(a + i, 0) for i in range(n))


def norm(b):
    """A PETSCII name as a lower-case PC name."""
    r = ""
    for c in b:
        if 0xC1 <= c <= 0xDA:
            r += chr(c - 0x80).lower()
        elif c == 0x5F or c == 0xA4:
            r += "_"
        else:
            r += chr(c).lower()
    return r


def find(root, path, name):
    """The file the C64 means, case aside. name may end in * (cut at 46 characters)."""
    d = root
    for part in [p for p in norm(path).split("/") if p]:
        for e in os.listdir(d):
            if e.lower() == part or fnmatch.fnmatch(e.lower(), part):
                d = os.path.join(d, e)
                break
        else:
            break
    want = norm(name)
    for e in sorted(os.listdir(d)):
        if e.lower() == want or fnmatch.fnmatch(e.lower(), want):
            return os.path.join(d, e)
    for dp, _, fs in os.walk(root):       # a path too long for the browser to keep
        for e in fs:
            if e.lower() == want or fnmatch.fnmatch(e.lower(), want):
                return os.path.join(dp, e)
    return None


def next_disk(cur):
    """As fw/diskset.c, for this helper: the D64s named the same but for the last number or letter
    (0, 1a, 1b; side a, b), else every D64 in the folder. After the last comes the first."""
    def key(n):
        m = re.match(r"(.*?)(\d+[a-z]?|[a-z])?( of \d+\)?)?$", n[:-4].lower())
        return m.group(1), m.group(2) or ""
    def order(n):
        t = key(n)[1]
        m = re.match(r"(\d+)([a-z]?)", t)
        return (int(m.group(1)), m.group(2)) if m else (10 ** 6, t)
    d, name = os.path.split(cur)
    all_ = [e for e in os.listdir(d) if e.lower().endswith(".d64")]
    disks = [e for e in all_ if key(e)[0] == key(name)[0]]
    disks = sorted(disks, key=order) if len(disks) > 1 else sorted(all_, key=str.lower)
    return os.path.join(d, disks[(disks.index(name) + 1) % len(disks)])


def swap_in(mon, tmp, f):
    """A copy for each disk, so a game's saves stay with it for this run and can't touch the card."""
    copy = os.path.join(tmp, os.path.basename(f))
    if not os.path.exists(copy):
        shutil.copyfile(f, copy)
    mon.do('attach "%s" 8' % copy)


def to_drive(mon):
    mon.do('resourceset "IECDevice8" "0"', 'resourceset "FileSystemDevice8" "0"',
           'resourceset "Drive8Type" "1541"', 'resourceset "Drive8TrueEmulation" "1"')


def to_folder(mon, folder):
    mon.do("detach 8", 'resourceset "Drive8Type" "0"', 'resourceset "FileSystemDevice8" "1"',
           'resourceset "IECDevice8" "1"', 'resourceset "VirtualDevice8" "0"')


current = [None]                          # the disk in the drive


def press_next(mon, tmp):
    if not current[0]:
        print("no disk in the drive", flush=True)
        return
    current[0] = next_disk(current[0])
    print("drive 8:", current[0], flush=True)
    swap_in(mon, tmp, current[0])


def serve(mon, root, tmp):
    """One look at the mailbox. A file goes in through a copy, so a game saving can't touch the card."""
    b = mon.mem(MBOX, 9)
    if b[:2] != b"PD" or b[2] == 0:
        return
    if b[2] == 1:
        name = mon.mem(b[4] | b[5] << 8, b[3])[3:]
        path = mon.mem(b[7] | b[8] << 8, b[6])
        f = find(root, path, name)
        if not f:
            print("not found:", norm(path), norm(name), flush=True)
            return
        print("drive 8:", f, flush=True)
        current[0] = f
        to_drive(mon)
        swap_in(mon, tmp, f)
        time.sleep(3)                     # the 1541's DOS starts up
    else:
        print("drive 8: the folder", flush=True)
        current[0] = None
        to_folder(mon, root)
    mon.do("> %04x 00" % (MBOX + 2))


def main():
    args = sys.argv[1:]
    test = None
    if "--test" in args:
        i = args.index("--test")
        test = args[i + 1:]
        args = args[:i]
    root = os.path.abspath(args[0])
    tmp = tempfile.mkdtemp()
    prg = os.path.join(HERE, "fw/browser/browser.prg")
    image = open(prg, "rb").read()
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    cmd = ["x64sc", "-default", "-silent"]
    if test is not None:
        cmd += ["-minimized"]
    cmd += ["-fslongnames", "-dos1541", DOS, "-drive8type", "0", "-iecdevice8", "+virtualdev8", "-fs8", root,
            "-remotemonitor", "-remotemonitoraddress", "ip4://127.0.0.1:%d" % port]
    vice = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mon = Monitor(port)
    # Straight into memory: a second drive on the bus to load it from stops demos' fast loaders.
    for _ in range(60):                   # READY. on the screen: before that the KERNAL is clearing memory
        if bytes([0x12, 0x05, 0x01, 0x04, 0x19, 0x2E]) in mon.mem(0x0400, 320):
            break
        time.sleep(0.25)
    end = 0x0801 + len(image) - 2
    mon.do('l "%s" 0' % prg, "> 002d %02x %02x" % (end & 255, end >> 8), "> 00ba 08", "keybuf run\\x0d")
    first = image[2:18]
    def serving():
        """Until something has loaded over the browser: then leave the C64 alone."""
        return mon.mem(0x0801, len(first)) == first
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    try:
        if test is None:
            print("Enter here: the next disk of the set", flush=True)
            watching = True
            while vice.poll() is None:
                try:
                    watching = watching and serving()
                    if watching:
                        serve(mon, root, tmp)
                    if select.select([sys.stdin], [], [], 0.4)[0]:
                        if not sys.stdin.readline():
                            sys.stdin = open(os.devnull)
                        else:
                            press_next(mon, tmp)
                except OSError:
                    pass
        else:
            def idle(t):
                end = time.time() + t
                while time.time() < end:
                    if serving():
                        serve(mon, root, tmp)
                    time.sleep(0.4)
            idle(4)
            for st in test:
                if st in ("warp", "nowarp"):
                    mon.do('resourceset "WarpMode" "%d"' % (st == "warp"))
                elif st == "next":
                    press_next(mon, tmp)
                elif st.startswith("shot:"):
                    mon.do('screenshot "%s" 2' % st[5:])
                    idle(1)
                elif re.fullmatch(r"w[0-9.]+", st):
                    idle(float(st[1:]))
                else:
                    mon.do("keybuf " + st)
                    idle(5)
    finally:
        vice.kill()
        shutil.rmtree(tmp, ignore_errors=True)


main()
