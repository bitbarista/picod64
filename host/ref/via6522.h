/* -------------------------------------------------------------------------
 * PicoD64 -- 6522 VIA, the parts a 1541 uses.
 *
 * CLEAN ROOM, from the Rockwell R6522 data sheet [S2]. Register map, IFR/IER,
 * port read rules and timer modes follow [S2]. ⚠ The cycle phase of timer
 * load, underflow and IRQ assertion is this file's reading of [S2] and is NOT
 * yet checked against an oracle (FEASIBILITY.md, F2). Fine for measuring cost;
 * not yet a claim of exactness.
 *
 * Not modelled yet: shift register, T2 pulse counting, PB7 timer output,
 * handshake pulse modes on CA2/CB2. The 1541's use of these is [A] unused.
 * ------------------------------------------------------------------------- */
#ifndef FF_VIA6522_H
#define FF_VIA6522_H
#include <stdint.h>

enum { IFR_CA2 = 0x01, IFR_CA1 = 0x02, IFR_SR = 0x04, IFR_CB2 = 0x08,
       IFR_CB1 = 0x10, IFR_T2 = 0x20, IFR_T1 = 0x40 };

typedef struct {
    uint8_t orb, ora, ddrb, ddra, acr, pcr, ifr, ier, sr;
    uint8_t pa_in, pb_in;        /* external pin levels, set by the board */
    uint8_t pa_latch;            /* PA latched on the CA1 edge, if ACR0 */
    uint8_t ca1;                 /* current CA1 level */
    uint8_t t1_armed, t1_reload, t2_armed;
    uint16_t t1c, t1l, t2c;
    uint8_t t2l_lo;
} via6522_t;

static inline void via_reset(via6522_t *v)
{
    /* [S2]: reset clears all registers except T1/T2 counters and latches and SR */
    v->orb = v->ora = v->ddrb = v->ddra = v->acr = v->pcr = v->ifr = v->ier = 0;
    v->t1_armed = v->t2_armed = v->t1_reload = 0;
    v->pa_in = v->pb_in = 0xFF; v->ca1 = 1;
}

static inline int via_irq(const via6522_t *v) { return (v->ifr & v->ier & 0x7F) != 0; }

/* One phi2 cycle of the timers. */
static inline void via_tick(via6522_t *v)
{
    if (v->t1_reload) { v->t1c = v->t1l; v->t1_reload = 0; }
    else if (v->t1c-- == 0) {
        /* free-run (ACR6) reloads on the following cycle: period N+2 [S2] */
        if (v->t1_armed) { v->ifr |= IFR_T1; if (!(v->acr & 0x40)) v->t1_armed = 0; }
        if (v->acr & 0x40) v->t1_reload = 1;
    }
    if (v->t2c-- == 0 && v->t2_armed) { v->ifr |= IFR_T2; v->t2_armed = 0; }
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

static inline uint8_t via_read(via6522_t *v, uint8_t r)
{
    switch (r & 15) {
    case 0x0: v->ifr &= ~(IFR_CB1 | IFR_CB2); return via_pb(v);
    case 0x1: v->ifr &= ~(IFR_CA1 | IFR_CA2); /* fall through */
    case 0xF: return (uint8_t)((v->ora & v->ddra) |
                  (((v->acr & 1) ? v->pa_latch : v->pa_in) & ~v->ddra));
    case 0x2: return v->ddrb;
    case 0x3: return v->ddra;
    case 0x4: v->ifr &= ~IFR_T1; return (uint8_t)v->t1c;
    case 0x5: return (uint8_t)(v->t1c >> 8);
    case 0x6: return (uint8_t)v->t1l;
    case 0x7: return (uint8_t)(v->t1l >> 8);
    case 0x8: v->ifr &= ~IFR_T2; return (uint8_t)v->t2c;
    case 0x9: return (uint8_t)(v->t2c >> 8);
    case 0xA: v->ifr &= ~IFR_SR; return v->sr;
    case 0xB: return v->acr;
    case 0xC: return v->pcr;
    case 0xD: return (uint8_t)(v->ifr | (via_irq(v) ? 0x80 : 0));
    default:  return (uint8_t)(v->ier | 0x80);   /* 0xE: bit 7 reads 1 [S2] */
    }
}

static inline void via_write(via6522_t *v, uint8_t r, uint8_t d)
{
    switch (r & 15) {
    case 0x0: v->orb = d; v->ifr &= ~(IFR_CB1 | IFR_CB2); break;
    case 0x1: v->ifr &= ~(IFR_CA1 | IFR_CA2); /* fall through */
    case 0xF: v->ora = d; break;
    case 0x2: v->ddrb = d; break;
    case 0x3: v->ddra = d; break;
    case 0x4: case 0x6: v->t1l = (uint16_t)((v->t1l & 0xFF00) | d); break;
    case 0x5: /* latch high, counter <- latch, flag cleared, armed [S2] */
        v->t1l = (uint16_t)((v->t1l & 0x00FF) | (d << 8));
        v->t1c = v->t1l; v->ifr &= ~IFR_T1; v->t1_armed = 1; v->t1_reload = 0; break;
    case 0x7: v->t1l = (uint16_t)((v->t1l & 0x00FF) | (d << 8)); v->ifr &= ~IFR_T1; break;
    case 0x8: v->t2l_lo = d; break;
    case 0x9: v->t2c = (uint16_t)(v->t2l_lo | (d << 8)); v->ifr &= ~IFR_T2; v->t2_armed = 1; break;
    case 0xA: v->sr = d; v->ifr &= ~IFR_SR; break;
    case 0xB: v->acr = d; break;
    case 0xC: v->pcr = d; break;
    case 0xD: v->ifr &= (uint8_t)~(d & 0x7F); break;   /* write 1 to clear [S2] */
    default:  if (d & 0x80) v->ier |= d & 0x7F; else v->ier &= (uint8_t)~d; break;
    }
}
#endif
