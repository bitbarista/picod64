/* -------------------------------------------------------------------------
 * Event-driven core vs the per-cycle reference model (host/ref/), on the
 * same workload. Built twice by equiv.sh: -DREF selects the reference. Each
 * build prints a rolling hash of every bus cycle (cycle, address, R/W, data)
 * at the end of each 1024-cycle block, then the fingerprint. The two outputs
 * must be identical; the first differing block locates any divergence.
 *
 *   equiv <workload.bin> <cycles> [multi | swap]
 * "multi": a different track on every half-track (track.h), for wl_step.
 * "swap": the multi disk, swapped with the bench disk (different track lengths)
 * every 7 blocks, at an instruction end that falls anywhere in a byte: wl_scan
 * logs every byte read, so a head moved wrongly by a swap shows.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint8_t bus_low;
static uint64_t h = 1469598103934665603ULL;
#define PLAT_CYCLE()      do { } while (0)   /* host/ref/ model */
#define PLAT_SYNC(c, k)   do { } while (0)   /* core/ model */
/* idle-time head lookahead: always taken, or (-DLA_FORCED) never, so the
 * forced path at the lower bound is exercised too */
#ifdef LA_FORCED
#define PLAT_SLACK(c)     0
#else
#define PLAT_SLACK(c)     1
#endif
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
#include "../bench/track.h"
#include "../bench/fingerprint.h"

static uint8_t rom[16384], trk[BENCH_TRACK_BYTES];
static disk_t disk;

/* "full": the multi disk with some half-tracks no framing can hold (2026-11-14): one in five gets ~350 short SYNC
 * runs (more than DRIVE_MAX_SYNCS), one is all 1s (SYNC all round). Read from the slow path, bit by bit; the skip
 * before a track's first SYNC must leave them alone (its n = 0 isn't "no SYNC") */
static uint8_t full_trk[DISK_HALFTRACKS][7692];
static void full_tracks(disk_t *dk)
{
    for (int i = 0; i < DISK_HALFTRACKS; i++) {
        if (!dk->gcr[i] || i % 5 != 2) continue;
        uint8_t *t = full_trk[i]; uint32_t L = dk->len[i], lcg = 99u + (uint32_t)i;
        for (uint32_t k = 0; k < L; k++) {
            lcg = lcg * 1103515245u + 12345u;
            uint8_t b = (uint8_t)(lcg >> 16); if (b == 0xFF) b = 0xFE;
            t[k] = (k % 22 < 2) ? 0xFF : b;                 /* 16+ 1s every 22 bytes: a SYNC run each */
        }
        if (i == 42) for (uint32_t k = 0; k < L; k++) t[k] = 0xFF;   /* SYNC all round */
        dk->gcr[i] = t;
    }
    static syncs_t sy[DISK_HALFTRACKS];
    disk_frame(dk, sy, DISK_HALFTRACKS);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: equiv <workload.bin> <cycles>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { perror(argv[1]); return 2; }
    fclose(f);
    uint32_t n = (uint32_t)strtoul(argv[2], 0, 0), blk = 1024;
    bench_track(trk);
    static disk_t disk2;
    int swap = argc > 3 && !strcmp(argv[3], "swap");
    if (argc > 3) multi_disk(&disk); else bench_disk(&disk, trk);
    if (argc > 3 && !strcmp(argv[3], "full")) full_tracks(&disk);
    if (swap) bench_disk(&disk2, trk);
    drive_init(rom, &disk);
#ifdef REF
    while (D.cycles < n && !CPU.jammed) {
        drive_step();
        if (D.cycles >= blk) {
            printf("%u %016llx\n", blk, (unsigned long long)h);
            if (swap && blk % 7168 == 0) drive_insert((blk / 7168) & 1 ? &disk2 : &disk);
            blk += 1024;
        }
    }
#else
    /* same blocks: drive_run stops at the first instruction end at or past
     * its target, exactly where the step loop above notices the crossing */
    while (D.cycles < n) {
        drive_run(blk < n ? blk : n);
        if (D.cycles >= blk) {
            printf("%u %016llx\n", blk, (unsigned long long)h);
            if (swap && blk % 7168 == 0) drive_insert((blk / 7168) & 1 ? &disk2 : &disk);
            blk += 1024;
        }
    }
    /* the fingerprint counts the bytes the head has passed: the lazy disk's backlog first (drive_run no longer
     * catches up outside idle time, and LA_FORCED has none) */
    drive_disk_catchup(D.cycles);
#endif
    print_fingerprint(printf);
    return 0;
}
