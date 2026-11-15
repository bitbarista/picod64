/* -------------------------------------------------------------------------
 * F4c: a C64 (host/c64.c: the real KERNAL on our reference 6502) LOADs from
 * the emulated 1541 running the real DOS [S9], over a shared IEC bus.
 *
 * The bus is wired-AND: a line is low if either side pulls it. The two
 * machines run in step by instruction, whichever is behind in real time
 * going next (C64 at C64_HZ, drive at 1 MHz). ⚠ So each side sees the other
 * up to one instruction (a few us) late: inside the KERNAL protocol's
 * margins [S12 p.365], and no model of the real bus timing (that is F3).
 *
 * Each LOAD runs a small driver at $C000: SETNAM, SETLFS 1,8,0, LOAD to
 * $1000 [S12 p.276-298, KERNAL table]. The bytes loaded are written to
 * <out>/<n>.bin with the end address and error, for f4c.sh to compare.
 *
 *   f4c <dos.rom> <kernal.rom> <basic.rom> <image.d64> <outdir> name...
 * Built twice by f4c.sh: -DREF selects the per-cycle drive model. Their
 * RESULTS must match (data, end, error, time to 10 ms); their bus traces
 * need not: the core sees an ATN edge at its next VIA1 access or sampled
 * instruction end, the reference in the very next cycle (the documented
 * ATN deviation, core/drive1541.h drive_lead), so an ATN interrupt can come
 * one instruction apart.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "c64.h"

static uint8_t drive_low, c64_low;
static uint64_t h = 1469598103934665603ULL;
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   do { } while (0)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     ((uint8_t)(drive_low | c64_low))
#define PLAT_IEC_OUT(p)   (drive_low = (uint8_t)(p))
#define DRIVE_TRACE(a, v, rw) \
    (h = (h ^ (((uint64_t)(DRIVE_NOW) << 32) | ((uint32_t)(a) << 9) | ((uint32_t)(v) << 1) | (rw))) * 1099511628211ULL)
#ifdef REF
#include "ref/drive1541.h"
#else
#include "../core/drive1541.h"
#endif

static uint8_t dos[16384], kernal[8192], basic[8192], d64[D64_BYTES], gcr[D64_GCR_BYTES];
static disk_t disk;
static syncs_t frames[D64_TRACKS];
static uint8_t d64b[D64_BYTES], gcrb[D64_GCR_BYTES];   /* a second disk, for swaps */
static disk_t diskb;
static syncs_t framesb[D64_TRACKS];
static uint64_t drive_base;           /* drive cycles, 64-bit (D.cycles wraps at 2^32) */
static uint32_t drive_last;

static void load(const char *path, uint8_t *b, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f || fread(b, 1, n, f) != n) { perror(path); exit(2); }
    fclose(f);
}

static uint64_t drive_now(void) { drive_base += (uint32_t)(D.cycles - drive_last); drive_last = D.cycles; return drive_base; }

/* one instruction on whichever machine is behind in real time */
static void step(void)
{
    if (c64_cycles() * 1000000u <= drive_now() * (uint64_t)C64_HZ) {
        c64_iec_in((uint8_t)(drive_low | c64_low));
        c64_step();
        c64_low = c64_iec_out();
    } else {
#ifdef REF
        drive_step();
#else
        drive_run(D.cycles + 1);
#endif
    }
}

/* both machines on, for s seconds */
static void advance(double s)
{
    uint64_t end = c64_cycles() + (uint64_t)(s * C64_HZ);
    while (c64_cycles() < end) step();
}

/* Swap disks as a person does [A: timings]: the old disk slides out past the
 * write-protect sensor (0.2 s blocked), none for 1 s, the new one slides in
 * (0.2 s blocked) and rests with its notch open. quiet: switch images with no
 * sensor change at all (a control: the DOS cannot know). */
static void swap_disk(const char *path, int quiet)
{
    load(path, d64b, sizeof d64b);
    d64_to_disk(d64b, gcrb, &diskb, framesb);
    if (quiet) { drive_insert(&diskb); return; }
    drive_wps(0); advance(0.2);
    drive_insert(0); drive_wps(1); advance(1.0);
    drive_wps(0); advance(0.2);
    drive_insert(&diskb); drive_wps(1); advance(0.2);
}

/* run until the C64's PC is `pc`; 0 on timeout (C64 cycles) */
static int run_until_pc(uint16_t pc, uint64_t limit)
{
    uint64_t end = c64_cycles() + limit;
    while (c64_pc() != pc) {
        if (c64_cycles() > end || CPU.jammed) return 0;
        step();
    }
    return 1;
}

/* The driver at $C000, assembled per command. Name at $C100; results at
 * $C200: end (2), A (1), P (1), status $90 (1). Ends in JMP * at `done`. */
static uint8_t *code; static uint16_t pc;
static void op(int n, ...) { va_list ap; va_start(ap, n); while (n--) code[pc++ & 0xFFFF] = (uint8_t)va_arg(ap, int); va_end(ap); }
#define JSR(a) op(3, 0x20, (a) & 0xFF, (a) >> 8)
enum { SETNAM = 0xFFBD, SETLFS = 0xFFBA, LOAD = 0xFFD5, SAVE = 0xFFD8, OPEN = 0xFFC0, CLOSE = 0xFFC3 };

static uint16_t build(uint8_t *m, char kind, size_t len, uint16_t save_end)
{
    code = m; pc = 0xC000;
    op(6, 0xA9, (int)len, 0xA2, 0x00, 0xA0, 0xC1); JSR(SETNAM);                  /* name at $C100 */
    if (kind == 'L') {
        op(6, 0xA9, 1, 0xA2, 8, 0xA0, 0); JSR(SETLFS);                           /* 1,8,0 */
        op(6, 0xA9, 0, 0xA2, 0x00, 0xA0, 0x10); JSR(LOAD);                      /* to $1000 */
    } else if (kind == 'S') {
        op(6, 0xA9, 1, 0xA2, 8, 0xA0, 0); JSR(SETLFS);
        op(8, 0xA9, 0x00, 0x85, 0xFB, 0xA9, 0x20, 0x85, 0xFC);                  /* ($FB) = $2000 */
        op(6, 0xA9, 0xFB, 0xA2, save_end & 0xFF, 0xA0, save_end >> 8); JSR(SAVE);
    } else {                                                                    /* OPEN 15,8,15,"cmd" : CLOSE 15 */
        op(6, 0xA9, 15, 0xA2, 8, 0xA0, 15); JSR(SETLFS);
        JSR(OPEN);
        op(2, 0xA9, 15); JSR(CLOSE);
    }
    op(6, 0x8E, 0x00, 0xC2, 0x8C, 0x01, 0xC2);                                  /* STX/STY $C200 */
    op(3, 0x8D, 0x02, 0xC2); op(2, 0x08, 0x68); op(3, 0x8D, 0x03, 0xC2);        /* A, P */
    op(5, 0xA5, 0x90, 0x8D, 0x04, 0xC2);                                        /* status */
    uint16_t done = pc; op(3, 0x4C, done & 0xFF, done >> 8);
    return done;
}
#define READY_LOOP  0xE5CD    /* KERNAL 901227-03 waiting for a key (as VICE showed, F4a) */

/* Commands:  NAME | load:NAME        LOAD"NAME",8 to $1000 -> <out>/<n>.bin
 *            save:NAME:SIZE:SEED     SAVE SIZE pseudo-random bytes from $2000 -> <out>/<n>.prg (as a PRG file)
 *            cmd:TEXT                OPEN 15,8,15,"TEXT":CLOSE 15
 *            swap:PATH | swapq:PATH  change to that D64: as a person does, or silently (a control)
 * At the end the disk, as the drive left it, is decoded to <out>/final.d64. */
int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "usage: f4c <dos> <kernal> <basic> <d64> <outdir> command...\n"); return 2; }
    load(argv[1], dos, sizeof dos); load(argv[2], kernal, sizeof kernal); load(argv[3], basic, sizeof basic);
    load(argv[4], d64, sizeof d64);
    d64_to_disk(d64, gcr, &disk, frames);
    drive_init(dos, &disk);
    c64_init(kernal, basic);
    if (!run_until_pc(READY_LOOP, 5000000)) { printf("C64 never reached READY\n"); return 1; }
    printf("READY at C64 cycle %llu, drive cycle %llu\n", (unsigned long long)c64_cycles(),
           (unsigned long long)drive_now());
    int fail = 0;
    for (int i = 6; i < argc; i++) {
        uint8_t *m = c64_ram();
        char kind = 'L', name[64] = { 0 };
        unsigned size = 0, seed = 0;
        const char *a = argv[i];
        if (!strncmp(a, "load:", 5)) a += 5;
        else if (!strncmp(a, "save:", 5)) { kind = 'S'; sscanf(a + 5, "%63[^:]:%u:%u", name, &size, &seed); a = name; }
        else if (!strncmp(a, "cmd:", 4)) { kind = 'C'; a += 4; }
        else if (!strncmp(a, "swap:", 5) || !strncmp(a, "swapq:", 6)) {
            int q = a[4] == 'q';
            swap_disk(a + (q ? 6 : 5), q);
            printf("%s: done bus=%016llx\n", argv[i], (unsigned long long)h);
            continue;
        }
        size_t len = strlen(a);
        for (size_t k = 0; k < len; k++)   /* ASCII letters to PETSCII: a-z -> $41-$5A */
            m[0xC100 + k] = (uint8_t)(a[k] >= 'a' && a[k] <= 'z' ? a[k] - 32 : a[k]);
        memset(m + 0x1000, 0, 0x9000);
        if (kind == 'S') { uint32_t x = seed; for (unsigned k = 0; k < size; k++) { x = x * 1103515245u + 12345u; m[0x2000 + k] = (uint8_t)(x >> 16); } }
        memset(m + 0xC200, 0xEE, 5);
        uint16_t done = build(m, kind, len, (uint16_t)(0x2000 + size));
        uint64_t t0 = c64_cycles();
        c64_jump(0xC000);
        int ok = run_until_pc(done, 300u * C64_HZ);
        unsigned end = m[0xC200] | (m[0xC201] << 8);
        int err = (m[0xC203] & 1) ? m[0xC202] : 0;
        double secs = (double)(c64_cycles() - t0) / C64_HZ;
        printf("%s: %s", argv[i], ok ? "done" : "TIMEOUT");
        if (kind == 'L') printf(" end=$%04X bytes=%u", end, end >= 0x1000 ? end - 0x1000 : 0);
        printf(" err=%d status=$%02X time=%.2fs bus=%016llx\n", err, m[0xC204], secs, (unsigned long long)h);
        if (!ok || err) { fail = 1; continue; }
        char path[512];
        FILE *f;
        if (kind == 'L') {
            snprintf(path, sizeof path, "%s/%d.bin", argv[5], i - 5);
            if (!(f = fopen(path, "wb"))) { perror(path); return 2; }
            fwrite(m + 0x1000, 1, end - 0x1000, f); fclose(f);
        } else if (kind == 'S') {
            snprintf(path, sizeof path, "%s/%d.prg", argv[5], i - 5);
            if (!(f = fopen(path, "wb"))) { perror(path); return 2; }
            fputc(0x00, f); fputc(0x20, f); fwrite(m + 0x2000, 1, size, f); fclose(f);
        }
        run_until_pc(READY_LOOP, 5000000);   /* back to BASIC's loop */
    }
    /* let the drive finish (BAM writes, motor run-on), then decode what it left */
    uint64_t t_end = c64_cycles() + 3u * C64_HZ;
    while (c64_cycles() < t_end) step();
    static uint8_t out[D64_BYTES];
    memcpy(out, d64, sizeof out);
    int n = disk_to_d64(&disk, out);
    char path[512];
    snprintf(path, sizeof path, "%s/final.d64", argv[5]);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 2; }
    fwrite(out, 1, sizeof out, f); fclose(f);
    printf("final disk: %d of %d sectors decoded\n", n, D64_SECTORS);
    return fail || n != D64_SECTORS;
}
