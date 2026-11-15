/* -------------------------------------------------------------------------
 * ⚠ REFERENCE MODEL, host only. The F1 per-cycle board, kept as the readable
 * spec the event-driven core/drive1541.h is checked against (host/equiv.sh).
 * Every peripheral steps every cycle; nothing is clever. Do not optimise.
 *
 * PicoD64 -- the 1541 board around the CPU: memory map, two VIAs, the GCR
 * read head and the IEC lines.
 *
 * CLEAN ROOM. Board facts come from the 1541 Service Manual [S7] and the
 * 251748 schematic [S8]; every pin assignment not yet traced on [S8] is
 * marked [A] here and in PROVENANCE.md.
 *
 * TEMPLATE. The includer defines the platform, before including:
 *
 *   PLAT_CYCLE()       start of each 1 MHz cycle: on hardware, waits for it
 *   PLAT_IEC_IN()      bus lines now, bit set = line LOW: IEC_ATN|IEC_CLK|IEC_DATA
 *   PLAT_IEC_OUT(p)    lines this drive pulls low (IEC_CLK|IEC_DATA)
 * ------------------------------------------------------------------------- */
#ifndef FF_DRIVE1541_H
#define FF_DRIVE1541_H
#include <stdint.h>
#include "via6522.h"
#include "../../core/disk.h"
#ifndef FF_HOT
#define FF_HOT
#endif

enum { IEC_ATN = 1, IEC_CLK = 2, IEC_DATA = 4 };

#define GCR_TRACK_MAX 7928   /* bytes; the bench uses 7692 [A] */

typedef struct {
    uint8_t  ram[2048];
    const uint8_t *rom;           /* 16 KiB, $C000-$FFFF */
    via6522_t via1, via2;         /* serial bus, disk controller [A: see F-1] */
    uint32_t cycles;
    /* read head */
    const disk_t *disk;
    uint8_t  ht, phase;           /* half-track under the head; stepper phase (VIA2 PB0-1) */
    const uint8_t *trk; uint32_t trk_bits, bitpos;
    uint8_t  acc;                 /* 16 MHz ticks into the current bit cell */
    uint16_t sr;                  /* last 10 bits read */
    uint8_t  bitcnt, sync;
    uint8_t  wsr;                 /* write shift register */
    uint32_t bytes_read;          /* stats */
    /* bus */
    uint8_t  iec_in, iec_out;
} drive_t;

static drive_t D;                 /* one drive, file scope: cheapest addressing */
#define DRIVE_NOW D.cycles        /* harness hook: the cycle of the access being traced */
static uint8_t g_so;              /* BYTE READY seen on SO this cycle */
/* ⚠ [A] an unformatted half-track reads as no flux: zeros, no SYNC. */
static const uint8_t drive_blank[6250];

static void head_select(uint8_t ht)
{
    D.ht = ht;
    if (D.disk->gcr[ht]) { D.trk = D.disk->gcr[ht]; D.trk_bits = D.disk->len[ht] * 8u; }
    else { D.trk = drive_blank; D.trk_bits = sizeof drive_blank * 8u; }
}

/* --- the stepper. ⚠ [A] STP0-1 = VIA2 PB0-1; one count = one half-track
 * [S10 p.30]; up = inward [A]; a jump of two counts does not move [A];
 * half-track 0 is the stop. The head moves at once [A] and keeps its angle:
 * bitpos scaled by the ratio of the track lengths; sr and bitcnt carry over. */
static void drive_stepper(void)
{
    uint8_t ph = via_pb(&D.via2) & 3, ht = D.ht;
    if (ph == ((D.phase + 1) & 3)) { if (ht < DISK_HALFTRACKS - 1) ht++; }
    else if (ph == ((D.phase + 3) & 3)) { if (ht > 0) ht--; }
    D.phase = ph;
    if (ht == D.ht) return;
    uint32_t old_bits = D.trk_bits;
    head_select(ht);
    D.bitpos = (uint32_t)((uint64_t)D.bitpos * D.trk_bits / old_bits);
}

/* --- IEC: ⚠ [A] VIA1 port B, from the 1541's commonly published map, not
 * yet traced on [S8]: PB0 DATA in, PB1 DATA out, PB2 CLK in, PB3 CLK out,
 * PB4 ATNA, PB5-6 device-number jumpers, PB7 ATN in; CA1 = ATN in.
 * Inputs read 1 when the line is LOW (inverting receivers, [S7] "inverted by
 * UA1"). DATA is also pulled by the ATN acknowledge gate: ATNA XOR ATN [S7]. */
static inline void drive_iec_eval(void)
{
    uint8_t pb = via_pb(&D.via1);
    uint8_t atn = (D.iec_in & IEC_ATN) ? 1 : 0;
    uint8_t out = 0;
    if (pb & 0x08) out |= IEC_CLK;
    if ((pb & 0x02) || (atn ^ ((pb >> 4) & 1))) out |= IEC_DATA;
    if (out != D.iec_out) { D.iec_out = out; PLAT_IEC_OUT(out); }
}

/* --- GCR head. Bit cell = 4 x (16 - zone) ticks of 16 MHz: dividers
 * 13..16 from [S7]'s table. Zone = VIA2 PB5-6 [S8]. SYNC = the last ten bits
 * all 1 [S7] "when all 8 bits are 1" is the 8-bit stage; ten is [A].
 *
 * WRITE MODE: VIA2 CB2 = MODE [S8], low = write (the DOS reads with it high)
 * [A]. Each bit cell writes the write shift register's top bit to the track;
 * every 8 bits BYTE READY fires as in read mode and the register reloads from
 * VIA2 port A (YB0-7 [S8]; inputs taken as high [A]). SYNC detection is off
 * while writing (the DOS's 5 x $FF sync must still give BYTE READY) [A]; sr
 * takes the written bits. A write to an unformatted half-track is lost [A]. */
static inline int drive_wmode(void) { return (D.via2.pcr & 0xE0) == 0xC0; }
static inline uint8_t drive_pa_out(void) { return (uint8_t)((D.via2.ora & D.via2.ddra) | ~D.via2.ddra); }
static inline void drive_head_tick(void)
{
    uint8_t cell = (uint8_t)(64 - 4 * ((D.via2.orb >> 5) & 3));
    D.acc += 16;
    if (D.acc < cell) return;
    D.acc -= cell;
    uint32_t p = D.bitpos;
    if (drive_wmode()) {
        uint8_t bit = (uint8_t)(D.wsr >> 7);
        D.wsr = (uint8_t)(D.wsr << 1);
        uint8_t *t = D.disk->gcr[D.ht];
        if (t) t[p >> 3] = (uint8_t)((t[p >> 3] & ~(0x80 >> (p & 7))) | (bit << (7 - (p & 7))));
        if (++p == D.trk_bits) p = 0;
        D.bitpos = p;
        D.sr = (uint16_t)(((D.sr << 1) | bit) & 0x3FF);
        D.sync = 0;
        if (++D.bitcnt == 8) {
            D.bitcnt = 0;
            D.wsr = drive_pa_out();
            D.bytes_read++;
            if (via_ca2_high(&D.via2)) g_so = 1;
            via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
        }
        return;
    }
    uint8_t bit = (D.trk[p >> 3] >> (7 - (p & 7))) & 1;
    if (++p == D.trk_bits) p = 0;
    D.bitpos = p;
    D.sr = (uint16_t)(((D.sr << 1) | bit) & 0x3FF);
    if (D.sr == 0x3FF) { D.sync = 1; D.bitcnt = 0; return; }
    D.sync = 0;
    if (++D.bitcnt == 8) {
        D.bitcnt = 0;
        D.via2.pa_in = (uint8_t)D.sr;
        D.bytes_read++;
        /* BYTE READY: to SO when SOE (VIA2 CA2) is high, and to VIA2 CA1 [A] */
        if (via_ca2_high(&D.via2)) g_so = 1;
        via_set_ca1(&D.via2, 0); via_set_ca1(&D.via2, 1);
    }
}

/* Everything that happens once per cycle, before the CPU's access. */
static inline FF_HOT void drive_cycle(void)
{
    PLAT_CYCLE();
    D.cycles++;
    uint8_t in = PLAT_IEC_IN();
    if (in != D.iec_in) {
        D.iec_in = in;
        /* PB0/2/7 read 1 when DATA/CLK/ATN are low [A] */
        D.via1.pb_in = (uint8_t)((D.via1.pb_in & 0x7A) | ((in & IEC_DATA) ? 0x01 : 0) |
                       ((in & IEC_CLK) ? 0x04 : 0) | ((in & IEC_ATN) ? 0x80 : 0));
        via_set_ca1(&D.via1, (in & IEC_ATN) ? 1 : 0);
        drive_iec_eval();
    }
    via_tick(&D.via1);
    via_tick(&D.via2);
    drive_head_tick();
    /* SYNC on VIA2 PB7, active low [A] */
    D.via2.pb_in = (uint8_t)((D.via2.pb_in & 0x7F) | (D.sync ? 0 : 0x80));
}

/* --- the memory map [S7]: RAM $0000-$07FF, VIAs at $1800 and $1C00, ROM
 * $C000-$FFFF. ⚠ Mirroring and open-bus behaviour are [A] until [S8]'s
 * decoder is traced: RAM, ROM and each VIA are assumed mirrored. */
static inline uint8_t bus_rd_(uint16_t a)
{
    if (a & 0x8000) return D.rom[a & 0x3FFF];
    if (a < 0x1800) return D.ram[a & 0x07FF];
    if ((a & 0x1C00) == 0x1800) return via_read(&D.via1, (uint8_t)a);
    if ((a & 0x1C00) == 0x1C00) return via_read(&D.via2, (uint8_t)a);
    return (uint8_t)(a >> 8);
}
static inline uint8_t BUS_RD(uint16_t a)
{
    drive_cycle();
    uint8_t v = bus_rd_(a);
#ifdef DRIVE_TRACE
    DRIVE_TRACE(a, v, 1);
#endif
    return v;
}

static inline void BUS_WR(uint16_t a, uint8_t v)
{
    drive_cycle();
#ifdef DRIVE_TRACE
    DRIVE_TRACE(a, v, 0);
#endif
    if (a & 0x8000) return;
    if (a < 0x1800) { D.ram[a & 0x07FF] = v; return; }
    if ((a & 0x1C00) == 0x1800) { via_write(&D.via1, (uint8_t)a, v); drive_iec_eval(); return; }
    if ((a & 0x1C00) == 0x1C00) { via_write(&D.via2, (uint8_t)a, v); if ((a & 15) == 0) drive_stepper(); }
}

/* --- disk change: see core/drive1541.h. The head keeps its angle, as on a step. */
static const disk_t drive_nodisk;
static void drive_wps(int light) { D.via2.pb_in = (uint8_t)((D.via2.pb_in & ~0x10) | (light ? 0x10 : 0)); }
static void drive_insert(const disk_t *dk)
{
    uint32_t old_bits = D.trk_bits;
    D.disk = dk ? dk : &drive_nodisk;
    head_select(D.ht);
    D.bitpos = (uint32_t)((uint64_t)D.bitpos * D.trk_bits / old_bits);
}

static inline int IRQ_ASSERTED(void) { return via_irq(&D.via1) || via_irq(&D.via2); }

/* BYTE READY on SO sets V in the cycle it arrives (cpu6502.h CPU_SO_HOOK).
 * ⚠ The exact cycle of the V update relative to the SO edge is F2. */
static inline int SO_EDGE(void) { if (g_so) { g_so = 0; return 1; } return 0; }
#define CPU_SO_HOOK SO_EDGE
#include "cpu6502.h"

static cpu6502_t CPU;

static inline void drive_init(const uint8_t *rom, const disk_t *disk)
{
    for (unsigned i = 0; i < sizeof D.ram; i++) D.ram[i] = 0;
    D.rom = rom; D.disk = disk; head_select(34); D.phase = 34 & 3; D.bitpos = 0;   /* [A] track 18 */
    D.acc = 0; D.sr = 0; D.bitcnt = 0; D.sync = 0; D.wsr = 0; D.bytes_read = 0; D.cycles = 0;
    via_reset(&D.via1); via_reset(&D.via2);
    D.via1.pb_in = 0x00;   /* idle bus reads 0; PB5-6 = 0 for device 8 [A] */
    /* ⚠ CA1 starts at the PIN's level: ATN released reads LOW after the
     * inverter [S7]. via_reset's ca1 = 1 made the first IEC input change a
     * spurious CA1 edge, so a spurious IRQ (found 2026-10-25, when echo
     * prediction removed that first change and the models diverged). */
    D.via1.ca1 = 0;
    D.iec_in = 0; D.iec_out = 0xFF;
    drive_iec_eval();
    cpu_reset(&CPU, (uint16_t)(rom[0x3FFC] | (rom[0x3FFD] << 8)));
    g_so = 0;
}

static inline void drive_step(void) { cpu_step(&CPU); }
#endif
