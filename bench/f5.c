/* -------------------------------------------------------------------------
 * PicoD64 F5 (part 1, no SD card): the REAL 1541 DOS [S9] on the Pico 2,
 * in real time at 150 MHz, on a disk in RAM.
 *
 * Core 1 runs the drive in lockstep (the F1 platform layer, copied from
 * main.c) and works the DOS's job queue exactly as host/f4b.c does: boot;
 * SEEK + READ every sector; SEEK + WRITE every sector with new data; SEEK +
 * READ them all back. So it exercises the stepper, the head, the write path
 * and the map rebuild after writing, under the real DOS, against real time.
 *
 * Pass: 0 late VIA1 syncs; every job OK and every sector's data right; and
 * jobhash (each job's code, track, sector, result and finishing cycle) equal
 * to the host's (host/f4b prints it), which shows the Pico ran the same
 * emulation cycle for cycle.
 *
 * The test disk is f4b's "rand" disk, generated here track by track (there
 * is no room for a whole D64 next to its GCR). Nothing need be attached:
 * GP2-4 (ATN, CLK, DATA) are pulled up, so the bus is idle.
 * 'B' over serial reboots to BOOTSEL.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "pico/bootrom.h"

#define PIN_ATN  2
#define PIN_CLK  3
#define PIN_DATA 4
#define PULL_MASK ((1u << PIN_CLK) | (1u << PIN_DATA))

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

/* --- the F1 platform layer (main.c) ---------------------------------------- */
static uint32_t clk_per_us, deadline, last_c;
/* profile: what ran since the previous sync (core FF_PROF bits: 1 events, 2 head lookahead,
 * 4 VIA read, 8 VIA write, 16 IEC change, 64 IRQ entry), kept for the worst VIA1 sync */
static uint8_t g_prof, worst_prof;
static uint32_t worst_cycle;
#define FF_PROF(b) (g_prof |= (uint8_t)(b))
static uint32_t n_sync[2], late_k[2];
static int32_t min_slack_k[2];

static inline void plat_sync(uint32_t c, int kind)
{
    deadline += (c - last_c) * clk_per_us; last_c = c;
    int32_t slack = (int32_t)(deadline - DWT_CYCCNT);
    n_sync[kind]++;
    if (slack < min_slack_k[kind]) {
        min_slack_k[kind] = slack;
        if (kind == 1) { worst_prof = g_prof; worst_cycle = c; }
    }
    if (slack < 0) late_k[kind]++;
    g_prof = 0;
    while ((int32_t)(deadline - DWT_CYCCNT) > 0) { }
}
static inline int32_t plat_slack(uint32_t c) { return (int32_t)(deadline + (c - last_c) * clk_per_us - DWT_CYCCNT); }
static inline void plat_lead(uint32_t c, uint32_t n)
{
    int32_t slack = plat_slack(c), lim = (int32_t)(n * clk_per_us);
    n_sync[0]++;
    if (slack < min_slack_k[0]) min_slack_k[0] = slack;
    if (slack < 0) late_k[0]++;
    while (slack > lim) slack = plat_slack(c);
}
/* Harness work between jobs (making and checking sector data, up to ~130 us)
 * is not the drive's: real time is paused around it, so it cannot show up
 * as lateness. On hardware the drive simply idles between commands. */
static inline uint32_t hold(void) { return DWT_CYCCNT; }
static inline void resume(uint32_t t) { deadline += DWT_CYCCNT - t; }
static inline uint8_t plat_iec_in(void) { return (uint8_t)((~sio_hw->gpio_in >> PIN_ATN) & 7); }
static inline void plat_iec_out(uint8_t pulls)
{
    uint32_t m = ((uint32_t)pulls << PIN_ATN) & PULL_MASK;
    sio_hw->gpio_oe_set = m;
    sio_hw->gpio_oe_clr = PULL_MASK & ~m;
}

#define FF_HOT __attribute__((section(".time_critical.ff")))
#define PLAT_SYNC(c, k)  plat_sync((c), (k))
#define PLAT_SLACK(c)    plat_slack(c)
#define PLAT_LEAD(c, n)  plat_lead((c), (n))
#define PLAT_LA_BUDGET   300
#define PLAT_IEC_IN()    plat_iec_in()
#define PLAT_IEC_OUT(p)  plat_iec_out(p)
#include "../core/drive1541.h"
#include "dos1541.h"                  /* the DOS ROM, from build.sh (test use only [S9]) */

static uint8_t gcr[D64_GCR_BYTES];
static disk_t disk;
static syncs_t frames[D64_TRACKS];

/* --- f4b's "rand" disk and write data, without a whole D64 in RAM ------------
 * Byte i of the image is an LCG's i-th output; the first two bytes of each
 * sector are then its track and sector; the BAM's ID bytes are "FF". The LCG
 * state at the start of each track is kept, so any sector can be regenerated. */
static uint32_t st_rand[D64_TRACKS + 1], st_new[D64_TRACKS + 1];
static void lcg_track_states(uint32_t seed, uint32_t *st)
{
    uint32_t x = seed;
    for (int t = 1; t <= D64_TRACKS; t++) {
        st[t] = x;
        for (int i = 0; i < d64_sectors(t) * 256; i++) x = x * 1103515245u + 12345u;
    }
}
static void sector(const uint32_t *st, int t, int s, int stamp, uint8_t *out)
{
    uint32_t x = st[t];
    for (int i = 0; i < s * 256; i++) x = x * 1103515245u + 12345u;
    for (int i = 0; i < 256; i++) { x = x * 1103515245u + 12345u; out[i] = (uint8_t)(x >> 16); }
    if (stamp) {
        out[0] = (uint8_t)t; out[1] = (uint8_t)s;
        if (t == 18 && s == 0) out[162] = out[163] = 'F';
    }
}

static void build_disk(void)
{
    static uint8_t trk[21 * 256];
    uint8_t *p = gcr;
    for (int i = 0; i < DISK_HALFTRACKS; i++) { disk.gcr[i] = 0; disk.len[i] = 0; }
    for (int t = 1; t <= D64_TRACKS; t++) {
        for (int s = 0; s < d64_sectors(t); s++) sector(st_rand, t, s, 1, trk + s * 256);
        d64_track_sectors(trk, t, 'F', 'F', p);
        disk.gcr[2 * (t - 1)] = p; disk.len[2 * (t - 1)] = (uint16_t)d64_track_bytes(t);
        p += d64_track_bytes(t);
    }
    disk_frame(&disk, frames, D64_TRACKS);
}

/* --- the jobs, exactly as host/f4b.c -------------------------------------- */
static uint32_t jobhash = 2166136261u;
static void hash(uint32_t v) { for (int i = 0; i < 4; i++) { jobhash ^= (v >> (8 * i)) & 0xFF; jobhash *= 16777619u; } }

static uint8_t job(uint8_t code, uint8_t t, uint8_t s)
{
    D.ram[6] = t; D.ram[7] = s; D.ram[0] = code;
    uint32_t limit = D.cycles + 2000000;
    while (D.ram[0] & 0x80) {
        if (D.cycles > limit || CPU.jammed) { hash(0xDEAD); return 0; }
        drive_run(D.cycles + 1000);
    }
    hash(code); hash(t); hash(s); hash(D.ram[0]); hash(D.cycles);
    return D.ram[0];
}

typedef struct { int read_ok, write_ok, reread_ok, bad; uint32_t cycles, ms, remap_clk, span_clk; } result_t;
static result_t res;
static volatile int done;

static void __not_in_flash_func(core1_main)(void)
{
    uint32_t irq = save_and_disable_interrupts();
    DEMCR |= 1u << 24; DWT_CYCCNT = 0; DWT_CTRL |= 1;
    clk_per_us = 150; last_c = 0;
    for (int k = 0; k < 2; k++) { n_sync[k] = late_k[k] = 0; min_slack_k[k] = INT32_MAX; }
    drive_init(dos1541, &disk);
    uint64_t t0 = time_us_64();
    deadline = DWT_CYCCNT + 1000;
    drive_run(2500000);                              /* the DOS boots */
    static uint8_t want[256];
    for (int t = 1; t <= D64_TRACKS; t++) {
        if (job(0xB0, (uint8_t)t, 0) != 1) { res.bad++; continue; }
        for (int s = 0; s < d64_sectors(t); s++) {
            uint8_t r = job(0x80, (uint8_t)t, (uint8_t)s);
            uint32_t h0 = hold();
            sector(st_rand, t, s, 1, want);
            if (r == 1 && !memcmp(D.ram + 0x300, want, 256)) res.read_ok++; else res.bad++;
            resume(h0);
        }
    }
    for (int t = 1; t <= D64_TRACKS; t++) {
        if (job(0xB0, (uint8_t)t, 0) != 1) { res.bad++; continue; }
        for (int s = 0; s < d64_sectors(t); s++) {
            if (job(0xB0, (uint8_t)t, 0) != 1) res.bad++;
            uint32_t h0 = hold();
            sector(st_new, t, s, 0, D.ram + 0x300);
            resume(h0);
            D.ram[0x50] = 0;                          /* GCRFLG: binary (f4b.c) */
            if (job(0x90, (uint8_t)t, (uint8_t)s) == 1) res.write_ok++; else res.bad++;
        }
    }
    for (int t = 1; t <= D64_TRACKS; t++) {
        if (job(0xB0, (uint8_t)t, 0) != 1) { res.bad++; continue; }
        for (int s = 0; s < d64_sectors(t); s++) {
            uint8_t r = job(0x80, (uint8_t)t, (uint8_t)s);
            uint32_t h0 = hold();
            sector(st_new, t, s, 0, want);
            if (r == 1 && !memcmp(D.ram + 0x300, want, 256)) res.reread_ok++; else res.bad++;
            resume(h0);
        }
    }
    res.cycles = D.cycles;
    /* the map rebuild after a write (head_remap), timed on its own: track 1, the longest */
    { uint32_t t0 = DWT_CYCCNT; disk_frame_track(disk.gcr[0], disk.len[0] * 8u, disk.sync[0]);
      res.remap_clk = DWT_CYCCNT - t0; }
    /* ...and as head_remap does it now (disk_frame_span): one sector's data block written,
     * from its SYNC on (5 SYNC bytes + 325 GCR bytes = 2640 bits), track 1's 2nd SYNC */
    { syncs_t *r = disk.sync[0]; uint32_t lo = r->first[1] - 9;
      uint32_t t0 = DWT_CYCCNT; disk_frame_span(disk.gcr[0], disk.len[0] * 8u, r, lo, 2640);
      res.span_clk = DWT_CYCCNT - t0; }
    res.ms = (uint32_t)((time_us_64() - t0) / 1000);
    restore_interrupts(irq);
    done = 1;
    for (;;) __wfe();
}

int main(void)
{
    gpio_init(PICO_DEFAULT_LED_PIN); gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    stdio_init_all();
    for (int p = PIN_ATN; p <= PIN_DATA; p++) { gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); gpio_put(p, 0); }
    set_sys_clock_khz(150000, true);
    lcg_track_states(64, st_rand);
    lcg_track_states(99, st_new);
    build_disk();
    sleep_ms(3000);
    printf("F5 (no SD): real DOS on the Pico 2 at 150 MHz, running...\n");
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    multicore_launch_core1(core1_main);
    while (!done) { int ch = getchar_timeout_us(100000); if (ch == 'B') reset_usb_boot(0, 0); }
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    for (;;) {
        printf("\n=== PicoD64 F5 (no SD): the real 1541 DOS in real time on the Pico 2 ===\n");
        printf("read %d/683, written %d/683, read back %d/683, failures %d\n",
               res.read_ok, res.write_ok, res.reread_ok, res.bad);
        printf("emulated %lu cycles in %lu ms real time (%.4f of real time)\n",
               (unsigned long)res.cycles, (unsigned long)res.ms, res.ms ? res.cycles / 1000.0 / res.ms : 0.0);
        printf("VIA1 syncs %lu, late %lu, worst slack %ld sysclk | insn syncs %lu, behind %lu, worst %ld\n",
               (unsigned long)n_sync[1], (unsigned long)late_k[1], (long)min_slack_k[1],
               (unsigned long)n_sync[0], (unsigned long)late_k[0], (long)min_slack_k[0]);
        printf("worst VIA1 sync at cycle %lu, after: %s%s%s%s%s%s\n", (unsigned long)worst_cycle,
               worst_prof & 1 ? "events " : "", worst_prof & 2 ? "lookahead " : "", worst_prof & 4 ? "VIA-read " : "",
               worst_prof & 8 ? "VIA-write " : "", worst_prof & 16 ? "IEC-change " : "", worst_prof & 64 ? "IRQ " : "");
        printf("map rebuild (head_remap) of track 1: %lu sysclk = %lu us\n", (unsigned long)res.remap_clk,
               (unsigned long)(res.remap_clk / 150));
        printf("span rebuild (head_remap now), one data block on track 1: %lu sysclk = %lu us\n", (unsigned long)res.span_clk,
               (unsigned long)(res.span_clk / 150));
        printf("jobhash=%08lX cycles=%lu\n", (unsigned long)jobhash, (unsigned long)res.cycles);
        printf("(end of report)\n");
        for (int i = 0; i < 100; i++) { int ch = getchar_timeout_us(100000); if (ch == 'B') { sleep_ms(100); reset_usb_boot(0, 0); } }
    }
}
