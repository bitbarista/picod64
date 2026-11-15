/* -------------------------------------------------------------------------
 * The board's firmware on the host: fw/bus.h (core 1) and fw/svc.c +
 * fw/storage.c (core 0) against the real C64 KERNAL (host/c64.c), with a FAT
 * image file as the SD card (fw/diskio_host.c). host/f6.c's checks, through
 * the code that will run on the board.
 *
 * Time: the drive's PLAT_SYNC / PLAT_LEAD step the C64 up to the drive's own
 * time, so the two machines stay in lockstep as on the board. While the drive
 * replays (off the bus, as fast as it can) the C64 runs on at REPLAY_SPEED of
 * the drive's pace, seeing the bus held "busy", as the real one would.
 *
 *   fwtwin <dos.rom> <kernal> <basic> <card.img> <outdir> command...
 * Commands as host/f6.c (load:, cmd:, cmdhex:, err:, type:, wait:, screen:,
 * peek:) and:  reset:  pulse the C64's RESET;  ui:mount:/path  ui:next:/path
 * ui:unmount:  as the knob would;  state:
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "c64.h"
#include "../fw/bus_plat.h"
#define FW_TRACE(...) do { if (getenv("FWTRACE")) printf(__VA_ARGS__); } while (0)

#define FW_TURBO_FAULT (getenv("FWTBAD") != 0)         /* one wrong byte: the checksum must catch it */
static uint8_t c64_low, host_pulls;                  /* what the C64 / "core 1" pull */
static int online_now(void);
static uint64_t c64_base; static uint32_t drv_base;   /* drive cycle drv_base = C64 cycle c64_base */
#define REPLAY_SPEED 4                               /* [A] the board replays ~4x real time: measure (F5) */
static void c64_run_to(uint32_t drive_c);
static void host_resync(void);
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   c64_run_to(c)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   c64_run_to(c)
/* the drive's ATN gate acts at once, as the 1541's XOR gate (and the board's PIO) */
/* The bus as it is in time. Whole C64 instructions used to run past the drive's time, so the drive saw C64 writes
 * before they happened, and the C64 read stale lines: harmless to most, but JiffyDOS's windows are 1-2
 * cycles (AUDIT R36: 1,609 of 25,000 bytes wrong). Now each write has its time, the C64 waits to reach the port
 * (c64_iec_after), and a write takes FWDR cycles to reach the drive: a CIA write is done at the END of its
 * cycle, then the 7406s and the line. host/fwtwin.sh measures the range that works. */
#define TW_NOW() (c64_base + (uint64_t)(uint32_t)(D.cycles - drv_base) * C64_HZ / 1000000u)
static uint8_t tw_c64_seen(uint64_t now);
#define PLAT_IEC_IN()     ((uint8_t)(bus_replaying ? (bus_replay_lines | bus_gate_pulls(bus_replay_lines & 1)) : (tw_c64_seen(TW_NOW()) | bus_gate_pulls(tw_c64_seen(TW_NOW()) & 1))))
#define PLAT_IEC_OUT(p)   (bus_drive_pulls = (uint8_t)(p))
#define PLAT_IEC_GATE(g)  (bus_drive_gate = (uint8_t)(g))
#define PLAT_RESYNC()     host_resync()
/* FWHOOK=hex: print the drive's A, X, Y and $70 each time it finishes the instruction before that address */
static unsigned hook_pc = 0x10000;
static unsigned hook_pc2 = 0x10000, hook_from;   /* FWHOOK2: a second address; FWHOOKFROM: only after that drive cycle */
uint16_t c64_pc(void);
/* FWRAMDUMP=prefix: at each FWHOOK hit, the C64's 64 KB RAM to prefix_NN.bin (compared with VICE's, 2026-11-07) */
uint8_t *c64_ram(void);
static int ramdump_n;
static void ramdump(void)
{
    const char *p = getenv("FWRAMDUMP"); if (!p) return;
    char fn[512]; snprintf(fn, sizeof fn, "%s_%02d.bin", p, ramdump_n++);
    FILE *f = fopen(fn, "wb"); if (f) { fwrite(c64_ram(), 1, 65536, f); fclose(f); }
}
#define DRIVE_INSN_HOOK(c, cyc) do { if ((c)->pc == hook_pc && (unsigned)(cyc) >= hook_from) ramdump(); \
    if (((c)->pc == hook_pc || (c)->pc == hook_pc2) && (unsigned)(cyc) >= hook_from) \
    printf("    [drive @%04X cyc %u A %02X X %02X Y %02X P %02X S %02X $70 %02X | via1 ifr %02X ier %02X pcr %02X acr %02X | via2 ifr %02X ier %02X pcr %02X acr %02X | c64 $%04X]\n", (c)->pc, (unsigned)(cyc), (c)->a, (c)->x, (c)->y, (c)->p, (c)->s, D.ram[0x70], \
           D.via1.ifr, D.via1.ier, D.via1.pcr, D.via1.acr, D.via2.ifr, D.via2.ier, D.via2.pcr, D.via2.acr, c64_pc()); } while (0)
static int via1_log;                                 /* FWVIA1=1: print the drive's writes to VIA1 once its code at $0335-$0350 runs */
#define PLAT_VIA1_WR(a, v, c) do { if (via1_log) printf("    [via1 %u %X %02X]\n", (unsigned)(c), (unsigned)((a) & 15), (unsigned)(v)); } while (0)
#include "../core/drive1541.h"

/* --- the storage side, called directly (core 0 on the board) --------------- */
#include "../fw/storage.h"
#include "../fw/svc.h"
int diskio_host_open(const char *path);
static uint8_t bus_gcr_[1];                          /* (bus.h owns the real buffers) */
static long st_prep_(const char *p, const char *f) { return st_prep(p, f); }
static int st_find_(const char *p, const char *n) { return st_find(p, n); }
static int st_newdisk_(const char *p, const uint8_t *n, int nl, uint8_t i1, uint8_t i2) { return svc_newdisk(p, n, nl, i1, i2); }
static int st_list_(const char *p, int i, fsent_t *e) { return st_list(p, i, e); }
static long st_read_(const char *p, uint8_t *b, long m) { return st_read(p, b, m); }
static int st_write_(const char *p, const uint8_t *b, long n) { int r = st_write(p, b, n); printf("  [card: save %s %ld bytes %s]\n", p, n, r ? "OK" : "FAILED"); return r; }
static int st_load_(const char *p);
static int st_virtual_(const char *d, const char *f);
static void st_flush_(void);
static int reset_low;
static uint32_t bus_us(void) { return (uint32_t)(c64_cycles() * 1000000u / C64_HZ); }
static uint8_t bus_in(void) { return (uint8_t)(c64_low | host_pulls | (online_now() ? bus_gate_pulls(c64_low & 1) : 0)); }
static void bus_out(uint8_t p) { host_pulls = p; }
static int bus_reset(void) { return reset_low; }
#include "../fw/bus.h"
#include "../fw/sound.h"
static int online_now(void) { return B.online; }
static int st_load_(const char *p) { int r = svc_load(p, bus_gcr, &bus_disk, bus_frames); printf("  [card: load %s %s]\n", p, r ? "OK" : "FAILED"); return r; }
static int st_virtual_(const char *d, const char *f)
{
    int r = svc_virtual(d, f, bus_gcr, &bus_disk, bus_frames, bus_gcr + D64_BYTES, (long)(D64_GCR_BYTES - D64_BYTES));
    printf("  [card: virtual D64 of %s (%s first) %s]\n", d, f, r ? "OK" : "FAILED");
    return r;
}
static void st_flush_(void)
{
    int n = svc_flush(B.mounted, &bus_disk, bus_dirty);
    if (n) printf("  [card: %d sectors written back to %s]\n", n, B.mounted);
}

/* --- lockstep ---------------------------------------------------------------- */
static int sync_log;                                  /* FWSYNC=n: the drive's sync points, once the turbo runs */
static int turbo_log;                                 /* FWTURBO=n: log n line changes once the drive runs $0300-$07FF code */
#define CQ 32
static uint8_t cq[CQ]; static uint64_t cqt[CQ]; static unsigned cqn; static int c_lat = -1;
static uint8_t tw_c64_seen(uint64_t now)
{
    if (c_lat < 0) { const char *e = getenv("FWDR"); c_lat = e ? atoi(e) : 1; }
    for (unsigned i = cqn; i-- > 0 && cqn - i <= CQ; )
        if (cqt[i % CQ] + (uint64_t)c_lat <= now) return cq[i % CQ];
    return cqn ? cq[(cqn - CQ) % CQ] : c64_low;
}
/* FWSND=file: the drive's sounds for this run, a duty per sample (fw/sound.h, as fw/pico_main.c's interrupt) */
static snd_t snd; static FILE *snd_f; static uint64_t snd_next; static int snd_init_;
static void snd_run(void)
{
    if (!snd_init_) { snd_init_ = 1; if (getenv("FWSND")) snd_f = fopen(getenv("FWSND"), "wb"); }
    if (!snd_f) return;
    uint64_t us = c64_cycles() * 1000000u / C64_HZ;
    while (snd_next <= us) {
        snd_next += 64;
        fputc(snd_tick(&snd, D.phase, D.ht, B.online && (via_pb(&D.via2) & 0x04), B.online, B.dev.listening || B.dev.talking, 1), snd_f);
    }
}
static void c64_one(void)
{
    snd_run();
    uint8_t lines = (uint8_t)(c64_low | host_pulls | (B.online && !bus_replaying ? bus_gate_pulls(c64_low & 1) : 0));
    c64_iec_in(lines);
    c64_step();
    { uint8_t n = c64_iec_out(); if (n != c64_low) { cq[cqn % CQ] = n; cqt[cqn % CQ] = c64_cycles(); cqn++; } c64_low = n; }
    if (getenv("FWTURBO") && B.online) {
        static uint8_t pl = 0xFF; static int armed;
        if (!armed && getenv("FWARMC64") && c64_pc() == (uint16_t)strtoul(getenv("FWARMC64"), 0, 16)) { armed = 1; turbo_log = atoi(getenv("FWTURBO")); if (getenv("FWSYNC")) sync_log = atoi(getenv("FWSYNC")); }
        if (!armed && !getenv("FWARMC64") && CPU.pc >= 0x0300 && CPU.pc < 0x0800) { armed = 1; turbo_log = atoi(getenv("FWTURBO")); if (getenv("FWSYNC")) sync_log = atoi(getenv("FWSYNC")); }
        uint8_t now = (uint8_t)(c64_low | bus_drive_pulls);
        if (armed && turbo_log > 0 && now != pl) {
            turbo_log--;
            printf("    [t %8llu c64 pc $%04X pulls %c%c%c | drive cyc %u pc $%04X pulls %c%c | lines %c%c%c]\n",
                   (unsigned long long)c64_cycles(), c64_pc(), c64_low & 1 ? 'A' : '-', c64_low & 2 ? 'C' : '-', c64_low & 4 ? 'D' : '-',
                   D.cycles, CPU.pc, bus_drive_pulls & 2 ? 'C' : '-', bus_drive_pulls & 4 ? 'D' : '-',
                   now & 1 ? 'A' : '-', now & 2 ? 'C' : '-', now & 4 ? 'D' : '-');
        }
        pl = now;
    }
    if (B.online && !bus_replaying)
        bus_sniff_feed(bus_us(), (uint8_t)(c64_low | bus_gate_pulls(c64_low & 1)));
}
static int c64_live;                                  /* the C64 exists (not during the drive's boot) */
static void c64_run_to(uint32_t c)
{
    if (sync_log > 0 && c64_live && B.online) {
        sync_log--;
        printf("      sync c %u pc $%04X in %c%c%c drive pulls %c%c c64 t %llu\n", c, CPU.pc,
               (c64_low | bus_drive_pulls) & 1 ? 'A' : '-', (c64_low | bus_drive_pulls) & 2 ? 'C' : '-', (c64_low | bus_drive_pulls) & 4 ? 'D' : '-',
               bus_drive_pulls & 2 ? 'C' : '-', bus_drive_pulls & 4 ? 'D' : '-', (unsigned long long)c64_cycles());
    }
    if (!c64_live || (!B.online && !bus_replaying)) return;
    uint64_t dc = (uint32_t)(c - drv_base);
    if (bus_replaying) dc /= REPLAY_SPEED;
    uint64_t target = c64_base + dc * C64_HZ / 1000000u;
    while (c64_cycles() < target && !c64_iec_after(target)) c64_one();
}
static void host_resync(void) { drv_base = D.cycles; c64_base = c64_cycles(); }

/* one step of everything: online, the drive leads (its syncs step the C64);
 * offline, the C64 steps and browse mode polls */
static void step(void)
{
    {   /* FWPC=n: where both CPUs are, every n drive steps (to see a hang) */
        static long every = -1, k; if (every < 0) every = getenv("FWPC") ? atol(getenv("FWPC")) : 0;
        if (every && ++k % every == 0) { printf("    [pc: c64 $%04X t %.3f s | drive $%04X cyc %u]\n", c64_pc(), c64_cycles() / (double)C64_HZ, CPU.pc, D.cycles); fflush(stdout); }
    }
    if (getenv("FWVIA1") && !via1_log && B.online && CPU.pc >= 0x0335 && CPU.pc <= 0x0350) via1_log = 1;
    if (B.online) {
        bus_poll();                                   /* runs ~1000 drive cycles; card work as needed */
        c64_run_to(D.cycles);                         /* the C64 keeps time even when the drive's code never touches the bus (a demo's loader, 2026-11-06) */
        /* over 35 tracks: core 0 brings the track in once the head has stayed (8 ms, as pico_main.c), and FWTRKMS more as if the card took that long */
        static uint64_t since; static int on = -1, trk_n;
        if (B.sw == SW_NONE && !B.virtual_disk && st_tracks() > D64_TRACKS && !(D.ht & 1) && !bus_disk.gcr[D.ht]) {
            const char *dl = getenv("FWTRKMS");
            uint64_t delay = (uint64_t)((8.0 + (dl ? atof(dl) : 0)) * C64_HZ / 1000.0);
            if (on != D.ht) { on = D.ht; since = c64_cycles(); }
            if (c64_cycles() - since >= delay && svc_track_poll(B.mounted, &bus_disk, bus_dirty, &D.ht)) { bus_track_ready = 1; printf("  [track %d in, %d so far]\n", D.ht / 2 + 1, ++trk_n); }
        } else on = -1;
        if (B.sw == SW_EMPTY && B.sw_out_ready && !B.sw_in_ready) {   /* core 0: write back, load the next */
            st_flush_();
            B.sw_ok = svc_load(B.swap_path, bus_gcr, &bus_disk, bus_frames);
            printf("  [swap: %s %s]\n", B.swap_path, B.sw_ok ? "loaded" : "FAILED");
            B.sw_in_ready = 1;
        }
    } else {
        c64_one();
        int was = B.online;
        bus_poll();
        if (!was && B.online) printf("  [mounted %s: the emulated 1541 has the bus]\n", B.virtual_disk ? "(virtual)" : B.mounted);
    }
    static int pw = 0;
    if (pw && !B.online) printf("  [unmounted: browse mode has the bus]\n");
    pw = B.online;
}

static int run_until_pc(uint16_t pc, uint64_t limit)
{
    uint64_t end = c64_cycles() + limit;
    while (c64_pc() != pc) {
        if (c64_cycles() > end || CPU.jammed) return 0;
        step();
    }
    return 1;
}
static void advance(double secs) { uint64_t end = c64_cycles() + (uint64_t)(secs * C64_HZ); while (c64_cycles() < end) step(); }

/* --- the driver at $C000 (host/f6.c) ----------------------------------------- */
static uint8_t *code; static uint16_t pc;
static void op(int n, ...) { va_list ap; va_start(ap, n); while (n--) code[pc++ & 0xFFFF] = (uint8_t)va_arg(ap, int); va_end(ap); }
#define JSR(a) op(3, 0x20, (a) & 0xFF, (a) >> 8)
enum { SETNAM = 0xFFBD, SETLFS = 0xFFBA, LOAD = 0xFFD5, SAVE = 0xFFD8, OPEN = 0xFFC0, CLOSE = 0xFFC3, CHKIN = 0xFFC6, CHRIN = 0xFFCF, CLRCHN = 0xFFCC };
static uint16_t build(uint8_t *m, char kind, size_t len)
{
    code = m; pc = 0xC000;
    op(6, 0xA9, (int)len, 0xA2, 0x00, 0xA0, 0xC1); JSR(SETNAM);
    if (kind == 'L') { op(6, 0xA9, 1, 0xA2, 8, 0xA0, 0); JSR(SETLFS); op(6, 0xA9, 0, 0xA2, 0x00, 0xA0, 0x10); JSR(LOAD); }
    else if (kind == 'S') {                            /* SAVE $2000-$2400 */
        op(6, 0xA9, 1, 0xA2, 8, 0xA0, 0); JSR(SETLFS);
        op(8, 0xA9, 0x00, 0x85, 0xFB, 0xA9, 0x20, 0x85, 0xFC);
        op(6, 0xA9, 0xFB, 0xA2, 0x00, 0xA0, 0x24); JSR(SAVE);
    } else if (kind == 'C') { op(6, 0xA9, 15, 0xA2, 8, 0xA0, 15); JSR(SETLFS); JSR(OPEN); op(2, 0xA9, 15); JSR(CLOSE); }
    else {
        op(6, 0xA9, 15, 0xA2, 8, 0xA0, 15); JSR(SETLFS); JSR(OPEN);
        op(2, 0xA2, 15); JSR(CHKIN); op(2, 0xA0, 0x00);
        uint16_t loop = pc;
        JSR(CHRIN); op(3, 0x99, 0x00, 0xC3); op(1, 0xC8);
        op(2, 0xA5, 0x90); op(2, 0xD0, 0x04);
        op(2, 0xC0, 0x40); op(2, 0x90, (uint8_t)(loop - (pc + 2)));
        op(3, 0x8C, 0x05, 0xC2);
        JSR(CLRCHN); op(2, 0xA9, 15); JSR(CLOSE);
    }
    op(6, 0x8E, 0x00, 0xC2, 0x8C, 0x01, 0xC2);
    op(3, 0x8D, 0x02, 0xC2); op(2, 0x08, 0x68); op(3, 0x8D, 0x03, 0xC2);
    op(5, 0xA5, 0x90, 0x8D, 0x04, 0xC2);
    uint16_t done = pc; op(3, 0x4C, done & 0xFF, done >> 8);
    return done;
}
#define READY_LOOP 0xE5CD

static void type_keys(const char *t)
{
    uint8_t *m = c64_ram();
    while (*t) {
        uint64_t limit = c64_cycles() + 20u * C64_HZ;
        while (m[0xC6] && c64_cycles() < limit) step();
        /* The KERNAL scans the keys in its interrupt: with the I flag left set a real C64 hears nothing. */
        while (c64_irq_off() && c64_cycles() < limit) step();
        if (c64_irq_off()) { printf("  [KEYBOARD DEAD: interrupts off for 20 s, keys not typed]\n"); return; }
        int n = 0;
        while (*t && n < 10) {
            char c = *t++;
            uint8_t k = c == '|' ? 0x0D : c == '~' ? 0x11 : c == '^' ? 0x91 : c == '<' ? 0x14 : (c >= 'a' && c <= 'z') ? (uint8_t)(c - 32) : (uint8_t)c;
            m[0x277 + n++] = k;
        }
        m[0xC6] = (uint8_t)n;
    }
    advance(0.2);
}
static void screen_dump(void)
{
    const uint8_t *m = c64_ram(); uint16_t base = c64_screen();
    printf("    +----------------------------------------+\n");
    for (int r = 0; r < 25; r++) {
        printf("    |");
        for (int c = 0; c < 40; c++) {
            uint8_t v = m[(uint16_t)(base + r * 40 + c)] & 0x7F;
            if (c64_vic(0x18) & 2) {                  /* the upper and lower case set */
                if (v >= 1 && v <= 26) { putchar('a' + v - 1); continue; }
                if (v >= 65 && v <= 90) { putchar(v); continue; }
                if (v == 0x64) { putchar('_'); continue; }
            }
            putchar(v == 0 ? '@' : v < 0x1B ? 'A' + v - 1 : v >= 0x20 && v < 0x40 ? v : v == 0x1E ? '^' : v == 0x1F ? '<' : '.');
        }
        printf("|\n");
    }
    printf("    +----------------------------------------+\n");
}

static uint8_t dos[16384], kernal[8192], basic[8192];
static void load_file(const char *path, uint8_t *b, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f || fread(b, 1, n, f) != n) { perror(path); exit(2); }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (getenv("FWHOOK")) hook_pc = (unsigned)strtoul(getenv("FWHOOK"), 0, 16);
    if (getenv("FWHOOK2")) hook_pc2 = (unsigned)strtoul(getenv("FWHOOK2"), 0, 16);
    if (getenv("FWHOOKFROM")) hook_from = (unsigned)strtoul(getenv("FWHOOKFROM"), 0, 10);
    if (argc < 7) { fprintf(stderr, "usage: fwtwin <dos> <kernal> <basic> <card.img> <outdir> command...\n"); return 2; }
    load_file(argv[1], dos, sizeof dos); load_file(argv[2], kernal, sizeof kernal); load_file(argv[3], basic, sizeof basic);
    if (!diskio_host_open(argv[4]) || !st_init()) { fprintf(stderr, "card image: mount failed\n"); return 2; }
    (void)bus_gcr_;
    if (getenv("FWDEV")) bus_device = (uint8_t)atoi(getenv("FWDEV"));   /* as the board's flash setting */
    bus_init(dos);
    svc_set_builtin(browser_prg, (long)sizeof browser_prg);
    c64_init(kernal, basic); c64_live = 1;
    if (!run_until_pc(READY_LOOP, 5000000)) { printf("C64 never reached READY\n"); return 1; }
    int fail = 0;
    for (int i = 6; i < argc; i++) {
        uint8_t *m = c64_ram();
        char kind = 'L';
        const char *a = argv[i];
        if (!strncmp(a, "type:", 5)) { type_keys(a + 5); printf("%s\n", a); continue; }
        if (!strncmp(a, "wait:", 5)) { advance(atof(a + 5)); continue; }
        if (!strncmp(a, "joy:", 4)) { c64_joy2((uint8_t)strtol(a + 4, 0, 16)); continue; }   /* joy:HEX -- bits held on joystick 2 */
        if (!strcmp(a, "screen:")) { screen_dump(); continue; }
        if (!strcmp(a, "reset:")) { reset_low = 1; advance(0.01); reset_low = 0; printf("reset: (resets %u)\n", B.resets); c64_init(kernal, basic); run_until_pc(READY_LOOP, 5000000); continue; }
        if (!strncmp(a, "ui:mount:", 9)) { strncpy(B.ui_mount_path, a + 9, BR_PATH - 1); B.ui_mount_req = 1; advance(0.3); printf("%s\n", a); continue; }
        if (!strncmp(a, "ui:next:", 8)) { strncpy(B.swap_path, a + 8, BR_PATH - 1); B.swap_req = 1; advance(2.5); printf("%s (swaps %u)\n", a, B.swaps); continue; }
        if (!strncmp(a, "ui:sound:", 9)) { int v = a[9] == '1'; svc_sound_set(v); B.br.cfg_sound = (uint8_t)v; printf("%s (now %d)\n", a, svc_sound_get()); continue; }   /* as fw/pico_main.c ui_sound() */
        if (!strcmp(a, "ui:unmount:")) { B.pending_unmount = 1; advance(0.3); printf("%s\n", a); continue; }
        if (!strcmp(a, "state:")) { printf("  [state: online %d mounted \"%s\" mounts %u unmounts %u swaps %u resets %u replays %u/%u turbos %u next %d]\n", B.online, B.mounted, B.mounts, B.unmounts, B.swaps, B.resets, B.replays_ok, B.replays_late, B.turbos, B.next_req); continue; }
        if (!strncmp(a, "dmem:", 5)) {                 /* dmem:ADDR:LEN:FILE -- the drive's RAM to a file */
            unsigned ad = 0, n = 0; char fn[256] = ""; sscanf(a + 5, "%x:%u:%255s", &ad, &n, fn);
            FILE *f = fopen(fn, "wb"); for (unsigned k = 0; k < n; k++) fputc(D.ram[(ad + k) & 0x7FF], f); fclose(f);
            printf("  [drive $%04X+%u -> %s]\n", ad, n, fn); continue;
        }
        if (!strncmp(a, "c64mem:", 7)) {               /* c64mem:ADDR:LEN -- the C64's RAM, hex */
            unsigned ad = 0, n = 16; sscanf(a + 7, "%x:%u", &ad, &n);
            printf("  [C64 $%04X:", ad); for (unsigned k = 0; k < n; k++) printf(" %02X", c64_ram()[(ad + k) & 0xFFFF]); printf("]\n");
            continue;
        }
        if (!strncmp(a, "c64dump:", 8)) {              /* c64dump:FILE -- all 64 KB of the C64's RAM, and where its CPU is */
            FILE *f = fopen(a + 8, "wb"); fwrite(c64_ram(), 1, 65536, f); fclose(f);
            printf("  [C64 RAM -> %s; C64 pc $%04X, drive pc $%04X, %.2f s]\n", a + 8, c64_pc(), CPU.pc, (double)c64_cycles() / C64_HZ);
            continue;
        }
        if (!strncmp(a, "shot:", 5)) {                 /* shot:FILE the screen, colours and VIC registers, for host/shot.py */
            FILE *f = fopen(a + 5, "wb"); uint8_t *m = c64_ram(); uint16_t sc = c64_screen();
            fwrite(m + sc, 1, 1000, f); fwrite(c64_colour(), 1, 1000, f);
            fputc(c64_vic(0x18), f); fputc(c64_vic(0x20), f); fputc(c64_vic(0x21), f); fclose(f);
            printf("  [shot %s]\n", a + 5); continue;
        }
        if (!strcmp(a, "bus:")) {                      /* where each side is, and who pulls which line (bit 0 ATN, 1 CLK, 2 DATA) */
            printf("  [bus: C64 pc $%04X pulls %02X; core 1 pulls %02X; drive pc $%04X pulls %02X gate %02X; online %d]\n",
                   c64_pc(), c64_low, host_pulls, CPU.pc, bus_drive_pulls, D.gate, B.online);
            continue;
        }
        if (!strncmp(a, "peek:", 5)) { unsigned ad = (unsigned)strtoul(a + 5, 0, 16); printf("  [drive RAM $%04X = $%02X]\n", ad, D.ram[ad & 0x7FF]); continue; }
        if (!strncmp(a, "load:", 5)) a += 5;
        else if (!strncmp(a, "save:", 5)) { kind = 'S'; a += 5; }
        else if (!strncmp(a, "cmd:", 4)) { kind = 'C'; a += 4; }
        else if (!strncmp(a, "cmdhex:", 7)) {
            static char hx[64]; int hn = 0;
            for (const char *q = a + 7; q[0] && q[1] && hn < 63; q += 2) { unsigned v; sscanf(q, "%2x", &v); hx[hn++] = (char)v; }
            memcpy(m + 0xC100, hx, (size_t)hn);
            uint16_t done = build(m, 'C', (size_t)hn);
            c64_jump(0xC000);
            int ok = run_until_pc(done, 30u * C64_HZ);
            printf("%s: %s\n", argv[i], ok ? "done" : "TIMEOUT");
            c64_jump(READY_LOOP);
            continue;
        }
        else if (!strncmp(a, "err:", 4)) { kind = 'E'; a += 4; }
        size_t len = strlen(a);
        for (size_t k = 0; k < len; k++) m[0xC100 + k] = (uint8_t)(a[k] >= 'a' && a[k] <= 'z' ? a[k] - 32 : a[k]);
        if (kind == 'S') { uint32_t x = 77; for (unsigned k = 0; k < 0x400; k++) { x = x * 1103515245u + 12345u; m[0x2000 + k] = (uint8_t)(x >> 16); } }
        else memset(m + 0x1000, 0, 0x9000);
        memset(m + 0xC200, 0xEE, 6); memset(m + 0xC300, 0, 64);
        uint16_t done = build(m, kind, len);
        uint64_t t0 = c64_cycles();
        c64_jump(0xC000);
        int ok = run_until_pc(done, 300u * C64_HZ);
        unsigned end = m[0xC200] | (m[0xC201] << 8);
        int err = (m[0xC203] & 1) ? m[0xC202] : 0;
        printf("%s: %s", argv[i], ok ? "done" : "TIMEOUT");
        if (kind == 'L') printf(" end=$%04X bytes=%u", end, end >= 0x1000 ? end - 0x1000 : 0);
        printf(" err=%d status=$%02X time=%.2fs\n", err, m[0xC204], (double)(c64_cycles() - t0) / C64_HZ);
        if (kind == 'L' && ok && !err && end > 0x1000) {
            char path[512]; snprintf(path, sizeof path, "%s/%d.bin", argv[5], i - 5);
            FILE *f = fopen(path, "wb"); if (f) { fwrite(m + 0x1000, 1, end - 0x1000, f); fclose(f); }
            if (len == 1 && a[0] == '$') {
                const uint8_t *b = m + 0x1000; int k = 0;
                while (k + 4 < (int)(end - 0x1000) && (b[k] | b[k + 1])) {
                    printf("    %u ", b[k + 2] | (b[k + 3] << 8)); k += 4;
                    while (b[k]) { uint8_t c = b[k++]; putchar(c >= 0xC1 && c <= 0xDA ? c - 0x80 : c == 0x12 ? ' ' : c < 0x20 || c > 0x7E ? '.' : c); }
                    putchar('\n'); k++;
                }
            }
        }
        if (kind == 'E') {
            int n = m[0xC205] == 0xEE ? 0 : m[0xC205];
            printf("    error channel: \"");
            for (int k = 0; k < n; k++) { uint8_t c = m[0xC300 + k]; putchar(c == '\r' ? '|' : c < 0x20 || c > 0x7E ? '.' : c); }
            printf("\"\n");
        }
        if (!ok) fail = 1;
        c64_jump(READY_LOOP);
    }
    if (B.online && !B.virtual_disk) st_flush_();     /* (the board does this when the motor stops) */
    return fail;
}
