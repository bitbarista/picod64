/* -------------------------------------------------------------------------
 * PicoD64 -- the disk under the head: GCR bit streams per half-track, and
 * the D64 -> GCR conversion.
 *
 * CLEAN ROOM. Format facts from Inside Commodore DOS [S10]:
 *   - 35 tracks; the stepper reaches "more than 70" positions (p.30): a
 *     half-track per step. Track t sits at half-track 2(t-1).
 *   - zones: tracks 1-17 21 sectors, 18-24 19, 25-30 18, 31-35 17 (p.30);
 *     bits per rotation at 300 rpm 61538.4 / 57142.8 / 53333.4 / 50000 (p.31).
 *   - sector: SYNC (5 x $FF, not GCR), header 8 bytes as 10 GCR bytes, header
 *     gap 8 x $55 (not GCR), SYNC, data block 260 bytes as 325 GCR (p.117).
 *   - header: $08, checksum, sector, track, ID 2, ID 1, $0F, $0F; checksum =
 *     track ^ sector ^ ID 1 ^ ID 2 (p.32-33).
 *   - data block: $07, 256 bytes, checksum (EOR of the 256), $00, $00 (p.33-34).
 *   - GCR nybble table (p.114); 4 bytes -> 5 GCR bytes, MSB first (p.114-116).
 *   - tail gap $55: 4-7 / 9-12 / 5-8 / 4-8 bytes by zone, the last sector's
 *     gap much longer, "in excess of 100 bytes" (p.117).
 * D64: the 683 sectors in track order, 256 bytes each (174,848 bytes) [S11:
 * VICE's c1541 as a black box; its sector dumps match these offsets in every
 * zone, 2026-10-29]. Tracks 36-42 (17 sectors each, as 31-35) follow: fw/storage.c.
 * Error bytes [S21]: a D64 may end in one byte per sector, in sector order: the
 * drive controller's code for that sector on the original disk, $01 = OK. The
 * codes are S10's FDC error codes (the error channel shows code + 18: S10 ch. 6, the job queue).
 * d64_track_sectors_err() builds each error into the GCR, so the DOS finds it
 * there as it would on the disk.
 * ------------------------------------------------------------------------- */
#ifndef FF_DISK_H
#define FF_DISK_H
#ifndef FF_HOT
#define FF_HOT
#endif
#include <stdint.h>

#define DISK_HALFTRACKS 84          /* G64's count; a D64 fills the even ones up to 68 */
#define D64_TRACKS      35
#define D64_SECTORS     683
#define D64_BYTES       (D64_SECTORS * 256)

/* A track's steady-state framing, as its SYNC runs: bit p is a SYNC bit iff
 * first[i] <= p <= last[i] for some i; every other bit completes a byte iff it
 * lies 8k bits after the end of the run before it, short of the next run.
 * first[] ascends inside [0, n); a run that wraps the track end has last >= n.
 * ⚠ [A] More than DRIVE_MAX_SYNCS runs (a protected or garbled track): the
 * track is left on the slow path, bit by bit, for ever. */
#ifndef DRIVE_MAX_SYNCS
#define DRIVE_MAX_SYNCS 96
#endif
/* full: n is 0 because the track could not be framed (too many runs, or no 0 bit at all: SYNC all round), not
 * because it has no SYNC. Anything that reads n = 0 as "no SYNC on this track" must check it (2026-11-14:
 * head_skip_nc) */
typedef struct { uint16_t n; uint8_t full; uint32_t first[DRIVE_MAX_SYNCS], last[DRIVE_MAX_SYNCS]; } syncs_t;

typedef struct {
    uint8_t *gcr[DISK_HALFTRACKS];         /* NULL: unformatted; the drive writes here */
    uint16_t len[DISK_HALFTRACKS];         /* bytes */
    syncs_t *sync[DISK_HALFTRACKS];        /* each track's framing (disk_frame); the drive rebuilds it after writing */
} disk_t;

/* --- the framing, once per track ---------------------------------------------
 * SYNC depends on the bit pattern only: bit p is a SYNC bit iff bits p-9..p are
 * all 1 (sr == 0x3FF; the steady state, so the track wraps). So the SYNC runs are
 * the runs of 1s at least 10 long, from their tenth bit to their end -- found a
 * BYTE at a time: inside one byte no run of 1s reaches 10, so only the 1s at each
 * byte's two ends matter (the old bit-by-bit framing cost 18 ms a track on the
 * Pico, F5). host/maps_test.c checks it bit by bit. */
static inline int syncs_add(syncs_t *r, uint32_t a, uint32_t b)   /* a run of 1s a..b; 0 = full */
{
    if (b - a + 1 < 10) return 1;
    if (r->n == DRIVE_MAX_SYNCS) return 0;
    r->first[r->n] = a + 9; r->last[r->n] = b; r->n++;
    return 1;
}

static FF_HOT inline void disk_frame_track(const uint8_t *trk, uint32_t n, syncs_t *r)
{
    uint32_t nb = n / 8, lead0 = 0, a = 0;
    int in = 0, ok = 1;
    r->n = 0; r->full = 0;
    while (lead0 < n && trk[lead0 / 8] == 0xFF) lead0 += 8;        /* the 1s at bit 0 */
    if (lead0 >= n) { r->full = 1; return; }           /* no 0 anywhere: no framing [A] */
    lead0 += (uint32_t)(__builtin_clz((uint32_t)(uint8_t)~trk[lead0 / 8]) - 24);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t byte = trk[i], p = 8 * i;
        if (byte == 0xFF) { if (!in) { in = 1; a = p; } continue; }
        uint32_t lead = (uint32_t)(__builtin_clz((uint32_t)(uint8_t)~byte) - 24);   /* 1s at the MSB end */
        uint32_t tail = (uint32_t)__builtin_ctz(~byte | 0x100u);                     /* 1s at the LSB end */
        if (in) { ok &= syncs_add(r, a, p + lead - 1); in = 0; }
        if (tail) { in = 1; a = p + 8 - tail; }
    }
    if (!ok) { r->n = 0; r->full = 1; return; }        /* ⚠ [A] too many runs: slow path */
    /* the 1s at the end carry on into the 1s at bit 0 (the track wraps) */
    uint32_t le = in ? n - a : 0;
    if (le && lead0) {
        if (lead0 >= 10) {                             /* the bit-0 run is part of this one */
            for (int k = 1; k < r->n; k++) { r->first[k - 1] = r->first[k]; r->last[k - 1] = r->last[k]; }
            r->n--;
        }
        if (le + lead0 >= 10) {
            uint32_t f = a + 9, l = n + lead0 - 1;
            if (f >= n) {                              /* its SYNC starts after the wrap: first run */
                if (r->n == DRIVE_MAX_SYNCS) { r->n = 0; r->full = 1; return; }
                for (int k = r->n; k > 0; k--) { r->first[k] = r->first[k - 1]; r->last[k] = r->last[k - 1]; }
                r->first[0] = f - n; r->last[0] = l - n; r->n++;
            } else if (r->n == DRIVE_MAX_SYNCS) { r->n = 0; r->full = 1; return; }
            else { r->first[r->n] = f; r->last[r->n] = l; r->n++; }
        }
    } else if (le && !syncs_add(r, a, n - 1)) { r->n = 0; r->full = 1; return; }
}

/* After bits [lo, lo + cnt) of the track were rewritten, bring its framing up to
 * date by rescanning only round them. SYNC at bit p depends on bits p-9..p alone,
 * so only runs near the span can change. The window is widened out to a 0 bit on
 * each side, OUTSIDE the span (so unchanged): no run of 1s crosses its edges, the
 * old runs inside it are dropped and the window's own runs spliced in. A span
 * across the track's end, or a window touching the run that wraps it: the full
 * rebuild (always right, just slower). F5: the full one cost ~1.1 ms on the Pico,
 * too near the C64's 1 ms. host/maps_test.c checks this against the full one. */
static inline int disk_tbit(const uint8_t *t, uint32_t p) { return (t[p >> 3] >> (7 - (p & 7))) & 1; }
static FF_HOT inline void disk_frame_span(const uint8_t *trk, uint32_t n, syncs_t *r, uint32_t lo, uint32_t cnt)
{
    if (!cnt) return;
    if (cnt + 64 >= n || lo + cnt > n || r->n == 0) { disk_frame_track(trk, n, r); return; }
    uint32_t hi = lo + cnt - 1;
    int64_t L = (int64_t)lo - 1;
    while (L >= 0 && disk_tbit(trk, (uint32_t)L)) L--;
    uint32_t R = hi + 1;
    while (R < n && disk_tbit(trk, R)) R++;
    if (L < 0 || R >= n) { disk_frame_track(trk, n, r); return; }
    if (r->last[r->n - 1] >= n &&                     /* the run through the track's end */
        (R >= r->first[r->n - 1] || (uint32_t)L <= r->last[r->n - 1] - n)) { disk_frame_track(trk, n, r); return; }
    /* the window's runs of 1s, a byte at a time as disk_frame_track (bits outside L+1..R-1
     * masked to 0: L and R are 0 bits anyway) */
    syncs_t w; w.n = 0;
    int in = 0; uint32_t a = 0;
    uint32_t b0 = ((uint32_t)L + 1) >> 3, b1 = (R - 1) >> 3;
    for (uint32_t i = b0; i <= b1; i++) {
        uint32_t byte = trk[i], p = 8 * i;
        if (p < (uint32_t)L + 1) byte &= 0xFFu >> ((uint32_t)L + 1 - p);
        if (p + 7 > R - 1) byte &= (0xFF00u >> (R - p)) & 0xFF;
        if (byte == 0xFF) { if (!in) { in = 1; a = p; } continue; }
        uint32_t lead = (uint32_t)(__builtin_clz((uint32_t)(uint8_t)~byte) - 24);
        uint32_t tail = (uint32_t)__builtin_ctz(~byte | 0x100u);
        if (in) {
            in = 0;
            if (!syncs_add(&w, a, p + lead - 1)) { disk_frame_track(trk, n, r); return; }
        }
        if (tail) { in = 1; a = p + 8 - tail; }
    }
    if (in && !syncs_add(&w, a, R - 1)) { disk_frame_track(trk, n, r); return; }
    /* splice: old runs before L, the window's, old runs after R */
    syncs_t o2; o2.n = 0; o2.full = 0;
    int i = 0;
    for (; i < r->n && r->last[i] < (uint32_t)L; i++) { o2.first[o2.n] = r->first[i]; o2.last[o2.n] = r->last[i]; o2.n++; }
    while (i < r->n && r->first[i] <= R) i++;        /* inside the window: replaced */
    if (o2.n + w.n + (r->n - i) > DRIVE_MAX_SYNCS) { disk_frame_track(trk, n, r); return; }
    for (int k = 0; k < w.n; k++) { o2.first[o2.n] = w.first[k]; o2.last[o2.n] = w.last[k]; o2.n++; }
    for (; i < r->n; i++) { o2.first[o2.n] = r->first[i]; o2.last[o2.n] = r->last[i]; o2.n++; }
    *r = o2;
}

/* Frame every formatted half-track; half-tracks sharing one GCR buffer share one
 * framing. pool: storage for npool. Returns 0 if the pool is too small. */
static inline int disk_frame(disk_t *dk, syncs_t *pool, int npool)
{
    int used = 0;
    for (int i = 0; i < DISK_HALFTRACKS; i++) {
        dk->sync[i] = 0;
        if (!dk->gcr[i]) continue;
        for (int j = 0; j < i && !dk->sync[i]; j++)
            if (dk->gcr[j] == dk->gcr[i] && dk->len[j] == dk->len[i]) dk->sync[i] = dk->sync[j];
        if (dk->sync[i]) continue;
        if (used == npool) return 0;
        dk->sync[i] = &pool[used++];
        disk_frame_track(dk->gcr[i], dk->len[i] * 8u, dk->sync[i]);
    }
    return 1;
}

static inline int d64_zone(int t) { return t <= 17 ? 0 : t <= 24 ? 1 : t <= 30 ? 2 : 3; }
static inline int d64_sectors(int t) { static const uint8_t n[4] = { 21, 19, 18, 17 }; return n[d64_zone(t)]; }
/* bytes per rotation at 300 rpm [S10 p.31], whole bytes */
static inline int d64_track_bytes(int t) { static const uint16_t n[4] = { 7692, 7142, 6666, 6250 }; return n[d64_zone(t)]; }
static inline int d64_offset(int t, int s)
{ int n = 0; for (int i = 1; i < t; i++) n += d64_sectors(i); return (n + s) * 256; }

/* tail gaps: the top of [S10]'s range per zone; the rest of the track goes
 * after the last sector. ⚠ [A] a choice within the range: the DOS never reads them */
static inline int d64_tail_gap(int t) { static const uint8_t g[4] = { 7, 12, 8, 8 }; return g[d64_zone(t)]; }

static const uint8_t gcr_nybble[16] = {               /* [S10 p.114] */
    0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
    0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15 };

/* 4 bytes -> 5 GCR bytes */
static inline void gcr_encode4(const uint8_t *in, uint8_t *out)
{
    uint64_t v = 0;
    for (int i = 0; i < 4; i++) v = (v << 10) | ((uint64_t)gcr_nybble[in[i] >> 4] << 5) | gcr_nybble[in[i] & 15];
    for (int i = 4; i >= 0; i--) { out[i] = (uint8_t)v; v >>= 8; }
}

static inline uint8_t *gcr_put(uint8_t *o, uint8_t b, int n) { while (n--) *o++ = b; return o; }

static inline uint8_t *gcr_block(uint8_t *o, const uint8_t *b, int n)   /* n multiple of 4 */
{ for (int i = 0; i < n; i += 4, o += 5) gcr_encode4(b + i, o); return o; }

/* Build track t (1..35) of a D64 into out[d64_track_bytes(t)].
 * id1, id2: the disk ID [S10 p.32]. ⚠ [A] taken from the BAM's cosmetic ID
 * (track 18 sector 0, bytes 162-163 [S10 p.38]); a D64 does not record the
 * header ID. Any ID reads, because a SEEK sets the master ID [S10 p.120]. */
static inline void d64_track_sectors(const uint8_t *sec, int t, uint8_t id1, uint8_t id2, uint8_t *out);
static inline void d64_track(const uint8_t *d64, int t, uint8_t id1, uint8_t id2, uint8_t *out)
{ d64_track_sectors(d64 + d64_offset(t, 0), t, id1, id2, out); }

/* D64 error codes [S21], the FDC's [S10 ch. 6]: what each does to the sector's GCR.
 *   $02 (20) header not found: the header's $08 is not $08
 *   $03 (21) no SYNC: the sector's two SYNCs are gap instead (a whole track of them: no SYNC at all)
 *   $04 (22) data block not present: the block's $07 is not $07
 *   $05 (23) data checksum: the checksum is wrong
 *   $09 (27) header checksum: the checksum is wrong
 *   $0B (29) disk ID mismatch: the header's two ID bytes inverted (its checksum, an EOR of both, unchanged)
 * Anything else ($01 OK, and the write-only codes $06-$08, $0A, $0F) leaves the sector as it is. */
#define D64_ERR_OK 0x01
static inline void d64_track_sectors_err(const uint8_t *sec, int t, uint8_t id1, uint8_t id2, const uint8_t *err, uint8_t *out)
{
    uint8_t *o = out, *end = out + d64_track_bytes(t);
    for (int s = 0; s < d64_sectors(t); s++) {
        int e = err ? err[s] : D64_ERR_OK;
        uint8_t i1 = e == 0x0B ? (uint8_t)~id1 : id1, i2 = e == 0x0B ? (uint8_t)~id2 : id2;
        uint8_t h[8] = { 0x08, (uint8_t)(t ^ s ^ i1 ^ i2), (uint8_t)s, (uint8_t)t, i2, i1, 0x0F, 0x0F };
        if (e == 0x02) h[0] = 0x00;
        if (e == 0x09) h[1] ^= 0xFF;
        uint8_t sync = e == 0x03 ? 0x55 : 0xFF;
        o = gcr_put(o, sync, 5);
        o = gcr_block(o, h, 8);
        o = gcr_put(o, 0x55, 8);
        o = gcr_put(o, sync, 5);
        uint8_t blk[260], ck = 0;
        const uint8_t *src = sec + s * 256;
        blk[0] = e == 0x04 ? 0x00 : 0x07;
        for (int i = 0; i < 256; i++) { blk[1 + i] = src[i]; ck ^= src[i]; }
        blk[257] = e == 0x05 ? (uint8_t)~ck : ck; blk[258] = 0; blk[259] = 0;
        o = gcr_block(o, blk, 260);
        if (s + 1 < d64_sectors(t)) o = gcr_put(o, 0x55, d64_tail_gap(t));
    }
    gcr_put(o, 0x55, (int)(end - o));                  /* the long gap before sector 0 */
}

/* the same with no errors, from this track's sectors only (sec[s * 256]) */
static inline void d64_track_sectors(const uint8_t *sec, int t, uint8_t id1, uint8_t id2, uint8_t *out)
{ d64_track_sectors_err(sec, t, id1, id2, 0, out); }

/* Whole D64 into a disk_t, framed; buf must hold the sum of d64_track_bytes
 * (252,004), pool D64_TRACKS framings. */
#define D64_GCR_BYTES 252004
static inline void d64_to_disk(const uint8_t *d64, uint8_t *buf, disk_t *dk, syncs_t *pool)
{
    const uint8_t *bam = d64 + d64_offset(18, 0);
    for (int i = 0; i < DISK_HALFTRACKS; i++) { dk->gcr[i] = 0; dk->len[i] = 0; }
    for (int t = 1; t <= D64_TRACKS; t++) {
        d64_track(d64, t, bam[162], bam[163], buf);
        dk->gcr[2 * (t - 1)] = buf; dk->len[2 * (t - 1)] = (uint16_t)d64_track_bytes(t);
        buf += d64_track_bytes(t);
    }
    disk_frame(dk, pool, D64_TRACKS);
}

/* --- GCR -> D64: what the drive wrote, back to a D64 ---------------------
 * Bit-level: a data block is rewritten wherever the DOS's timing put it, so
 * it need not start on a byte of our track. A SYNC is ten or more 1s [S10
 * p.31]; the block starts at the first 0 after it (every GCR byte the DOS
 * puts after a SYNC, $08 or $07, begins with a 0 [S10 p.114]). */
static inline int gcr_bit(const uint8_t *t, uint32_t nbits, uint32_t p)
{ p %= nbits; return (t[p >> 3] >> (7 - (p & 7))) & 1; }

/* n bytes from the GCR at bit p; returns 0 if a 5-bit group is not a code */
static inline int gcr_decode_at(const uint8_t *t, uint32_t nbits, uint32_t p, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        uint8_t v = 0;
        for (int half = 0; half < 2; half++) {
            uint8_t g = 0;
            for (int k = 0; k < 5; k++) g = (uint8_t)((g << 1) | gcr_bit(t, nbits, p++));
            int nyb = -1;
            for (int j = 0; j < 16; j++) if (gcr_nybble[j] == g) nyb = j;
            if (nyb < 0) return 0;
            v = (uint8_t)((v << 4) | nyb);
        }
        out[i] = v;
    }
    return 1;
}

/* the bit after the next SYNC at or after p (within one revolution), or -1 */
static inline int64_t gcr_after_sync(const uint8_t *t, uint32_t nbits, uint32_t p)
{
    int ones = 0;
    for (uint32_t i = 0; i < 2 * nbits; i++) {
        if (gcr_bit(t, nbits, p + i)) ones++;
        else { if (ones >= 10) return (int64_t)(p + i); ones = 0; }
    }
    return -1;
}

/* Decode track t of a disk into sec[s * 256] (this track's sectors only).
 * Returns a mask, bit s set for each sector found with a good header, data
 * block and checksum; sectors not found are left as they were. */
static inline uint32_t disk_track_to_sectors_id(const disk_t *dk, int t, uint8_t *sec, uint16_t *ids);
static inline uint32_t disk_track_to_sectors(const disk_t *dk, int t, uint8_t *sec)
{ return disk_track_to_sectors_id(dk, t, sec, 0); }
/* the same; ids (if given): each found sector's header ID, ID 2 << 8 | ID 1 */
static inline uint32_t disk_track_to_sectors_id(const disk_t *dk, int t, uint8_t *sec, uint16_t *ids)
{
    const uint8_t *g = dk->gcr[2 * (t - 1)];
    if (!g) return 0;
    uint32_t nbits = dk->len[2 * (t - 1)] * 8u, p = 0;
    uint32_t found = 0;
    uint8_t seen[21] = { 0 };
    for (;;) {
        int64_t h = gcr_after_sync(g, nbits, p);
        if (h < 0) break;
        uint8_t hdr[8], blk[260];
        if (gcr_decode_at(g, nbits, (uint32_t)h, hdr, 8) && hdr[0] == 0x08 && hdr[3] == t &&
            hdr[2] < d64_sectors(t) && (hdr[1] ^ hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]) == 0) {
            int64_t d = gcr_after_sync(g, nbits, (uint32_t)h + 80);
            if (d >= 0 && gcr_decode_at(g, nbits, (uint32_t)d, blk, 260) && blk[0] == 0x07) {
                uint8_t ck = 0;
                for (int i = 1; i <= 256; i++) ck ^= blk[i];
                if (ck == blk[257] && !seen[hdr[2]]) {
                    for (int i = 0; i < 256; i++) sec[hdr[2] * 256 + i] = blk[1 + i];
                    seen[hdr[2]] = 1; found |= 1u << hdr[2];
                    if (ids) ids[hdr[2]] = (uint16_t)(hdr[4] << 8 | hdr[5]);
                }
            }
        }
        if ((uint64_t)h + 80 >= nbits) break;      /* once round the track */
        p = (uint32_t)h + 80;
    }
    return found;
}

/* the same into a whole D64; returns how many sectors were found */
static inline int disk_track_to_d64(const disk_t *dk, int t, uint8_t *d64)
{
    return __builtin_popcount(disk_track_to_sectors(dk, t, d64 + d64_offset(t, 0)));
}

static inline int disk_to_d64(const disk_t *dk, uint8_t *d64)
{
    int n = 0;
    for (int t = 1; t <= D64_TRACKS; t++) n += disk_track_to_d64(dk, t, d64);
    return n;
}
#endif
