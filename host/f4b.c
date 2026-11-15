/* -------------------------------------------------------------------------
 * F4b: the real 1541 DOS [S9] reads every sector of a D64 through our disk.
 * No C64: jobs go straight into the DOS's job queue, as the interface
 * processor would put them [S10 p.103-104]: buffer 0 ($0300), job code at
 * $0000, track/sector at $0006/$0007. A SEEK ($B0) to each track first sets
 * the master ID; then a READ ($80) per sector. The DOS replaces the job code
 * with its result; $01 = OK [S10 p.104].
 * Every sector must come back OK and equal the D64's bytes.
 * Then (F4d) every sector is WRITTEN ($90) with new data, read back, and the
 * whole disk decoded from our GCR (disk_to_d64) must equal what was written.
 *
 * Built twice by equiv.sh: -DREF for the per-cycle reference model. Both print
 * a hash of every bus cycle per job, which must be identical.
 *
 *   f4b <rom.bin> <image.d64 | rand>
 * "rand": our own test disk, pseudo-random bytes with each sector's first two
 * bytes set to its track and sector (so a misplaced read shows), ID "FF".
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint8_t bus_low;
static uint64_t h = 1469598103934665603ULL;
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   do { } while (0)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     (bus_low)
#define PLAT_IEC_OUT(p)   (bus_low = (uint8_t)(p))
#define DRIVE_TRACE(a, v, rw) \
    (h = (h ^ (((uint64_t)(DRIVE_NOW) << 32) | ((uint32_t)(a) << 9) | ((uint32_t)(v) << 1) | (rw))) * 1099511628211ULL)
#ifdef REF
#include "ref/drive1541.h"
#else
#include "../core/drive1541.h"
#endif

static uint8_t rom[16384], d64[D64_BYTES], gcr[D64_GCR_BYTES];
static disk_t disk;
static syncs_t frames[D64_TRACKS];
static uint8_t d64b[D64_BYTES], gcrb[D64_GCR_BYTES];
static disk_t diskb;
static syncs_t framesb[D64_TRACKS];

/* run whole instructions to at least cycle t */
static void run_to(uint32_t t)
{
#ifdef REF
    while (D.cycles < t && !CPU.jammed) drive_step();
#else
    drive_run(t);
#endif
}

/* jobhash: each job's code, track, sector, result and finishing cycle; the
 * Pico prints the same (bench/f5.c), which shows it ran the same emulation */
static uint32_t jobhash = 2166136261u;
static void jhash(uint32_t v) { for (int i = 0; i < 4; i++) { jobhash ^= (v >> (8 * i)) & 0xFF; jobhash *= 16777619u; } }

/* one job; returns its result code, or 0 on timeout (2 s) */
static uint8_t job(uint8_t code, uint8_t t, uint8_t s)
{
    D.ram[6] = t; D.ram[7] = s; D.ram[0] = code;
    uint32_t limit = D.cycles + 2000000;
    while (D.ram[0] & 0x80) {
        if (D.cycles > limit || CPU.jammed) { jhash(0xDEAD); return 0; }
        run_to(D.cycles + 1000);
    }
    jhash(code); jhash(t); jhash(s); jhash(D.ram[0]); jhash(D.cycles);
    return D.ram[0];
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: f4b <rom.bin> <image.d64 | rand>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { perror(argv[1]); return 2; }
    fclose(f);
    if (strcmp(argv[2], "rand") == 0) {
        uint32_t x = 64;
        for (int i = 0; i < D64_BYTES; i++) { x = x * 1103515245u + 12345u; d64[i] = (uint8_t)(x >> 16); }
        for (int t = 1; t <= D64_TRACKS; t++)
            for (int s = 0; s < d64_sectors(t); s++) { d64[d64_offset(t, s)] = (uint8_t)t; d64[d64_offset(t, s) + 1] = (uint8_t)s; }
        d64[d64_offset(18, 0) + 162] = 'F'; d64[d64_offset(18, 0) + 163] = 'F';
    } else {
        f = fopen(argv[2], "rb");
        if (!f || fread(d64, 1, sizeof d64, f) != sizeof d64) { perror(argv[2]); return 2; }
        fclose(f);
    }
    d64_to_disk(d64, gcr, &disk, frames);
    drive_init(rom, &disk);
    run_to(2500000);                                   /* the DOS boots (F4a) */
    int bad = 0, ok = 0;
    for (int t = 1; t <= D64_TRACKS; t++) {
        uint8_t r = job(0xB0, (uint8_t)t, 0);
        printf("T%02d seek=%02X cyc=%u bus=%016llx\n", t, r, D.cycles, (unsigned long long)h);
        if (r != 1) { bad++; continue; }
        for (int s = 0; s < d64_sectors(t); s++) {
            r = job(0x80, (uint8_t)t, (uint8_t)s);
            const uint8_t *want = d64 + d64_offset(t, s);
            int diff = 0;
            for (int i = 0; i < 256; i++) diff += D.ram[0x300 + i] != want[i];
            printf("T%02d S%02d read=%02X diff=%d cyc=%u bus=%016llx\n", t, s, r, diff, D.cycles,
                   (unsigned long long)h);
            if (r == 1 && diff == 0) ok++; else bad++;
        }
    }
    printf("sectors OK: %d of %d, failures %d\n", ok, D64_SECTORS, bad);

    /* F4d: WRITE ($90) new data to every sector, read it all back through
     * the DOS, and decode our GCR independently (disk_to_d64). */
    static uint8_t want[D64_BYTES], got[D64_BYTES];
    uint32_t x = 99;
    for (int i = 0; i < D64_BYTES; i++) { x = x * 1103515245u + 12345u; want[i] = (uint8_t)(x >> 16); }
    int wbad = 0, wok = 0;
    for (int t = 1; t <= D64_TRACKS; t++) {
        uint8_t r = job(0xB0, (uint8_t)t, 0);
        if (r != 1) { printf("T%02d seek=%02X\n", t, r); wbad++; continue; }
        for (int s = 0; s < d64_sectors(t); s++) {
            /* ⚠ A SEEK first. Without the interface processor's own steps between
             * jobs, a second WRITE on a track wrote the first 69 bytes (the GCR
             * overflow buffer, $01BB-$01FF) stale: the DOS leaves its buffer in
             * GCR form after a write (GCRFLG $50 [S10 p.221]). The IP's real
             * sequence is tested by KERNAL SAVE (f4c.sh). */
            if (job(0xB0, (uint8_t)t, 0) != 1) wbad++;
            for (int i = 0; i < 256; i++) D.ram[0x300 + i] = want[d64_offset(t, s) + i];
            D.ram[0x50] = 0;   /* GCRFLG: binary */
            r = job(0x90, (uint8_t)t, (uint8_t)s);
            printf("T%02d S%02d write=%02X cyc=%u bus=%016llx\n", t, s, r, D.cycles, (unsigned long long)h);
            if (r != 1) wbad++;
        }
    }
    for (int t = 1; t <= D64_TRACKS; t++) {
        uint8_t r = job(0xB0, (uint8_t)t, 0);
        if (r != 1) { wbad++; continue; }
        for (int s = 0; s < d64_sectors(t); s++) {
            r = job(0x80, (uint8_t)t, (uint8_t)s);
            int diff = 0;
            for (int i = 0; i < 256; i++) diff += D.ram[0x300 + i] != want[d64_offset(t, s) + i];
            printf("T%02d S%02d reread=%02X diff=%d bus=%016llx\n", t, s, r, diff, (unsigned long long)h);
            if (r == 1 && diff == 0) wok++; else wbad++;
        }
    }
    int dec = disk_to_d64(&disk, got);
    int same = memcmp(got, want, D64_BYTES) == 0;
    printf("written and read back OK: %d of %d, failures %d; decoded from GCR: %d sectors, %s\n",
           wok, D64_SECTORS, wbad, dec, same ? "identical" : "DIFFERENT");
    printf("before the swap (bench/f5.c ends here): jobhash=%08X cycles=%u\n", jobhash, D.cycles);

    /* Disk swap (as f4c.c, in drive cycles): out past the write-protect sensor, none,
     * in. The DOS's IRQ must see it (WPSW $1C, [S10 p.220]); then every track's first
     * and last sectors of the new disk (ID "GG") must read. */
    x = 77;
    for (int i = 0; i < D64_BYTES; i++) { x = x * 1103515245u + 12345u; d64b[i] = (uint8_t)(x >> 16); }
    d64b[d64_offset(18, 0) + 162] = 'G'; d64b[d64_offset(18, 0) + 163] = 'G';
    d64_to_disk(d64b, gcrb, &diskb, framesb);
    D.ram[0x1C] = 0;
    drive_wps(0); run_to(D.cycles + 200000);
    drive_insert(0); drive_wps(1); run_to(D.cycles + 1000000);
    drive_wps(0); run_to(D.cycles + 200000);
    drive_insert(&diskb); drive_wps(1); run_to(D.cycles + 200000);
    int noticed = D.ram[0x1C] != 0, sok = 0, sbad = 0;
    for (int t = 1; t <= D64_TRACKS; t++) {
        if (job(0xB0, (uint8_t)t, 0) != 1) { sbad++; continue; }
        for (int s = 0; s < d64_sectors(t); s += d64_sectors(t) - 1) {
            uint8_t r = job(0x80, (uint8_t)t, (uint8_t)s);
            int diff = 0;
            for (int i = 0; i < 256; i++) diff += D.ram[0x300 + i] != d64b[d64_offset(t, s) + i];
            printf("swap T%02d S%02d read=%02X diff=%d bus=%016llx\n", t, s, r, diff, (unsigned long long)h);
            if (r == 1 && diff == 0) sok++; else sbad++;
        }
    }
    printf("swap: DOS noticed (WPSW) %s; new disk read OK: %d of %d, failures %d\n",
           noticed ? "yes" : "NO", sok, 2 * D64_TRACKS, sbad);
    printf("jobhash=%08X cycles=%u\n", jobhash, D.cycles);
    return (bad || wbad || dec != D64_SECTORS || !same || !noticed || sbad) ? 1 : 0;
}
