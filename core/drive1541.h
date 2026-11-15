/* -------------------------------------------------------------------------
 * PicoD64 -- the 1541 board around the CPU: memory map, two VIAs, the GCR
 * read head and the IEC lines. EVENT-DRIVEN: per cycle, only the wait, the
 * bus sample and one compare; the VIAs and the head do work only on the
 * cycles something happens. host/ref/ holds the per-cycle model this one is
 * checked against, cycle for cycle (host/equiv.sh).
 *
 * CLEAN ROOM. Board facts come from the 1541 Service Manual [S7] and the
 * 251748 schematic [S8]; every pin assignment not yet traced on [S8] is
 * marked [A] here and in PROVENANCE.md.
 *
 * TEMPLATE. The includer defines the platform, before including:
 *
 *   PLAT_SLACK(c)      sysclk left before cycle c's real time (< 0: late)
 *   PLAT_LA_BUDGET     slack needed to do the head lookahead in idle time
 *   PLAT_LEAD(c, n)    wait only while cycle c is more than n cycles ahead of real time
 *   PLAT_SYNC(c, k)    wait until real time reaches the start of cycle c;
 *                      k = SYNC_INSN (end of instruction) or SYNC_VIA1
 *   PLAT_IEC_IN()      bus lines now, bit set = line LOW: IEC_ATN|IEC_CLK|IEC_DATA
 *   PLAT_IEC_OUT(p)    lines this drive pulls low (IEC_CLK|IEC_DATA)
 * ------------------------------------------------------------------------- */
#ifndef FF_DRIVE1541_H
#define FF_DRIVE1541_H
#include <stdint.h>
static inline int drive_imasked(void);     /* the drive CPU's I flag (below): a masked T1 interrupt runs lazy */
#define VIA_IRQ_MASKED() drive_imasked()
#include "via6522.h"
#include "disk.h"
#ifndef FF_HOT
#define FF_HOT
#endif
#ifndef FF_SCRATCH
#define FF_SCRATCH FF_HOT   /* the platform's other fast memory for core 1's code (the board: SCRATCH_X, below core 1's stack) */
#endif
/* timing marks in the VIA1 write path, for a platform that measures them (fw/pico_main.c) */
#ifndef PLAT_EV
#define PLAT_EV(k) do { } while (0)
#endif
#ifndef PLAT_RMARK
#define PLAT_RMARK(n) do { } while (0)
#endif
#ifndef PLAT_CS
#define PLAT_CS(k, t0) do { } while (0)   /* the platform's cost meter (fw/pico_main.c COST): kind, start */
#define PLAT_CS_T() 0u
#define PLAT_CS_USED(t0) ((void)(t0))
#else
#define PLAT_CS_USED(t0) ((void)(t0))
#endif
#ifndef PLAT_MARK
#define PLAT_MARK(n) do { } while (0)
#endif
/* a VIA1 write, for a platform that logs them (host/fwtwin.c FWVIA1) */
#ifndef PLAT_VIA1_WR
#define PLAT_VIA1_WR(a, v, c) do { } while (0)
#endif
#ifndef FF_PROF
#define FF_PROF(b) do { } while (0)
#endif

enum { IEC_ATN = 1, IEC_CLK = 2, IEC_DATA = 4 };

#define GCR_TRACK_MAX 7928   /* bytes; the bench uses 7692 [A] */

typedef struct {
    uint32_t at;                  /* cycle of the last completed bit (or commit) */
    uint32_t bitpos;              /* next bit on the track */
    uint16_t sr;                  /* last 10 bits read */
    uint8_t  acc;                 /* 16 MHz ticks into the current cell, after `at` */
    uint8_t  bitcnt, sync;        /* framing, kept only on the slow path */
    uint8_t  canon;               /* past a SYNC on this track: the maps apply */
    uint8_t  since;               /* bits read on this track, up to 10 */
    uint8_t  wsr;                 /* write shift register */
} head_t;

#define HEAD_NONE 0xFFFFFFFFu


typedef struct {
    uint8_t  ram[2048];
    const uint8_t *rom;           /* 16 KiB, $C000-$FFFF */
    via6522_t via1, via2;         /* serial bus, disk controller [A: see F-1] */
    uint32_t cycles;
    /* read head */
    const disk_t *disk;
    uint8_t  ht, phase;           /* half-track under the head; stepper phase (VIA2 PB0-1) */
    uint8_t  wmode;               /* writing (VIA2 CB2 low) */
    uint32_t wfirst, wcount;      /* this track: where the writing began, how many bits since */
    const uint8_t *trk; uint32_t trk_bits;
    head_t   h, hnext;            /* committed head state; state at the next byte */
    uint32_t s1;                  /* this track: bit index of its first SYNC, or HEAD_NONE */
    const syncs_t *syncs;         /* this track's steady-state framing */
    uint32_t head_next;           /* cycle of the next BYTE READY (or of the lookahead's deadline) */
    uint8_t  hnext_val;           /* the byte that BYTE READY at hnext delivers */
    uint8_t  la_pending;          /* hnext not computed yet: head_next is only a lower bound */
    uint8_t  la_rvalid, la_rwrap; /* next_byte_bit's last region (2026-11-13): after run la_rq, up to the next run's */
    uint32_t la_rq, la_rnf;       /* start la_rnf, in its coordinates (p + n if la_rwrap); head_select and writes reset it */
    /* the coming bytes, worked out ahead in idle time (2026-11-13): reading on the fast path, each the head's state
     * after the byte (at, bitpos, acc) and its value, from the committed head as it stood at ring_base_*. A real 1541's
     * read electronics cost its CPU nothing; here a catch-up takes the next one, whatever region the head is in */
#define HEAD_RING 8
    struct { uint32_t at, bitpos; uint8_t acc, val; } ring[HEAD_RING];
    uint8_t  ring_r, ring_n, ring_cell;
    uint8_t  ring_base_acc;
    uint32_t ring_base_at, ring_base_pos;
    uint16_t ring_gen, trk_gen;   /* trk_gen: bumped when the track or its framing changes (head_select, writes) */
    uint32_t bytes_read;          /* stats */
    /* bus */
    uint8_t  iec_in, iec_out;
    uint8_t  gate;                /* the ATN gate's configuration (PLAT_IEC_GATE) */
    uint8_t  irq;                 /* /IRQ, cached: recomputed whenever a flag can change */
    uint32_t next_event;          /* earliest of the VIA and head events */
    /* LAZY DISK (2026-11-07): while nothing can see a byte at once (VIA2's CA1 interrupt off, not writing), the head's
     * events aren't scheduled; they are run when something looks (a VIA2 access, the V flag: CPU_VSYNC), in idle time
     * before a bus sync, and when drive_run returns. A fast loader's bus loop then pays nothing for the disk under it */
    uint32_t disk_due;            /* the head's next event while lazy; far ahead (now + VIA_NEVER) while not */
    uint8_t  imask;               /* the CPU's I flag, as its last change left it (cpu_run keeps P in a local copy) */
    uint8_t  so_pending;          /* a byte reached SO in a catch-up outside the CPU: V to set at the next CPU_VSYNC */
    uint32_t v_over;              /* the cycle V was last overwritten (CPU_VOVER): an SO up to then no longer counts */
    uint32_t run_until;           /* drive_run()'s stop cycle */
} drive_t;

static drive_t D;                 /* one drive, file scope: cheapest addressing */

#ifndef PLAT_TRACK_WRITTEN
#define PLAT_TRACK_WRITTEN(ht) do { } while (0)   /* half-track ht was written to */
#endif

/* Framing: each track's SYNC runs, built by whoever loads the disk (disk_frame,
 * core/disk.h; on the Pico, core 0), so inserting a disk is a pointer swap. A
 * track with none (blank, or never framed) stays on the slow path. */
static const syncs_t drive_nosync;
/* ⚠ [A] an unformatted half-track reads as no flux: zeros, no SYNC. A real
 * drive reads noise there. 6250 bytes = a zone-4 rotation [S10 p.31]. */
static const uint8_t drive_blank[6250];

/* --- IEC: ⚠ [A] VIA1 port B, from the 1541's commonly published map, not
 * yet traced on [S8]: PB0 DATA in, PB1 DATA out, PB2 CLK in, PB3 CLK out,
 * PB4 ATNA, PB5-6 device-number jumpers, PB7 ATN in; CA1 = ATN in.
 * Inputs read 1 when the line is LOW (inverting receivers, [S7] "inverted by
 * UA1"). DATA is also pulled by the ATN acknowledge gate: ATNA XOR ATN [S7]. */
/* The ATN acknowledge gate is HARDWARE on a real 1541: DATA follows ATN at once,
 * whatever the CPU is doing. The platform can do the same outside the emulation
 * (fw/: a PIO state machine), given the gate's two possible outputs:
 *   PLAT_IEC_GATE(cfg): bit 0 CLK pulled; bit 1 DATA pulled while ATN is released;
 *   bit 2 DATA pulled while ATN is asserted. (FB64 Turbo checks DATA 4 cycles after
 *   pulling ATN; sampled in software, the ack came ~24 us late: 2026-10-30.) */
#ifndef PLAT_IEC_GATE
#define PLAT_IEC_GATE(cfg) do { } while (0)
#endif
/* predict: after a VIA1 write only (below). From drive_iec_change the inputs are a fresh sample of the pins, which
 * already show the ATN gate's answer (hardware on a 1541, a PIO on the board); a guess there overwrote it */
static FF_HOT inline void drive_iec_eval_p(int predict)   /* (FF_HOT: if the compiler keeps it out of line, it stays in RAM) */
{
    uint8_t pb = via_pb(&D.via1);
    {
        uint8_t dout = (pb >> 1) & 1, atna = (pb >> 4) & 1;
        uint8_t cfg = (uint8_t)(((pb >> 3) & 1) | ((dout | atna) << 1) | ((dout | (atna ^ 1)) << 2));
        if (cfg != D.gate) { D.gate = cfg; PLAT_IEC_GATE(cfg); }
    }
    uint8_t atn = (D.iec_in & IEC_ATN) ? 1 : 0;
    uint8_t out = 0;
    if (pb & 0x08) out |= IEC_CLK;
    if ((pb & 0x02) || (atn ^ ((pb >> 4) & 1))) out |= IEC_DATA;
    if (out != D.iec_out) {
        D.iec_out = out; PLAT_IEC_OUT(out);
        /* ⚠ PREDICT THE ECHO. The bus will now read low wherever this drive
         * pulls (and, as a guess, high where it released). Without this the
         * next VIA1 access first processed the drive's own echo, ~0.3 us
         * between its deadline and the pins (F1d, 150 MHz, 2026-10-25).
         * A wrong guess (another device holding a line) is harmless: only a
         * VIA1 read can observe PB, and every VIA1 read samples the real bus
         * first. CLK/DATA do not reach CA1, so no flag is missed. */
        uint8_t pred = (uint8_t)((D.iec_in & IEC_ATN) | (out & (IEC_CLK | IEC_DATA)));
        /* ⚠ NOT after an input change (2026-11-10). There the guess "released where this drive released" overwrote
         * a sample just taken: Transwarp's receiver reads $1800 as the C64 releases ATN and pulls CLK and DATA; the
         * gate let go of DATA, the guess said CLK and DATA high, and that very read returned it: bits 5 and 7 of
         * 210 of 512 uploaded bytes lost (VICE black box, the board's read log). */
        if (predict && pred != D.iec_in) {
            D.iec_in = pred;
            D.via1.pb_in = (uint8_t)((D.via1.pb_in & 0xFA) | ((pred & IEC_DATA) ? 0x01 : 0) |
                                     ((pred & IEC_CLK) ? 0x04 : 0));
        }
    }
}

#define drive_iec_eval() drive_iec_eval_p(1)

/* --- GCR read head. Bit cell = 4 x (16 - zone) ticks of 16 MHz: dividers
 * 13..16 from [S7]'s table. Zone = VIA2 PB5-6 [A]. SYNC = the last ten bits
 * all 1; [S7] describes only the 8-bit stage, ten is [A].
 *
 * O(1) PER BYTE. 16 ticks pass per cycle; a bit completes when the ticks
 * since the last bit reach the cell. Two facts make the head cheap:
 *   1. TIMING is closed-form: from (at, acc), the j-th bit completes at
 *      at + ceil((j*cell - acc) / 16).
 *   2. FRAMING (where bytes complete, where SYNC is set) depends only on the
 *      bit sequence, not on timing. After the first SYNC it is the same on
 *      every revolution, so disk_frame() records it once per track as the
 *      list of its SYNC runs (syncs_t): bytes complete every 8 bits after
 *      each run, short of the next.
 * So the next BYTE READY is a binary search plus that formula, and SYNC on a
 * PB read is a division and a binary search. Until the head first passes a
 * SYNC (h.canon == 0), the bits are stepped one by one: the slow path.
 * Spec: host/ref/drive1541.h drive_head_tick(), checked by host/equiv.sh. */
static inline uint8_t head_cell(void) { return (uint8_t)(64 - 4 * ((D.via2.orb >> 5) & 3)); }
static inline uint8_t trk_bit(uint32_t p) { return (D.trk[p >> 3] >> (7 - (p & 7))) & 1; }

/* The SYNC run at or before bit p (cyclically): its index; *P = p, unwrapped
 * to the run's coordinates (p + n when p precedes every run's start). */
static inline int sync_run(uint32_t p, uint32_t *P)
{
    const syncs_t *r = D.syncs;
    if (p < r->first[0]) { *P = p + D.trk_bits; return r->n - 1; }
    int lo = 0, hi = r->n - 1;
    while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (r->first[mid] <= p) lo = mid; else hi = mid - 1; }
    *P = p; return lo;
}
/* start of the run after run i, in the same coordinates */
static inline uint32_t sync_next(int i)
{ const syncs_t *r = D.syncs; return i + 1 < r->n ? r->first[i + 1] : r->first[0] + D.trk_bits; }
static inline int is_sync_bit(uint32_t p) { uint32_t P; int i = sync_run(p, &P); return P <= D.syncs->last[i]; }
static inline int is_byte_bit(uint32_t p)
{
    uint32_t P; int i = sync_run(p, &P), q = (int)D.syncs->last[i];
    return P > (uint32_t)q && ((P - (uint32_t)q) & 7) == 0 && P < sync_next(i);
}

/* framing of one bit: returns 1 if it completes a byte */
static inline int frame_bit(head_t *h, uint8_t bit)
{
    h->sr = (uint16_t)(((h->sr << 1) | bit) & 0x3FF);
    if (h->sr == 0x3FF) { h->sync = 1; h->bitcnt = 0; return 0; }
    h->sync = 0;
    if (++h->bitcnt == 8) { h->bitcnt = 0; return 1; }
    return 0;
}

/* --- slow path: one bit at a time, used only before the first SYNC --- */
static inline uint32_t head_k(const head_t *h, uint8_t cell)
{ int k = (cell - h->acc + 15) / 16; return (uint32_t)(k < 1 ? 1 : k); }

/* WRITE MODE (spec: host/ref/drive1541.h): the bit comes from wsr and, if
 * `w`, goes onto the track; no SYNC; a byte every 8 bits. Only the committed
 * head (D.h) writes: lookahead copies step with w = 0, because what they
 * pass over may yet be cut short by a mode change. */
static inline int head_step(head_t *h, uint8_t cell, int w)
{
    uint32_t k = head_k(h, cell);
    h->at += k; h->acc = (uint8_t)(h->acc + 16 * k - cell);
    uint32_t p = h->bitpos;
    if (D.wmode) {
        uint8_t bit = (uint8_t)(h->wsr >> 7);
        h->wsr = (uint8_t)(h->wsr << 1);
        uint8_t *t = D.disk->gcr[D.ht];
        if (w && t) {
            t[p >> 3] = (uint8_t)((t[p >> 3] & ~(0x80 >> (p & 7))) | (bit << (7 - (p & 7))));
            if (!D.wcount) D.wfirst = p;
            D.wcount++;
        }
        h->bitpos = p + 1 == D.trk_bits ? 0 : p + 1;
        h->sr = (uint16_t)(((h->sr << 1) | bit) & 0x3FF);
        h->sync = 0;
        if (h->since < 10) h->since++;
        if (++h->bitcnt == 8) { h->bitcnt = 0; return 1; }
        return 0;
    }
    uint8_t bit = trk_bit(p);
    if (++p == D.trk_bits) p = 0;
    h->bitpos = p;
    int byte = frame_bit(h, bit);
    /* At a SYNC, with ten bits of THIS track in sr (so not after a step that
     * left the old track's bits in it), the framing is the steady state's:
     * from here on the maps describe this head exactly. */
    if (h->since < 10) h->since++;
    if (h->sync && h->since >= 10 && D.s1 != HEAD_NONE) h->canon = 1;
    return byte;
}

static inline void head_advance(head_t *h, uint32_t until, uint8_t cell, int w)
{
    while (!h->canon && (int32_t)(until - (h->at + head_k(h, cell))) >= 0) head_step(h, cell, w);
}


/* Put half-track `ht` under the head. */
static FF_SCRATCH void head_select(uint8_t ht)   /* (a step or an insert: the scratch bank, 2026-11-13) */
{
    D.ht = ht;
    if (D.disk->gcr[ht]) { D.trk = D.disk->gcr[ht]; D.trk_bits = D.disk->len[ht] * 8u; }
    else { D.trk = drive_blank; D.trk_bits = sizeof drive_blank * 8u; }
    D.syncs = D.disk->sync[ht] ? D.disk->sync[ht] : &drive_nosync;
    D.la_rvalid = 0; D.trk_gen++;
    D.s1 = D.syncs->n ? D.syncs->first[0] : HEAD_NONE;
}

/* next_byte_bit with its region remembered (2026-11-13): between two SYNC runs the answer is the same arithmetic on
 * the same run, so the binary search (sync_run) is only needed when the head leaves it. The lookahead's cost was
 * ~220 sysclk of a ~490 sysclk catch-up, and Transwarp's LDY $1C01 / STA $1800 have 4 cycles (600 sysclk) between */
static inline uint32_t next_byte_bit(uint32_t p);
static inline uint32_t next_byte_bit_r(uint32_t p)
{
    uint32_t n = D.trk_bits, P = p + (D.la_rwrap ? n : 0);
    if (D.la_rvalid && !D.wmode && P > D.la_rq && P < D.la_rnf) {
        uint32_t c = D.la_rq + 8 * ((P - D.la_rq + 7) >> 3);
        if (c < D.la_rnf) return c >= n ? c - n : c;
    }
    uint32_t Q; int i = sync_run(p, &Q);
    D.la_rvalid = 0;
    if (Q > D.syncs->last[i]) { D.la_rq = D.syncs->last[i]; D.la_rnf = sync_next(i); D.la_rwrap = Q != p; D.la_rvalid = 1; }
    return next_byte_bit(p);
}

/* first bit >= p (cyclically) that completes a byte; HEAD_NONE if none */
static inline uint32_t next_byte_bit(uint32_t p)
{
    const syncs_t *r = D.syncs;
    uint32_t n = D.trk_bits, P;
    int i = sync_run(p, &P);
    for (int k = 0; k <= r->n; k++) {
        uint32_t q = r->last[i], nf = sync_next(i);
        uint32_t c = P <= q ? q + 8 : q + 8 * ((P - q + 7) >> 3);
        if (c < nf) return c % n;
        /* none before the next run: carry on from its start */
        i = i + 1 < r->n ? i + 1 : 0;
        P = r->first[i];
    }
    return HEAD_NONE;
}

/* the 8 bits ending at bit e: the byte just completed */
static inline uint8_t byte_ending_at(uint32_t e)
{
    uint32_t s = e >= 7 ? e - 7 : e + D.trk_bits - 7, b = s >> 3, o = s & 7;
    uint32_t nb = D.trk_bits >> 3;
    uint16_t w = (uint16_t)((D.trk[b] << 8) | D.trk[b + 1 < nb ? b + 1 : 0]);
    return (uint8_t)(w >> (8 - o));
}

/* From D.h, find the next byte: D.hnext and D.head_next. */
static FF_HOT void head_find_next(void)
{
    uint8_t cell = head_cell();
    FF_PROF(2);
    D.la_pending = 0;
    D.hnext = D.h;
    if (!D.h.canon) {
        /* ⚠ A track with no byte in two revolutions would loop for ever */
        for (uint32_t n = 2 * D.trk_bits; n && !D.hnext.canon; n--)
            if (head_step(&D.hnext, cell, 0)) {
                D.head_next = D.hnext.at; D.hnext_val = (uint8_t)D.hnext.sr; return;
            }
        if (!D.hnext.canon) { D.head_next = D.h.at + VIA_NEVER; return; }
        /* crossed the first SYNC: the rest is the fast path, from there */
    }
    /* ⚠ A zone change to a shorter cell can leave acc >= cell: the bit in
     * progress then completes on the very next cycle, which the closed form
     * (it assumes acc < cell) would get wrong. Take that one bit explicitly. */
    if (D.hnext.acc >= cell) {
        uint32_t p = D.hnext.bitpos;
        D.hnext.at += 1; D.hnext.acc = (uint8_t)(D.hnext.acc + 16 - cell);
        D.hnext.bitpos = p + 1 == D.trk_bits ? 0 : p + 1;
        if (is_byte_bit(p)) { D.head_next = D.hnext.at; D.hnext_val = byte_ending_at(p); return; }
    }
    uint32_t idx = next_byte_bit_r(D.hnext.bitpos);
    if (idx == HEAD_NONE) { D.head_next = D.h.at + VIA_NEVER; return; }
    uint32_t j = (idx >= D.hnext.bitpos ? idx - D.hnext.bitpos : idx + D.trk_bits - D.hnext.bitpos) + 1;
    uint32_t k = (j * cell - D.hnext.acc + 15) >> 4;
    D.hnext.acc = (uint8_t)(D.hnext.acc + 16 * k - j * cell);
    D.hnext.at += k;
    D.hnext.bitpos = idx + 1 == D.trk_bits ? 0 : idx + 1;
    D.head_next = D.hnext.at;
    D.hnext_val = byte_ending_at(idx);
}

/* The byte event: commit and deliver BYTE READY. The lookahead to the next
 * byte is NOT done here: a byte event followed at once by an IEC write is a
 * common loader pattern, and the lookahead (~hundreds of sysclk) landed right
 * between them (F1d, 2026-10-25). It is left pending, with head_next set to
 * a LOWER BOUND (a byte needs at least 8 bits), and is done either in idle
 * time at a sync point (drive_sync) or, failing that, when the bound comes
 * due (drive_events). Zone changes compute it at once (head_commit callers). */
static inline uint8_t drive_pa_out(void) { return (uint8_t)((D.via2.ora & D.via2.ddra) | ~D.via2.ddra); }

static FF_HOT int head_byte(void)   /* returns 1 if BYTE READY reached SO */
{
    if (D.wmode) {   /* replay to hnext, writing; then reload from port A */
        uint8_t cell = head_cell();
        while (!head_step(&D.h, cell, 1)) { }
        D.h.wsr = drive_pa_out();
    } else {
        D.h = D.hnext;
        D.via2.pa_in = D.hnext_val;
    }
    D.bytes_read++;
    /* BYTE READY: to SO when SOE (VIA2 CA2) is high, and to VIA2 CA1 [A] */
    via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
    D.la_pending = 1;
    /* 8 bits from (at, acc), acc < cell here: at + ceil((8 cell - acc) / 16) */
    D.head_next = D.h.at + (uint32_t)((8 * head_cell() - D.h.acc + 15) >> 4);
    return via_ca2_high(&D.via2);
}

/* Bytes completing in the len bits from bit a (cyclic, len may exceed a revolution): how many, and the offset from a
 * of the last one (*off). The same bits is_byte_bit() takes: after a SYNC run's last bit q, every 8th, short of the
 * next run. O(SYNC runs passed), not O(bytes). Fast path only (D.syncs with runs). */
static inline uint32_t bytes_in(uint32_t a, uint32_t len, uint32_t *off)
{
    const syncs_t *r = D.syncs;
    uint32_t cnt = 0, done = 0, P;
    int i = sync_run(a, &P);
    *off = HEAD_NONE;
    while (done < len) {
        uint32_t q = r->last[i], nf = sync_next(i);    /* this run's region, in its coordinates: [P, nf) */
        uint32_t end = nf - P > len - done ? P + (len - done) : nf;
        uint32_t lo = P > q ? P : q + 1;
        uint32_t c0 = q + 8 * ((lo - q + 7) >> 3);     /* first byte bit >= lo */
        if (c0 < end) {
            uint32_t m = (end - 1 - c0) >> 3;
            cnt += m + 1;
            *off = done + (c0 + 8 * m - P);
        }
        done += end - P;
        if (done >= len) break;
        i = i + 1 < r->n ? i + 1 : 0;
        P = r->first[i];
    }
    return cnt;
}

/* A lazy catch-up with a long backlog (2026-11-11): rather than one event per byte (~200 sysclk each; a fast loader
 * that sends for a while, then reads, ran ~70 at once, ~100 us, and its next answers to the C64 went out that late:
 * Transwarp's garbled blocks), go straight to the last byte that completed by `now`. Nothing saw the ones in between
 * (lazy: no CA1 interrupt, no CPU access, V's reads catch up first), so the result is the same as the events one by
 * one: the head at that byte, PA = it, the CA1 edge (flag, latch), SO if one came after V was last overwritten, and
 * bytes_read. 1 if done; 0 leaves it to the event loop (slow path, writing, a cell still in progress, few bytes). */
static FF_SCRATCH __attribute__((noinline)) int head_skip(uint32_t now)
{
    uint8_t cell = head_cell();
    if (!D.h.canon || D.wmode || D.h.acc >= cell || D.syncs->n == 0) return 0;
    if (now - D.h.at > (1u << 26)) return 0;                 /* (16 x that must fit in 32 bits) */
    uint32_t nb = (D.h.acc + 16 * (now - D.h.at)) / cell;   /* bits completed by `now` */
    if (nb < 32) return 0;
    uint32_t off, cnt = bytes_in(D.h.bitpos, nb, &off);
    if (cnt < 2) return 0;
    uint32_t j = off + 1, k = (j * cell - D.h.acc + 15) >> 4;
    uint32_t e = (uint32_t)(((uint64_t)D.h.bitpos + off) % D.trk_bits);
    D.h.acc = (uint8_t)(D.h.acc + 16 * k - j * cell);
    D.h.at += k;
    D.h.bitpos = e + 1 == D.trk_bits ? 0 : e + 1;
    D.via2.pa_in = byte_ending_at(e);
    D.bytes_read += cnt;
    via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
    if (via_ca2_high(&D.via2) && (int32_t)(D.h.at - D.v_over) > 0) D.so_pending = 1;
    D.la_pending = 1;
    D.head_next = D.h.at + (uint32_t)((8 * cell - D.h.acc + 15) >> 4);
    return 1;
}

/* The same before the first SYNC on a track (2026-11-13): after a step the head works bit by bit (head_step) until it
 * passes a SYNC, and a fast loader that steps, then sends for a while without looking at the disk, left a backlog that
 * the catch-up then stepped through one bit at a time: Transwarp's reads came 240 us late. With ten bits of this track
 * in sr (since >= 10) there is no SYNC before the track's next mapped run (the maps are where sr is ten ones), so until
 * then a byte completes every 8 bits from the bit count as it stands: straight to the last one that completed by
 * `now`, short of that run, as head_skip does after one. The run itself is left to the event loop: a few bits, then
 * the fast path. 1 if done */
static FF_SCRATCH __attribute__((noinline)) int head_skip_nc(uint32_t now)
{
    uint8_t cell = head_cell();
    if (D.h.canon || D.wmode || D.h.since < 10 || D.h.acc >= cell) return 0;
    if (now - D.h.at > (1u << 26)) return 0;
    uint32_t nb = (D.h.acc + 16 * (now - D.h.at)) / cell;   /* bits completed by `now` */
    if (nb < 32) return 0;
    uint32_t p0 = D.h.bitpos, P, n = D.trk_bits, L = 0xFFFFFFFFu;   /* a track with no SYNC (a half-track, an empty one):
                                                                       a byte every 8 bits for ever */
    if (D.syncs->full) return 0;                        /* (n = 0 there is "couldn't frame", not "no SYNC": bit by bit) */
    if (D.syncs->n) {
        int i = sync_run(p0, &P);
        if (P <= D.syncs->last[i]) return 0;                 /* the next bit is a SYNC bit */
        L = sync_next(i) - P;                               /* bits before the next run */
    }
    uint32_t m = nb < L ? nb : L, t1 = 8u - D.h.bitcnt;    /* the first byte completes on bit t1 (1-based) */
    if (m < t1 + 8) return 0;                               /* fewer than two bytes */
    uint32_t cnt = 1 + (m - t1) / 8, j = t1 + 8 * (cnt - 1);
    uint32_t e = (p0 + j - 1) % n;                          /* the last byte's last bit */
    uint32_t k = (j * cell - D.h.acc + 15) >> 4;
    D.h.acc = (uint8_t)(D.h.acc + 16 * k - j * cell);
    D.h.at += k;
    D.h.bitpos = e + 1 == n ? 0 : e + 1;
    D.via2.pa_in = byte_ending_at(e);
    D.h.sr = (uint16_t)((byte_ending_at((e + n - 8) % n) & 3) << 8 | D.via2.pa_in);   /* the last ten bits */
    D.h.bitcnt = 0; D.h.sync = 0;
    D.bytes_read += cnt;
    via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
    if (via_ca2_high(&D.via2) && (int32_t)(D.h.at - D.v_over) > 0) D.so_pending = 1;
    D.la_pending = 1;
    D.head_next = D.h.at + (uint32_t)((8 * cell - D.h.acc + 15) >> 4);
    return 1;
}

/* SYNC on VIA2 PB7, active low [A], as it stands at cycle `now`. */
static inline void head_sync_to_pb(uint32_t now)
{
    uint8_t cell = head_cell(), sync;
    head_t t = D.h;
    head_advance(&t, now, cell, 0);
    if (!t.canon) sync = t.sync;
    else {
        uint32_t n = (t.acc + 16 * (now - t.at)) / cell;     /* bits completed since t.at */
        uint32_t last = (t.bitpos + n + D.trk_bits - 1) % D.trk_bits;
        sync = (uint8_t)is_sync_bit(last);
    }
    D.via2.pb_in = (uint8_t)((D.via2.pb_in & 0x7F) | (sync ? 0 : 0x80));
}

/* VIA2 PB is about to change at cycle `now`: commit the head under the OLD
 * zone; the caller then writes PB and calls head_find_next(). */
static FF_HOT inline void head_commit(uint32_t now)
{
    uint8_t cell = head_cell();
    head_advance(&D.h, now, cell, 1);
    uint32_t ticks = D.h.acc + 16 * (now - D.h.at);
    if (D.h.canon) {
        uint32_t n = ticks / cell;
        D.h.bitpos = (D.h.bitpos + n) % D.trk_bits;
        ticks -= n * cell;
    }
    D.h.acc = (uint8_t)ticks; D.h.at = now;
}

/* --- events --------------------------------------------------------------- */
static inline void drive_irq_update(void)
{ D.irq = (uint8_t)(via_irq(&D.via1) | via_irq(&D.via2)); }

#ifdef DRIVE_EAGER                                   /* host/equiv.sh: the eager path checked on every workload too */
static inline int drive_lazy(void) { return 0; }
#else
static inline int drive_lazy(void) { return !(D.via2.ier & IFR_CA1) && !D.wmode; }
#endif
static inline void drive_resched(uint32_t now)
{
    uint32_t n = D.head_next - now, a = via_next(&D.via1, now), b = via_next(&D.via2, now);
    if (drive_lazy()) { D.disk_due = D.head_next; n = VIA_NEVER; } else D.disk_due = now + VIA_NEVER;
    if (a < n) n = a;
    if (b < n) n = b;
    D.next_event = now + n;
}

/* Everything due at cycle `now`, in the per-cycle model's order: VIA1, VIA2,
 * head. Kept out of line: it runs about once every three cycles. */
static FF_HOT __attribute__((noinline)) int drive_events(uint32_t now)   /* 1: SO */
{
    int so = 0;
    FF_PROF(1); PLAT_EV(0);
    if (D.via1.t1_next == now) PLAT_EV(1);
    if (D.via2.t1_next == now) PLAT_EV(2);
    if ((D.via1.t2_armed && D.via1.t2_next == now) || (D.via2.t2_armed && D.via2.t2_next == now)) PLAT_EV(3);
    via_events(&D.via1, now);
    via_events(&D.via2, now);
    if (D.head_next == now && !drive_lazy()) {
        if (D.la_pending) { PLAT_EV(4); head_find_next(); }   /* the bound came due: compute it now */
        if (D.head_next == now) { PLAT_EV(5); so = head_byte(); }
    }
    drive_irq_update();
    drive_resched(now);
    return so;
}

/* The ring (above) holds for the head as it is now: same track and framing, zone, mode, and the committed head where
 * the ring started from (it moves on with each byte taken from it) */
static inline int head_ring_ok(uint8_t cell)
{
    return D.ring_gen == D.trk_gen && D.ring_cell == cell && D.h.canon && !D.wmode &&
           D.h.at == D.ring_base_at && D.h.bitpos == D.ring_base_pos && D.h.acc == D.ring_base_acc;
}
/* One more byte onto the ring: 1 if added. The same arithmetic as head_find_next's fast path, from the last entry */
static FF_HOT __attribute__((noinline)) int head_ring_fill(void)
{
    uint8_t cell = head_cell();
    if (!D.la_rvalid) return 0;                     /* only the cheap case, in a wait: a new region's search is the catch-up's */
    if (!D.h.canon || D.wmode) return 0;
    if (D.ring_n && !head_ring_ok(cell)) D.ring_n = 0;
    if (D.ring_n == HEAD_RING) return 0;
    if (!D.ring_n) {
        D.ring_r = 0; D.ring_cell = cell; D.ring_gen = D.trk_gen;
        D.ring_base_at = D.h.at; D.ring_base_pos = D.h.bitpos; D.ring_base_acc = D.h.acc;
    }
    uint32_t at, pos; uint8_t acc;
    if (D.ring_n) { int l = (D.ring_r + D.ring_n - 1) % HEAD_RING; at = D.ring[l].at; pos = D.ring[l].bitpos; acc = D.ring[l].acc; }
    else { at = D.h.at; pos = D.h.bitpos; acc = D.h.acc; }
    if (acc >= cell) return 0;
    uint32_t tb = D.trk_bits, P = pos + (D.la_rwrap ? tb : 0);
    if (!(P > D.la_rq && P < D.la_rnf)) return 0;
    uint32_t c = D.la_rq + 8 * ((P - D.la_rq + 7) >> 3);
    if (c >= D.la_rnf) return 0;                    /* the next run starts first: the catch-up's slow path takes it */
    uint32_t idx = c >= tb ? c - tb : c;
    uint32_t j = (idx >= pos ? idx - pos : idx + tb - pos) + 1;
    uint32_t k = (j * cell - acc + 15) >> 4;
    int w = (D.ring_r + D.ring_n) % HEAD_RING;
    D.ring[w].at = at + k;
    D.ring[w].acc = (uint8_t)(acc + 16 * k - j * cell);
    D.ring[w].bitpos = idx + 1 == tb ? 0 : idx + 1;
    D.ring[w].val = byte_ending_at(idx);
    D.ring_n++;
    return 1;
}
#ifndef PLAT_RING_FILL
#define PLAT_RING_FILL() do { while (head_ring_fill()) { } } while (0)   /* (host: at every sync; the board, in its waits) */
#endif

/* Lazy disk: run the head's events up to cycle `now`, in order, as drive_events would have at their cycles. A byte that
 * reached SO leaves so_pending for the CPU (CPU_VSYNC). Out of line: it runs when something looks or in idle time. */
static FF_HOT __attribute__((noinline)) void drive_disk_catchup_n(uint32_t now, int n)   /* at most n events */
{
    uint32_t t0 = PLAT_CS_T();
    if (n > 1 && (int32_t)(now - D.head_next) >= 0) { if (!head_skip(now)) head_skip_nc(now); }   /* a long backlog: straight to its last byte */
    while ((int32_t)(now - D.head_next) >= 0 && n-- > 0) {
        PLAT_CS(5, 0);
        /* The common case in one go (2026-11-13): reading, on the fast path, between two SYNC runs (the lookahead's
         * region, next_byte_bit_r): the next byte's end, its time and value, as head_find_next works them out, and its
         * commit, as head_byte does, with no calls and no copy of the head. Transwarp's LDY $1C01 / STA $1800 have 4
         * cycles (600 sysclk) between them; the calls took ~280 sysclk of them */
        if (D.ring_n) {                                 /* the next byte, worked out ahead */
            uint8_t cell = head_cell();
            if (head_ring_ok(cell)) {
                int r = D.ring_r;
                uint32_t at = D.ring[r].at;
                if ((int32_t)(now - at) < 0) { D.head_next = at; break; }
                D.h.at = at; D.h.acc = D.ring[r].acc; D.h.bitpos = D.ring[r].bitpos;
                D.via2.pa_in = D.ring[r].val;
                D.ring_base_at = at; D.ring_base_pos = D.h.bitpos; D.ring_base_acc = D.h.acc;
                D.ring_r = (uint8_t)((r + 1) % HEAD_RING); D.ring_n--;
                D.bytes_read++;
                via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
                if (via_ca2_high(&D.via2) && (int32_t)(at - D.v_over) > 0) D.so_pending = 1;
                D.la_pending = 1;
                D.head_next = D.ring_n ? D.ring[D.ring_r].at : at + (uint32_t)((8 * cell - D.h.acc + 15) >> 4);
                PLAT_EV(5);
                continue;
            }
            D.ring_n = 0;
        }
        if (D.h.canon && !D.wmode && D.la_rvalid) {
            uint8_t cell = head_cell();
            uint32_t tb = D.trk_bits, p = D.h.bitpos, P = p + (D.la_rwrap ? tb : 0);
            if (D.h.acc < cell && P > D.la_rq && P < D.la_rnf) {
                uint32_t c = D.la_rq + 8 * ((P - D.la_rq + 7) >> 3);
                if (c < D.la_rnf) {
                    uint32_t idx = c >= tb ? c - tb : c;
                    uint32_t j = (idx >= p ? idx - p : idx + tb - p) + 1;
                    uint32_t k = (j * cell - D.h.acc + 15) >> 4, at = D.h.at + k;
                    if ((int32_t)(now - at) < 0) { D.head_next = at; break; }   /* not yet: the exact time, a bound still */
                    D.h.acc = (uint8_t)(D.h.acc + 16 * k - j * cell);
                    D.h.at = at;
                    D.h.bitpos = idx + 1 == tb ? 0 : idx + 1;
                    D.via2.pa_in = byte_ending_at(idx);
                    D.bytes_read++;
                    via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
                    if (via_ca2_high(&D.via2) && (int32_t)(at - D.v_over) > 0) D.so_pending = 1;
                    D.la_pending = 1;
                    D.head_next = at + (uint32_t)((8 * cell - D.h.acc + 15) >> 4);
                    PLAT_EV(5);
                    continue;
                }
            }
        }
        /* (before a track's first SYNC the skip needs ten bits of it first: the events below give them, then skip) */
        if (n > 1 && !D.h.canon && D.h.since >= 10 && (int32_t)(now - D.head_next) > 64 && head_skip_nc(now) && (int32_t)(now - D.head_next) < 0) break;
        if (D.la_pending) { head_find_next(); if ((int32_t)(now - D.head_next) < 0) break; }
        PLAT_EV(5);
        uint32_t at = D.head_next;
        if (head_byte() && (int32_t)(at - D.v_over) > 0) D.so_pending = 1;
    }
    PLAT_RING_FILL();   /* (the host: so the ring is exercised on every workload) */
    drive_irq_update();
    drive_resched(now);
    PLAT_CS(4, t0); PLAT_CS_USED(t0);
}
static FF_HOT __attribute__((noinline)) void drive_disk_catchup(uint32_t now) { drive_disk_catchup_n(now, 0x7FFFFFFF); }
#define DRIVE_DISK_CATCHUP(now) ((int32_t)((now) - D.disk_due) >= 0 ? drive_disk_catchup(now) : (void)0)

static FF_HOT __attribute__((noinline)) void drive_iec_change(uint8_t in)
{
    FF_PROF(16);
    D.iec_in = in;
    /* PB0/2/7 read 1 when DATA/CLK/ATN are low [A] */
    D.via1.pb_in = (uint8_t)((D.via1.pb_in & 0x7A) | ((in & IEC_DATA) ? 0x01 : 0) |
                   ((in & IEC_CLK) ? 0x04 : 0) | ((in & IEC_ATN) ? 0x80 : 0));
    via_set_ca1(&D.via1, (in & IEC_ATN) ? 1 : 0);
    drive_iec_eval_p(0);
    drive_irq_update();
}

/* --- time ------------------------------------------------------------------
 * TWO CLOCKS. Every access advances emulated time and runs the events due
 * (timers, BYTE READY): deterministic, so they need no real time. Only what
 * the outside world can see or change is tied to real time:
 *   - a VIA1 access (the IEC port) waits for its exact cycle, then samples
 *     the bus or drives it (drive_sync);
 *   - at instruction ends, while the ATN interrupt can fire, the bus is
 *     sampled under a lead limit (drive_lead, below).
 * In between, the emulator runs ahead of real time, having consumed no input. */
/* A VIA1 write: the platform may start it early, by the time its own output path takes (PLAT_SYNC_WR) */
#ifndef PLAT_SYNC_WR
#define PLAT_SYNC_WR(c) PLAT_SYNC((c), 1)
#endif
#ifndef PLAT_LA_BUDGET_WR
#define PLAT_LA_BUDGET_WR PLAT_LA_BUDGET
#endif
static FF_HOT __attribute__((noinline)) void drive_sync(int kind)
{
    /* idle work first, if there is time before the deadline for it (a write's starts early: PLAT_LA_BUDGET_WR):
     * the lazy disk's events so far, then the lookahead */
    /* one event at a time, the slack checked before each: a backlog run in one go made the read itself late */
    while ((int32_t)(D.cycles - D.disk_due) >= 0 && PLAT_SLACK(D.cycles) > (kind == 2 ? PLAT_LA_BUDGET_WR : PLAT_LA_BUDGET))
        drive_disk_catchup_n(D.cycles, 1);
    if (D.la_pending && PLAT_SLACK(D.cycles) > (kind == 2 ? PLAT_LA_BUDGET_WR : PLAT_LA_BUDGET)) {
        head_find_next(); drive_resched(D.cycles);
    }
    PLAT_RING_FILL();
    if (kind == 2) { PLAT_SYNC_WR(D.cycles); return; }   /* a write needn't take the inputs: the next read does, on its cycle */
    PLAT_SYNC(D.cycles, kind);
    uint8_t in = PLAT_IEC_IN();
    if (in != D.iec_in) drive_iec_change(in);
}
enum { SYNC_INSN = 0, SYNC_VIA1 = 1, SYNC_VIA1_WR = 2 };

/* One instruction end. ATN is sampled so it can reach CA1 and the IRQ line,
 * but only when that can matter: CA1 interrupt enabled and I clear (fast
 * loaders run with SEI). An ATN edge still sets IFR CA1 exactly as the CPU
 * sees it, because every VIA1 read waits for real time first.
 *
 * ⚠ LEAD LIMIT, not a sync. Waiting for real time at every instruction end
 * stopped the emulator from ever getting ahead, so it had no slack to absorb
 * bursts (F1d at 150 MHz, 2026-10-25: 2.4 us jitter with the ATN IRQ on).
 * Here it may run up to DRIVE_LEAD cycles ahead of real time; it waits only
 * beyond that. The bus sampled at that moment is real time, which may be up
 * to DRIVE_LEAD cycles behind the emulated cycle, so:
 * ⚠ DEVIATION [A]: an ATN edge reaches CA1 up to DRIVE_LEAD + DRIVE_LEAD_EVERY
 * cycles (plus the instruction) late. And when the emulator is BEHIND real
 * time it samples the bus later than the emulated cycle, so an edge can land
 * early by that lateness (sub-microsecond in F1d). IEC reads and writes
 * still wait for their exact cycle; their lateness is what F1d measures. */
#ifndef DRIVE_LEAD
#define DRIVE_LEAD 16
#endif
static FF_HOT __attribute__((noinline)) void drive_lead(void)
{
    while ((int32_t)(D.cycles - D.disk_due) >= 0 && PLAT_SLACK(D.cycles) > PLAT_LA_BUDGET) drive_disk_catchup_n(D.cycles, 1);
    if (D.la_pending && PLAT_SLACK(D.cycles) > PLAT_LA_BUDGET) {
        head_find_next(); drive_resched(D.cycles);
    }
    PLAT_LEAD(D.cycles, DRIVE_LEAD);
    uint8_t in = PLAT_IEC_IN();
    if (in != D.iec_in) drive_iec_change(in);
}

/* --- the memory map [S7]: RAM $0000-$07FF, VIAs at $1800 and $1C00, ROM
 * $C000-$FFFF. ⚠ Mirroring and open-bus behaviour are [A] until [S8]'s
 * decoder is traced: RAM, ROM and each VIA are assumed mirrored.
 * A VIA access may change its flags, timers or (VIA2 PB) the head's zone, so
 * it is followed by the IRQ and event bookkeeping; RAM and ROM are not.
 * VIA1 accesses first wait for real time (drive_sync). */
static FF_HOT __attribute__((noinline)) uint8_t drive_via_rd(uint16_t a)
{
    FF_PROF(4); PLAT_RMARK(0);
    uint32_t now = D.cycles; uint8_t v;
    if ((a & 0x1C0F) == 0x1800) {
        /* VIA1 port B, a fast loader's every read (2026-11-07): what via_read does for it (the CB flags cleared, the
         * pins), and the IRQ line only if a CB flag was set. Timers and the head can't change: no other work */
        drive_sync(SYNC_VIA1); PLAT_RMARK(1);
        uint8_t f = D.via1.ifr & (IFR_CB1 | IFR_CB2);
        D.via1.ifr &= (uint8_t)~(IFR_CB1 | IFR_CB2);
        v = via_pb(&D.via1); PLAT_RMARK(2);
        if (f) drive_irq_update();
        PLAT_RMARK(3);
        return v;
    }
    if ((a & 0x1C00) == 0x1800) { drive_sync(SYNC_VIA1); PLAT_RMARK(1); v = via_read(&D.via1, (uint8_t)a, now); PLAT_RMARK(2); }
    else {
        DRIVE_DISK_CATCHUP(now);                   /* the disk, up to now: VIA2 shows it */
        if ((a & 15) == 0) head_sync_to_pb(now);   /* SYNC is only ever seen here */
        v = via_read(&D.via2, (uint8_t)a, now);
    }
    drive_irq_update(); PLAT_RMARK(3);
    return v;
}

/* --- the stepper. ⚠ [A] STP0-1 = VIA2 PB0-1 (the commonly published map; [S7]
 * says only "two outputs on port B of UC2", a binary count driving four
 * phases). One count = one half-track [S10 p.30]; counting up moves inward
 * (to higher tracks) [A]; a jump of two counts is ambiguous and does not move
 * [A]. Half-track 0 (track 1) is the mechanical stop [S10 p.30]. The head
 * moves at once [A: a real head takes milliseconds].
 * The bit under the head keeps its ANGLE: the bit position is scaled by the
 * ratio of the track lengths. sr and the bit count carry over: the read
 * electronics don't know the head moved. Spec: host/ref/drive1541.h. */
/* On the fast path sr, sync and bitcnt are not kept (the maps stand in for
 * them). Leaving it, rebuild them from the current track as it stands at
 * h->bitpos: sr = the last ten bits; sync and bitcnt from the maps (bitcnt =
 * bits since the last byte or SYNC bit, at most 7). */
static FF_SCRATCH void head_unmap(head_t *h)
{
    if (!h->canon) return;
    uint32_t n = D.trk_bits, last = (h->bitpos + n - 1) % n;
    uint16_t sr = 0;
    for (uint32_t k = 10; k > 0; k--) sr = (uint16_t)((sr << 1) | trk_bit((last + n + 1 - k) % n));
    h->sr = sr;
    uint32_t P; int i = sync_run(last, &P);
    h->sync = (uint8_t)(P <= D.syncs->last[i]);
    h->bitcnt = h->sync ? 0 : (uint8_t)((P - D.syncs->last[i]) & 7);
}

/* The current track was written: bring its framing up to date, round the bits
 * written only (disk_frame_span; F5: a full rebuild was 1.1 ms on the Pico). */
static FF_SCRATCH void head_remap(void)
{
    if (!D.disk->gcr[D.ht]) return;
    syncs_t *r = D.disk->sync[D.ht];
    uint32_t c = D.wcount;
    D.wcount = 0;
    if (!r) return;
    disk_frame_span(D.trk, D.trk_bits, r, D.wfirst, c);   /* only round what was written */
    D.la_rvalid = 0; D.trk_gen++;
    D.s1 = r->n ? r->first[0] : HEAD_NONE;
    if (c) PLAT_TRACK_WRITTEN(D.ht);    /* the board writes it back to the card (fw/) */
}

/* VIA2 PCR written: CB2 may have switched read/write. Called with the head
 * committed to `now` under the old mode. */
static void drive_mode(void)
{
    uint8_t w = (D.via2.pcr & 0xE0) == 0xC0;
    if (w == D.wmode) return;
    if (w) head_unmap(&D.h);           /* the fast path's framing, made explicit */
    else head_remap();
    D.wmode = w;
    D.h.canon = 0;
}

static void drive_stepper(void)
{
    uint8_t ph = via_pb(&D.via2) & 3, ht = D.ht;
    if (ph == ((D.phase + 1) & 3)) { if (ht < DISK_HALFTRACKS - 1) ht++; }
    else if (ph == ((D.phase + 3) & 3)) { if (ht > 0) ht--; }
    D.phase = ph;
    if (ht == D.ht) return;
    head_unmap(&D.h);
    if (D.wmode) head_remap();         /* leaving a track written on */
    uint32_t old_bits = D.trk_bits;
    head_select(ht);
    D.h.bitpos = (uint32_t)((uint64_t)D.h.bitpos * D.trk_bits / old_bits);
    D.h.canon = 0; D.h.since = 0;
}

/* The track under the head has come into RAM since the head got there (fw/storage.c st_track_poll: a
 * disk of over 35 tracks). As a step: the head reads it from here on. Between instructions. */
static FF_HOT void drive_retrack(void)
{
    DRIVE_DISK_CATCHUP(D.cycles);
    if (!D.disk || !D.disk->gcr[D.ht] || D.trk == D.disk->gcr[D.ht]) return;
    head_unmap(&D.h);
    uint32_t old_bits = D.trk_bits;
    head_select(D.ht);
    D.h.bitpos = (uint32_t)((uint64_t)D.h.bitpos * D.trk_bits / old_bits);
    D.h.canon = 0; D.h.since = 0;
}

/* --- disk change -----------------------------------------------------------
 * A real 1541 knows a disk was changed only by its write-protect sensor (VIA2
 * PB4, WPS [S8]): a disk sliding in or out passes the light beam, the DOS sees
 * the change in its IRQ and sets WPSW ($1C), and the next command re-reads the
 * disk's ID and BAM [S10 p.220, p.248]. So a swap is a SEQUENCE the platform
 * plays in time: drive_wps(0) while a disk passes, drive_insert(NULL) with
 * drive_wps(1) while there is none, and the same into the new disk. Both act at
 * the current cycle, between instructions.
 * ⚠ [A] light = 1 = not protected (the DOS writes with PB4 high, F4d); no disk
 * lets the light through. Spec: host/ref/drive1541.h. */
static const disk_t drive_nodisk;
static void drive_wps(int light) { D.via2.pb_in = (uint8_t)((D.via2.pb_in & ~0x10) | (light ? 0x10 : 0)); }
static FF_SCRATCH void drive_insert(const disk_t *dk)
{
    uint32_t now = D.cycles;
    DRIVE_DISK_CATCHUP(now);
    head_commit(now);
    head_unmap(&D.h);
    if (D.wmode) head_remap();         /* leaving a track written on */
    uint32_t old_bits = D.trk_bits;
    D.disk = dk ? dk : &drive_nodisk;
    head_select(D.ht);
    D.h.bitpos = (uint32_t)((uint64_t)D.h.bitpos * D.trk_bits / old_bits);
    D.h.canon = 0; D.h.since = 0;
    head_find_next(); drive_irq_update(); drive_resched(now);
}

static FF_HOT __attribute__((noinline)) void drive_via_wr(uint16_t a, uint8_t v)
{
    FF_PROF(8);
    PLAT_RMARK(4);
    uint32_t now = D.cycles;
    if ((a & 0x1C00) == 0x1800 && (a & 15) == 0 && !((v ^ D.via1.orb) & D.via1.ddrb & 0x1A)) {
        /* port B, and no output it drives changes (CLK, DATA, ATNA): nothing outside can see it, so it doesn't wait for
         * real time. A read-modify-write's first write is one (it writes back what it read): LSR $1800 in a loader's
         * command loop read, wrote and wrote on three cycles running, more than the board can do in three cycles
         * (first board, 2026-11-06) */
        via_write(&D.via1, (uint8_t)a, v, now);
        PLAT_VIA1_WR(a, v, now);
    } else if ((a & 0x1C0F) == 0x1800) {
        /* VIA1 port B, an output changing (2026-11-07): what via_write does for it, the pins, and the IRQ line only if a
         * CB flag was set; timers and the head can't change, so no reschedule. A fast loader's every answer */
        drive_sync(SYNC_VIA1_WR); PLAT_MARK(0);
        uint8_t f = D.via1.ifr & (IFR_CB1 | IFR_CB2);
        D.via1.orb = v; D.via1.ifr &= (uint8_t)~(IFR_CB1 | IFR_CB2); PLAT_MARK(1);
        drive_iec_eval(); PLAT_MARK(2);
        PLAT_VIA1_WR(a, v, now);
        if (f) drive_irq_update();
        return;
    } else if ((a & 0x1C00) == 0x1800) {
        drive_sync(SYNC_VIA1_WR); PLAT_MARK(0); via_write(&D.via1, (uint8_t)a, v, now); PLAT_MARK(1); drive_iec_eval(); PLAT_MARK(2);
        PLAT_VIA1_WR(a, v, now);                  /* (the platform may log it: fw/pico_main.c) */
    } else if (DRIVE_DISK_CATCHUP(now), (a & 15) == 0) {   /* (VIA2: the disk up to now first) PB: the zone or the track may change mid-bit */
        head_commit(now); via_write(&D.via2, (uint8_t)a, v, now); drive_stepper(); head_find_next();
    } else if ((a & 15) == 0xC) { /* PCR: read/write may change mid-bit */
        head_commit(now); via_write(&D.via2, (uint8_t)a, v, now); drive_mode(); head_find_next();
    } else via_write(&D.via2, (uint8_t)a, v, now);
    drive_irq_update(); PLAT_MARK(3);
    drive_resched(now); PLAT_MARK(4);
}

/* --- the bus, as cpu6502.h's cpu_run() sees it ------------------------------
 * REGISTER-RESIDENT: the cycle count, the next event's cycle and the IRQ line
 * are locals of cpu_run (cyc, nev, irq), so a RAM or ROM access is: count,
 * compare, load. D is written back only around the out-of-line paths, and
 * reloaded after them. */
#define BUS_LOCALS  uint32_t cyc = D.cycles, nev = D.next_event, until = D.run_until, lead_at = cyc; \
                    uint8_t irq = D.irq;
#define BUS_SAVE()  (D.cycles = cyc)
#define BUS_RELOAD() do { nev = D.next_event; irq = D.irq; } while (0)
#define DRIVE_NOW   cyc
/* SO: BYTE READY sets V within the access (equivalent to after it: the
 * access itself never reads P, and nothing between reads it) */
#define BUS_TICK()  do { if (++cyc == nev) { D.cycles = cyc; \
                        if (drive_events(cyc)) { c->p |= F_V; } BUS_RELOAD(); } } while (0)
#ifdef DRIVE_TRACE
#define BUS_TRACE(a, v, rw) DRIVE_TRACE(a, v, rw)
#else
#define BUS_TRACE(a, v, rw) do { } while (0)
#endif
#define BUS_RD(a) ({ uint8_t v__; BUS_TICK(); \
        if ((a) & 0x8000) v__ = D.rom[(a) & 0x3FFF]; \
        else if ((a) < 0x1800) v__ = D.ram[(a) & 0x07FF]; \
        else if (((a) & 0x1800) == 0x1800) { D.cycles = cyc; v__ = drive_via_rd(a); BUS_RELOAD(); } \
        else v__ = (uint8_t)((a) >> 8); \
        BUS_TRACE(a, v__, 1); v__; })
#define BUS_WR(a, v) do { BUS_TICK(); BUS_TRACE(a, v, 0); \
        if ((a) & 0x8000) { } \
        else if ((a) < 0x1800) D.ram[(a) & 0x07FF] = (v); \
        else if (((a) & 0x1800) == 0x1800) { D.cycles = cyc; drive_via_wr(a, v); BUS_RELOAD(); } \
        } while (0)
#define BUS_IRQ     irq
/* The lead check is out of line and reads the cycle counter, so it runs only
 * when the bus has changed or DRIVE_LEAD_EVERY cycles have passed since the
 * last; the input compare itself is inline and cheap. */
#ifndef DRIVE_LEAD_EVERY
#define DRIVE_LEAD_EVERY 8
#endif
#ifndef DRIVE_INSN_HOOK
#define DRIVE_INSN_HOOK(c, cyc)        /* host tools only (host/fwtwin.c): a look at each instruction */
#endif
#define INSN_END()  do { DRIVE_INSN_HOOK(c, cyc); if ((D.via1.ier & IFR_CA1) && !(c->p & F_I) && \
                        (PLAT_IEC_IN() != D.iec_in || cyc - lead_at >= DRIVE_LEAD_EVERY)) { \
                        D.cycles = cyc; drive_lead(); BUS_RELOAD(); lead_at = cyc; } } while (0)
/* PLAT_STOP_OK(cyc, until): the platform may hold a run's end back to a moment with time to spare (the board: a run's
 * end, its catch-up and the caller's housekeeping landed between a fast loader's read and its answer, 2026-11-07).
 * Where the run ends changes nothing the drive does: the next run carries on from the same instruction */
#ifndef PLAT_STOP_OK
#define PLAT_STOP_OK(cyc, until) 1
#endif
#define RUN_STOP    ((int32_t)(cyc - until) >= 0 && PLAT_STOP_OK(cyc, until))
/* the V flag is about to be read or overwritten: the lazy disk's SO first (cpu6502.h) */
#define CPU_VSYNC() do { if ((int32_t)(cyc - D.disk_due) >= 0) { D.cycles = cyc; drive_disk_catchup(cyc); BUS_RELOAD(); } \
                         if (D.so_pending) { c->p |= F_V; D.so_pending = 0; } } while (0)
/* V about to be overwritten: no catch-up, the bytes up to now can't show (a fast loader's BIT $1800 polls) */
#define CPU_VOVER() do { D.v_over = cyc; D.so_pending = 0; } while (0)
/* I changed (CPU_IFLAG: SEI, CLI, PLP, RTI, BRK, an IRQ entry). Just cleared: a T1 left lazy because its interrupt was masked gets its events back, its flag (and
 * so the IRQ line) brought up to date first, before the next IRQ sample (core/via6522.h, T1_LAZY) */
static FF_SCRATCH __attribute__((noinline)) void drive_iclr(uint32_t now)   /* (CLI, PLP, RTI are rare: out of line) */
{
    int k = 0;
    if (D.via1.t1_ev == T1_LAZY && (D.via1.ier & IFR_T1)) { via_sched_t1(&D.via1, now); k = 1; }
    if (D.via2.t1_ev == T1_LAZY && (D.via2.ier & IFR_T1)) { via_sched_t1(&D.via2, now); k = 1; }
    if (k) { drive_irq_update(); drive_resched(now); }
}
#define CPU_IFLAG() do { D.imask = (uint8_t)(c->p & F_I); if (!D.imask) { D.cycles = cyc; drive_iclr(cyc); BUS_RELOAD(); } } while (0)
#include "cpu6502.h"

static cpu6502_t CPU;
static inline int drive_imasked(void) { return D.imask; }

/* ⚠ [A] the head starts on track 18 (half-track 34), resting on the phase
 * that holds it there (half-track & 3). */
#define DRIVE_HT0 34
static inline void drive_init(const uint8_t *rom, const disk_t *disk)
{
    for (unsigned i = 0; i < sizeof D.ram; i++) D.ram[i] = 0;
    D.rom = rom; D.disk = disk;
    head_select(DRIVE_HT0); D.phase = DRIVE_HT0 & 3; D.wmode = 0; D.wcount = 0;
    D.h = (head_t){ 0 }; D.bytes_read = 0; D.cycles = 0;
    via_reset(&D.via1, 0); via_reset(&D.via2, 0);
    head_find_next();
    D.via1.pb_in = 0x00;   /* idle bus reads 0; PB5-6, the device jumpers, 0: device 8 [A] (drive_set_device) */
    /* ⚠ CA1 starts at the PIN's level: ATN released reads LOW after the
     * inverter [S7]. via_reset's ca1 = 1 made the first IEC input change a
     * spurious CA1 edge, so a spurious IRQ (found 2026-10-25, when echo
     * prediction removed that first change and the models diverged). */
    D.via1.ca1 = 0;
    D.iec_in = 0; D.iec_out = 0xFF;
    drive_iec_eval();
    drive_irq_update(); drive_resched(0);
    cpu_reset(&CPU, (uint16_t)(rom[0x3FFC] | (rom[0x3FFD] << 8)));
    D.imask = F_I;                                   /* (reset sets I) */
}

/* The whole drive -- board, CPU and RAM -- as a value: browse mode boots the DOS
 * once, keeps it, and restores it on every mount, so a mounted image answers at
 * once (a cold DOS needs ~1.2 s for its RAM test, F4a). The disk is not part of
 * it: drive_insert() the image after restoring. */
typedef struct { drive_t d; cpu6502_t cpu; } drive_snap_t;
static inline void drive_save(drive_snap_t *s) { s->d = D; s->cpu = CPU; }
static inline void drive_load(const drive_snap_t *s) { D = s->d; CPU = s->cpu; }

/* Run whole instructions until emulated time reaches `until` (it stops at the
 * first instruction end at or past it). */
/* The lazy disk's backlog, at a run's end: only in idle time, one event at a time, as drive_sync does (2026-11-07: a
 * whole run's catch-up at once, after a run held back to a quiet moment, made the next sync 13 us late). Nothing needs
 * it caught up here: whatever looks (VIA2, V) catches it up first */
static inline void drive_run(uint32_t until)
{
    D.run_until = until; cpu_run(&CPU);
    while ((int32_t)(D.cycles - D.disk_due) >= 0 && PLAT_SLACK(D.cycles) > PLAT_LA_BUDGET) drive_disk_catchup_n(D.cycles, 1);
    if (D.so_pending) { CPU.p |= F_V; D.so_pending = 0; }
}
#endif
