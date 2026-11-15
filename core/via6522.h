/* -------------------------------------------------------------------------
 * PicoD64 -- 6522 VIA, the parts a 1541 uses. EVENT-DRIVEN.
 *
 * CLEAN ROOM, from the Rockwell R6522 data sheet [S2]. Register map, IFR/IER,
 * port read rules and timer modes follow [S2]. ⚠ The cycle phase of timer
 * load, underflow and IRQ assertion is this file's reading of [S2] and is NOT
 * yet checked against an oracle (FEASIBILITY.md, F2).
 *
 * The timers are not counted down every cycle. Each holds a base cycle and
 * the value it had then; the count is worked out when it is read, and the
 * underflow is an event at an absolute cycle the board schedules. The
 * behaviour is defined by, and tested against, the per-cycle model in
 * host/ref/via6522.h (host/equiv.sh): that model is the readable spec, this
 * one is the fast one.
 *
 * Cycle numbering: "now" is the cycle whose tick has just run. A timer loaded
 * in cycle W holds L after W, L-k after W+k, reaches 0 at W+L and underflows
 * (reads $FFFF, flag set) at W+L+1; free-run reloads L at W+L+2, so the
 * period is N+2 [S2].
 *
 * Not modelled yet: shift register, T2 pulse counting, PB7 timer output,
 * handshake pulse modes on CA2/CB2. The 1541's use of these is [A] unused.
 * ------------------------------------------------------------------------- */
#ifndef FF_VIA6522_H
#define FF_VIA6522_H
#ifndef FF_HOT
#define FF_HOT
#endif
#include <stdint.h>

enum { IFR_CA2 = 0x01, IFR_CA1 = 0x02, IFR_SR = 0x04, IFR_CB2 = 0x08,
       IFR_CB1 = 0x10, IFR_T2 = 0x20, IFR_T1 = 0x40 };

/* (The T1 helpers below: one copy each, in RAM (FF_HOT): inlined everywhere they cost 830 bytes of RAM, and left to the
 * compiler one landed in flash, which core 1 must never run, 2026-11-10) */
/* T1 event kinds */
enum { T1_NONE, T1_UNDERFLOW, T1_RELOAD, T1_LAZY };
/* T1_LAZY (2026-11-10): free-run with its interrupt off. Nothing but a read of T1 or IFR can see it then (PB7 output
 * isn't modelled), so it schedules no events: the counter and the flag are worked out when they are read. Transwarp's
 * drive code runs T1 free with latch $0000 and IER $00: an underflow and a reload every two cycles, two events at
 * ~122 sysclk each, ~73% of core 1 (the board's cost meter), and the drive fell behind real time for good.
 * The segment: the counter held t1_bval at t1_base; it underflows at t1_base + t1_bval + 1, then every t1l + 2
 * cycles, showing $FFFF in the underflow cycle and the latch the cycle after. t1_seen: the flag is up to date there.
 * Also (2026-11-13) with its interrupt ON while the CPU has I set: the IRQ line can't be taken then either, so only a
 * read can see the flag. VIA_IRQ_MASKED() says whether I is set; whoever clears I (CLI, PLP, RTI) must call
 * via_sched_t1 for a lazy T1 with its interrupt on (core/drive1541.h CPU_ICLR) before the next IRQ sample. Reanim8ed's
 * loader (SEI, VIA2 T1 free with its interrupt on): an underflow's two events between a read and its answer 6 cycles
 * later made the answer up to 1.4 us late. */
#ifndef VIA_IRQ_MASKED
#define VIA_IRQ_MASKED() 0
#endif

#define VIA_NEVER 0x40000000u     /* "no event": this far past now */

typedef struct {
    uint8_t orb, ora, ddrb, ddra, acr, pcr, ifr, ier, sr;
    uint8_t pa_in, pb_in;        /* external pin levels, set by the board */
    uint8_t pa_latch;            /* PA latched on the CA1 edge, if ACR0 */
    uint8_t ca1;                 /* current CA1 level */
    uint8_t t1_armed, t2_armed, t1_ev;
    uint16_t t1l, t1_bval, t2_bval;
    uint8_t t2l_lo;
    uint32_t t1_base, t2_base;   /* cycle at which the counter held *_bval */
    uint32_t t1_next, t2_next;   /* cycle of the next event, if any */
    uint32_t t1_seen;            /* T1_LAZY: the flag is up to date at this cycle */
} via6522_t;

static FF_HOT __attribute__((noinline)) uint32_t via_t1_m(const via6522_t *v, uint32_t now, uint16_t *cnt)   /* T1_LAZY: cycles past the */
{                                                                                    /* last underflow, or ~0 */
    int32_t d = (int32_t)(now - v->t1_base);
    if (d <= (int32_t)v->t1_bval) { *cnt = (uint16_t)(v->t1_bval - d); return 0xFFFFFFFFu; }
    uint32_t m = (uint32_t)(d - v->t1_bval - 1) % (v->t1l + 2u);
    *cnt = m == 0 ? 0xFFFF : (uint16_t)(v->t1l - (m - 1));
    return m;
}
static FF_HOT inline uint16_t via_t1(const via6522_t *v, uint32_t now)
{
    if (v->t1_ev == T1_LAZY) { uint16_t c; via_t1_m(v, now, &c); return c; }
    return (uint16_t)(v->t1_bval - (now - v->t1_base));
}
/* T1_LAZY: the flag, as the underflow events would have set it, up to now */
static FF_HOT __attribute__((noinline)) void via_t1_flag(via6522_t *v, uint32_t now)
{
    if (v->t1_ev != T1_LAZY) return;
    uint16_t c; uint32_t m = via_t1_m(v, now, &c);
    if (m != 0xFFFFFFFFu && v->t1_armed) {
        uint32_t last = now - m;                                  /* the latest underflow, at or before now */
        if ((int32_t)(last - v->t1_seen) > 0) v->ifr |= IFR_T1;
    }
    v->t1_seen = now;
}
/* leave T1_LAZY: the segment as the event model holds it at now (the flag already brought up to date) */
static FF_HOT __attribute__((noinline)) void via_t1_unlazy(via6522_t *v, uint32_t now)
{
    if (v->t1_ev != T1_LAZY) return;
    uint16_t c; uint32_t m = via_t1_m(v, now, &c);
    if (m == 0) { v->t1_ev = T1_RELOAD; v->t1_next = now + 1; v->t1_base = now; v->t1_bval = 0xFFFF; }   /* reload due */
    else { v->t1_ev = T1_NONE; v->t1_base = now; v->t1_bval = c; }
}
static inline uint16_t via_t2(const via6522_t *v, uint32_t now)
{ return (uint16_t)(v->t2_bval - (now - v->t2_base)); }

/* An underflow event is needed only if it does something: set the flag
 * (armed) or reload (free-run, ACR6). Otherwise the count just wraps, which
 * via_t1() already gives. */
static FF_HOT __attribute__((noinline)) void via_sched_t1(via6522_t *v, uint32_t now)
{
    via_t1_flag(v, now); via_t1_unlazy(v, now);
    if (v->t1_ev == T1_RELOAD) return;
    /* ⚠ REBASE FIRST. An unarmed one-shot wraps silently, so base + bval + 1
     * can lie in the past; the next underflow is counted from the value NOW. */
    v->t1_bval = via_t1(v, now); v->t1_base = now;
    if ((v->acr & 0x40) && (!(v->ier & IFR_T1) || VIA_IRQ_MASKED())) {   /* free-run, its interrupt off or masked: T1_LAZY */
        v->t1_ev = T1_LAZY; v->t1_seen = now;
        v->t1_next = now + VIA_NEVER;
    } else if (v->t1_armed || (v->acr & 0x40)) {
        v->t1_ev = T1_UNDERFLOW;
        v->t1_next = now + v->t1_bval + 1u;
    } else {
        v->t1_ev = T1_NONE;
        v->t1_next = now + VIA_NEVER;
    }
}

static inline void via_reset(via6522_t *v, uint32_t now)
{
    /* [S2]: reset clears all registers except T1/T2 counters and latches and
     * SR. ⚠ Their power-up contents are unknown; both counters start at 0
     * here so a run is deterministic. */
    v->orb = v->ora = v->ddrb = v->ddra = v->acr = v->pcr = v->ifr = v->ier = 0;
    v->t1_armed = v->t2_armed = 0;
    v->pa_in = v->pb_in = 0xFF; v->ca1 = 1;
    v->t1l = 0; v->t2l_lo = 0;
    v->t1_base = v->t2_base = now; v->t1_bval = v->t2_bval = 0;
    v->t1_ev = T1_NONE; via_sched_t1(v, now);
    v->t2_next = now + VIA_NEVER;
}

static inline int via_irq(const via6522_t *v) { return (v->ifr & v->ier & 0x7F) != 0; }

/* The earlier of this VIA's two events, as an offset from now. */
static inline uint32_t via_next(const via6522_t *v, uint32_t now)
{
    uint32_t a = v->t1_next - now, b = v->t2_armed ? v->t2_next - now : VIA_NEVER;
    return a < b ? a : b;
}

/* Run the events due at cycle `now`. Called only when one is due. */
static inline void via_events(via6522_t *v, uint32_t now)
{
    if (v->t1_next == now) {
        if (v->t1_ev == T1_UNDERFLOW) {
            if (v->t1_armed) { v->ifr |= IFR_T1; if (!(v->acr & 0x40)) v->t1_armed = 0; }
            if (v->acr & 0x40) { v->t1_ev = T1_RELOAD; v->t1_next = now + 1; }
            else { v->t1_base = now; v->t1_bval = 0xFFFF; via_sched_t1(v, now); }
        } else if (v->t1_ev == T1_RELOAD) {
            /* the latch as it stands now: a write in the underflow cycle counts */
            v->t1_ev = T1_NONE; v->t1_base = now; v->t1_bval = v->t1l; via_sched_t1(v, now);
        } else {
            v->t1_next = now + VIA_NEVER;     /* a NONE (or LAZY) marker came due: push it on */
        }
    }
    if (v->t2_armed && v->t2_next == now) { v->ifr |= IFR_T2; v->t2_armed = 0; }
}

/* CA1 input; PCR0 picks the active edge [S2]. */
static inline void via_set_ca1(via6522_t *v, uint8_t level)
{
    if (level == v->ca1) return;
    v->ca1 = level;
    if (level == (v->pcr & 1)) {
        v->ifr |= IFR_CA1;
        if (v->acr & 1) v->pa_latch = v->pa_in;
    }
}

/* CA2/CB2 as manual outputs (PCR modes 110 = low, 111 = high) [S2]. */
static inline int via_ca2_high(const via6522_t *v) { return (v->pcr & 0x0E) == 0x0E; }
static inline int via_cb2_high(const via6522_t *v) { return (v->pcr & 0xE0) == 0xE0; }

static inline uint8_t via_pb(const via6522_t *v)   /* what the PB pins read */
{ return (uint8_t)((v->orb & v->ddrb) | (v->pb_in & ~v->ddrb)); }

static FF_HOT inline uint8_t via_read(via6522_t *v, uint8_t r, uint32_t now)
{
    switch (r & 15) {
    case 0x0: v->ifr &= ~(IFR_CB1 | IFR_CB2); return via_pb(v);
    case 0x1: v->ifr &= ~(IFR_CA1 | IFR_CA2); /* fall through */
    case 0xF: return (uint8_t)((v->ora & v->ddra) |
                  (((v->acr & 1) ? v->pa_latch : v->pa_in) & ~v->ddra));
    case 0x2: return v->ddrb;
    case 0x3: return v->ddra;
    case 0x4: via_t1_flag(v, now); v->ifr &= ~IFR_T1; return (uint8_t)via_t1(v, now);
    case 0x5: return (uint8_t)(via_t1(v, now) >> 8);
    case 0x6: return (uint8_t)v->t1l;
    case 0x7: return (uint8_t)(v->t1l >> 8);
    case 0x8: v->ifr &= ~IFR_T2; return (uint8_t)via_t2(v, now);
    case 0x9: return (uint8_t)(via_t2(v, now) >> 8);
    case 0xA: v->ifr &= ~IFR_SR; return v->sr;
    case 0xB: return v->acr;
    case 0xC: return v->pcr;
    case 0xD: via_t1_flag(v, now); return (uint8_t)(v->ifr | (via_irq(v) ? 0x80 : 0));
    default:  return (uint8_t)(v->ier | 0x80);   /* 0xE: bit 7 reads 1 [S2] */
    }
}

static FF_HOT inline void via_write(via6522_t *v, uint8_t r, uint8_t d, uint32_t now)
{
    switch (r & 15) {
    case 0x0: v->orb = d; v->ifr &= ~(IFR_CB1 | IFR_CB2); break;
    case 0x1: v->ifr &= ~(IFR_CA1 | IFR_CA2); /* fall through */
    case 0xF: v->ora = d; break;
    case 0x2: v->ddrb = d; break;
    case 0x3: v->ddra = d; break;
    case 0x4: case 0x6: via_t1_flag(v, now); via_t1_unlazy(v, now); v->t1l = (uint16_t)((v->t1l & 0xFF00) | d); via_sched_t1(v, now); break;
    case 0x5: /* latch high, counter <- latch, flag cleared, armed [S2] */
        via_t1_flag(v, now); via_t1_unlazy(v, now);
        v->t1l = (uint16_t)((v->t1l & 0x00FF) | (d << 8));
        v->ifr &= ~IFR_T1; v->t1_armed = 1;
        v->t1_ev = T1_NONE; v->t1_base = now; v->t1_bval = v->t1l; via_sched_t1(v, now); break;
    case 0x7: via_t1_flag(v, now); via_t1_unlazy(v, now); v->t1l = (uint16_t)((v->t1l & 0x00FF) | (d << 8)); v->ifr &= ~IFR_T1; via_sched_t1(v, now); break;
    case 0x8: v->t2l_lo = d; break;
    case 0x9: v->t2_base = now; v->t2_bval = (uint16_t)(v->t2l_lo | (d << 8));
        v->ifr &= ~IFR_T2; v->t2_armed = 1; v->t2_next = now + v->t2_bval + 1u; break;
    case 0xA: v->sr = d; v->ifr &= ~IFR_SR; break;
    case 0xB: v->acr = d; via_sched_t1(v, now); break;   /* free-run on/off changes T1's events */
    case 0xC: v->pcr = d; break;
    case 0xD: via_t1_flag(v, now); v->ifr &= (uint8_t)~(d & 0x7F); break;   /* write 1 to clear [S2] */
    default:  if (d & 0x80) v->ier |= d & 0x7F; else v->ier &= (uint8_t)~d; via_sched_t1(v, now); break;   /* (T1_LAZY or not) */
    }
}
#endif
