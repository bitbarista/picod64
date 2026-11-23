/* Host check of serial O, line timing (fw/linetime.h, fw/linetime/linetime.prg): the real C64 program on the host C64
 * (host/c64.c, real KERNAL) against a model of PicoD64 and the bus with known delays, through the board's own sweep
 * (lt_measure). It must give back the model's release-minus-pull lag on both lines, and the C64 must end at READY
 * with "done" on its screen. What it can't check: the board's pin loop (fw/pico_main.c lt_trial) and real edges.
 *   linetime_test <kernal> <basic> <linetime.prg>       (host/linetime.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "c64.h"
#include "../fw/linetime.h"

#define MHZ 150u
static const double T = 1e9 / C64_HZ;   /* ns per C64 cycle */
/* the model (ns): the C64's pull to our input; our loop to see it (+ 0..JIT); our output's work; then the line's edge
 * as the CIA reads it, a pull and a release, per line */
static double C64OUT = 40, PICO_IN = 20, JIT = 25, LOOP = 30, PULL_NS[2] = { 140, 150 }, REL_NS[2] = { 300, 420 };
static const double PORT_AT = 950;     /* where in its 4th cycle a port access happens (the same for reads and writes) */

static struct { int before, after; double teff; } pl[2];   /* our pull of CLK (0), DATA (1): before/after teff */
static int pico_low(int l, double t) { return t >= pl[l].teff ? pl[l].after : pl[l].before; }
static void pico_set(int l, int low, double t_act, double delay)
{
    pl[l].before = pico_low(l, t_act); pl[l].after = low; pl[l].teff = t_act + delay;
}
static double now(void) { return (double)c64_cycles() * T; }

/* one C64 instruction, with the lines it reads as they are at its port access; returns the time its port write
 * (if any) happened, else -1 */
static double step(void)
{
    int port = c64_iec_after(0);
    uint64_t c0 = c64_cycles();
    double ta = port ? (double)(c0 + 3) * T + PORT_AT : now();
    uint8_t low = c64_iec_out();
    if (pico_low(0, ta)) low |= C64_CLK;
    if (pico_low(1, ta)) low |= C64_DATA;
    c64_iec_in(low);
    uint8_t o0 = c64_iec_out();
    c64_step();
    return port && c64_iec_out() != o0 ? ta : -1;
}
static unsigned rng = 12345;
static double jit(void) { rng = rng * 1103515245u + 12345u; return JIT * ((rng >> 8) & 0xFFFF) / 65536.0; }

static uint8_t prev_out;
static int trial(int l, int pull, uint32_t d)
{
    const uint8_t TRIG = l ? C64_CLK : C64_DATA;
    double t_end = now() + 20e6, te = 0; int waiting = 1;
    while (now() < t_end) {
        double tw = step(); uint8_t o = c64_iec_out();
        if (o != prev_out) {
            if (tw < 0) tw = now();
            int fell = (o & TRIG) && !(prev_out & TRIG), rose = !(o & TRIG) && (prev_out & TRIG);
            prev_out = o;
            if (waiting && fell) {
                te = tw + C64OUT + PICO_IN + jit();
                double ta = te + d * 1000.0 / MHZ + LOOP;
                pico_set(l, pull, ta, pull ? PULL_NS[l] : REL_NS[l]);
                waiting = 0;
            } else if (!waiting && rose) {
                double dt = tw + C64OUT - te;
                pico_set(l, !pull, tw + C64OUT + 1000, pull ? REL_NS[l] : PULL_NS[l]);   /* set up again */
                return dt < 50e3;
            }
        }
    }
    return -1;
}
static void run_for(double ns) { double t = now() + ns; while (now() < t) { step(); prev_out = c64_iec_out(); } }

static uint8_t kernal[8192], basic[8192], prg[65536];
static size_t load(const char *p, uint8_t *b, size_t n)
{
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
    size_t r = fread(b, 1, n, f); fclose(f); return r;
}
int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: linetime_test <kernal> <basic> <linetime.prg>\n"); return 2; }
    load(argv[1], kernal, sizeof kernal); load(argv[2], basic, sizeof basic);
    size_t n = load(argv[3], prg, sizeof prg);
    c64_init(kernal, basic);
    while (c64_cycles() < 3000000) { c64_iec_in(0); c64_step(); }
    memcpy(c64_ram() + 0x0801, prg + 2, n - 2);
    { uint16_t end = (uint16_t)(0x0801 + n - 2); uint8_t *m = c64_ram();   /* RUN typed, as after a LOAD */
      m[0x2D] = m[0x2F] = m[0x31] = (uint8_t)end; m[0x2E] = m[0x30] = m[0x32] = (uint8_t)(end >> 8);
      memcpy(m + 0x0277, "RUN\r", 4); m[0xC6] = 4; }
    prev_out = c64_iec_out();
    int fail = 0;
    run_for(300e6);                                            /* RUN typed: the C64 then waits for us */
    pico_set(0, 1, now(), PULL_NS[0]); pico_set(1, 1, now(), PULL_NS[1]);   /* hello */
    run_for(10e6);
    pico_set(0, 0, now(), REL_NS[0]); pico_set(1, 0, now(), REL_NS[1]);
    lt_res_t r[2][2];
    for (int l = 0; l < 2; l++) for (int p = 1; p >= 0; p--) {
        pico_set(l, !p, now(), 500); run_for(20e3);            /* set up: released for a pull, pulled for a release */
        r[l][p] = lt_measure(trial, l, p, MHZ);
        pico_set(l, 0, now(), REL_NS[l]); run_for(20e3);
    }
    pico_set(0, 1, now(), PULL_NS[0]); pico_set(1, 1, now(), PULL_NS[1]);   /* done */
    run_for(10e6);
    pico_set(0, 0, now(), REL_NS[0]); pico_set(1, 0, now(), REL_NS[1]);
    run_for(200e6);
    for (int l = 0; l < 2; l++) {
        const char *nm = l ? "DATA" : "CLK";
        for (int p = 1; p >= 0; p--) {
            double want = (4 * T - C64OUT - PICO_IN - JIT / 2 - LOOP - (p ? PULL_NS[l] : REL_NS[l])) * MHZ / 1000.0;
            double got = r[l][p].d50x10 / 10.0;
            int bad = r[l][p].ok != 1 || fabs(got - want) > 1.0;
            printf("%s %s: ok %d, d50 %.1f sysclk (model %.1f), spread %ld sysclk, ends %u/%u of %d%s\n", nm, p ? "pull" : "release",
                   r[l][p].ok, got, want, (long)r[l][p].w, r[l][p].lo10, r[l][p].hi10, LT_FINE_T, bad ? "  FAIL" : "");
            fail |= bad;
        }
        double lag = (r[l][1].d50x10 - r[l][0].d50x10) / 10.0 * 1000.0 / MHZ, mlag = REL_NS[l] - PULL_NS[l];
        int bad = fabs(lag - mlag) > 10.0;
        printf("%s: release reaches the C64 %.0f ns after a pull would (model %.0f)%s\n", nm, lag, mlag, bad ? "  FAIL" : "");
        fail |= bad;
    }
    /* the C64 back at READY with "done" on its screen */
    const uint8_t *scr = c64_ram() + c64_screen(); int found = 0;
    for (int i = 0; i + 4 <= 1000 && !found; i++) found = scr[i] == 4 && scr[i + 1] == 15 && scr[i + 2] == 14 && scr[i + 3] == 5;   /* "done" */
    int at_ready = c64_pc() >= 0xA000;   /* in BASIC or the KERNAL: waiting for a key */
    printf("C64: %s, PC $%04X%s\n", found ? "\"done\" on screen" : "no \"done\" on screen", c64_pc(), found && at_ready && !c64_irq_off() ? "" : "  FAIL");
    fail |= !found || !at_ready || c64_irq_off();
    if (!found) for (int y = 0; y < 25; y++) { char ln[41]; for (int x = 0; x < 40; x++) { uint8_t c = scr[y * 40 + x] & 0x7F; ln[x] = c == 32 ? ' ' : c < 27 ? (char)('a' + c - 1) : c < 64 ? (char)c : '?'; } ln[40] = 0; printf("  |%s|\n", ln); }
    printf(fail ? "linetime: FAIL\n" : "linetime: ok\n");
    return fail;
}
