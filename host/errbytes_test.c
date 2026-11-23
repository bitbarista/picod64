/* -------------------------------------------------------------------------
 * D64 error bytes [S21]: each code built into the GCR (core/disk.h,
 * d64_track_sectors_err) must be what the real 1541 DOS [S9] reports when it
 * reads that sector through our drive. Jobs go straight into the DOS's job
 * queue, as f4b.c: a SEEK ($B0) on track 18 sets the master ID, then a READ
 * ($80) of every sector; the DOS replaces the job code with its own result.
 *   - a sector with $02, $04 or $05: the DOS returns that code
 *   - $09 and $0B are header ("seek") errors: the DOS checks the first header
 *     that passes the head, whichever sector it is, so on their track any read
 *     may return them, as on a real disk [S21: they "do *not* necessarily apply
 *     to the exact sector"]; the sector itself is never found ($02)
 *   - every other sector: $01 and its bytes
 *   - a track whose sectors are all $03 (no SYNC): $03 on every one
 *   - $03 on one sector of a track: the DOS sees the header missing, $02
 *   - the write-only codes ($07 here): the sector reads normally
 *   - no error bytes, or all $01: the GCR is byte for byte what it was before
 *
 *   errbytes_test <rom.bin>
 * Built twice by equiv.sh: -DREF for the per-cycle reference model. Each job's
 * line carries a hash of every bus cycle so far, which must be identical: the
 * no-SYNC track (and the DOS's wait for a SYNC there) through both.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint8_t bus_low;
static uint64_t h = 1469598103934665603ULL;
#define DRIVE_TRACE(a, v, rw) \
    (h = (h ^ (((uint64_t)(DRIVE_NOW) << 32) | ((uint32_t)(a) << 9) | ((uint32_t)(v) << 1) | (rw))) * 1099511628211ULL)
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   do { } while (0)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     (bus_low)
#define PLAT_IEC_OUT(p)   (bus_low = (uint8_t)(p))
#ifdef REF
#include "ref/drive1541.h"
static void run_to(uint32_t t) { while (D.cycles < t && !CPU.jammed) drive_step(); }
#else
#include "../core/drive1541.h"
static void run_to(uint32_t t) { drive_run(t); }
#endif

static uint8_t rom[16384], d64[D64_BYTES], err[D64_SECTORS], gcr[D64_GCR_BYTES], gcr0[D64_GCR_BYTES];
static disk_t disk;
static syncs_t frames[D64_TRACKS];

static uint8_t job(uint8_t code, uint8_t t, uint8_t s)
{
    D.ram[6] = t; D.ram[7] = s; D.ram[0] = code;
    uint32_t limit = D.cycles + 4000000;
    while (D.ram[0] & 0x80) {
        if (D.cycles > limit || CPU.jammed) return 0;
        run_to(D.cycles + 1000);
    }
    return D.ram[0];
}

/* as d64_to_disk, with error bytes (e: NULL for none) */
static void build(const uint8_t *e, uint8_t *buf)
{
    const uint8_t *bam = d64 + d64_offset(18, 0);
    for (int i = 0; i < DISK_HALFTRACKS; i++) { disk.gcr[i] = 0; disk.len[i] = 0; }
    for (int t = 1; t <= D64_TRACKS; t++) {
        d64_track_sectors_err(d64 + d64_offset(t, 0), t, bam[162], bam[163], e ? e + d64_offset(t, 0) / 256 : 0, buf);
        disk.gcr[2 * (t - 1)] = buf; disk.len[2 * (t - 1)] = (uint16_t)d64_track_bytes(t);
        buf += d64_track_bytes(t);
    }
    disk_frame(&disk, frames, D64_TRACKS);
}

static void set(int t, int s, uint8_t code) { err[d64_offset(t, s) / 256] = code; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: errbytes_test <rom.bin>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { perror(argv[1]); return 2; }
    fclose(f);
    uint32_t x = 64;
    for (int i = 0; i < D64_BYTES; i++) { x = x * 1103515245u + 12345u; d64[i] = (uint8_t)(x >> 16); }
    for (int t = 1; t <= D64_TRACKS; t++)
        for (int s = 0; s < d64_sectors(t); s++) { d64[d64_offset(t, s)] = (uint8_t)t; d64[d64_offset(t, s) + 1] = (uint8_t)s; }
    d64[d64_offset(18, 0) + 162] = 'F'; d64[d64_offset(18, 0) + 163] = 'F';
    int fail = 0;

    /* no error bytes, and a table of all $01: unchanged, byte for byte */
    build(0, gcr0);
    memset(err, D64_ERR_OK, sizeof err);
    build(err, gcr);
    int same = !memcmp(gcr, gcr0, sizeof gcr);
    uint8_t ref[D64_GCR_BYTES];
    d64_to_disk(d64, ref, &disk, frames);
    same &= !memcmp(ref, gcr0, sizeof gcr);
    printf("%s  no errors: GCR identical to d64_to_disk\n", same ? "PASS" : "FAIL"); fail |= !same;

    set(1, 3, 0x02); set(1, 5, 0x04); set(1, 7, 0x05); set(1, 9, 0x09); set(1, 11, 0x0B);
    set(2, 4, 0x03);                                     /* one sector without SYNC: header not found */
    for (int s = 0; s < d64_sectors(3); s++) set(3, s, 0x03);   /* a whole track: no SYNC */
    set(4, 1, 0x07);                                     /* write-only: reads normally */
    set(20, 0, 0x05); set(26, 17, 0x02); set(35, 16, 0x09);
    build(err, gcr);
    drive_init(rom, &disk);
    run_to(2500000);
    if (job(0xB0, 18, 0) != 1) { printf("FAIL  seek on track 18\n"); return 1; }
    int bad = 0, n = 0;
    for (int t = 1; t <= D64_TRACKS; t++)
        for (int s = 0; s < d64_sectors(t); s++) {
            uint8_t e = err[d64_offset(t, s) / 256], want = e;
            if (e == 0x07) want = 1;
            if (e == 0x03 && t == 2) want = 0x02;
            if (e == 0x09 || e == 0x0B) want = 0x02;
            int seek9 = 0, seekB = 0;                    /* header errors on this track */
            for (int k = 0; k < d64_sectors(t); k++) { seek9 |= err[d64_offset(t, k) / 256] == 0x09; seekB |= err[d64_offset(t, k) / 256] == 0x0B; }
            uint8_t r = job(0x80, (uint8_t)t, (uint8_t)s);
            int diff = 0;
            if (r == 1) for (int i = 0; i < 256; i++) diff += D.ram[0x300 + i] != d64[d64_offset(t, s) + i];
            int alt = (r == 0x09 && seek9) || (r == 0x0B && seekB);
            printf("T%02d S%02d read=%02X cyc=%u bus=%016llx\n", t, s, r, D.cycles, (unsigned long long)h);
            if (alt) printf("ok    T%02d S%02d code %02X: read %02X (a header error that passed first)\n", t, s, e, r);
            else if (r != want || diff) { printf("FAIL  T%02d S%02d code %02X: read %02X, want %02X, diff %d\n", t, s, e, r, want, diff); bad++; }
            else if (e != 1) printf("ok    T%02d S%02d code %02X: read %02X\n", t, s, e, r);
            n++;
        }
    printf("%s  %d sectors read by the DOS, %d wrong\n", bad ? "FAIL" : "PASS", n, bad);
    return fail || bad;
}
