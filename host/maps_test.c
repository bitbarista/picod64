/* -------------------------------------------------------------------------
 * The SYNC-run framing (core/disk.h disk_frame_track; core/drive1541.h next_byte_bit,
 * is_sync_bit, is_byte_bit) against the bitmaps it replaced, rebuilt here
 * straight from frame_bit: for EVERY bit of every track tried, the next byte,
 * "is a byte" and "is SYNC" must agree. Tracks: a D64's 35, the multi disk's
 * 84 (bench/track.h), and random ones with SYNCs cut across the wrap.
 * Then disk_frame_span (the rebuild round a written span only) against the full
 * one, run for run, after random writes: GCR, SYNCs, junk, near the wrap.
 *   maps_test
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint8_t bus_low;
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   do { } while (0)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     (bus_low)
#define PLAT_IEC_OUT(p)   (bus_low = (uint8_t)(p))
#include "../core/drive1541.h"
#include "../bench/track.h"

static uint8_t bm[GCR_TRACK_MAX * 8], sm[GCR_TRACK_MAX * 8];
static syncs_t rs;
static long overflowed;

static long check(const uint8_t *trk, uint32_t n)
{
    head_t t = { 0 };
    int has = 0;
    for (uint32_t p = 0; p < n; p++) { frame_bit(&t, (trk[p >> 3] >> (7 - (p & 7))) & 1); has |= t.sync; }
    for (uint32_t p = 0; p < n; p++) {
        bm[p] = (uint8_t)frame_bit(&t, (trk[p >> 3] >> (7 - (p & 7))) & 1); sm[p] = t.sync;
    }
    disk_frame_track(trk, n, &rs);
    if (!has) return rs.n == 0 ? 0 : 1;
    if (rs.n == 0) { overflowed++; return 0; }        /* too many runs: slow path */
    D.trk = trk; D.trk_bits = n; D.syncs = &rs;
    long bad = 0;
    for (uint32_t p = 0; p < n; p++) {
        uint32_t want = HEAD_NONE;
        for (uint32_t k = 0; k < n; k++) if (bm[(p + k) % n]) { want = (p + k) % n; break; }
        if (next_byte_bit(p) != want) bad++;
        if (is_byte_bit(p) != bm[p]) bad++;
        if (is_sync_bit(p) != sm[p]) bad++;
    }
    return bad;
}

int main(void)
{
    static uint8_t d64[D64_BYTES], g[D64_GCR_BYTES], tr[7928];
    static disk_t dk;
    long bad = 0, tracks = 0;
    uint32_t x = 7;
    for (int i = 0; i < D64_BYTES; i++) { x = x * 1103515245u + 12345u; d64[i] = (uint8_t)(x >> 16); }
    static syncs_t fr[D64_TRACKS];
    d64_to_disk(d64, g, &dk, fr);
    for (int t = 0; t < 70; t += 2) { bad += check(dk.gcr[t], dk.len[t] * 8u); tracks++; }
    multi_disk(&dk);
    for (int t = 0; t < DISK_HALFTRACKS; t++) if (dk.gcr[t]) { bad += check(dk.gcr[t], dk.len[t] * 8u); tracks++; }
    /* random tracks: runs of 1s of random length (some >= 10: SYNC), some across the wrap */
    for (int k = 0; k < 40; k++) {
        uint32_t n = 6000 + (uint32_t)(rand() % 1900), bytes = (n + 7) / 8;
        memset(tr, 0, sizeof tr);
        for (uint32_t p = 0; p < bytes * 8; ) {
            int run = rand() % (k < 36 ? 30 : 3) ? rand() % 10 : 10 + rand() % 40;   /* the last 4: SYNC-heavy, to overflow */
            for (int j = 0; j < run && p < bytes * 8; j++, p++) tr[p >> 3] |= (uint8_t)(0x80 >> (p & 7));
            p += 1 + rand() % 3;
        }
        if (k & 1) { tr[0] = 0xFF; tr[1] |= 0xC0; tr[bytes - 1] = 0xFF; }   /* a SYNC through the wrap */
        bad += check(tr, bytes * 8); tracks++;
    }
    /* edge cases at the wrap: (1s at the end, 1s at bit 0) */
    static const int edge[][2] = { {3, 12}, {5, 5}, {12, 0}, {0, 15}, {9, 1}, {1, 9}, {4, 4}, {20, 20}, {0, 10}, {10, 0} };
    for (unsigned k = 0; k < sizeof edge / sizeof edge[0]; k++) {
        uint32_t bytes = 6250, nn = bytes * 8;
        memset(tr, 0x55, bytes);                       /* 0101...: never a SYNC */
        for (int j = 0; j < 40; j++) tr[1000 + j] = 0xFF;   /* one ordinary SYNC mid-track */
        for (int j = 0; j < edge[k][0]; j++) tr[(nn - 1 - j) >> 3] |= (uint8_t)(0x80 >> ((nn - 1 - j) & 7));
        if (edge[k][0]) tr[(nn - 1 - edge[k][0]) >> 3] &= (uint8_t)~(0x80 >> ((nn - 1 - edge[k][0]) & 7));
        for (int j = 0; j < edge[k][1]; j++) tr[j >> 3] |= (uint8_t)(0x80 >> (j & 7));
        tr[edge[k][1] >> 3] &= (uint8_t)~(0x80 >> (edge[k][1] & 7));
        long e = check(tr, nn); bad += e; tracks++;
        if (e) printf("edge %d/%d: %ld disagreements\n", edge[k][0], edge[k][1], e);
    }
    /* the incremental rebuild: write a random span, update; it must equal the full rebuild */
    long sbad = 0, spans = 0;
    static uint8_t cp[7928];
    static syncs_t inc, full;
    for (int k = 0; k < 20000; k++) {
        const uint8_t *src; uint32_t n;
        int t = rand() % 2;                            /* the multi disk's tracks, or random */
        if (t == 0) { int h; do h = rand() % DISK_HALFTRACKS; while (!dk.gcr[h]); src = dk.gcr[h]; n = dk.len[h] * 8u; }
        else {
            n = 8 * (6000 / 8 + (uint32_t)(rand() % 200));
            memset(tr, 0x55, n / 8);
            for (int j = 0; j < 30; j++) { uint32_t q = (uint32_t)rand() % (n / 8 - 8); memset(tr + q, 0xFF, 1 + rand() % 7); }
            if (rand() & 1) { tr[0] = 0xFF; tr[n / 8 - 1] = 0xFF; }
            src = tr;
        }
        memcpy(cp, src, n / 8);
        disk_frame_track(cp, n, &inc);
        int writes = 1 + rand() % 3;
        for (int w = 0; w < writes; w++) {
            uint32_t lo = rand() % 4 ? (uint32_t)rand() % n : (rand() & 1 ? (uint32_t)rand() % 40 : n - 1 - (uint32_t)rand() % 40);
            uint32_t cnt = rand() % 5 ? 1 + (uint32_t)rand() % 3000 : 1 + (uint32_t)rand() % 30;
            if (lo + cnt > n) cnt = n - lo;
            int kind = rand() % 4;                     /* GCR-ish, SYNC, all 0, random */
            for (uint32_t q = lo; q < lo + cnt; q++) {
                int b = kind == 1 ? 1 : kind == 2 ? 0 : kind == 0 ? ((q - lo) % 5 != 2) : rand() & 1;
                if (kind == 1 && q - lo >= 40) b = (q - lo) & 1;
                cp[q >> 3] = (uint8_t)((cp[q >> 3] & ~(0x80 >> (q & 7))) | (b << (7 - (q & 7))));
            }
            disk_frame_span(cp, n, &inc, lo, cnt);
        }
        disk_frame_track(cp, n, &full);
        spans++;
        if (inc.n != full.n || memcmp(inc.first, full.first, sizeof inc.first[0] * (size_t)full.n)
            || memcmp(inc.last, full.last, sizeof inc.last[0] * (size_t)full.n)) {
            if (sbad < 5) printf("span: track kind %d, %d runs vs %d\n", t, inc.n, full.n);
            sbad++;
        }
    }
    printf("%ld random writes: incremental framing differs from the full rebuild %ld times\n", spans, sbad);
    bad += sbad;
    /* ...and through the drive's own write path (head_step in write mode records where the
     * writing began; head_remap rebuilds round it): writes started INSIDE a SYNC run, the
     * first bit a 0 half the time, so a SYNC left of the start can end at it */
    long dbad = 0, dw = 0;
    static uint8_t keep[7928];
    D.disk = &dk; D.wmode = 1; D.wcount = 0;
    for (int k = 0; k < 4000; k++) {
        int h; do h = rand() % DISK_HALFTRACKS; while (!dk.gcr[h] || !dk.sync[h] || dk.sync[h]->n < 2);
        head_select((uint8_t)h);
        uint32_t n = D.trk_bits;
        memcpy(keep, D.trk, n / 8);
        disk_frame_track(D.trk, n, dk.sync[h]);
        const syncs_t *r = dk.sync[h];
        int i = rand() % (r->n - 1);
        uint32_t len = r->last[i] - r->first[i] + 10;
        head_t hd = { 0 };
        hd.bitpos = (r->first[i] + n - 9 + (uint32_t)rand() % len) % n;
        int nb = 1 + rand() % 40;
        for (int b = 0; b < nb; b++) {
            hd.wsr = (uint8_t)rand();
            if (b == 0 && (k & 1)) hd.wsr &= 0x7F;          /* the first bit written: 0 */
            for (int j = 0; j < 8; j++) head_step(&hd, 16, 1);
        }
        head_remap();
        disk_frame_track(D.trk, n, &full);
        dw++;
        if (r->n != full.n || memcmp(r->first, full.first, sizeof full.first[0] * (size_t)full.n)
            || memcmp(r->last, full.last, sizeof full.last[0] * (size_t)full.n)) dbad++;
        memcpy(dk.gcr[h], keep, n / 8);                    /* the track as it was */
        disk_frame_track(D.trk, n, dk.sync[h]);
    }
    D.wmode = 0;
    printf("%ld writes through the drive: framing differs from the full rebuild %ld times\n", dw, dbad);
    bad += dbad;
    printf("%ld tracks, every bit: %ld disagreements with the bitmaps; %ld over %d runs (slow path)\n",
           tracks, bad, overflowed, DRIVE_MAX_SYNCS);
    return bad != 0 || overflowed != 4;
}
