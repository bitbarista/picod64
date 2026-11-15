/* -------------------------------------------------------------------------
 * PicoD64 -- core 1's runtime: browse mode or the emulated 1541 on the IEC
 * bus (FIRMWARE.md "Board firmware design"). The logic of host/f6.c, shared by
 * the board (fw/pico_main.c) and its host twin (host/fwtwin.c).
 *
 * Header-only. Before including, the platform:
 *  - includes fw/bus_plat.h, defines the drive's PLAT_* macros so that, while
 *    bus_replaying is set, the drive sees bus_replay_lines instead of the bus,
 *    drives nothing, and does not wait for real time; then core/drive1541.h;
 *  - defines these:
 *      uint32_t bus_us(void)         free-running µs
 *      uint8_t  bus_in(void)         the bus, bit set = line low (IECD_ATN|CLK|DATA)
 *      void     bus_out(uint8_t p)   what we pull (IECD_CLK|IECD_DATA)
 *      int      bus_reset(void)      the C64's RESET line is low
 *    and the storage side (core 0 on the board), each BLOCKING; the caller has
 *    already put its pulls on the bus, so the C64 waits ("busy"):
 *      int  st_list_(path, idx, fsent_t *), long st_read_(path, buf, max)
 *      int  st_find_(path, name)  an entry's number, or -1
 *      int  st_newdisk_(path, name, nl, id1, id2)  a new formatted D64: 1, -1 exists, 0 failed
 *      int  st_write_(path, buf, len)   SAVE in browse mode: the whole file
 *      int  st_load_(path)        the D64 into bus_gcr / bus_disk; 1 if OK
 *      int  st_virtual_(dir, first)  build the PRGs' D64 and load it; 1 if OK
 *      void st_flush_(void)       write the mounted disk's dirty tracks back
 * ------------------------------------------------------------------------- */
#ifndef FF_BUS_H
#define FF_BUS_H
#ifndef FW_TRACE
#define FW_TRACE(...) do { } while (0)    /* the host twin prints what the bus carries */
#endif
#ifndef FF_C1
#define FF_C1          /* core 1 code: the board puts it in SRAM */
#endif
/* after each run of the drive: the platform may hold it back to near real time. The board's deadlines are 32-bit
 * sysclk (28.6 s at 150 MHz): a drive that touched no VIA1 for long ran so far ahead that the difference wrapped and
 * read as late (2026-11-07, The Hat's Flip It wait: 28 s ahead read as 0.635 s late, and it ran that long unsynced) */
#ifndef BUS_RUN_LEAD
#define BUS_RUN_LEAD() do { } while (0)
#endif
#include <string.h>
#include "../core/browse.h"
#include "browser/browser_prg.h"

/* The device number, 8-11: set before bus_init (fw/pico_main.c, from flash). A 1541 reads it from two
 * jumpers, VIA1 PB5-6 [S7], when its DOS boots, so they are set before the boot that makes the snapshot.
 * Browse mode, the sniffer and the replay use the same number. */
static uint8_t bus_device = 8;
#include "../core/iec_host.h"

/* --- the disk in RAM (shared with core 0, which loads and writes it back) --- */
static uint8_t bus_gcr[D64_GCR_BYTES];
static disk_t bus_disk;
static syncs_t bus_frames[D64_TRACKS];

/* --- state ------------------------------------------------------------------ */
enum { SW_NONE, SW_OUT, SW_EMPTY, SW_LOADED, SW_IN, SW_SETTLE };   /* disk swap (F9) */
typedef struct {
    iecdev_t dev;                    /* browse mode's device */
    browse_t br;
    iecl_t convo, replay;            /* the conversation since the last OPEN; its copy at M-W */
    int online, virtual_disk, pending_mount;
    volatile int pending_unmount;    /* set by the sniffer (core 0) or the UI */
    volatile int next_req;           /* "XN" on the command channel while a disk is in: the next disk of the set (core 0) */
    char mounted[BR_PATH];           /* card path of the D64 in the drive ("" = virtual) */
    char prg[BR_NAME];               /* virtual: the program put first, for the OLED ("" = a fast loader's) */
    char last_dir[BR_PATH];          /* browse folder when the mount happened */
    int reset_prev;
    /* swap: requested by the UI with swap_path; core 1 plays the sensor sequence */
    volatile int swap_req; char swap_path[BR_PATH];
    int sw, sw_ok; uint32_t sw_at;
    volatile int sw_out_ready, sw_in_ready;   /* core 1 -> core 0: disk out; core 0 -> 1: new disk loaded */
    /* the UI mounts a D64 in browse mode: core 1 does it when the bus is idle */
    volatile int ui_mount_req, ui_mount_prg; char ui_mount_path[BR_PATH];   /* ui_mount_prg: a program, as browse mode's */
    uint32_t mounts, unmounts, swaps, resets, replays_ok, replays_late, turbos; uint8_t turbo_on, turbo_done;
    uint32_t turbo_starts, turbo_atn;
    uint8_t knob_disk;                                /* the knob put this disk in: a C64 reset keeps it (the browser's: out) */                 /* transfers begun; ended by ATN before the last byte (first board, 2026-11-07) */
} bus_t;
static bus_t B;
static drive_snap_t bus_booted;      /* the DOS, booted once at power-up */
static const uint8_t *bus_rom;

/* --- browse mode's card access: through the platform -------------------- */
static FF_C1 int bfs_list(void *c, const char *p, int i, fsent_t *e) { (void)c; return st_list_(p, i, e); }
static FF_C1 long bfs_read(void *c, const char *p, uint8_t *b, long m) { (void)c; return st_read_(p, b, m); }
static FF_C1 int bfs_mount(void *c, const char *path)
{
    (void)c;
    B.knob_disk = 0;                                  /* (the knob's path sets it again after this) */
    if (!st_load_(path)) return 0;
    strncpy(B.mounted, path, BR_PATH - 1); B.mounted[BR_PATH - 1] = 0;
    if (path != B.br.img) { strncpy(B.br.img, path, BR_PATH - 1); B.br.img[BR_PATH - 1] = 0; }
    { const char *sl = strrchr(path, '/'); if (sl) { strncpy(B.br.sel, sl + 1, BR_NAME - 1); B.br.sel[BR_NAME - 1] = 0; } }   /* the browser's cursor, on the way back */
    strcpy(B.last_dir, B.br.cwd); B.prg[0] = 0;
    B.virtual_disk = 0; B.pending_mount = 1;          /* switch once ATN is released */
    return 1;
}
static FF_C1 uint8_t bfs_peek(void *c, uint16_t a)
{
    (void)c;
    if (a >= 0xC000) return bus_rom[a - 0xC000];
    if (a < 0x0800) return bus_booted.d.ram[a];
    return (uint8_t)(a >> 8);                        /* [A] open bus, as the core's map */
}
static FF_C1 int bfs_drivecode(void *c, const char *path, const char *first, uint16_t addr, const uint8_t *data, int n)
{
    B.knob_disk = 0;
    (void)c; (void)addr; (void)data; (void)n;        /* the M-W itself is in the conversation */
    if (!st_virtual_(path, first)) return 0;
    B.replay = B.convo;
    FW_TRACE("  [replay: %d ops%s:", B.replay.n, B.replay.over ? " (overflowed)" : "");
    for (int i = 0, r = 0; i < B.replay.n; i++) {
        const iecl_op_t *o = &B.replay.op[i];
        if (o->type == IECL_READ) { r++; continue; }
        if (r) { FW_TRACE(" read x%d", r); r = 0; }
        if (o->type == IECL_ATN) FW_TRACE(" ATN %02X", o->b); else if (o->type == IECL_DATA) FW_TRACE(" %02X", o->b);
    }
    FW_TRACE("]\n");
    B.mounted[0] = 0; strcpy(B.last_dir, path); B.prg[0] = 0;
    B.virtual_disk = 1; B.pending_mount = 1;
    return 1;
}
static FF_C1 int bfs_write(void *c, const char *p, const uint8_t *b, long n) { (void)c; return st_write_(p, b, n); }
static FF_C1 int bfs_mount_prg(void *c, const char *dir, const char *first)
{
    (void)c;
    B.knob_disk = 0;
    if (!st_virtual_(dir, first)) return 0;
    B.replay.n = 0;                                   /* nothing open to replay: a plain mount */
    B.mounted[0] = 0; strcpy(B.last_dir, dir);
    strncpy(B.prg, first, BR_NAME - 1); B.prg[BR_NAME - 1] = 0;
    B.virtual_disk = 1; B.pending_mount = 1;
    return 1;
}
static FF_C1 long bfs_prep(void *c, const char *p, const char *f) { (void)c; return st_prep_(p, f); }
static FF_C1 int bfs_find(void *c, const char *p, const char *n) { (void)c; return st_find_(p, n); }
static int nd_fs(void *c, const char *p, const uint8_t *n, int nl, uint8_t i1, uint8_t i2) { (void)c; return st_newdisk_(p, n, nl, i1, i2); }
static const fs_ops_t bus_fsops = { bfs_list, bfs_read, bfs_mount, bfs_peek, bfs_drivecode, bfs_write, bfs_mount_prg, bfs_prep, bfs_find, nd_fs };

/* the conversation log (f6.c) */
static FF_C1 int bus_talk(iecdev_t *d, uint8_t sa, uint8_t *c, int *last)
{
    int r = browse_ops.talk_byte(d, sa, c, last);
    if (r) iecl_add(&B.convo, IECL_READ, 0, 0);
    return r;
}
static FF_C1 void bus_cmd(iecdev_t *d, uint8_t c)
{
    if ((c & 0xF0) == 0xF0 && (c & 0x0F) != 15 && d->listening) {   /* OPEN of a file: a new conversation */
        B.convo.n = 0; B.convo.over = 0;
        iecl_add(&B.convo, IECL_ATN, (uint8_t)(0x20 | d->dev), 0);
    }
    if ((c & 0xF0) == 0xE0 && (c & 0x0F) != 15 && d->listening) {   /* CLOSE of a file: nothing left open */
        B.convo.n = 0; B.convo.over = 0;
        return;
    }
    iecl_add(&B.convo, IECL_ATN, c, 0);
}
static FF_C1 void bus_unlisten(iecdev_t *d)
{
    if (B.br.nlen) FW_TRACE("  [browse gets %s sa %d: %.*s]\n", d->opening ? "OPEN" : "command", d->sa, B.br.nlen, (const char *)B.br.name);
    browse_ops.unlisten(d);
}
static FF_C1 void bus_atn_end(iecdev_t *d) { (void)d; iecl_add(&B.convo, IECL_ATN_END, 0, 0); }
static FF_C1 void bus_byte(iecdev_t *d, uint8_t sa, uint8_t c, int eoi) { iecl_add(&B.convo, IECL_DATA, c, (uint8_t)eoi); browse_ops.listen_byte(d, sa, c, eoi); }
static iecdev_ops_t bus_devops;

/* --- the sniffer: while the 1541 has the bus, channel-15 "CD_", "CD:_", "CD//"
 * mean "back to browsing". Fed from the drive's own view of the bus (core 0 on
 * the board, from a log of line changes; see bus_sniff_feed). ---------------- */
static iecdev_t bus_sniff;
static uint8_t sn_cmd[48]; static int sn_n;
static FF_C1 void sn_open(iecdev_t *d, uint8_t sa) { (void)d; (void)sa; sn_n = 0; }
static FF_C1 void sn_byte(iecdev_t *d, uint8_t sa, uint8_t c, int eoi) { (void)eoi; if ((sa == 15 || d->opening) && sn_n < 48) sn_cmd[sn_n++] = c; }
static uint8_t sn_last[4][12]; static int sn_last_sa[4]; static uint32_t sn_unl, sn_cd;   /* the last 4 seen, CD acted on (serial 's') */
static FF_C1 void sn_unlisten(iecdev_t *d)
{
    const uint8_t *s = sn_cmd;
    { int i = (int)(sn_unl++ & 3); sn_last_sa[i] = d->sa; for (int k = 0; k < 12; k++) sn_last[i][k] = k < sn_n ? sn_cmd[k] : 0; }
    if (sn_n) FW_TRACE("  [1541 gets %s sa %d: %.*s]\n", d->opening ? "OPEN" : "command", d->sa, sn_n, (const char *)s);
    if (sn_n >= 6 && s[0] == 'M' && s[1] == '-') FW_TRACE("    [%c-%c at $%02X%02X, %d bytes]\n", s[0], s[2], s[4], s[3], sn_n - 5);
    if (d->sa == 15 && sn_n >= 3 && s[0] == 'C' && s[1] == 'D' &&
        (s[2] == 0x5F || (sn_n >= 4 && s[2] == ':' && s[3] == 0x5F) || (sn_n >= 4 && s[2] == '/' && s[3] == '/')))
        { B.pending_unmount = 1; sn_cd++; }
    if (d->sa == 15 && sn_n >= 2 && s[0] == 'X' && s[1] == 'N') B.next_req = 1;   /* a KERNAL key's "next disk" (2026-11-08) */
    sn_n = 0;
}
static FF_C1 int sn_talk(iecdev_t *d, uint8_t sa, uint8_t *b, int *l) { (void)d; (void)sa; (void)b; (void)l; return 0; }
static const iecdev_ops_t bus_sniff_ops = { sn_byte, sn_open, 0, sn_unlisten, sn_talk };
static FF_C1 void bus_sniff_reset(void)
{
    memset(&bus_sniff, 0, sizeof bus_sniff);
    bus_sniff.dev = bus_device; bus_sniff.passive = 1; bus_sniff.ops = &bus_sniff_ops;
    sn_n = 0;
}
/* one observation: the bus at time us (call on every change, and now and then
 * without one, so its timeouts run) */
static FF_C1 void bus_sniff_feed(uint32_t us, uint8_t lines) { iec_poll(&bus_sniff, us, lines); }

/* --- power-up ------------------------------------------------------------------ */
static FF_C1 void bus_init(const uint8_t *rom)
{
    memset(&B, 0, sizeof B);
    bus_rom = rom;
    /* the 1541, booted once, off the bus: a cold DOS needs ~1.2 s for its RAM test (F4a) */
    bus_replaying = 1;                               /* off the bus, as fast as it can */
    bus_replay_lines = 0;
    drive_init(rom, &drive_nodisk);
    D.via1.pb_in = (uint8_t)(D.via1.pb_in | ((bus_device - 8) << 5));
    drive_run(2500000);
    drive_save(&bus_booted);
    bus_replaying = 0;
    browse_init(&B.br, &B.dev, &bus_fsops, 0, bus_gcr, (long)sizeof bus_gcr);
    B.dev.dev = bus_device;
    B.br.builtin = browser_prg; B.br.builtin_len = (long)sizeof browser_prg;   /* LOAD"BROWSER",8; XB */
    B.br.boot = browser_boot; B.br.boot_len = (long)sizeof browser_boot;       /* LOAD"*",8 at the root */
    bus_devops = browse_ops; bus_devops.talk_byte = bus_talk; bus_devops.listen_byte = bus_byte; bus_devops.unlisten = bus_unlisten;
    B.dev.ops = &bus_devops; B.dev.trace = bus_cmd; B.dev.atn_end = bus_atn_end;
    bus_sniff_reset();
    B.reset_prev = bus_reset();
}

/* --- browse mode -> the emulated 1541 ------------------------------------------ */
#ifndef BUS_EV_LOG
#define BUS_EV_LOG(s) do { } while (0)
#endif
static FF_C1 void bus_go_online(void)
{
    BUS_EV_LOG("online");
    drive_load(&bus_booted);                          /* a freshly booted 1541 ... */
    bus_drive_gate = D.gate;                          /* (its gate, restored with it; to the pins below) */
    drive_insert(&bus_disk);                          /* ... with the image in it */
    if (B.virtual_disk && B.replay.n) {
        /* replay the conversation into the DOS, off the bus; the real bus held "busy"
         * (DATA low): a C64 that starts a command meanwhile waits [S14 part 4] */
        static iech_t h;
        bus_out(IECD_DATA);
        memset(&h, 0, sizeof h); h.log = &B.replay; h.e.dev = bus_device;
        bus_replaying = 1; bus_replay_lines = 0;
        uint32_t limit = D.cycles + 5000000u;
        while (!h.done && (int32_t)(limit - D.cycles) > 0) {
            uint8_t ctl = iech_poll(&h, D.cycles, (uint8_t)(bus_gate_pulls(bus_replay_lines & IECD_ATN) | bus_replay_lines));
            bus_replay_lines = ctl;
            drive_run(D.cycles + 1);
        }
        bus_replaying = 0; bus_replay_lines = 0;
        if (h.done) B.replays_ok++; else B.replays_late++;
        B.replay.n = 0;
        PLAT_RESYNC();                                /* real time starts again from now */
    } else PLAT_RESYNC();
    bus_out(0);                                       /* browse mode's pulls (and the replay's hold) off: */
    PLAT_IEC_GATE(D.gate);                            /* the drive's own on the bus */
    B.online = 1; B.pending_mount = 0; B.pending_unmount = 0; B.mounts++;
    bus_sniff_reset();
}

static FF_C1 void bus_go_offline(void)
{
    BUS_EV_LOG("offline");
    bus_out(0); bus_drive_pulls = 0;
    B.online = 0; B.pending_unmount = 0; B.unmounts++;
    if (!B.virtual_disk) st_flush_();                /* written tracks back to the card first */
    B.virtual_disk = 0; B.mounted[0] = 0; B.prg[0] = 0;
    B.dev.pc = B.dev.rpc = B.dev.tpc = 0; B.dev.listening = B.dev.talking = 0; B.dev.atn_prev = 0;
    br_error(&B.br, 0, " OK");
}

/* --- disk swap (F9): the sensor sequence of host/f4c.c, in drive time ---------- */
#define US2CYC(us) ((uint32_t)(us))                   /* the 1541 runs at 1 MHz [S7] */
static FF_C1 void bus_swap_step(void)
{
    uint32_t now = D.cycles;
    switch (B.sw) {
    case SW_NONE:
        if (!B.swap_req) return;
        B.swap_req = 0; B.sw_out_ready = 0; B.sw_in_ready = 0;
        drive_wps(0); B.sw = SW_OUT; B.sw_at = now;           /* the disk slides out past the light */
        return;
    case SW_OUT:
        if (now - B.sw_at < US2CYC(200000)) return;
        drive_insert(0); drive_wps(1); B.sw = SW_EMPTY; B.sw_at = now;
        B.sw_out_ready = 1;                                   /* core 0: write back, load the new one */
        return;
    case SW_EMPTY:
        if (now - B.sw_at < US2CYC(1000000) || !B.sw_in_ready) return;
        drive_wps(0); B.sw = SW_IN; B.sw_at = now;            /* the new disk slides in */
        return;
    case SW_IN:
        if (now - B.sw_at < US2CYC(200000)) return;
        if (B.sw_ok) { drive_insert(&bus_disk); strncpy(B.mounted, B.swap_path, BR_PATH - 1); }
        drive_wps(1); B.sw = SW_SETTLE; B.sw_at = now; B.swaps++;
        return;
    case SW_SETTLE:
        if (now - B.sw_at >= US2CYC(200000)) B.sw = SW_NONE;
        return;
    }
}

/* --- C64 reset ---------------------------------------------------------------- */
/* BUS_RESET_EDGE(): the platform's latch of a RESET pulse since the last look, so a pulse shorter than a drive run
 * still counts (first board, 2026-11-07: the Ultimate's reset fell between two looks; the emulated drive kept running
 * a demo's loader and the C64's next OPEN waited for ever) */
/* BUS_TX_ERR(): the platform may note the moment browse mode's talker gave up on a byte (no acknowledge) */
#ifndef BUS_TX_ERR
#define BUS_TX_ERR() do { } while (0)
#endif
#ifndef BUS_RESET_EDGE
#define BUS_RESET_EDGE() 0
#endif
static FF_C1 void bus_check_reset(void)
{
    int edge = BUS_RESET_EDGE(), r = bus_reset();
    if ((r || edge) && (!B.reset_prev || edge)) {
        B.resets++;
        if (B.online && !B.knob_disk && B.sw == SW_NONE) {
            /* a disk the browser put in comes out with a reset, and browse mode goes to the root: LOAD"*",8 after a
             * reset is the browser again, which reopens on that disk (2026-11-07). The knob's disks stay in */
            bus_go_offline();
            B.pending_mount = 0;
            B.br.cwd[0] = '/'; B.br.cwd[1] = 0; B.br.cfg_read = 0;
            br_error(&B.br, 73, "PICOD64 BROWSE");
        } else if (B.online) {                        /* a real 1541 resets with the C64 */
            drive_load(&bus_booted);
            PLAT_IEC_GATE(D.gate);
            drive_insert(B.sw == SW_EMPTY ? 0 : &bus_disk);
            PLAT_RESYNC();
        } else {
            B.dev.pc = B.dev.rpc = B.dev.tpc = 0; B.dev.listening = B.dev.talking = 0;
            B.br.bsa = -1; B.br.blen = 0;
            /* back to the card's root, so LOAD"*",8 after a reset is always the browser (2026-11-07); the
             * browser loses nothing: its next XR reads the folder and entry saved on the card again */
            B.br.cwd[0] = '/'; B.br.cwd[1] = 0; B.br.cfg_read = 0;
            br_error(&B.br, 73, "PICOD64 BROWSE");
            bus_out(0);
        }
    }
    B.reset_prev = r;
}

/* --- the built-in browser's turbo ------------------------------------------------
 * No drive code: we are the far end of the cable. The file is already in RAM, so nothing
 * interrupts it. With the screen blanked and interrupts off, for each byte the C64 pulses DATA
 * low (c0) and reads $DD00 at c0+16, 24, 32, 40 cycles; we hold each of 4 bit pairs for 8 us,
 * centred on its read, so PAL's and NTSC's clocks (+-3 %) stay well inside. Low bits first: DATA
 * then CLK (released = 1). ATN is not used, so another drive on the bus sleeps through it. After
 * the file, one XOR byte: on a mismatch the browser does a plain LOAD. ATN aborts.
 * fw/browser/browser.asm has the C64's side. */
#define TURBO_PAIR0_US 12
#define TURBO_END_US   44
#define TURBO_NEXT_US  47
static struct { long i; uint32_t t0; uint8_t phase, x; } bus_tt;
static FF_C1 uint8_t bus_turbo(uint32_t now, uint8_t lines)
{
    if (lines & IECD_ATN) { if (bus_tt.i) B.turbo_atn++; B.br.turbo_go = 0; B.br.turbo_len = 0; bus_tt.phase = 0; B.turbo_done = 1; return 0; }
    long n = B.br.turbo_len - 2;
    if (bus_tt.phase == 2) {                         /* first DATA released: it may still be low from our */
        if (!(lines & IECD_DATA)) bus_tt.phase = 0;  /* own acknowledge under ATN, which is no request */
        return 0;
    }
    if (bus_tt.phase == 0) {
        if (lines & IECD_DATA) { bus_tt.t0 = now; bus_tt.phase = 1; }
        return 0;
    }
    uint32_t dt = now - bus_tt.t0;
    if (dt < TURBO_PAIR0_US) return 0;
    uint8_t byte = bus_tt.i < n ? B.br.buf[2 + bus_tt.i] : bus_tt.x;
#ifdef FW_TURBO_FAULT
    if (FW_TURBO_FAULT && bus_tt.i == n / 2) byte ^= 1;
#endif
    if (dt < TURBO_END_US) {
        uint8_t pair = (uint8_t)((byte >> (2 * ((dt - TURBO_PAIR0_US) / 8))) & 3);
        return (uint8_t)(((pair & 2) ? 0 : IECD_DATA) | ((pair & 1) ? 0 : IECD_CLK));
    }
    if (dt < TURBO_NEXT_US) return 0;                /* a gap: our own pull's release is not a request */
    if (bus_tt.i < n) bus_tt.x ^= B.br.buf[2 + bus_tt.i];
    bus_tt.i++;
    bus_tt.phase = 0;
    if (bus_tt.i > n) { B.br.turbo_go = 0; B.br.turbo_len = 0; B.turbos++; B.turbo_done = 1; }
    return 0;
}

/* --- core 1's loop body: call for ever ------------------------------------------ */
static FF_C1 void bus_poll(void)
{
    if (bus_track_ready) { bus_track_ready = 0; if (B.online) drive_retrack(); }
    bus_check_reset();
    if (!B.online) {
        uint8_t lines = bus_in();
        if (B.br.turbo_go) {
            if (!B.turbo_on) { B.turbo_on = 1; bus_tt.i = 0; bus_tt.x = 0; bus_tt.phase = 2; B.turbo_starts++; }
            if (!(lines & IECD_ATN) || B.turbo_on == 2) {
                B.turbo_on = 2;                       /* (armed under ATN: it starts when ATN ends) */
                bus_out(bus_turbo(bus_us(), lines));
                if (!B.br.turbo_go) B.turbo_on = 0;
                return;
            }
        }
        /* a file out of a mounted D64: the disk goes back in. Also after a failed XM */
        if (B.br.turbo_remount && !B.br.turbo_go && !B.br.turbo_len && B.br.img[0] && !(lines & IECD_ATN)
            && !B.dev.listening && !B.dev.talking) {
            B.br.turbo_remount = 0; B.turbo_done = 0;
            bfs_mount(0, B.br.img);
        }
        uint32_t err0 = B.dev.errors;
        uint8_t out = iec_poll(&B.dev, bus_us(), lines);
        if (B.dev.errors != err0) BUS_TX_ERR();
        bus_out(out);
        if (B.ui_mount_req && !(lines & IECD_ATN) && !B.dev.listening && !B.dev.talking && !B.pending_mount) {
            B.ui_mount_req = 0;
            const char *sl = strrchr(B.ui_mount_path, '/');
            brx_cfg_read(&B.br);                          /* (first: it sets the folder and entry from the card) */
            if (B.ui_mount_prg) {                          /* a program: its folder's PRGs as a disk, it first */
                B.ui_mount_prg = 0;
                if (sl) {
                    char d[BR_PATH]; size_t l = (size_t)(sl - B.ui_mount_path);
                    memcpy(d, B.ui_mount_path, l ? l : 1); d[l ? l : 1] = 0;
                    if (bfs_mount_prg(0, d, sl + 1)) {
                        strcpy(B.br.cwd, d); B.knob_disk = 1;
                        strncpy(B.br.sel, sl + 1, BR_NAME - 1); B.br.sel[BR_NAME - 1] = 0; brx_cfg_note(&B.br);
                    }
                }
            } else if (bfs_mount(0, B.ui_mount_path) && (B.knob_disk = 1) && sl) {     /* browse mode follows to the image's folder */
                size_t l = (size_t)(sl - B.ui_mount_path);
                memcpy(B.br.cwd, B.ui_mount_path, l ? l : 1); B.br.cwd[l ? l : 1] = 0;
                brx_cfg_note(&B.br);                      /* the browser reopens here (bfs_mount set the entry) */
            }
        }
        if (B.pending_mount && !(lines & IECD_ATN)) bus_go_online();
        return;
    }
    drive_run(D.cycles + 1000);                       /* ~1 ms of the drive, in lockstep */
    BUS_RUN_LEAD();                                   /* (the board: never far ahead of real time) */
    bus_swap_step();
    /* back to browsing only between the drive's transactions: ATN released and the DOS neither listener nor talker
     * (LSNACT $79, TLKACT $7A [S10]). The sniffer sees CD_ on core 0 a few ms late, and "ATN released" alone then fell
     * inside the browser's next command, after its TALK: browse mode never saw the TALK, the C64 waited for ever
     * holding DATA (RETURN at once after opening a D64 locked the browser, 2026-11-08). "All lines released"
     * isn't idle either: they all are, briefly, in every byte's ready-to-send and in the turnaround */
    if (B.pending_unmount && !(bus_in() & IECD_ATN) && B.sw == SW_NONE && !D.ram[0x79] && !D.ram[0x7A]) bus_go_offline();
}
#endif
