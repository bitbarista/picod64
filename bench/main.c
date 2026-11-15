/* -------------------------------------------------------------------------
 * PicoD64 feasibility bench F1/F1d -- Raspberry Pi Pico 2 (RP2350).
 *
 * Core 1 runs the emulated 1541 (core/drive1541.h) against real time: it
 * waits for the exact real time of every VIA1 access (and of instruction
 * ends while the ATN interrupt is enabled), and runs ahead in between.
 * Interrupts are off on core 1; the image runs from SRAM (copy_to_ram).
 *
 * F1: a sweep over clock x workload x mode. Core 1's own bookkeeping says
 *     how late each sync point was reached.
 * F1d: an INDEPENDENT check of that. Core 0 polls the IEC pins in a tight
 *     loop, with interrupts off, and timestamps every edge on ITS OWN cycle
 *     counter. Core 1 logs the emulated cycle of every output change. Each
 *     edge's error = measured time - ideal time (cycle x 1 us); a constant
 *     offset is latency, the spread is the jitter the C64 would see.
 *
 * Results over USB serial, repeated every 10 s. 'r' reruns everything;
 * 'B' reboots into BOOTSEL, so a new image can be flashed without the button.
 *
 * Bench IEC pins (nothing need be attached): GP2 ATN, GP3 CLK, GP4 DATA.
 * F1: pulled up, driven low by enabling the output. F1d: CLK and DATA driven
 * push-pull, so the edges are sharp; the pull-up's slow rise would otherwise
 * be measured as lateness. ⚠ NOT bus-safe as wired: a real C64 bus needs the
 * interface of the product design (F3).
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "pico/bootrom.h"

#define PIN_ATN  2
#define PIN_CLK  3
#define PIN_DATA 4
#define PULL_MASK ((1u << PIN_CLK) | (1u << PIN_DATA))

/* Cortex-M33 DWT cycle counter; each core has its own [ARMv8-M ARM]. */
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)


static uint32_t clk_per_us;  /* sysclk cycles per emulated cycle */
static int realtime;
static uint32_t deadline;
static int32_t min_slack;
static uint32_t late;

/* Real time, F1c: called only at the sync points (core/drive1541.h): before
 * each VIA1 access (kind 1) and at each instruction end (kind 0). Waits until
 * cycle c's start; records how late it got there, per kind. Lateness at VIA1
 * is the figure that matters: it is when the IEC lines are read or driven. */
static uint32_t last_c;
static uint32_t n_sync[2], late_k[2];
static int32_t min_slack_k[2];

/* PROFILE of late VIA1 syncs: what ran since the previous one (FF_PROF bits:
 * 1 events, 2 head lookahead, 4 VIA read, 8 VIA write, 16 IEC change,
 * 64 IRQ entry; 32, opcode dispatch, is masked off as it is always set). */
static uint8_t g_prof;
#define FF_PROF(b) (g_prof |= (uint8_t)(b))
static uint32_t pl_n[128]; static int32_t pl_worst[128];

static inline void plat_sync(uint32_t c, int kind)
{
    if (!realtime) return;
    deadline += (c - last_c) * clk_per_us; last_c = c;
    int32_t slack = (int32_t)(deadline - DWT_CYCCNT);
    n_sync[kind]++;
    if (slack < min_slack_k[kind]) min_slack_k[kind] = slack;
    if (slack < 0) {
        late_k[kind]++;
        uint8_t m = g_prof & 0x5F;
        pl_n[m]++; if (slack < pl_worst[m]) pl_worst[m] = slack;
    }
    g_prof = 0;
    while ((int32_t)(deadline - DWT_CYCCNT) > 0) { }
}

/* sysclk left before cycle c's real time; used to decide whether idle work
 * (the head lookahead) fits before the next sync */
static inline int32_t plat_slack(uint32_t c)
{
    if (!realtime) return INT32_MAX;
    return (int32_t)(deadline + (c - last_c) * clk_per_us - DWT_CYCCNT);
}

/* The lead limit (core/drive1541.h drive_lead): wait only while cycle c is
 * more than n cycles ahead of real time. Counted as kind 0; "late" here means
 * the emulator was behind, which is allowed: nothing external depends on it. */
static inline void plat_lead(uint32_t c, uint32_t n)
{
    if (!realtime) return;
    int32_t slack = plat_slack(c), lim = (int32_t)(n * clk_per_us);
    n_sync[0]++;
    if (slack < min_slack_k[0]) min_slack_k[0] = slack;
    if (slack < 0) late_k[0]++;
    while (slack > lim) slack = plat_slack(c);
}

static inline uint8_t plat_iec_in(void)
{
    /* GP2..4 = ATN, CLK, DATA; a LOW line reads as a set bit */
    return (uint8_t)((~sio_hw->gpio_in >> PIN_ATN) & 7);
}

/* F1d: core 1's log of output changes, (cycle << 2) | pin levels (bit 0 CLK,
 * bit 1 DATA, 1 = released/high) */
#define LOGN 12288   /* ⚠ RAM: at 16384 only ~8 KB of heap was left with the 100 KB core */
static int tmode;
static uint32_t log1[LOGN], n1;

static inline void plat_iec_out(uint8_t pulls, uint32_t c)
{
    if (!tmode) {
        uint32_t m = ((uint32_t)pulls << PIN_ATN) & PULL_MASK;
        sio_hw->gpio_oe_set = m;
        sio_hw->gpio_oe_clr = PULL_MASK & ~m;
        return;
    }
    uint32_t lv = (uint32_t)(!(pulls & 2)) | ((uint32_t)!(pulls & 4) << 1);
    /* ⚠ ONE store for both pins: set-then-clear would show core 0 a
     * half-way state when CLK and DATA change together */
    sio_hw->gpio_out = (sio_hw->gpio_out & ~PULL_MASK) | (lv << PIN_CLK);
    if (n1 < LOGN) log1[n1++] = (c << 2) | lv;
}

/* Hot code is also marked for SRAM; with copy_to_ram everything is there. */
#ifdef BENCH_XIP
#define FF_HOT                      /* experiment: everything from flash */
#else
#define FF_HOT __attribute__((section(".time_critical.ff")))
#endif
#define PLAT_SYNC(c, k)  plat_sync((c), (k))
#define PLAT_SLACK(c)    plat_slack(c)
#define PLAT_LEAD(c, n)  plat_lead((c), (n))
#define PLAT_LA_BUDGET   300   /* sysclk; [A] the lookahead's cost is not yet measured */
#define PLAT_IEC_IN()    plat_iec_in()
#define PLAT_IEC_OUT(p)  plat_iec_out((p), D.cycles)
#include "../core/drive1541.h"
#include "track.h"
#include "fingerprint.h"
#include "wl_dos.h"
#include "wl_stress.h"
#include "wl_timers.h"
#include "wl_iec.h"
#include "wl_iecatn.h"
#include "wl_nop.h"
#include "wl_ram.h"

static uint8_t trk[BENCH_TRACK_BYTES];
static disk_t disk;

/* --- one run on core 1 --------------------------------------------------- */
typedef struct {
    uint32_t mhz; const char *wl; const uint8_t *rom; int rt; uint32_t target;
    /* results */
    uint32_t cycles, late, us; int32_t min_slack; char fp[128];
    char prof[400];
    uint32_t late_via1, late_insn; int32_t worst_via1;
    char causes[200];
} run_t;
static run_t *volatile cur;
static volatile int g_go, g_done;

#ifdef BENCH_XIP
static void core1_run(void)
#else
static void __not_in_flash_func(core1_run)(void)
#endif
{
    run_t *r = cur;
    uint32_t irq = save_and_disable_interrupts();
    while (tmode && !g_go) { }          /* F1d: core 0's sampler is running */
    DEMCR |= 1u << 24; DWT_CYCCNT = 0; DWT_CTRL |= 1;
    clk_per_us = r->mhz; realtime = r->rt;
    min_slack = INT32_MAX; late = 0; last_c = 0;
    for (int k = 0; k < 2; k++) { n_sync[k] = late_k[k] = 0; min_slack_k[k] = INT32_MAX; }
    memset(pl_n, 0, sizeof pl_n); memset(pl_worst, 0, sizeof pl_worst); g_prof = 0;
    drive_init(r->rom, &disk);
    uint64_t t0 = time_us_64();
    deadline = DWT_CYCCNT + 1000;   /* cycle 0 starts 1000 sysclk from now */
    drive_run(r->target);
    r->us = (uint32_t)(time_us_64() - t0);
    g_done = 1;
    restore_interrupts(irq);
    r->cycles = D.cycles; r->late = late; r->min_slack = min_slack;
    snprintf(r->fp, sizeof r->fp, "cycles=%lu pc=%04X passes=%u irqs=%u bytes=%lu ram_crc=%08lX%s",
             (unsigned long)D.cycles, CPU.pc, D.ram[0x05] | (D.ram[0x06] << 8),
             D.ram[0x08] | (D.ram[0x09] << 8), (unsigned long)D.bytes_read,
             (unsigned long)crc32_buf(D.ram, sizeof D.ram), CPU.jammed ? " JAMMED" : "");
    r->prof[0] = 0;
    if (r->rt) {
        r->late = late_k[0] + late_k[1];
        r->late_via1 = late_k[1]; r->late_insn = late_k[0]; r->worst_via1 = min_slack_k[1];
        int len = 0; r->causes[0] = 0;
        for (int k = 0; k < 5; k++) {
            int best = -1;
            for (int m = 0; m < 128; m++) if (pl_n[m] && (best < 0 || pl_n[m] > pl_n[best])) best = m;
            if (best < 0) break;
            len += snprintf(r->causes + len, sizeof r->causes - len, " m%02x:%lu/%ld",
                            best, (unsigned long)pl_n[best], (long)pl_worst[best]);
            pl_n[best] = 0;
        }
        r->min_slack = min_slack_k[1] < min_slack_k[0] ? min_slack_k[1] : min_slack_k[0];
        snprintf(r->prof, sizeof r->prof,
                 "VIA1 syncs %lu, late %lu, worst slack %ld | insn syncs %lu, late %lu, worst slack %ld (sysclk)",
                 (unsigned long)n_sync[1], (unsigned long)late_k[1], (long)min_slack_k[1],
                 (unsigned long)n_sync[0], (unsigned long)late_k[0], (long)min_slack_k[0]);
    }
    multicore_fifo_push_blocking(1);
    for (;;) __wfe();
}

static void set_clock(uint32_t mhz)
{
    /* ⚠ 250 MHz is an overclock: core voltage raised to 1.20 V first. 150 MHz
     * is the RP2350's rated clock and the one the verdict is about. */
    if (mhz > 150) { vreg_set_voltage(VREG_VOLTAGE_1_20); sleep_ms(5); }
    set_sys_clock_khz(mhz * 1000, true);
    if (mhz <= 150) { sleep_ms(5); vreg_set_voltage(VREG_VOLTAGE_DEFAULT); }
    sleep_ms(20);
}

static run_t runs[24];
static int nruns;

static void report(void)
{
    printf("\n=== PicoD64 F1 bench: emulated 1541 on RP2350, lockstep at 1 MHz ===\n");
    printf("%4s %-6s %-5s %9s %7s  %s\n", "MHz", "load", "mode", "result", "budget", "detail");
    for (int i = 0; i < nruns; i++) {
        run_t *r = &runs[i];
        if (r->rt)
            printf("%4lu %-6s %-5s %9s %7lu  late syncs=%lu, worst slack %ld sysclk\n",
                   (unsigned long)r->mhz, r->wl, "lock", r->late ? "LATE" : "ok", (unsigned long)r->mhz,
                   (unsigned long)r->late, (long)r->min_slack);
        else
            printf("%4lu %-6s %-5s %6.2fMHz %7lu  free-running: %.1f sysclk per emulated cycle, average\n",
                   (unsigned long)r->mhz, r->wl, "free", (double)r->cycles / r->us,
                   (unsigned long)r->mhz, (double)r->mhz * r->us / r->cycles);
        printf("%27s fingerprint: %s\n", "", r->fp);
        if (r->prof[0]) printf("%27s profile: %s\n", "", r->prof);
    }
    printf("(budget = sysclk per emulated 1 MHz cycle; LATE = a sync point reached after its real time)\n");
}

static void sweep(void)
{
    static const uint32_t mhz[] = { 150, 200 };   /* 250 dropped: 150 is the target */
    nruns = 0;
    for (unsigned m = 0; m < 2; m++) {
        set_clock(mhz[m]);
        for (int w = 0; w < 7; w++)
            for (int rt = 0; rt < 2; rt++) {
                if (w >= 3 && rt) continue;   /* iec/micro loads: free-running cost only */
                run_t *r = &runs[nruns++];
                memset(r, 0, sizeof *r);
                r->mhz = mhz[m]; r->rt = rt; r->target = 5000000;
                static const char *const name[] = { "dos", "stress", "timers", "iec", "iecatn", "nop", "ram" };
                static const uint8_t *const rom[] = { wl_dos, wl_stress, wl_timers, wl_iec, wl_iecatn, wl_nop, wl_ram };
                r->wl = name[w]; r->rom = rom[w];
                printf("running %lu MHz %s %s...\n", (unsigned long)r->mhz, r->wl, rt ? "lockstep" : "free");
                cur = r;
                multicore_reset_core1();
                multicore_launch_core1(core1_run);
                multicore_fifo_pop_blocking();
                multicore_reset_core1();
            }
    }
    set_clock(150);
}


/* --- F1d: the timing truth test ------------------------------------------ */
static uint32_t log0_t[LOGN]; static uint8_t log0_v[LOGN];

typedef struct {
    uint32_t mhz; const char *wl;
    uint32_t n0, n1, matched, mismatch, loops, us;
    int64_t spread; uint32_t bins[7]; double slope_ppm;
    uint32_t via1_late, insn_late; int32_t via1_worst;
    char causes[200];
} trun_t;
static trun_t truns[4];
static int ntruns;

static void __not_in_flash_func(sample_pins)(trun_t *t)
{
    uint32_t irq = save_and_disable_interrupts();
    DEMCR |= 1u << 24; DWT_CTRL |= 1;     /* core 0's own cycle counter */
    uint32_t prev = (sio_hw->gpio_in >> PIN_CLK) & 3, n = 0, loops = 0;
    g_go = 1;
    while (!g_done) {
        uint32_t v = (sio_hw->gpio_in >> PIN_CLK) & 3;
        loops++;
        if (v != prev) {
            if (n < LOGN) { log0_t[n] = DWT_CYCCNT; log0_v[n] = (uint8_t)v; n++; }
            prev = v;
        }
    }
    restore_interrupts(irq);
    t->n0 = n; t->loops = loops;
}

static void analyse(trun_t *t)
{
    /* walk core 1's log, skipping entries that changed nothing on the pins */
    uint32_t pin = 3, i0 = 0, k = 0;
    int64_t emin = INT64_MAX, emax = INT64_MIN, T = 0;
    static int32_t err[LOGN];          /* sysclk; far inside int32 for 5 s */
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    t->mismatch = 0;
    for (uint32_t i = 0; i < n1 && i0 < t->n0; i++) {
        uint32_t c = log1[i] >> 2, lv = log1[i] & 3;
        if (lv == pin) continue;
        pin = lv;
        if (log0_v[i0] != lv) { t->mismatch++; }
        if (i0) T += (uint32_t)(log0_t[i0] - log0_t[i0 - 1]);
        int64_t e = T - (int64_t)c * t->mhz;
        err[k] = (int32_t)e; k++;
        if (e < emin) emin = e;
        if (e > emax) emax = e;
        double x = c, y = (double)T;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
        i0++;
    }
    t->matched = k;
    t->spread = k ? emax - emin : 0;
    /* jitter histogram, ns above the earliest edge */
    static const int edge_ns[6] = { 25, 50, 100, 250, 500, 1000 };
    memset(t->bins, 0, sizeof t->bins);
    for (uint32_t j = 0; j < k; j++) {
        int64_t ns = (err[j] - emin) * 1000 / t->mhz; int b = 0;
        while (b < 6 && ns >= edge_ns[b]) b++;
        t->bins[b]++;
    }
    double slope = k > 1 ? (k * sxy - sx * sy) / (k * sxx - sx * sx) : 0;
    t->slope_ppm = k > 1 ? (slope / t->mhz - 1.0) * 1e6 : 0;
}

static void timing_test(void)
{
    static const uint32_t mhz[] = { 150, 200 };
    static const char *const name[] = { "iec", "iecatn" };
    static const uint8_t *const rom[] = { wl_iec, wl_iecatn };
    ntruns = 0;
    /* CLK and DATA push-pull, released (high) */
    sio_hw->gpio_out |= PULL_MASK; sio_hw->gpio_oe_set = PULL_MASK;
    tmode = 1;
    for (int m = 0; m < 2; m++) {
        set_clock(mhz[m]);
        for (int w = 0; w < 2; w++) {
            trun_t *t = &truns[ntruns++];
            run_t r; memset(&r, 0, sizeof r); memset(t, 0, sizeof *t);
            r.mhz = t->mhz = mhz[m]; r.rt = 1; r.target = 5000000;
            r.wl = t->wl = name[w]; r.rom = rom[w];
            printf("timing %lu MHz %s...\n", (unsigned long)r.mhz, r.wl);
            sleep_ms(50);
            sio_hw->gpio_out |= PULL_MASK;
            n1 = 0; g_go = 0; g_done = 0; cur = &r;
            multicore_reset_core1();
            multicore_launch_core1(core1_run);
            sample_pins(t);
            multicore_fifo_pop_blocking();
            multicore_reset_core1();
            t->n1 = n1; t->us = r.us;
            /* ⚠ VIA1 only: run 9 printed the sum with instruction-end syncs
             * under a "VIA1" label (2026-10-25) */
            t->via1_late = r.late_via1; t->via1_worst = r.worst_via1; t->insn_late = r.late_insn;
            memcpy(t->causes, r.causes, sizeof t->causes);
            analyse(t);
        }
    }
    tmode = 0;
    sio_hw->gpio_oe_clr = PULL_MASK; sio_hw->gpio_out &= ~PULL_MASK;
    set_clock(150);
}

static void report_timing(void)
{
    printf("\n=== F1d timing: every IEC edge measured on the pins by core 0 ===\n");
    printf("%4s %-7s %6s %4s %9s  %s\n", "MHz", "load", "edges", "bad", "jitter", "edges by ns after the earliest: <25 <50 <100 <250 <500 <1000 >=1000");
    for (int i = 0; i < ntruns; i++) {
        trun_t *t = &truns[i];
        printf("%4lu %-7s %6lu %4lu %6lld ns  ", (unsigned long)t->mhz, t->wl, (unsigned long)t->matched,
               (unsigned long)t->mismatch, (long long)(t->spread * 1000 / t->mhz));
        for (int b = 0; b < 7; b++) printf("%lu ", (unsigned long)t->bins[b]);
        printf("\n%20s slope %+.2f ppm; sampler %.0f ns/poll; logs %lu/%lu edges; core 1 says VIA1 late %lu (worst slack %ld sysclk), behind at insn ends %lu\n%20s late VIA1 by cause (mask:count/worst):%s\n", "",
               t->slope_ppm, t->loops ? (double)t->us * 1000.0 / t->loops : 0.0,
               (unsigned long)t->n0, (unsigned long)t->n1, (unsigned long)t->via1_late, (long)t->via1_worst, (unsigned long)t->insn_late, "", t->causes);
    }
    printf("(jitter = latest minus earliest edge error; error = measured time - emulated cycle x 1 us)\n");
}

static void blink(int n)
{
    for (int i = 0; i < n; i++) { gpio_put(PICO_DEFAULT_LED_PIN, 1); sleep_ms(120); gpio_put(PICO_DEFAULT_LED_PIN, 0); sleep_ms(120); }
}

int main(void)
{
    /* LED: 3 blinks = main() reached; on = a sweep is running; slow blink = reporting */
    gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    blink(3);
    stdio_init_all();
    for (int p = PIN_ATN; p <= PIN_DATA; p++) {
        gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); gpio_put(p, 0);
    }
    bench_track(trk); bench_disk(&disk, trk);
    sleep_ms(3000);           /* time to open the serial port */
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    sweep();
    timing_test();
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    for (;;) {
        report();
        report_timing();
        printf("(end of report)\n");
        blink(1);
        for (int i = 0; i < 100; i++) {
            int ch = getchar_timeout_us(100000);
            if (ch == 'B') { printf("rebooting to BOOTSEL\n"); sleep_ms(100); reset_usb_boot(0, 0); }
            if (ch == 'r') { gpio_put(PICO_DEFAULT_LED_PIN, 1); sweep(); timing_test(); gpio_put(PICO_DEFAULT_LED_PIN, 0); break; }
        }
    }
}
