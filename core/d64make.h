/* -------------------------------------------------------------------------
 * PicoD64 -- make a D64 from files: the virtual disk the emulated 1541 gets
 * when a program loaded in browse mode uploads drive code (a fast loader), so
 * the loader finds, by name, any PRG of the folder it came from.
 *
 * Layout from Inside Commodore DOS [S10]: the BAM in 18/0 (p.37: link to 18/1,
 * 'A', the BAM 4 bytes a track at byte 4 -- free count, then a bit map, bit n =
 * sector n, 1 = free (p.38) -- name at 144 padded with $A0, ID at 162, "2A" at
 * 165); directory sectors from 18/1, eight 32-byte entries each (p.43-45:
 * type at +2, first track/sector +3/+4, name +5..+20 padded $A0, blocks +30);
 * a file's sectors carry 254 bytes, linked by their first two bytes, the last
 * one with track 0 and the index of its last byte.
 * Placement: the DOS's interleave of 10 for files and 3 for the directory [A:
 * figures commonly given for the 1541, not yet found in [S10]; any placement
 * reads, the interleave only sets the speed]. Checked by VICE's c1541 (f6.sh).
 * ------------------------------------------------------------------------- */
#ifndef FF_D64MAKE_H
#define FF_D64MAKE_H
#include <stdint.h>
#include <string.h>
#include "disk.h"


static uint8_t *d64m_sec(uint8_t *d, int t, int s) { return d + d64_offset(t, s); }
static int d64m_free(uint8_t *d, int t, int s) { uint8_t *b = d64m_sec(d, 18, 0) + 4 * t; return (b[1 + s / 8] >> (s & 7)) & 1; }
static void d64m_use(uint8_t *d, int t, int s)
{
    uint8_t *b = d64m_sec(d, 18, 0) + 4 * t;
    if (d64m_free(d, t, s)) { b[1 + s / 8] &= (uint8_t)~(1 << (s & 7)); b[0]--; }
}

/* a blank formatted disk: name and id in PETSCII (padded/truncated here) */
static void d64_blank(uint8_t *d, const uint8_t *name, int nl, uint8_t id1, uint8_t id2)
{
    memset(d, 0, D64_BYTES);
    uint8_t *b = d64m_sec(d, 18, 0);
    b[0] = 18; b[1] = 1; b[2] = 0x41;
    for (int t = 1; t <= D64_TRACKS; t++) {
        int n = d64_sectors(t);
        b[4 * t] = (uint8_t)n;
        for (int s = 0; s < n; s++) b[4 * t + 1 + s / 8] |= (uint8_t)(1 << (s & 7));
    }
    memset(b + 144, 0xA0, 27);
    for (int i = 0; i < 16 && i < nl; i++) b[144 + i] = name[i];
    b[162] = id1; b[163] = id2; b[165] = '2'; b[166] = 'A';
    d64m_use(d, 18, 0); d64m_use(d, 18, 1);
    uint8_t *dir = d64m_sec(d, 18, 1);
    dir[0] = 0; dir[1] = 0xFF;                       /* the last directory sector */
}

/* the next free sector after s on track t, `il` on; -1 if the track is full */
static int d64m_next(uint8_t *d, int t, int s, int il)
{
    int n = d64_sectors(t);
    for (int k = 0; k < n; k++) { int c = (s + il + k) % n; if (d64m_free(d, t, c)) return c; }
    return -1;
}

/* track order: outward then inward from the directory, as the DOS fills a disk */
static int d64m_track(int i) { static const int o[34] = { 17, 19, 16, 20, 15, 21, 14, 22, 13, 23, 12, 24, 11, 25, 10, 26,
                                                      9, 27, 8, 28, 7, 29, 6, 30, 5, 31, 4, 32, 3, 33, 2, 34, 1, 35 };
                               return o[i]; }

/* Add a PRG (its bytes as stored: the load address first). 0 if the disk is full. */
static int d64_add_prg(uint8_t *d, const uint8_t *name, int nl, const uint8_t *data, long len)
{
    long blocks = len ? (len + 253) / 254 : 1, freeb = 0;
    for (int t = 1; t <= D64_TRACKS; t++) if (t != 18) freeb += d64m_sec(d, 18, 0)[4 * t];
    if (blocks > freeb) return 0;
    /* a directory slot: 8 per sector, a new sector (interleave 3 on 18) when full */
    uint8_t *e = 0;
    int dt = 18, ds = 1;
    for (;;) {
        uint8_t *sec = d64m_sec(d, dt, ds);
        for (int k = 0; k < 8 && !e; k++) if (sec[32 * k + 2] == 0) e = sec + 32 * k;
        if (e) break;
        if (sec[0]) { dt = sec[0]; ds = sec[1]; continue; }
        int ns = d64m_next(d, 18, ds, 3);
        if (ns < 0) return 0;
        d64m_use(d, 18, ns);
        sec[0] = 18; sec[1] = (uint8_t)ns;
        uint8_t *nsec = d64m_sec(d, 18, ns);
        memset(nsec, 0, 256); nsec[1] = 0xFF;
        dt = 18; ds = ns;
    }
    /* the data, 254 bytes a sector */
    int ti = 0, t = d64m_track(0), s = -10, ft = 0, fs = 0;
    uint8_t *prev = 0;
    for (long done = 0, b = 0; b < blocks; b++) {
        int ns;
        while ((ns = d64m_next(d, t, s < 0 ? 0 : s, s < 0 ? 0 : 10)) < 0) { t = d64m_track(++ti); s = -10; }
        s = ns; d64m_use(d, t, s);
        if (!prev) { ft = t; fs = s; } else { prev[0] = (uint8_t)t; prev[1] = (uint8_t)s; }
        uint8_t *sec = d64m_sec(d, t, s);
        long n = len - done > 254 ? 254 : len - done;
        memcpy(sec + 2, data + done, (size_t)n);
        done += n;
        sec[0] = 0; sec[1] = (uint8_t)(n + 1);       /* the last one, until another follows */
        prev = sec;
    }
    e[2] = 0x82; e[3] = (uint8_t)ft; e[4] = (uint8_t)fs;   /* PRG, closed */
    memset(e + 5, 0xA0, 16);
    for (int i = 0; i < 16 && i < nl; i++) e[5 + i] = name[i];
    e[30] = (uint8_t)blocks; e[31] = (uint8_t)(blocks >> 8);
    return 1;
}
#endif
