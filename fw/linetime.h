/* -------------------------------------------------------------------------
 * Line timing (serial O, 2026-11-19): how late our pulls and releases of CLK and DATA reach the C64, as its CIA reads
 * them. The C64 runs fw/linetime/linetime.prg: it pulls one line and reads the other 4 cycles later; we wait a delay d
 * after seeing its pull, then change the other line; the C64 tells us whether its read saw the change (by when it lets
 * go). Swept 6.7 ns at a time (one sysclk at 150 MHz), the delay at which half the reads see it, d50, is where the
 * edge crosses the CIA's threshold, less our fixed costs. The same costs are in a pull's d50 and a release's, so
 * pull d50 - release d50 = how much later a release reaches the C64 than a pull. Shared by the board (fw/pico_main.c)
 * and its host check (host/linetime_test.c), which give it the trial.
 * ------------------------------------------------------------------------- */
#ifndef FF_LINETIME_H
#define FF_LINETIME_H
#include <stdint.h>

/* One trial on line l (0 CLK, 1 DATA): our change (a pull if pull, else a release) d sysclk after the C64's trigger.
 * 1 = the C64 saw it in time, 0 = not, -1 = no trigger came (the C64 program isn't running). The line is set up for
 * the trial (released for a pull, pulled for a release) before and after each. */
typedef int (*lt_trial_fn)(int l, int pull, uint32_t d);

#define LT_COARSE_N 60      /* 100 ns steps: 0-6 us (the C64 reads 4 us after its pull) */
#define LT_COARSE_T 8
#define LT_FINE_N   96      /* 1 sysclk steps around the coarse crossing (+-48: over 300 ns at 150 MHz) */
#define LT_FINE_T   32

typedef struct {
    int ok;                 /* 1: measured; 0: no trigger; -1: the curve never fell to 50% (or was not 1 at the start) */
    int32_t d50x10;         /* sysclk x 10 */
    int32_t w;              /* sysclk from 90% seen to 10% seen: the edge's spread (our jitter) */
    uint16_t lo10, hi10;    /* the fine curve's first and last counts (of LT_FINE_T): a check it spans 1 to 0 */
} lt_res_t;

static lt_res_t lt_measure(lt_trial_fn tr, int l, int pull, uint32_t mhz)
{
    lt_res_t r = { 0, 0, 0, 0, 0 };
    uint32_t step = mhz / 10, dc = 0; int found = 0;
    for (int i = 0; i < LT_COARSE_N && !found; i++) {
        int seen = 0;
        for (int k = 0; k < LT_COARSE_T; k++) { int v = tr(l, pull, i * step); if (v < 0) return r; seen += v; }
        if (seen * 2 < LT_COARSE_T) { dc = i * step; found = 1; }
    }
    if (!found) { r.ok = -1; return r; }
    int32_t d0 = (int32_t)dc - LT_FINE_N / 2; if (d0 < 0) d0 = 0;
    uint32_t S = 0; int i90 = -1, i10 = -1;
    for (int i = 0; i < LT_FINE_N; i++) {
        int seen = 0;
        for (int k = 0; k < LT_FINE_T; k++) { int v = tr(l, pull, (uint32_t)(d0 + i)); if (v < 0) return r; seen += v; }
        S += (uint32_t)seen;
        if (i == 0) r.lo10 = (uint16_t)seen;
        if (i == LT_FINE_N - 1) r.hi10 = (uint16_t)seen;
        if (i90 < 0 && seen * 10 < LT_FINE_T * 9) i90 = i;
        if (i10 < 0 && seen * 10 < LT_FINE_T) i10 = i;
    }
    /* a curve falling from 1 to 0: its 50% point = d0 + (sum of the fractions) - 1/2 step */
    r.d50x10 = d0 * 10 + (int32_t)((S * 10u + LT_FINE_T / 2) / LT_FINE_T) - 5;
    r.w = (i90 >= 0 && i10 >= 0) ? i10 - i90 : -1;
    r.ok = (r.lo10 * 10 >= LT_FINE_T * 9 && r.hi10 * 10 <= LT_FINE_T) ? 1 : -1;
    return r;
}
#endif
