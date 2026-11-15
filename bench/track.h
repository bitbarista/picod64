/* -------------------------------------------------------------------------
 * A synthetic GCR track for the bench: our own pattern, not a disk image.
 * Shape only: SYNC (40 one-bits), a 10-byte header, a gap, SYNC, 325 data
 * bytes, a gap -- repeated to fill a zone-3 track. Data bytes are never $FF,
 * so no false SYNC is formed by two adjacent bytes' high/low bits... except
 * by chance across a boundary, which the bench does not care about.
 * ⚠ 7692 bytes = 0.2 s at 1.2307 MHz / 4 / 8: a 300 rpm zone-3 track [S7] [A].
 * ------------------------------------------------------------------------- */
#ifndef FF_TRACK_H
#define FF_TRACK_H
#include <stdint.h>
#include "../core/disk.h"
#define BENCH_TRACK_BYTES 7692

static void bench_track(uint8_t *t)
{
    uint32_t lcg = 12345, n = 0;
#define PUT(b) do { if (n < BENCH_TRACK_BYTES) t[n++] = (uint8_t)(b); } while (0)
#define RND() (lcg = lcg * 1103515245u + 12345u, (uint8_t)(lcg >> 16))
    while (n < BENCH_TRACK_BYTES) {
        for (int i = 0; i < 5; i++) PUT(0xFF);
        for (int i = 0; i < 10; i++) { uint8_t b = RND(); PUT(b == 0xFF ? 0xFE : b); }
        for (int i = 0; i < 9; i++) PUT(0x55);
        for (int i = 0; i < 5; i++) PUT(0xFF);
        for (int i = 0; i < 325; i++) { uint8_t b = RND(); PUT(b == 0xFF ? 0xFE : b); }
        for (int i = 0; i < 8; i++) PUT(0x55);
    }
#undef PUT
#undef RND
}

/* A different track on every half-track, for wl_step (host only): its own
 * pattern and seed per half-track, the four zone lengths in turn, and every
 * seventh half-track unformatted. */
static uint8_t multi_trk[DISK_HALFTRACKS][7692];
static void multi_disk(disk_t *dk)
{
    static const uint16_t len[4] = { 7692, 7142, 6666, 6250 };
    for (int i = 0; i < DISK_HALFTRACKS; i++) {
        uint8_t *t = multi_trk[i];
        uint32_t lcg = 777u + 31u * (uint32_t)i, n = 0, L = len[i & 3];
#define PUT(b) do { if (n < L) t[n++] = (uint8_t)(b); } while (0)
#define RND() (lcg = lcg * 1103515245u + 12345u, (uint8_t)(lcg >> 16))
        while (n < L) {
            for (int k = 0; k < 5; k++) PUT(0xFF);
            for (int k = 0; k < 10 + (i % 5); k++) { uint8_t b = RND(); PUT(b == 0xFF ? 0xFE : b); }
            for (int k = 0; k < 9; k++) PUT(0x55);
            for (int k = 0; k < 5; k++) PUT(0xFF);
            for (int k = 0; k < 325; k++) { uint8_t b = RND(); PUT(b == 0xFF ? 0xFE : b); }
            for (int k = 0; k < 8 + (i % 3); k++) PUT(0x55);
        }
#undef PUT
#undef RND
        dk->gcr[i] = (i % 7 == 3) ? 0 : t; dk->len[i] = (uint16_t)L;
    }
    static syncs_t sy[DISK_HALFTRACKS];
    disk_frame(dk, sy, DISK_HALFTRACKS);
}

/* The bench's disk: that one track on every half-track, so a step changes
 * nothing the workloads can see. */
static void bench_disk(disk_t *dk, uint8_t *t)
{
    for (int i = 0; i < DISK_HALFTRACKS; i++) { dk->gcr[i] = t; dk->len[i] = BENCH_TRACK_BYTES; }
    static syncs_t sy;
    disk_frame(dk, &sy, 1);
}
#endif
