/* -------------------------------------------------------------------------
 * PicoD64 -- the board firmware (rev 1, Pico 2). FIRMWARE.md, "Board
 * firmware design". Core 1: fw/bus.h on the IEC pins, the drive in lockstep
 * (bench/f5.c's platform layer). Core 0: the SD card (FatFs, fw/storage.c,
 * fw/svc.c), the OLED and knob (fw/ui.c), the LED, write-back, and the
 * sniffer that sees "CD_" while the emulated 1541 has the bus.
 *
 * The DOS ROM is the user's: /dos1541.bin on the card (16384 bytes) [2026-10-29:
 * the ROM is not shipped].
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "atn_gate.pio.h"
#include "iec_sniff.pio.h"

#include "sound.h"

/* --- pins (hardware/gen/gen_sch.py, AUDIT A2) -------------------------------- */
#define PIN_TX 0
#define PIN_SDA 4
#define PIN_SCL 5
#define PIN_SD_DET 7
#define PIN_SD_MISO 8
#define PIN_SD_CS 9
#define PIN_SD_SCK 10
#define PIN_SD_MOSI 11
#define PIN_ATN_IN 12            /* 12-15: ATN CLK DATA RESET, pin high = line released (divider) */
#define PIN_RST_IN 15
#define PIN_CLK_OUT 16           /* 16-17: CLK DATA, pin high = line pulled LOW (2N3904) */
#define PIN_DATA_OUT 17
#define PIN_ENC_A 18
#define PIN_ENC_B 19
/* B, A into enc_update: with A, B the knob ran backwards on the first board (2026-11-06); the
 * direction in ui.c was an assumption [A], this is the real board's */
#define PIN_ENC_SW 20
#define PIN_LED 21
#define PIN_PIEZO 22

#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

/* core 1's code in SRAM: the drive's per-cycle code (FF_HOT) and browse mode's (FF_C1)
 * never wait on flash while core 0 runs from it */
#define FF_C1 __attribute__((section(".time_critical.c1")))
#include "bus_plat.h"

/* --- the drive's platform layer: lockstep (bench/f5.c), the bus on GP12-17 --- */
/* the system clock, MHz: 150 is the RP2350's rating. 200 (out of spec) was tried on the first board for headroom
 * (2026-11-06); bash fw/build.sh passes SYS_MHZ through CMake. Everything below timed in sysclk derives from it */
#ifndef SYS_MHZ
#define SYS_MHZ 150
#endif
static uint32_t clk_per_us = SYS_MHZ, deadline, last_c;
static uint32_t late_syncs;
#include <limits.h>
static volatile int32_t wr_head = 6 * SYS_MHZ;   /* sysclk (6 us): a VIA1 write's work starts this early (serial H / h, +-1 us) */
#define WR_HEAD ((uint32_t)wr_head)
/* sysclk: the pins change this long after the write's cycle (serial > <, 0.1 us). -0.6 us (first board, The Hat) answered
 * 0.6-0.9 us before the Ultimate's 1541 (2026-11-09, logic analyser on the same C64: The Hat's answers to ATN 5.95-12.25 us
 * vs 6.53-12.89; Transwarp's first byte 5.75 vs 6.63 min) and broke Transwarp's bootstrap, which wants the drive no sooner
 * than 0.54 us + 6 cycles after the C64's release. -0.1 us: 6.39-12.72 on The Hat, and Transwarp's bootstrap passes.
 * +0.3 us since 2026-11-13: until the lazy T1 with its interrupt masked (core/via6522.h), nearly every write's pins came
 * up to 0.25 us after their aim, so -0.1 had that lateness in it; on time, -0.1 answered ~0.4 us early. Measured on
 * Reanim8ed's loader (the C64 samples bit pairs at fixed cycles after its ATN edge, a window under 1 us wide), the
 * bus from the C64's ATN edge to each bit pair, middle 96%: the Ultimate's 1541 pulls 6.50-13.25 us, releases
 * 6.57-13.33; PicoD64 at -0.1 6.14-12.77 / 6.37-13.03 (the demo stalled or crashed at random); at +0.3 with releases
 * 0.14 us early (rel_lead) 6.58-13.13 / 6.68-13.33, and it runs both sides to its end */
static volatile int32_t wr_trim = 3 * (SYS_MHZ / 10);
/* A line PicoD64 releases is seen high ~0.4 us after its pin changes (the transistor letting go, the pull-up's rise);
 * one it pulls is seen low within ~0.16 us (logic analyser, Mafio-DOS, 2026-11-09). A 1541's 7406 switches both fast.
 * Mafio's loader reads its first bit pair 8.12 us after it sees the sync where the drive sends it at 8.00: with the
 * release that late the C64 read bits 0-1 as 00 in about one byte in 70. So a write's releases went rel_lead (0.3 us)
 * before its pulls (2026-11-09, ddf4f19).
 * ⚠ 0 since 2026-11-12: that lead let a line released by a write rise BEFORE a line pulled by the same write fell, an
 * order a 1541 never shows (its pull is the fast edge). Krill's loader (Coma Light 13) tests "block ready" with
 * BIT $DD00 / BVC (CLK released?) / BMI (DATA released too: no block): the drive's LDA #$02 / STA $1800 releases CLK
 * and pulls DATA at once, so a poll landing between the two edges read "no block", the C64 ended a file early and the
 * demo crashed later, at random. With 0 it runs to its turn-disk screen, and Mafio-DOS (fixed meanwhile by the pin
 * timing, 98e7315) still loads its menu. Kept as a serial knob (} {, 0.05 us).
 * 0.14 us since 2026-11-13 (with the pins at +0.3, above): a release then lands on the bus ~0.1 us AFTER a pull of the
 * same write, as the Ultimate's 1541 gives (0.07), where at 0 it came 0.25 after */
static volatile int32_t rel_lead = 3 * (SYS_MHZ / 20);
/* sysclk: a VIA1 read samples the pins this long after its cycle's deadline (serial ] [, 0.1 us). A 6502 takes its read
 * data at the end of the cycle; Transwarp's upload (the C64 clocks 2-bit pairs at fixed cycles, the drive reads them at
 * fixed cycles after ATN) is where it shows (2026-11-09) */
static volatile int32_t rd_trim = 0;
static int32_t wr_lat_max = INT32_MIN, wr_lat_min = INT32_MAX; static uint32_t wr_deadline;
/* lean (always on, 2026-11-07): pins set after their write's cycle, the worst (sysclk) and how many; the yardstick for
 * the speed work (a 1541 answers The Hat with 0.19 us to spare). Serial 's' prints them, 'z' clears them */
static int32_t pin_late_max; static uint32_t pin_late_n, pin_writes, pin_late_gap; static uint16_t pin_late_pc;
static uint16_t wl_pc; static uint8_t wl_prof; static uint32_t wl_gap, wl_work; static int32_t wl_arrive;   /* the worst one's */
static uint32_t wr_arrive, wr_prev_end, sync_end_t;   /* this write's arrival; the previous sync's end; any sync's end */
/* how late (first board, 2026-11-06: a demo's loader fails with late climbing): the worst, in sysclk, the
 * drive's PC then; and how many were over 1 us and over 10 us. Serial 's' prints them, 'z' clears them */
static uint32_t late_max, late_1us, late_10us; static uint16_t late_pc; static const volatile uint16_t *late_pcp;
/* what ran since the last sync (core/drive1541.h FF_PROF: 1 events, 2 lookahead, 4 VIA read, 8 VIA write, 16 bus
 * change; the CPU's own 32/64 are left out, they'd cost every instruction), and the drive cycles in between */
static uint8_t g_prof, late_prof, wr_pre_prof; static uint32_t late_gap, sync_c, wr_pre_gap;
/* DIAG=1 (bash fw/build.sh with DIAG=1) builds in the timing diagnostics below: about 200 sysclk on a fast loader's
 * read-then-write, which a 150 MHz board can't spare (2026-11-07) */
#ifndef DIAG
#define DIAG 0
#endif
#if DIAG
#define FF_PROF(b) do { if ((b) < 32) g_prof |= (uint8_t)(b); } while (0)
#endif
/* A profile of the bus accesses (2026-11-07): sysclk per segment, total, count and worst, for the stretch a fast
 * loader's read-then-write must fit (a 1541 does BIT $1800 / BPL / STA $1800 in 6 us).
 *   0 read: its sync's end to the inputs taken   1 read: the VIA   2 read: the IRQ update
 *   3 a read's end to the next VIA write's entry, when 10 drive cycles or fewer apart
 *   4 write: entry to its sync   5 write: the sync's end to the inputs   6 write: the VIA
 *   7 write: the outputs (the pins' wait left out)   8 write: the IRQ update   9 write: the reschedule
 * Serial 'p' prints it, 'z' clears it. */
/* drive events by kind (0 all, 1 VIA1 T1, 2 VIA2 T1, 3 a T2, 4 a lookahead come due, 5 a disk byte), and the drive
 * cycles they came in; serial 'p' prints them with the profile */
static uint32_t ev_cnt[6], ev_c0;
#if DIAG
#define PLAT_EV(k) (ev_cnt[k]++)
#endif
#define PROF_N 12
static uint32_t prof_sum[PROF_N], prof_cnt[PROF_N], prof_max[PROF_N], prof_t, rd_end_t, rd_end_c, rd_end_ev, wr_entry_t, gate_wait;
static FF_C1 __attribute__((noinline)) void prof(int k, uint32_t d)
{
    if (bus_replaying || d > 100000u) return;
    prof_sum[k] += d; prof_cnt[k]++; if (d > prof_max[k]) prof_max[k] = d;
}
static uint32_t mark_max[5], wr_sync_end;
#ifndef COST
#define COST 0   /* COST=1: a diagnostic build, the VIA paths' and events' cost, serial u (2026-11-10) */
#endif
#if COST && !DIAG
static uint32_t cs_t0, cs_sum[6], cs_n[6], cs_max[6]; static uint8_t cs_v1;
static void __not_in_flash_func(cs_add)(int k, uint32_t d) { cs_sum[k] += d; cs_n[k]++; if (d > cs_max[k]) cs_max[k] = d; }
#define PLAT_RMARK(n) do { uint32_t t_ = DWT_CYCCNT; if ((n) == 0) { cs_t0 = t_; cs_v1 = 0; } else if ((n) == 1) { cs_t0 = t_; cs_v1 = 1; } \
        else if ((n) == 3) cs_add(cs_v1 ? 1 : 2, t_ - cs_t0); } while (0)
#define PLAT_MARK(n) do { if ((n) == 0) cs_t0 = DWT_CYCCNT; else if ((n) == 2) cs_add(0, DWT_CYCCNT - cs_t0); } while (0)
#define PLAT_CS_T() DWT_CYCCNT
#define PLAT_CS(k, t0) do { if ((k) == 5) cs_n[5]++; else cs_add((k), DWT_CYCCNT - (t0)); } while (0)
#endif
#if DIAG
#define PLAT_RMARK(n) do { uint32_t t__ = DWT_CYCCNT; \
    if ((n) == 1) prof(0, t__ - sync_end_t); else if ((n) == 2) prof(1, t__ - prof_t); \
    else if ((n) == 3) { if (prof_t) prof(2, t__ - prof_t); rd_end_t = prof_t ? t__ : 0; rd_end_c = D.cycles; rd_end_ev = ev_cnt[0]; } \
    else if ((n) == 4) { if (rd_end_t && D.cycles - rd_end_c <= 10) { prof(3, t__ - rd_end_t); prof((ev_cnt[0] != rd_end_ev) ? 10 : 11, t__ - rd_end_t); } rd_end_t = 0; wr_entry_t = t__; } \
    prof_t = (n) == 3 ? 0 : t__; } while (0)
#define PLAT_MARK(n) do { uint32_t t__ = DWT_CYCCNT; \
    if ((n) == 0) { prof(4, wr_arrive - wr_entry_t); prof(5, t__ - wr_sync_end); gate_wait = 0; } \
    else if ((n) == 1) prof(6, t__ - prof_t); else if ((n) == 2) prof(7, t__ - prof_t - gate_wait); \
    else if ((n) == 3) prof(8, t__ - prof_t); else prof(9, t__ - prof_t); \
    prof_t = t__; } while (0)
#endif
static FF_C1 __attribute__((noinline)) void plat_late(int32_t over)   /* one copy, not one per sync */
{
    late_syncs++;
    if (over > SYS_MHZ) late_1us++;
    if (over > 10 * SYS_MHZ) late_10us++;
    if ((uint32_t)over > late_max) { late_max = (uint32_t)over; late_pc = late_pcp ? *late_pcp : 0; late_prof = g_prof; late_gap = last_c - sync_c; }
}
/* the disk's coming bytes, worked out in the waits below (core/drive1541.h, the ring), so a catch-up only takes them */
static int head_ring_fill(void);
#define PLAT_RING_FILL() do { } while (0)
#define RING_COST (2 * SYS_MHZ)                         /* sysclk one more byte may take, at most, with margin */
static inline void plat_sync(uint32_t c)
{
    deadline += (c - last_c) * clk_per_us; last_c = c; wr_deadline = 0;
    if (bus_replaying) return;                       /* off the bus: as fast as it can */
    int32_t over = (int32_t)(DWT_CYCCNT - deadline);
    if (over > 0) plat_late(over);
    while ((int32_t)(deadline + rd_trim - DWT_CYCCNT) > RING_COST && head_ring_fill()) { }
    while ((int32_t)(deadline + rd_trim - DWT_CYCCNT) > 0) { }   /* (rd_trim, above) */
    g_prof = 0; sync_c = c; sync_end_t = DWT_CYCCNT;
}
/* A VIA1 write's pins must change on its cycle: a demo's loader reads the bus 13.19 us after its ATN edge where a
 * 1541 answers by 13.0 (FIRMWARE.md, first board). Setting them after the deadline put them 0.8-2.2 us late (the VIA,
 * the outputs, the gate, taking 115-330 sysclk, varying), and a fixed early start only moved that spread. So the
 * write's work starts WR_HEAD early, and the pins alone wait for the cycle (plat_iec_gate): a few sysclk.
 * wr_lat: sysclk from the cycle to the pins being set, worst and best (serial 's'). */
static inline void plat_sync_wr(uint32_t c)
{
    wr_arrive = DWT_CYCCNT; wr_prev_end = sync_end_t;
    deadline += (c - last_c) * clk_per_us; last_c = c;
    if (bus_replaying) { wr_deadline = 0; return; }   /* off the bus its deadlines run far ahead: never wait for them */
    wr_deadline = deadline;
    uint32_t t = deadline - WR_HEAD;
    if ((int32_t)(DWT_CYCCNT - t) > 0 && (int32_t)(DWT_CYCCNT - deadline) > 0) plat_late((int32_t)(DWT_CYCCNT - deadline));
    while ((int32_t)(t - DWT_CYCCNT) > RING_COST && head_ring_fill()) { }
    while ((int32_t)(t - DWT_CYCCNT) > 0) { }
    wr_pre_prof = g_prof; wr_pre_gap = c - sync_c;    /* what ran before this write's sync */

    g_prof = 0; sync_c = c; wr_sync_end = sync_end_t = DWT_CYCCNT;
}
static inline int32_t plat_slack(uint32_t c) { return bus_replaying ? 1 << 30 : (int32_t)(deadline + (c - last_c) * clk_per_us - DWT_CYCCNT); }
static inline void plat_lead(uint32_t c, uint32_t n)
{
    if (bus_replaying) return;
    int32_t lim = (int32_t)(n * clk_per_us);
    while (plat_slack(c) > lim) { }
}
static inline void plat_resync(uint32_t c) { last_c = c; deadline = DWT_CYCCNT + 1000; wr_deadline = 0; }

/* the sniffer's log: every change of the bus as the drive saw it (core 0 reads it). 2048 (was 4096, 2026-11-06,
 * 8 KB for the undocumented opcodes and timing work): the sniffer only looks for "CD_", sent the slow standard
 * way, so 2048 changes are over 100 ms of it [C]; core 0 drains it every pass, its longest stall an OLED frame
 * (~27 ms at 400 kHz [C]). A fast loader's traffic can overrun it: the oldest are dropped, which loses nothing */
#define LOG_N 1024   /* (2048 until 2026-11-14: 4 KB for the disk's look-ahead and the skips; 1024 changes are ~50 ms of the slow
                     * standard protocol the sniffer looks at, still twice core 0's longest stall) */
static volatile uint32_t log_buf[LOG_N], log_w;
static uint8_t log_last = 0xFF;
static inline uint8_t pins_in(void) { return (uint8_t)((~sio_hw->gpio_in >> PIN_ATN_IN) & 7); }
/* CLK_OUT (GP16) from the SIO; DATA_OUT (GP17) from the ATN gate's PIO state machine:
 * gate config bit 0 = pull DATA while ATN released, bit 1 = while asserted */
#define GATE_PIO pio0
#define GATE_SM  0
static inline void gate_set(uint32_t cfg) { pio_sm_put(GATE_PIO, GATE_SM, cfg); }
static inline void clk_set(int pull) { if (pull) sio_hw->gpio_set = 1u << PIN_CLK_OUT; else sio_hw->gpio_clr = 1u << PIN_CLK_OUT; }
static inline void pins_out(uint8_t pulls)        /* browse mode: IECD_CLK (2), IECD_DATA (4), whatever ATN does */
{
    clk_set(pulls & 2);
    gate_set((pulls & 4) ? 3u : 0u);
}
#define FF_HOT __attribute__((section(".time_critical.ff")))
/* SCRATCH_X: core 1's stack is 1.4 KB at its top (PICO_CORE1_STACK_SIZE, fw/CMakeLists.txt; it has used 816 bytes at most), the 2.6 KB below it
 * takes core 1's code that doesn't fit main RAM's floor and seldom runs: the lazy disk's skip (2026-11-11), inserting a
 * disk, selecting and re-framing a track, I clearing with a lazy T1, the skip before a track's first SYNC (2026-11-13) */
#define FF_SCRATCH __attribute__((section(".scratch_x.ff")))
#define PLAT_SYNC(c, k)  plat_sync(c)
#define PLAT_SYNC_WR(c)  plat_sync_wr(c)
#define PLAT_SLACK(c)    plat_slack(c)
#define PLAT_LEAD(c, n)  plat_lead((c), (n))
#define PLAT_LA_BUDGET   (4 * SYS_MHZ)
#define PLAT_LA_BUDGET_WR ((int32_t)(2 * SYS_MHZ + wr_head))    /* before a write, whose work starts WR_HEAD early */
#define PLAT_IEC_IN()    plat_iec_in()
#define PLAT_IEC_OUT(p)  plat_iec_out(p)
#define PLAT_RESYNC()    plat_resync(D.cycles)
#define PLAT_IEC_GATE(g) plat_iec_gate(g)
/* a run ends only with 3 us to spare, or 3000 cycles late at most (so the caller still runs every few ms) */
#define BUS_RUN_LEAD()   plat_lead(D.cycles, 10000)   /* at most 10 ms ahead (fw/bus.h) */
#define PLAT_STOP_OK(c, u) (bus_replaying || plat_slack(c) > 3 * SYS_MHZ || (int32_t)((c) - (u)) > 3000)
static inline void plat_iec_gate(uint8_t g);
static inline uint8_t plat_iec_in(void);
static inline void plat_iec_out(uint8_t p);
#include "../core/drive1541.h"
static inline uint8_t plat_iec_in(void)
{
    if (bus_replaying) return (uint8_t)(bus_replay_lines | bus_gate_pulls(bus_replay_lines & IEC_ATN));
    uint8_t l = pins_in();
    if (l != log_last) { log_last = l; log_buf[log_w & (LOG_N - 1)] = (D.cycles << 3) | l; log_w++; }
    return l;
}
static inline void plat_iec_out(uint8_t p) { bus_drive_pulls = p; }   /* the pins follow the gate, below */
/* one copy, not inlined at every write (that cost 712 bytes of RAM) */
static void __not_in_flash_func(gate_release)(uint8_t rel, uint32_t t)
{
    uint32_t tr = t - (uint32_t)rel_lead;
    while ((int32_t)(tr - DWT_CYCCNT) > 0) { }
    clk_set(rel & 1);
    gate_set((uint32_t)(rel >> 1) & 3);
}
static inline __attribute__((always_inline)) void plat_iec_gate(uint8_t g)   /* (inline at every write: out of line it lands in flash) */
{
    uint8_t was = bus_drive_gate;
    bus_drive_gate = g;
    if (bus_replaying) return;                       /* off the bus */
    if (wr_deadline) {                               /* a write's: on its cycle, never more than its head start away */
        uint32_t t = wr_deadline + (uint32_t)wr_trim;
#if DIAG
        uint32_t w0 = DWT_CYCCNT;
#endif
        if ((int32_t)(t - DWT_CYCCNT) <= (int32_t)(wr_head + 2 * SYS_MHZ)) {
            while ((int32_t)(t - (uint32_t)rel_lead - DWT_CYCCNT) > RING_COST && head_ring_fill()) { }
            if ((was & g) != was) gate_release(was & g, t);   /* its releases alone, first (rel_lead, above) */
            while ((int32_t)(t - DWT_CYCCNT) > 0) { }
        }
#if DIAG
        gate_wait = DWT_CYCCNT - w0;
#endif
    }
    clk_set(g & 1);
    gate_set((uint32_t)(g >> 1) & 3);
    if (wr_deadline) {
        int32_t l = (int32_t)(DWT_CYCCNT - wr_deadline - (uint32_t)wr_trim) - SYS_MHZ / 10;   /* more than 0.1 us after the aim (2026-11-13: the cycle was the yardstick, and at -0.1 the gate's few sysclk counted nearly every write) */
        pin_writes++;
        if (l > 0) { pin_late_n++; if (l > pin_late_max) { pin_late_max = l; pin_late_pc = late_pcp ? *late_pcp : 0; pin_late_gap = wr_pre_gap; } }
    }
#if !DIAG
    wr_deadline = 0;
#else
    if (wr_deadline) {                               /* (how long after its cycle the pins were set) */
        int32_t l = (int32_t)(DWT_CYCCNT - wr_deadline);
        if (l > wr_lat_max) { wr_lat_max = l; wl_pc = late_pcp ? *late_pcp : 0; wl_prof = wr_pre_prof; wl_gap = wr_pre_gap;
                              wl_arrive = (int32_t)(wr_arrive - wr_deadline); wl_work = wr_arrive - wr_prev_end; }
        wr_deadline = 0;
        if (l < wr_lat_min) wr_lat_min = l;
    }
#endif
}
#include "../core/browse.h"

/* --- core 1 -> core 0: card requests (blocking) --------------------------------- */
enum { RQ_NONE, RQ_LIST, RQ_READ, RQ_LOAD, RQ_VIRTUAL, RQ_FLUSH, RQ_WRITE, RQ_PREP, RQ_FIND, RQ_NEW };
static struct {
    volatile int op, done;
    const char *path, *first; int idx; fsent_t *e; uint8_t *buf; long max;
    volatile long result;
} rq;
static long request(int op, const char *path, const char *first, int idx, fsent_t *e, uint8_t *buf, long max)
{
    rq.path = path; rq.first = first; rq.idx = idx; rq.e = e; rq.buf = buf; rq.max = max;
    rq.done = 0; __dmb(); rq.op = op;
    while (!rq.done) { }
    __dmb();
    return rq.result;
}
static uint32_t bus_us(void) { return time_us_32(); }
static uint8_t bus_in(void) { return pins_in(); }
static void bus_out(uint8_t p) { pins_out(p); }
static int bus_reset(void) { return !((sio_hw->gpio_in >> PIN_RST_IN) & 1); }
/* a RESET pulse since the last look: GPIO15's falling edge, latched in the raw interrupt status whether or not the
 * interrupt is enabled; reading clears it (fw/bus.h BUS_RESET_EDGE) */
static inline int bus_reset_edge(void)
{
    uint32_t m = GPIO_IRQ_EDGE_FALL << (4 * (PIN_RST_IN % 8));
    if (!(io_bank0_hw->intr[PIN_RST_IN / 8] & m)) return 0;
    io_bank0_hw->intr[PIN_RST_IN / 8] = m;           /* write 1 to clear */
    return 1;
}
#define BUS_RESET_EDGE() bus_reset_edge()
struct bus_t_fwd;
static void hold_bus(void);
static long st_prep_(const char *p, const char *f) { hold_bus(); return request(RQ_PREP, p, f, 0, 0, 0, 0); }
static int st_newdisk_(const char *p, const uint8_t *n, int nl, uint8_t i1, uint8_t i2) { hold_bus(); return (int)request(RQ_NEW, p, (const char *)n, nl | (i1 << 8) | (i2 << 16), 0, 0, 0); }
static int st_find_(const char *p, const char *n) { hold_bus(); return (int)request(RQ_FIND, p, n, 0, 0, 0, 0); }
static int st_list_(const char *p, int i, fsent_t *e) { hold_bus(); return (int)request(RQ_LIST, p, 0, i, e, 0, 0); }
static long st_read_(const char *p, uint8_t *b, long m) { hold_bus(); return request(RQ_READ, p, 0, 0, 0, b, m); }
static int st_write_(const char *p, const uint8_t *b, long n) { hold_bus(); return (int)request(RQ_WRITE, p, 0, 0, 0, (uint8_t *)b, n); }
static int st_load_(const char *p) { hold_bus(); return (int)request(RQ_LOAD, p, 0, 0, 0, 0, 0); }
static int st_virtual_(const char *d, const char *f) { hold_bus(); return (int)request(RQ_VIRTUAL, d, f, 0, 0, 0, 0); }
static void st_flush_(void) { request(RQ_FLUSH, 0, 0, 0, 0, 0, 0); }
/* browse mode's talker giving up on a byte (no acknowledge in 1 ms): how often, and the last one's wait, lines and byte
 * count. 2026-11-08: LOADs hung at byte 34 and 299 of the bootstrap with core 1 looking every 1-4 us (measured): the
 * C64 missed a bit; IECD_HOLD_US 70 -> 80 cured it (5,723 bytes, none lost). Serial 's' */
static uint32_t tx_err_n, tx_err_wait, tx_err_bytes; static uint8_t tx_err_lines;
#define BUS_TX_ERR() do { tx_err_n++; tx_err_wait = B.dev.now - B.dev.tt; tx_err_lines = B.dev.in; tx_err_bytes = B.dev.bytes_out; } while (0)
#include "bus.h"
/* before core 1 waits on the card: browse mode's pulls (the frame acknowledge,
 * DATA low = busy) on the pins now, not after iec_poll returns */
static void hold_bus(void) { if (!B.online) pins_out(B.dev.out); }

/* The device number lives in the Pico's flash, not on the card: it belongs to the drive, like a 1541's
 * jumpers. The last sector: "PD64", the number, its inverse. Written only at start-up, before core 1 runs, so
 * nothing is timed while the flash is busy. */
#define DEV_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
static int device_load(void)
{
    const uint8_t *p = (const uint8_t *)(XIP_BASE + DEV_OFFSET);
    if (memcmp(p, "PD64", 4) || p[4] < 8 || p[4] > 11 || (uint8_t)~p[4] != p[5]) return 8;
    return p[4];
}
static void device_save(int dev)
{
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof page);
    memcpy(page, "PD64", 4); page[4] = (uint8_t)dev; page[5] = (uint8_t)~dev;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(DEV_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(DEV_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
}
static uint8_t dos_rom[16384];
static volatile int core1_up;
#define TRACK_SETTLE_MS 8
#include "stackmark.h"
extern uint32_t __StackBottom[], __StackTop[], __StackOneBottom[], __StackOneTop[];
/* before core 1 starts: all of its stack, and core 0's below where it is now */
static void __attribute__((noinline)) stacks_paint(void)
{
    uint32_t *sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    sm_paint(__StackBottom, sp - 16);
    sm_paint(__StackOneBottom, __StackOneTop);
}

/* A speed benchmark (2026-11-07): serial 'b' runs the booted DOS, 'k' a loader-like loop (BIT $1800 / STA $1800 / JMP,
 * 11 cycles), each for 1,000,000 drive cycles off the bus (no waiting for real time), with the last disk in. Core 1
 * does it while offline; the result is sysclk per drive cycle x 100 (real time is 150). */
static volatile int bench_req; static volatile uint32_t bench_res;
/* bus off (serial X, 2026-11-07): core 1 lets go of every line, the ATN acknowledge too, and answers nothing, so
 * another drive can have the bus while PicoD64 stays plugged in (and its sniffer watches). Only with no disk in */
static volatile int bus_off;
static __attribute__((noinline)) void bench_run(int kind)   /* (in flash: only the drive code it runs needs SRAM) */
{
    drive_load(&bus_booted);
    drive_insert((kind == 3 || kind == 4 || kind == 6) ? 0 : &bus_disk);       /* 3, 4, 6: no disk */
    if (kind == 2 || kind == 3) {
        static const uint8_t prg[] = { 0x78, 0x2C, 0x00, 0x18, 0x8D, 0x00, 0x18, 0x4C, 0x01, 0x03 };
        for (unsigned i = 0; i < sizeof prg; i++) D.ram[0x300 + i] = prg[i];
        CPU.pc = 0x0300; CPU.a = 0;
    }
    if (kind == 4) {                                 /* NOPs: the CPU alone */
        D.ram[0x300] = 0x78;
        for (unsigned i = 1; i < 0x40; i++) D.ram[0x300 + i] = 0xEA;
        D.ram[0x340] = 0x4C; D.ram[0x341] = 0x01; D.ram[0x342] = 0x03;
        CPU.pc = 0x0300;
    }
    bus_replaying = 1;
    uint32_t c0 = D.cycles, t0 = DWT_CYCCNT;
    if (kind == 5 || kind == 6) {                    /* the DOS reading track 18 sector 0 again and again (job $80, buffer 0) */
        while (D.cycles - c0 < 1000000u) {
            if (D.ram[0x00] < 0x80) { D.ram[0x06] = 18; D.ram[0x07] = 0; D.ram[0x00] = 0x80; }
            drive_run(D.cycles + 10000);
        }
    } else
    drive_run(c0 + 1000000u);
    uint32_t dt = DWT_CYCCNT - t0, dc = D.cycles - c0;
    bus_replaying = 0;
    bench_res = (uint32_t)((uint64_t)dt * 100u / dc);
}

static void __not_in_flash_func(core1_main)(void)
{
    uint32_t irq = save_and_disable_interrupts();
    DEMCR |= 1u << 24; DWT_CYCCNT = 0; DWT_CTRL |= 1;
    deadline = DWT_CYCCNT + 1000; last_c = 0;
    bus_init(dos_rom);
    core1_up = 1;
    for (;;) {
        if (bench_req && !B.online) { bench_run(bench_req); bench_req = 0; }
        if (bus_off && !B.online) { clk_set(0); gate_set(0); while (bus_off) { } continue; }
        bus_poll();
    }
    restore_interrupts(irq);
}

/* --- core 0: the card --------------------------------------------------------- */
#include "storage.h"
#include "svc.h"
#include "ui.h"
uint8_t sd_spi_xfer(uint8_t out) { uint8_t in; spi_write_read_blocking(spi1, &out, &in, 1); return in; }
void sd_spi_cs(int low) { gpio_put(PIN_SD_CS, !low); }
void sd_spi_speed(int f) { spi_set_baudrate(spi1, f ? 25000000 : 400000); }
uint32_t sd_ms(void) { return to_ms_since_boot(get_absolute_time()); }
int sd_present(void) { return !gpio_get(PIN_SD_DET); }   /* [A] the switch closes to GND with a card in */

static unsigned card_tries, card_removals;   /* (serial 's': the card going and coming, 2026-11-09) */
static long rq_read_n, rq_read_last; static char rq_read_path[44];   /* (serial 's': the last card read, 2026-11-07) */
static void serve(void)
{
    int op = rq.op;
    if (op == RQ_NONE) return;
    long r = 0;
    switch (op) {
    case RQ_LIST: r = st_list(rq.path, rq.idx, rq.e); break;
    case RQ_PREP: r = st_prep(rq.path, rq.first); break;
    case RQ_FIND: r = st_find(rq.path, rq.first); break;
    case RQ_NEW: r = svc_newdisk(rq.path, (const uint8_t *)rq.first, rq.idx & 255, (uint8_t)(rq.idx >> 8), (uint8_t)(rq.idx >> 16)); break;
    case RQ_READ: r = st_read(rq.path, rq.buf, rq.max); rq_read_n++; rq_read_last = r;
                  strncpy(rq_read_path, rq.path, sizeof rq_read_path - 1); break;
    case RQ_LOAD: r = svc_load(rq.path, bus_gcr, &bus_disk, bus_frames); break;
    case RQ_VIRTUAL: r = svc_virtual(rq.path, rq.first, bus_gcr, &bus_disk, bus_frames, bus_gcr + D64_BYTES, (long)(D64_GCR_BYTES - D64_BYTES)); break;
    case RQ_FLUSH: r = svc_flush(B.mounted, &bus_disk, bus_dirty); break;
    case RQ_WRITE: r = st_write(rq.path, rq.buf, rq.max); break;
    }
    rq.result = r; rq.op = RQ_NONE; __dmb(); rq.done = 1;
}

/* the sniffer: the drive's log of line changes, through a passive device engine */
static uint32_t log_r;
static FF_C1 __attribute__((noinline)) void sniff(void)
{
    if (!B.online) { log_r = log_w; return; }
    /* time must never go back for the passive device, or its byte timeout fires and a byte is lost (first board,
     * 2026-11-07: CD// seen as "CD/", "CD" or not at all, half the time). So `now` is read BEFORE the log's end:
     * whatever core 1 logs after this has a later cycle, and "time passes" below is fed the later of now
     * and the last entry's time */
    uint32_t now = D.cycles;
    __dmb();
    uint32_t w = log_w, last = now;
    if (w - log_r > LOG_N) log_r = w - LOG_N;        /* overrun: lose the oldest [A: 2048 changes] */
    /* an entry holds 29 bits of its cycle (above the 3 lines): its full cycle from now's, by the signed 29-bit
     * difference (entries logged after `now` was read are a little newer than it) */
    while (log_r != w) {
        uint32_t v = log_buf[log_r++ & (LOG_N - 1)];
        int32_t d = (int32_t)(((now & 0x1FFFFFFFu) - (v >> 3)) << 3) >> 3;
        last = now - (uint32_t)d;
        bus_sniff_feed(last, (uint8_t)(v & 7));
    }
    bus_sniff_feed((int32_t)(last - now) > 0 ? last : now, log_last);   /* time passes: its timeouts run */
}

/* --- core 0: the OLED over I2C0 ---------------------------------------------------- */
static int oled_ok;
static void oled_cmds(const uint8_t *c, int n)
{
    uint8_t b[40]; b[0] = 0x00;                       /* control byte: Co 0, D/C 0: commands follow [D p.13] */
    memcpy(b + 1, c, (size_t)n);
    oled_ok = i2c_write_timeout_us(i2c0, OLED_ADDR, b, (size_t)n + 1, false, 5000) == n + 1;
}
static void oled_show(const ui_t *u)
{
    static uint8_t data[133];
    uint8_t cmd[3];
    for (int p = 0; p < 8 && oled_ok; p++) {
        oled_page(u, p, cmd, data + 1);
        oled_cmds(cmd, 3);
        data[0] = 0x40;                               /* control byte: D/C 1: display data follows */
        i2c_write_timeout_us(i2c0, OLED_ADDR, data, 133, false, 20000);
    }
}

/* --- core 0: the UI's actions ---------------------------------------------------- */
static void ui_mount(const char *path)
{
    if (B.online) {
        if (!strcmp(path, B.mounted) || B.sw != SW_NONE) return;
        strncpy(B.swap_path, path, BR_PATH - 1); B.swap_req = 1;
    } else { strncpy(B.ui_mount_path, path, BR_PATH - 1); B.ui_mount_prg = 0; __dmb(); B.ui_mount_req = 1; }
}
static void ui_eject(void) { if (B.online) B.pending_unmount = 1; }
/* a program: out with the disk in (if any), then browse mode builds its folder's PRGs as a disk, it first */
static void ui_mount_prg(const char *path)
{
    if (B.sw != SW_NONE) return;
    strncpy(B.ui_mount_path, path, BR_PATH - 1); B.ui_mount_prg = 1; __dmb(); B.ui_mount_req = 1;
    if (B.online) B.pending_unmount = 1;
}
static void device_choose(ui_t *ui, enc_t *knob)
{
    int dev = bus_device, redraw = 1;
    ui_draw_device(ui, dev); oled_show(ui);           /* at once: the knob is still down, show it worked */
    while (!gpio_get(PIN_ENC_SW)) sleep_ms(10);       /* the press that got us here is not the save */
    sleep_ms(50);
    for (;;) {
        uint32_t ms = sd_ms();
        int ev = enc_update(knob, gpio_get(PIN_ENC_B), gpio_get(PIN_ENC_A), gpio_get(PIN_ENC_SW), ms);
        if (ev == EV_PRESS) break;
        int next = ui_device_step(dev, ev);
        if (next != dev) { dev = next; redraw = 1; }
        if (redraw) { ui_draw_device(ui, dev); oled_show(ui); redraw = 0; }
    }
    if (dev != bus_device) device_save(dev);
    bus_device = (uint8_t)dev;
    printf("PicoD64: device %d\n", dev);
}
static int ui_list(const char *p, int i, fsent_t *e) { return st_list(p, i, e); }
static int ui_count(const char *p) { return st_count(p); }
static int ui_newdisk(const char *p, const uint8_t *n, int nl, uint8_t i1, uint8_t i2) { return svc_newdisk(p, n, nl, i1, i2); }

/* --- the drive's sounds (fw/sound.h): core 0 only, in flash, reading D and B. Core 1 never
 * sees this interrupt. The PWM's carrier is above hearing; a sample every second period. */
#define SND_WRAP (32 * SYS_MHZ - 1)                   /* 31.25 kHz */
static snd_t snd;
static uint8_t snd_on = 1, snd_odd;
static volatile uint16_t snd_test;                   /* serial T: samples of a full-swing 3.9 kHz test tone */
static void snd_irq(void)
{
    pwm_clear_irq(pwm_gpio_to_slice_num(PIN_PIEZO));
    if ((snd_odd ^= 1)) return;
    if (snd_test) { snd_test--; pwm_set_gpio_level(PIN_PIEZO, (snd_test & 2) ? SND_WRAP + 1u : 0); return; }
    int online = *(volatile int *)&B.online;
    uint8_t d = snd_tick(&snd, *(volatile uint8_t *)&D.phase, *(volatile uint8_t *)&D.ht, online && (via_pb(&D.via2) & 0x04), online,
                         B.dev.listening || B.dev.talking, snd_on);
    pwm_set_gpio_level(PIN_PIEZO, (uint16_t)((d * (SND_WRAP + 1u)) >> 8));
}
static void snd_start(void)
{
    snd_on = (uint8_t)svc_sound_get();
    uint slice = pwm_gpio_to_slice_num(PIN_PIEZO);
    pwm_set_wrap(slice, SND_WRAP);
    pwm_set_gpio_level(PIN_PIEZO, 0);
    gpio_set_function(PIN_PIEZO, GPIO_FUNC_PWM);
    gpio_set_drive_strength(PIN_PIEZO, GPIO_DRIVE_STRENGTH_12MA); gpio_set_slew_rate(PIN_PIEZO, GPIO_SLEW_RATE_FAST);   /* R14 sets the edge current */
    pwm_clear_irq(slice); pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP_0, snd_irq);
    irq_set_enabled(PWM_IRQ_WRAP_0, true);
    pwm_set_enabled(slice, true);
}
/* --- a logic analyser on core 0 (first board, 2026-11-06: a demo's loader fails on the board, not on the twin).
 * Serial 'L' arms it; it starts when the drive runs code in la_lo..la_hi (default: The Hat's send loop), then records
 * every change of ATN, CLK, DATA as seen (GP12-14, 1 = released) and CLK/DATA out (GP16-17, 1 = pulling low), with
 * core 0's cycle counter, interrupts off, until LA_N changes or 0.2 s. 'l' prints it: ns since the first, the pins. */
/* the drive's story, polled by core 0: each change of the loader's command byte (drive RAM cmd_at, The Hat's $70) and
 * of the half-track, with the time in ms. Serial 'c' prints it, 'C' clears it (2026-11-07) */
#define EV_N 256
static uint32_t ev_ms[EV_N]; static uint8_t ev_kind[EV_N], ev_val[EV_N]; static int ev_n; static uint16_t cmd_at = 0x70;
static void ev_poll(uint32_t ms)
{
    static uint8_t lc = 0xFF, lh = 0xFF;
    if (!B.online) { lc = lh = 0xFF; return; }
    uint8_t c = D.ram[cmd_at], h = D.ht;
    if (c != lc && ev_n < EV_N) { ev_ms[ev_n] = ms; ev_kind[ev_n] = 'c'; ev_val[ev_n++] = c; }
    if (h != lh && ev_n < EV_N) { ev_ms[ev_n] = ms; ev_kind[ev_n] = 't'; ev_val[ev_n++] = h; }
    lc = c; lh = h;
}
static int quiet;                                    /* serial Q: core 0 idle, to see if core 1 is disturbed */
#define LA_N 256                             /* (was 1024: RAM for the event log, 2026-11-07) */
/* la_t: the sniffer's ring too, so 1 KB-aligned for its DMA: in the scratch bank under core 0's stack (aligned by the bank,
 * no padding; CMakeLists.txt) */
static uint32_t la_t[LA_N] __attribute__((section(".scratch_y.la_t"), aligned(LA_N * 4))); static uint8_t la_p[LA_N];
_Static_assert(LA_N * 4 == 1024, "la_t: the sniffer's ring is 1 KB (channel_config_set_ring 10)"); static int la_n, la_armed, la_atn; static uint16_t la_lo = 0x0335, la_hi = 0x0350;
/* The block log (serial V, 2026-11-12): while the drive runs code in la_lo..la_hi (serial W: a loader's send loop),
 * a hash of its block buffer ($0600/$0700, as Krill's loader keeps it: GCR quintets) and the half-track, whenever it
 * changes, into the analyser's buffers (never both at once); serial v prints them. host: which sector each was */
static int bl_on; static uint32_t bl_n, bl_last;
/* The answer watch (serial A, 2026-11-12): core 0 polls the pins for as long as it runs and, for every ATN edge, takes
 * the drive's answer (its last output change more than 1 us after the edge: the gate's own answer is instant) against
 * the C64's read of it. Every fast loader seen reads the bus 4 PAL cycles (4.06 us) before its next ATN write, so the
 * read is the next edge less that. margin = read - answer: below 0 the C64 read the bits before the drive set them.
 * Statistics only (no storage), so it can watch a whole demo; card requests are still served between polls. A key
 * stops it. */
static void lat_watch(void)
{
    const uint32_t ATN = 1u << PIN_ATN_IN, OUTS = 3u << PIN_CLK_OUT, US = SYS_MHZ, RD = 4060u * SYS_MHZ / 1000u;
    DEMCR |= 1u << 24; DWT_CTRL |= 1;
    uint32_t hist[16] = {0}, n = 0, skipped = 0, worst_n = 0; int32_t mmin = INT32_MAX;
    struct { int32_t m, ans; uint32_t ms; uint16_t pc; uint8_t ht, rise; } w[8];
    uint32_t v = sio_hw->gpio_in, t_edge = 0, t_ans = 0, ms0 = sd_ms(); int have_edge = 0, rise = 0;
    printf("answer watch: a key stops it\n");
    for (;;) {
        uint32_t t_slice = DWT_CYCCNT;
        while (DWT_CYCCNT - t_slice < 2000u * US) {           /* 2 ms of polling, then the card and the keys */
            uint32_t nv = sio_hw->gpio_in, t = DWT_CYCCNT;
            if (nv == v) continue;
            if ((nv ^ v) & ATN) {
                if (have_edge && t_ans && t - t_edge < 40u * US) {   /* the edge before, answered, and the C64 kept going */
                    int32_t m = (int32_t)((t - RD) - t_ans); n++;
                    int b = m < -2 * (int32_t)US ? 0 : m >= 13 * (int32_t)US ? 15 : (m + 2 * (int32_t)US) / (int32_t)US;
                    hist[b]++;
                    if (m < mmin) mmin = m;
                    int k = worst_n < 8 ? (int)worst_n++ : -1;
                    if (k < 0) { k = 0; for (int i = 1; i < 8; i++) if (w[i].m > w[k].m) k = i; if (w[k].m <= m) k = -1; }
                    if (k >= 0) { w[k].m = m; w[k].ans = (int32_t)(t_ans - t_edge); w[k].ms = sd_ms() - ms0; w[k].pc = CPU.pc; w[k].ht = D.ht; w[k].rise = (uint8_t)rise; }
                }
                have_edge = 1; t_edge = t; t_ans = 0; rise = (nv & ATN) != 0;   /* pin high: ATN released */
            }
            if (((nv ^ v) & OUTS) && have_edge && t - t_edge > US) t_ans = t;
            v = nv;
        }
        have_edge = 0; skipped++;                              /* (an edge during the pause isn't measured) */
        serve();
        if (getchar_timeout_us(0) >= 0) break;
    }
    printf("answer watch: %lu answers in %lu s; margin before the C64's read (read = next ATN edge - 4.06 us): min %ld.%02ld us\n",
           (unsigned long)n, (unsigned long)((sd_ms() - ms0) / 1000), (long)(mmin / (int32_t)US), (long)((mmin < 0 ? -mmin : mmin) % US * 100 / US));
    printf("  margin, us:"); for (int i = 0; i < 16; i++) printf(" %s%d:%lu", i == 0 ? "<" : i == 15 ? ">=" : "", i == 15 ? 13 : i - 2, (unsigned long)hist[i]); printf("\n");
    for (uint32_t i = 0; i < worst_n; i++)
        printf("  worst: margin %ld ns, answer %ld ns after the ATN %s, at %lu ms, drive PC $%04X, half-track %u\n", (long)(w[i].m * 1000 / (int32_t)US),
               (long)(w[i].ans * 1000 / (int32_t)US), w[i].rise ? "release" : "pull", (unsigned long)w[i].ms, w[i].pc, w[i].ht);
}
static void la_capture(void)
{
    const uint32_t mask = (7u << PIN_ATN_IN) | (3u << PIN_CLK_OUT);
    DEMCR |= 1u << 24; DWT_CTRL |= 1;               /* core 0's own counter */
    uint32_t irq = save_and_disable_interrupts();
    uint32_t last = sio_hw->gpio_in & mask, start = DWT_CYCCNT; int n = 0;
    la_t[n] = start; la_p[n++] = (uint8_t)(last >> PIN_ATN_IN);
    while (n < LA_N && DWT_CYCCNT - start < 30000000u) {
        uint32_t v = sio_hw->gpio_in & mask;
        if (v != last) { la_t[n] = DWT_CYCCNT; la_p[n++] = (uint8_t)(v >> PIN_ATN_IN); last = v; }
    }
    restore_interrupts(irq);
    la_n = n; la_armed = 0;
    printf("logic analyser: %d changes captured ('l' prints them)\n", n);
}
static void la_print(void)
{
    printf("ns pins: ATN CLK DATA (as seen, 1 = released) | CLK DATA out (1 = pulling)\n");
    for (int i = 0; i < la_n; i++) {
        uint8_t p = la_p[i];
        printf("%lu %u%u%u %u%u\n", (unsigned long)((la_t[i] - la_t[0]) * 1000u / SYS_MHZ), p & 1, (p >> 1) & 1, (p >> 2) & 1, (p >> 4) & 1, (p >> 5) & 1);
    }
}

/* The bus sniffer (fw/iec_sniff.pio on PIO 1, 2026-11-07): each ATN edge's word, by DMA into a ring, printed by
 * core 0 as one character, '0' + bits 31:26 (the lines just before the edge, then ~13 us after: ATN CLK DATA, 1 =
 * released). A line is "S<ms> <gap> <chars>" (gap: 0.4 us ticks since the previous edge, 0 = it continues the line
 * before) after a gap of 2 ms or 200 edges; "SOVF n"
 * says n edges were lost. Serial S starts and stops it; while it runs the OLED isn't drawn (an I2C frame is ~27 ms) */
#define CAP_N 256
#define cap_buf la_t                                  /* the logic analyser's buffer (never both at once): no RAM */
static int cap_dma = -1, cap_on; static uint32_t cap_r, cap_x; static uint cap_off;
#define CAP_COUNT 0x0FFFFFFFu
static void cap_start(void)
{
    if (cap_dma < 0) {
        uint off = cap_off = pio_add_program(pio1, &iec_sniff_program);
        pio_sm_config c = iec_sniff_program_get_default_config(off);
        sm_config_set_in_pins(&c, PIN_ATN_IN);
        sm_config_set_jmp_pin(&c, PIN_ATN_IN);
        sm_config_set_in_shift(&c, false, false, 32);
        sm_config_set_clkdiv(&c, (float)SYS_MHZ / 10.0f);   /* 10 MHz */
        pio_sm_init(pio1, 0, off + iec_sniff_offset_wait_fall, &c);
        cap_dma = dma_claim_unused_channel(true);
    }
    pio_sm_set_enabled(pio1, 0, false);
    pio_sm_clear_fifos(pio1, 0);
    dma_channel_abort((uint)cap_dma);
    dma_channel_config d = dma_channel_get_default_config((uint)cap_dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_ring(&d, true, 10);           /* 2^10 bytes: CAP_N words */
    channel_config_set_dreq(&d, pio_get_dreq(pio1, 0, false));
    dma_channel_configure((uint)cap_dma, &d, cap_buf, &pio1->rxf[0], CAP_COUNT, true);
    cap_r = 0; cap_x = 0;
    pio_sm_exec(pio1, 0, pio_encode_set(pio_x, 0));
    pio_sm_exec(pio1, 0, pio_encode_jmp(cap_off + iec_sniff_offset_wait_fall));
    pio_sm_set_enabled(pio1, 0, true);
    cap_on = 1;
}
static void cap_stop(void) { pio_sm_set_enabled(pio1, 0, false); dma_channel_abort((uint)cap_dma); cap_on = 0; }
static void cap_drain(void)
{
    static char line[256]; static int n; static uint32_t idle_ms, l_ms, l_gap;
    uint32_t w = CAP_COUNT - dma_hw->ch[cap_dma].transfer_count, w0 = cap_r;
    if (w - cap_r > CAP_N - 16) { if (n) printf("S%lu %lu %.*s\n", (unsigned long)l_ms, (unsigned long)l_gap, n, line);
                                  n = 0; printf("SOVF %lu\n", (unsigned long)(w - cap_r)); cap_r = w; }
    while (cap_r != w) {
        uint32_t v = cap_buf[cap_r++ & (CAP_N - 1)], x = v & 0x3FFFFFFu, gap = (cap_x - x) & 0x3FFFFFFu;
        cap_x = x;
        if (n && (gap > 5000 || n >= 200)) { printf("S%lu %lu %.*s\n", (unsigned long)l_ms, (unsigned long)l_gap, n, line); n = 0; }
        if (!n) { l_ms = sd_ms(); l_gap = gap > 5000 ? gap : 0; }
        line[n++] = (char)('0' + (v >> 26));
    }
    if (cap_r != w0) idle_ms = sd_ms();
    else if (n && sd_ms() - idle_ms > 100) { printf("S%lu %lu %.*s\n", (unsigned long)l_ms, (unsigned long)l_gap, n, line); n = 0; }
}
/* ⚠ core 0: the card directly. brx_cfg_read()/write() go through core 1's request(), which core 0 itself
 * serves, so calling them here would wait for ever. */
static int ui_sound(int toggle)
{
    if (toggle) { snd_on = !snd_on; svc_sound_set(snd_on); B.br.cfg_sound = snd_on; __dmb(); }
    return snd_on;
}

int main(void)
{
    set_sys_clock_khz(SYS_MHZ * 1000, true);
    stdio_init_all();
    /* IEC: inputs with no pulls (the dividers set the level; AUDIT B1), outputs low (released) */
    for (int p = PIN_ATN_IN; p <= PIN_RST_IN; p++) { gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_disable_pulls(p); gpio_set_input_hysteresis_enabled(p, true); }
    gpio_init(PIN_CLK_OUT); gpio_put(PIN_CLK_OUT, 0); gpio_set_dir(PIN_CLK_OUT, GPIO_OUT);
    {   /* the ATN gate: DATA_OUT driven by PIO, following ATN_IN (fw/atn_gate.pio) */
        uint off = pio_add_program(GATE_PIO, &atn_gate_program);
        pio_sm_config c = atn_gate_program_get_default_config(off);
        sm_config_set_out_pins(&c, PIN_DATA_OUT, 1);
        sm_config_set_jmp_pin(&c, PIN_ATN_IN);
        sm_config_set_out_shift(&c, true, false, 32);
        pio_gpio_init(GATE_PIO, PIN_DATA_OUT);
        pio_sm_set_pins_with_mask(GATE_PIO, GATE_SM, 0, 1u << PIN_DATA_OUT);   /* released */
        pio_sm_set_consecutive_pindirs(GATE_PIO, GATE_SM, PIN_DATA_OUT, 1, true);
        pio_sm_init(GATE_PIO, GATE_SM, off, &c);
        pio_sm_set_enabled(GATE_PIO, GATE_SM, true);
        gate_set(0);
    }
    for (int p = PIN_ENC_A; p <= PIN_ENC_SW; p++) { gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); }
    gpio_init(PIN_LED); gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_init(PIN_PIEZO); gpio_set_dir(PIN_PIEZO, GPIO_OUT); gpio_put(PIN_PIEZO, 0);
    gpio_init(PIN_SD_DET); gpio_set_dir(PIN_SD_DET, GPIO_IN); gpio_pull_up(PIN_SD_DET);
    gpio_init(PIN_SD_CS); gpio_put(PIN_SD_CS, 1); gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    spi_init(spi1, 400000);
    gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI); gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI); gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_SD_MISO);
    i2c_init(i2c0, 400000);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C); gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA); gpio_pull_up(PIN_SCL);

    static ui_t ui; static ui_io_t io; static enc_t knob;
    bus_device = (uint8_t)device_load();             /* (core 1 not started yet: bus_init reads it) */
    io.device = bus_device;
    io.sound = ui_sound;
    io.disk = B.mounted; io.prg = B.prg; io.mount_prg = ui_mount_prg; io.browse_dir = B.br.cwd; io.mount = ui_mount; io.eject = ui_eject; io.list = ui_list; io.count = ui_count; io.newdisk = ui_newdisk;
    ui_init(&ui, &io);
    const uint8_t *init; int n = oled_init_seq(&init);
    oled_ok = 1; oled_cmds(init, n);
    if (!gpio_get(PIN_ENC_SW)) {                     /* the knob held down at power-on: the device number */
        device_choose(&ui, &knob);
        io.device = bus_device;
    }

    /* the card and the ROM: wait for both */
    for (;;) {
        io.card = sd_present() && st_init();
        if (io.card && st_read("/dos1541.bin", dos_rom, sizeof dos_rom) == (long)sizeof dos_rom) break;
        strcpy(ui.msg, io.card ? "Need /dos1541.bin" : "Insert SD card"); ui.msg_until = sd_ms() + 1000000;
        ui_draw(&ui, sd_ms()); oled_show(&ui);
        printf("PicoD64: %s\n", ui.msg);
        sleep_ms(1000);
    }
    ui.msg[0] = 0;
    static uint8_t chargen[4096];                    /* optional: another font for the OLED, from the card */
    long font_len = st_read("/chargen.bin", chargen, sizeof chargen);
    if (font_len == (long)sizeof chargen) ui_font(chargen);
    else if (st_read("/font5x7.bin", chargen, sizeof chargen) == UI_FONT57_SIZE) ui_font57(chargen);   /* (the same buffer: no more RAM) */
    svc_set_builtin(browser_prg, (long)sizeof browser_prg);
    stacks_paint();
    late_pcp = &CPU.pc;
    multicore_launch_core1(core1_main);
    while (!core1_up) serve();                       /* (the boot needs no card) */
    printf("PicoD64: ready\n");

    snd_start();
    uint32_t last_draw = 0, motor_off_since = 0;
    int card_prev = 1; uint32_t card_at = 0;
    for (;;) {
        serve();
        if (cap_on) cap_drain();
        if (la_armed && !cap_on && B.online && CPU.pc >= la_lo && CPU.pc <= la_hi && (!la_atn || (pins_in() & IEC_ATN)))
            la_capture();                            /* (la_atn, serial Y: only with ATN asserted, 2026-11-10) */
        if (bl_on && !cap_on && !la_armed && B.online && CPU.pc >= la_lo && CPU.pc <= la_hi) {   /* serial V: the block log */
            uint32_t hv = 2166136261u;
            if (bl_on == 2)                          /* (serial U) FNV-1a over a decoded sector's bytes 2-255 at $0702: host/blockcheck.py */
                for (int i = 2; i < 256; i++) hv = (hv ^ D.ram[0x700 + i]) * 16777619u;
            else {                                   /* FNV-1a over the block's GCR quintets, bytes 2-255 (0-1: the loader's own) */
                for (int i = 2; i < 256; i++) { hv = (hv ^ D.ram[0x600 + i]) * 16777619u; hv = (hv ^ D.ram[0x700 + i]) * 16777619u; }
                hv = (hv & 0xFFF00000u) | (uint32_t)(D.ram[0x700] & 31) << 15 | (uint32_t)(D.ram[0x600] & 31) << 10 |   /* + the quintets of */
                     (uint32_t)(D.ram[0x701] & 31) << 5 | (D.ram[0x601] & 31);                    /* bytes 0-1: the loader's header */
            }
            if (hv != bl_last) { bl_last = hv; la_t[bl_n & (LA_N - 1)] = hv; la_p[bl_n & (LA_N - 1)] = D.ht; bl_n++; }
        }
        if (quiet) {                                 /* serial Q: core 0 does nothing but core 1's requests and Q again */
            int c = getchar_timeout_us(0);
            if (c == 'Q') { quiet = 0; printf("quiet off\n"); }
            continue;
        }
        sniff();
        uint32_t ms = sd_ms();
        ev_poll(ms);
        int ev = enc_update(&knob, gpio_get(PIN_ENC_B), gpio_get(PIN_ENC_A), gpio_get(PIN_ENC_SW), ms);
        io.online = B.online;
        if (B.next_req) { B.next_req = 0; ui_next_disk(&ui, ms); }   /* XN on the bus: as the knob's long press */
        ui_event(&ui, ev, ms);
        /* a disk of over 35 tracks: the track under the head (fw/storage.c) */
        if (B.online && !B.virtual_disk && io.card && B.sw == SW_NONE && st_tracks() > D64_TRACKS) {
            static uint8_t on_ht; static uint32_t on_since;   /* not for a track the head is only passing over */
            if (D.ht != on_ht) { on_ht = D.ht; on_since = ms; }
            else if (ms - on_since >= TRACK_SETTLE_MS && svc_track_poll(B.mounted, &bus_disk, bus_dirty, &D.ht)) { __dmb(); bus_track_ready = 1; }
        }
        /* the card going or coming */
        int present = sd_present();
        if (!present && card_prev) { st_unmount(); io.card = 0; card_removals++; card_tries = 0; }
        if (present && !card_prev) card_at = ms + 200;             /* let it settle and power up */
        if (present && !io.card && (int32_t)(ms - card_at) >= 0) {   /* and again each second until it mounts: */
            io.card = st_init(); card_tries++; card_at = ms + 1000;   /* one try alone stayed "No SD card" (2026-11-09) */
        }
        card_prev = present;
        /* written tracks back to the card when the motor has been off 1 s (VIA2 PB2 [S7]) */
        int motor = B.online && (via_pb(&D.via2) & 0x04);
        if (motor) motor_off_since = ms;
        if (B.online && !B.virtual_disk && io.card && ms - motor_off_since >= 1000 && B.sw == SW_NONE) {
            int any = 0; for (int t = 0; t < 42; t++) any |= bus_dirty[t];
            if (any) svc_flush(B.mounted, &bus_disk, bus_dirty);
        }
        /* a swap: core 1 has the old disk out */
        if (B.sw == SW_EMPTY && B.sw_out_ready && !B.sw_in_ready) {
            if (io.card) svc_flush(B.mounted, &bus_disk, bus_dirty);
            B.sw_ok = io.card && svc_load(B.swap_path, bus_gcr, &bus_disk, bus_frames);
            __dmb(); B.sw_in_ready = 1;
        }
        /* the LED: the drive's motor, or browse mode talking/listening */
        gpio_put(PIN_LED, motor || (!B.online && (B.dev.listening || B.dev.talking)));
        if (!cap_on && (ev != EV_NONE || ms - last_draw >= 100)) { io.activity = motor; ui_draw(&ui, ms); oled_show(&ui); last_draw = ms; }
        int ch = getchar_timeout_us(0);
        if (ch == 'B') reset_usb_boot(0, 0);
        if (ch == 'z') { memset(prof_sum, 0, sizeof prof_sum); memset(ev_cnt, 0, sizeof ev_cnt); ev_c0 = D.cycles; memset(prof_cnt, 0, sizeof prof_cnt); memset(prof_max, 0, sizeof prof_max); wr_lat_max = INT32_MIN; wr_lat_min = INT32_MAX; late_syncs = late_max = late_1us = late_10us = 0; pin_late_max = 0; pin_late_n = pin_writes = 0; pin_late_pc = 0; pin_late_gap = 0; late_pc = 0; printf("late counters cleared\n"); }
        if (ch == 'W') {                             /* Wllllhhhh: the analyser's trigger, drive code $llll-$hhhh (2026-11-09) */
            char h[9]; int n = 0, k; uint32_t t0 = sd_ms();
            while (n < 8 && sd_ms() - t0 < 2000) { k = getchar_timeout_us(10000); if (k >= 0) h[n++] = (char)k; }
            h[n] = 0;
            if (n == 8) {                            /* (not strtoul: it cost 640 bytes of RAM) */
                uint32_t v = 0; for (int i = 0; i < 8; i++) v = v << 4 | (uint32_t)((h[i] | 32) <= '9' ? h[i] - '0' : (h[i] | 32) - 'a' + 10);
                la_lo = (uint16_t)(v >> 16); la_hi = (uint16_t)v;
            }
            printf("logic analyser trigger: drive code $%04X-$%04X\n", la_lo, la_hi);
        }
        if (ch == 'J') {                             /* Jhhhh: after hhhh ms, record the bus now, whoever drives it (another drive: bus off).
                                                        (Not K: that is the bench's, and ran after it, waiting for ever on a bus off) */
            char h[4]; int n = 0, k; uint32_t t0 = sd_ms(), ms = 0;
            while (n < 4 && sd_ms() - t0 < 2000) { k = getchar_timeout_us(10000); if (k >= 0) h[n++] = (char)k; }
            for (int i = 0; i < n; i++) ms = ms << 4 | (uint32_t)((h[i] | 32) <= '9' ? h[i] - '0' : (h[i] | 32) - 'a' + 10);
            printf("logic analyser: recording in %lu ms\n", (unsigned long)ms);
            sleep_ms(ms);
            uint8_t l0 = pins_in(); uint32_t t1 = sd_ms();   /* then the first change, within 60 s, starts it */
            while (pins_in() == l0 && sd_ms() - t1 < 60000) { }
            la_capture();
        }
        if (ch == 'L') { la_armed = 1; la_n = 0; printf("logic analyser armed: drive code $%04X-$%04X\n", la_lo, la_hi); }
        if (ch == 'l') la_print();
        if (ch == 'A') lat_watch();
        if (ch == 'w') printf("w %lu\n", (unsigned long)pin_writes);   /* the timed-write count alone: a cheap 'still loading?' (2026-11-14) */
        if (ch == 'U') { bl_on = bl_on ? 0 : 2; bl_n = 0; bl_last = 0; la_n = 0; printf("block log (decoded sectors at $0700) %s: drive code $%04X-$%04X\n", bl_on ? "on" : "off", la_lo, la_hi); }
        if (ch == 'V') { bl_on = !bl_on; bl_n = 0; bl_last = 0; la_n = 0; printf("block log %s: drive code $%04X-$%04X\n", bl_on ? "on" : "off", la_lo, la_hi); }
        if (ch == 'v') { uint32_t n = bl_n < LA_N ? bl_n : LA_N; printf("block log: %lu blocks, the last %lu (half-track hash):\n", (unsigned long)bl_n, (unsigned long)n);
                         for (uint32_t i = bl_n - n; i != bl_n; i++) printf("B %u %08lX\n", la_p[i & (LA_N - 1)], (unsigned long)la_t[i & (LA_N - 1)]); }
        if (ch == 'G') {                             /* G: the last 400 bus changes the drive saw, oldest first (after a hang: its last frames) */
            uint32_t w = log_w, n = w < 400 ? w : 400, c0 = log_buf[(w - n) & (LOG_N - 1)] >> 3;
            printf("bus ring: drive cycle (from the first shown), lines pulled ATN CLK DATA; now cycle %lu\n", (unsigned long)(D.cycles & 0x1FFFFFFFu));
            for (uint32_t i = w - n; i != w; i++) {
                uint32_t v = log_buf[i & (LOG_N - 1)];
                printf("%lu %u%u%u\n", (unsigned long)(((v >> 3) - c0) & 0x1FFFFFFFu), v & 1, (v >> 1) & 1, (v >> 2) & 1);
            }
        }
#if COST && !DIAG
        if (ch == 'u') { static const char *nm[6] = { "VIA1 write, its work", "VIA1 read, after its wait", "VIA2 read, all of it", "events (BUS_TICK)", "disk catch-ups", "  head events in them" };
                         for (int k = 0; k < 6; k++) printf("%-26s n %10lu  avg %4lu  max %6lu sysclk\n", nm[k], (unsigned long)cs_n[k], (unsigned long)(cs_n[k] ? cs_sum[k] / cs_n[k] : 0), (unsigned long)cs_max[k]);
                         memset(cs_sum, 0, sizeof cs_sum); memset(cs_n, 0, sizeof cs_n); memset(cs_max, 0, sizeof cs_max); }
#endif
        if (ch == 'Y') { la_atn = !la_atn; printf("logic analyser trigger %s ATN\n", la_atn ? "needs" : "ignores"); }
        if (ch == 'M') {                             /* M/path/to/disk.d64<newline>: insert it, as the knob does (unattended tests) */
            static char mp[BR_PATH]; int n = 0, k;
            uint32_t t0 = sd_ms();
            while (n < BR_PATH - 1 && sd_ms() - t0 < 2000) { k = getchar_timeout_us(10000); if (k < 0) continue; if (k == '\n' || k == '\r') break; mp[n++] = (char)k; }
            mp[n] = 0;
            if (n) { ui_mount(mp); printf("mount %s\n", mp); }
        }
        if (ch == 'E') { ui_eject(); printf("eject\n"); }
        if (ch == 'd') {                             /* the drive's RAM, all 2 KB, hex (2026-11-07; from $0000 since 2026-11-10: */
            for (int a = 0x000; a < 0x800; a += 32) { /* the stack page holds Transwarp's "TW" marker at $0159) */ printf("D%04X ", a); for (int i = 0; i < 32; i++) printf("%02X", D.ram[a + i]); printf("\n"); }
        }
        if (ch == 'X') {
            if (B.online && !bus_off) printf("bus off: eject first\n");
            else { bus_off = !bus_off; printf("bus %s\n", bus_off ? "off: PicoD64 lets go of every line and answers nothing" : "on"); }
        }
        if (ch == 'S') { if (cap_on) { cap_stop(); printf("\nsniffer off\n"); } else { printf("sniffer on\n"); cap_start(); } }
        if (ch == 'r' || ch == 'R') {                /* r: the DOS reading a sector over and over; R: the same, no disk */
            if (B.online) printf("bench: eject first\n");
            else { bench_res = 0; bench_req = ch == 'r' ? 5 : 6; while (bench_req) serve();
                   printf("bench DOS reading%s: %lu.%02lu sysclk per drive cycle (real time: %d); job result $%02X\n", ch == 'R' ? ", no disk" : "",
                          (unsigned long)(bench_res / 100), (unsigned long)(bench_res % 100), SYS_MHZ, D.ram[0]); }
        }
        if (ch == 'b' || ch == 'k' || ch == 'K' || ch == 'n' || ch == 'N') {
            /* N: the NOPs with core 0 spinning on its own stack (SCRATCH_Y), off the striped SRAM: what core 0's
             * loads cost core 1 (2026-11-07) */
            if (B.online) printf("bench: eject first\n");
            else { bench_res = 0; bench_req = ch == 'b' ? 1 : ch == 'k' ? 2 : ch == 'K' ? 3 : 4;
                   if (ch == 'N') { busy_wait_us(1); for (;;) { volatile int i; for (i = 0; i < 2000; i++) { } if (!bench_req) break; } }
                   while (bench_req) serve();
                   printf("bench %s: %lu.%02lu sysclk per drive cycle (real time: %d)\n", ch == 'b' ? "DOS" : ch == 'k' ? "loop" : ch == 'K' ? "loop, no disk" : ch == 'N' ? "NOPs, no disk, core 0 off SRAM" : "NOPs, no disk", (unsigned long)(bench_res / 100), (unsigned long)(bench_res % 100), SYS_MHZ); }
        }
        if (ch == 'p') { static const char *nm[PROF_N] = { "read inputs", "read VIA", "read IRQ", "read->write gap", "write to sync",
                                                     "write inputs", "write VIA", "write outputs", "write IRQ", "write resched", "gap, events", "gap, none" };
            printf("drive events in %lu cycles: all %lu, VIA1 T1 %lu, VIA2 T1 %lu, T2 %lu, lookahead due %lu, disk byte %lu\n", (unsigned long)(D.cycles - ev_c0),
                   (unsigned long)ev_cnt[0], (unsigned long)ev_cnt[1], (unsigned long)ev_cnt[2], (unsigned long)ev_cnt[3], (unsigned long)ev_cnt[4], (unsigned long)ev_cnt[5]);
            for (int k = 0; k < PROF_N; k++) printf("%-16s avg %4lu  max %6lu  n %lu\n", nm[k], (unsigned long)(prof_cnt[k] ? prof_sum[k] / prof_cnt[k] : 0), (unsigned long)prof_max[k], (unsigned long)prof_cnt[k]); }
        if (ch == 'C') { ev_n = 0; printf("event log cleared\n"); }
        if (ch == 'c') { for (int i = 0; i < ev_n; i++) printf("%lu %s %02X%s\n", (unsigned long)ev_ms[i], ev_kind[i] == 'c' ? "cmd-byte" : "half-track", ev_val[i], ev_kind[i] == 't' ? "" : ""); printf("(%d events)\n", ev_n); }
        if (ch == 'Q') { quiet = 1; printf("quiet: core 0 idle (no OLED, sniffer, knob) until Q\n"); }
        if (ch == 'H' || ch == 'h') { wr_head += ch == 'H' ? SYS_MHZ : -SYS_MHZ; if (wr_head < 0) wr_head = 0; wr_lat_max = INT32_MIN; wr_lat_min = INT32_MAX; printf("bus write work starts %ld sysclk early\n", (long)wr_head); }
        if (ch == ']' || ch == '[') { rd_trim += ch == ']' ? SYS_MHZ / 10 : -(SYS_MHZ / 10); printf("bus reads %ld sysclk after their cycle\n", (long)rd_trim); }
        if (ch == '}' || ch == '{') { rel_lead += ch == '}' ? SYS_MHZ / 20 : -(SYS_MHZ / 20); if (rel_lead < 0) rel_lead = 0; printf("bus releases %ld sysclk before pulls\n", (long)rel_lead); }
        if (ch == '>' || ch == '<') { wr_trim += ch == '>' ? SYS_MHZ / 10 : -(SYS_MHZ / 10); wr_lat_max = INT32_MIN; wr_lat_min = INT32_MAX; printf("bus write pins %ld sysclk after their cycle\n", (long)wr_trim); }
        if (ch == 'T') { snd_test = SND_HZ; printf("test tone: 1 s, 3.9 kHz, full swing\n"); }   /* the piezo's loudest: the hardware's ceiling */
        if (ch == 's') {
            printf("stack used: core 0 %u of %u, core 1 %u of %u\n", sm_used(__StackBottom, __StackTop), (unsigned)(__StackTop - __StackBottom) * 4u,
                   sm_used(__StackOneBottom, __StackOneTop), (unsigned)(__StackOneTop - __StackOneBottom) * 4u);
            printf("online %d mounted \"%s\" mounts %lu unmounts %lu swaps %lu resets %lu replays %lu/%lu late %lu\n",
                             B.online, B.mounted, (unsigned long)B.mounts, (unsigned long)B.unmounts, (unsigned long)B.swaps,
                             (unsigned long)B.resets, (unsigned long)B.replays_ok, (unsigned long)B.replays_late, (unsigned long)late_syncs);
            printf("sniffer: %lu unlistens, %lu CD acted on; the last 4 (oldest first):", (unsigned long)sn_unl, (unsigned long)sn_cd);
            for (uint32_t k = sn_unl > 4 ? sn_unl - 4 : 0; k < sn_unl; k++) printf(" [sa %d \"%.12s\"]", sn_last_sa[k & 3], (const char *)sn_last[k & 3]);
            printf("\n");
            printf("browse: pc %d rpc %d tpc %d, listening %d talking %d sa %d, bytes in %lu out %lu, errors %lu; buf %ld of %ld; lines in %02X out %02X\n",
                   (int)B.dev.pc, (int)B.dev.rpc, (int)B.dev.tpc, B.dev.listening, B.dev.talking, B.dev.sa, (unsigned long)B.dev.bytes_in,
                   (unsigned long)B.dev.bytes_out, (unsigned long)B.dev.errors, (long)B.br.bpos, (long)B.br.blen, pins_in(), B.dev.out);
            printf("talker: gave up on %lu bytes (no acknowledge); the last waited %lu us, lines %02X, after %lu bytes\n",
                   (unsigned long)tx_err_n, (unsigned long)tx_err_wait, tx_err_lines, (unsigned long)tx_err_bytes);
            printf("card reads: %ld, the last \"%s\": %ld bytes\n", rq_read_n, rq_read_path, rq_read_last);
            printf("card: present %d, mounted %d, taken out %u, mount tries since %u\n", sd_present(), io.card, card_removals, card_tries);
            printf("turbo: setting %s, transfers begun %lu, finished %lu, ended by ATN %lu, now byte %ld of %ld\n", B.br.cfg_turbo ? "on" : "off",
                   (unsigned long)B.turbo_starts, (unsigned long)B.turbos, (unsigned long)B.turbo_atn, (long)bus_tt.i, (long)(B.br.turbo_len - 2));
            printf("bus writes: pins set %ld..%ld sysclk after their cycle (aim %ld, '>' '<'; releases %ld early, '}' '{'; reads %ld after, ']' '['; work %ld early, 'H' 'h')\n", (long)wr_lat_min, (long)wr_lat_max, (long)wr_trim, (long)rel_lead, (long)rd_trim, (long)wr_head);
            printf("worst write: drive PC $%04X, %lu cycles (%lu sysclk of emulation for them) after the previous sync, its work began %ld sysclk from its cycle, running %s%s%s%s%s\n",
                   wl_pc, (unsigned long)wl_gap, (unsigned long)wl_work, (long)wl_arrive, wl_prof & 1 ? "events " : "", wl_prof & 2 ? "lookahead " : "",
                   wl_prof & 4 ? "VIA-read " : "", wl_prof & 8 ? "VIA-write " : "", wl_prof & 16 ? "bus-change " : "");
            printf("pins late: %lu of %lu timed writes over 0.1 us after their aim, worst %ld sysclk (%ld.%02ld us) at drive PC $%04X, %lu cycles after the previous sync\n", (unsigned long)pin_late_n,
                   (unsigned long)pin_writes, (long)pin_late_max, (long)(pin_late_max / SYS_MHZ), (long)(pin_late_max % SYS_MHZ * 100 / SYS_MHZ), pin_late_pc, (unsigned long)pin_late_gap);
            printf("drive now: PC $%04X, half-track %u, motor %u, VIA2 IER %02X, writing %u, lazy disk %u\n", CPU.pc, D.ht, (unsigned)((via_pb(&D.via2) >> 2) & 1),
                   D.via2.ier, D.wmode, drive_lazy());
            printf("late: worst %lu sysclk (%lu.%02lu us) at drive PC $%04X, after %lu drive cycles running %s%s%s%s%s; over 1 us %lu, over 10 us %lu; sound %s\n",
                   (unsigned long)late_max, (unsigned long)(late_max / SYS_MHZ), (unsigned long)(late_max % SYS_MHZ * 100 / SYS_MHZ), late_pc, (unsigned long)late_gap,
                   late_prof & 1 ? "events " : "", late_prof & 2 ? "lookahead " : "", late_prof & 4 ? "VIA-read " : "", late_prof & 8 ? "VIA-write " : "", late_prof & 16 ? "bus-change " : "",
                   (unsigned long)late_1us, (unsigned long)late_10us, snd_on ? "on" : "off");
        }
    }
}
