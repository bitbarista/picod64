# bmon.py: a client for VICE's binary monitor (protocol from VICE's own manual, doc/vice.texi, "Binary monitor"):
# VICE used as a black box. Commands: STX 0x02, API 0x02, u32 body length, u32 request id, u8 command, body.
# Responses: STX, API, u32 body length, u8 type, u8 error, u32 request id (0xffffffff = an event), body.
import os, socket, struct, subprocess, time

DOS = os.path.expanduser("~/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin")
MEM_C64, MEM_D8 = 0, 1

class Vice:
    def __init__(self, disk=None, extra=()):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0)); self.port = s.getsockname()[1]
        args = ["x64sc", "-default", "-silent", "-minimized", "-dos1541", DOS, "-drive8type", "1541", "-drive8truedrive",
                "-binarymonitor", "-binarymonitoraddress", "ip4://127.0.0.1:%d" % self.port] + list(extra)
        if disk: args += ["-autostart", disk]
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try: self.s = socket.create_connection(("127.0.0.1", self.port), timeout=5); break
            except OSError: time.sleep(0.2)
        self.s.settimeout(None); self.rid = 1; self.events = []; self.buf = b""

    def close(self):
        try: self.proc.kill()
        except Exception: pass

    def _read(self, n):
        while len(self.buf) < n:
            d = self.s.recv(65536)
            if not d: raise EOFError("VICE closed the monitor")
            self.buf += d
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self):
        """One response: (type, error, request id, body)."""
        h = self._read(12)
        assert h[0] == 2, h
        ln, typ, err, rid = struct.unpack("<IBBI", h[2:12])
        return typ, err, rid, self._read(ln)

    def cmd(self, c, body=b""):
        """Send a command; return its response body; events that come meanwhile are queued."""
        rid = self.rid; self.rid += 1
        self.s.sendall(struct.pack("<BBIIB", 2, 2, len(body), rid, c) + body)
        while True:
            typ, err, r, b = self.recv()
            if r == rid:
                if err: raise RuntimeError(f"command {c:#x}: error {err:#x}")
                return b
            self.events.append((typ, b))

    def event(self, timeout=None):
        """The next event (type, body), waiting for one."""
        if self.events: return self.events.pop(0)
        self.s.settimeout(timeout)
        try:
            typ, err, r, b = self.recv()
        finally:
            self.s.settimeout(None)
        return typ, b

    # --- commands
    def mem(self, a, b, space=MEM_C64):
        body = self.cmd(0x01, struct.pack("<BHHBH", 0, a, b, space, 0))
        return body[2:]

    def checkpoint(self, a, b=None, op=4, stop=True, space=MEM_C64, temp=False):
        body = self.cmd(0x12, struct.pack("<HHBBBBB", a, a if b is None else b, int(stop), 1, op, int(temp), space))
        return struct.unpack("<I", body[:4])[0]

    def cp_delete(self, n): self.cmd(0x13, struct.pack("<I", n))

    def regs_available(self, space=MEM_C64):
        b = self.cmd(0x83, struct.pack("<B", space)); n = struct.unpack("<H", b[:2])[0]; i = 2; out = {}
        for _ in range(n):
            sz = b[i]; rid, bits, nl = b[i + 1], b[i + 2], b[i + 3]; out[b[i + 4:i + 4 + nl].decode()] = rid; i += sz + 1
        return out

    def regs(self, space=MEM_C64):
        b = self.cmd(0x31, struct.pack("<B", space)); n = struct.unpack("<H", b[:2])[0]; i = 2; out = {}
        for _ in range(n):
            sz = b[i]; rid = b[i + 1]; out[rid] = struct.unpack("<H", b[i + 2:i + 4])[0]; i += sz + 1
        return out

    def go(self):
        self.cmd(0xAA); self.events = [e for e in self.events if e[0] not in (0x62, 0x63, 0x31)]

    def wait_hit(self, timeout=60):
        """Resume and wait for a checkpoint hit (0x11 with 'currently hit'), then its stop; returns its number."""
        self.go(); n = None
        while True:
            typ, b = self.event(timeout)
            if typ == 0x11 and b[4]: n = struct.unpack("<I", b[:4])[0]
            if typ == 0x62 and n is not None: return n

    def step(self, n=1, over=False): self.cmd(0x71, struct.pack("<BH", int(over), n))

    def resource(self, name):
        b = self.cmd(0x51, struct.pack("<B", len(name)) + name.encode())
        return b[2:2 + b[1]] if b[0] == 0 else int.from_bytes(b[2:2 + b[1]], "little")

def _cond(self, n, expr):
    e = expr.encode(); self.cmd(0x22, struct.pack("<IB", n, len(e)) + e)
Vice.condition = _cond

def _memset(self, a, data, space=MEM_C64):
    self.cmd(0x02, struct.pack("<BHHBH", 0, a, a + len(data) - 1, space, 0) + data)
def _regset(self, regs, space=MEM_C64):
    body = struct.pack("<BH", space, len(regs)) + b"".join(struct.pack("<BBH", 3, k, v) for k, v in regs.items())
    self.cmd(0x32, body)
Vice.memset = _memset; Vice.regset = _regset
