/* -------------------------------------------------------------------------
 * A minimal C64 for host tests (F4c): the real KERNAL and BASIC ROMs run on
 * our reference 6502 (host/ref/cpu6502.h, checked against perfect6502 and
 * Klaus Dormann's test), with just enough of the machine around them.
 *
 * Sources: C64 Programmer's Reference Guide [S12]: memory map and the 6510
 * port (p.260), CIA2 port A = the serial bus (p.324: PA3 ATN out, PA4
 * CLK out, PA5 DATA out, PA6 CLK in, PA7 DATA in), the 6526 (Appendix M:
 * ports, timers, ICR, CRA/CRB), the VIC-II raster registers (Appendix N).
 *
 * Modelled: 64 KiB RAM; BASIC, KERNAL and I/O banking by the 6510 port;
 * CIA1 and CIA2 ports, timers A/B (one-shot/continuous, force load, the
 * stopped-high-byte load), ICR and /IRQ; VIC raster counter and raster IRQ
 * (PAL, 63 x 312); everything else in I/O reads back what was written.
 *
 * ⚠ NOT modelled, so this C64 is no timing reference: VIC bad lines and
 * sprite DMA (the real CPU is stalled for them), the CIAs' one-cycle
 * pipeline delays, TOD, the serial port, NMI. The KERNAL's serial protocol
 * has margins of tens of microseconds, far larger than these. Bus timing
 * against a real C64 is F3's job, on hardware.
 * ⚠ [A] CIA2 PA3-5 drive the bus through inverters: a 1 pulls the line low;
 * PA6-7 read 1 when the line is high. Not in [S12]; the KERNAL itself
 * confirms it (with either polarity wrong, no device would ever answer).
 * ------------------------------------------------------------------------- */
#include <string.h>
#include "c64.h"

static uint8_t ram[65536], io[0x400], cram[0x400];
static const uint8_t *kern, *bas;
static uint64_t cyc;
static uint8_t iec_low;              /* the bus, from outside */
static uint8_t port_ddr, port_dat;   /* 6510 $00/$01 */

/* --- 6526 [S12 Appendix M] ------------------------------------------------ */
typedef struct {
    uint8_t pra, prb, ddra, ddrb, icr, mask, cra, crb;
    uint16_t ta, tb, la, lb;
} cia_t;
static cia_t cia1, cia2;
static uint8_t joy2;                 /* joystick 2: bits held (1 up, 2 down, 4 left, 8 right, 16 fire) */
void c64_joy2(uint8_t held) { joy2 = held; }

static void cia_tick(cia_t *c)
{
    int ua = 0;
    if (c->cra & 1) {
        if (c->ta == 0) { ua = 1; c->icr |= 1; c->ta = c->la; if (c->cra & 8) c->cra &= (uint8_t)~1; }
        else c->ta--;
    }
    int tb_count = 0;
    switch ((c->crb >> 5) & 3) {
    case 0: tb_count = 1; break;      /* phi2 */
    case 2: case 3: tb_count = ua; break;   /* TA underflows (CNT taken as high) */
    default: break;                   /* CNT: not wired here */
    }
    if ((c->crb & 1) && tb_count) {
        if (c->tb == 0) { c->icr |= 2; c->tb = c->lb; if (c->crb & 8) c->crb &= (uint8_t)~1; }
        else c->tb--;
    }
}
static int cia_irq(const cia_t *c) { return (c->icr & c->mask & 0x1F) != 0; }

static uint8_t cia_rd(cia_t *c, uint8_t r, uint8_t pa_in, uint8_t pb_in)
{
    switch (r & 15) {
    case 0: return (uint8_t)((c->pra & c->ddra) | (pa_in & ~c->ddra));
    case 1: return (uint8_t)((c->prb & c->ddrb) | (pb_in & ~c->ddrb));
    case 2: return c->ddra;
    case 3: return c->ddrb;
    case 4: return (uint8_t)c->ta;
    case 5: return (uint8_t)(c->ta >> 8);
    case 6: return (uint8_t)c->tb;
    case 7: return (uint8_t)(c->tb >> 8);
    case 13: { uint8_t v = (uint8_t)(c->icr | (cia_irq(c) ? 0x80 : 0)); c->icr = 0; return v; }
    case 14: return (uint8_t)(c->cra & ~0x10);
    case 15: return (uint8_t)(c->crb & ~0x10);
    default: return 0;               /* TOD, SDR: not modelled */
    }
}

static void cia_wr(cia_t *c, uint8_t r, uint8_t v)
{
    switch (r & 15) {
    case 0: c->pra = v; break;
    case 1: c->prb = v; break;
    case 2: c->ddra = v; break;
    case 3: c->ddrb = v; break;
    case 4: c->la = (uint16_t)((c->la & 0xFF00) | v); break;
    case 5: c->la = (uint16_t)((c->la & 0xFF) | (v << 8)); if (!(c->cra & 1)) c->ta = c->la; break;
    case 6: c->lb = (uint16_t)((c->lb & 0xFF00) | v); break;
    case 7: c->lb = (uint16_t)((c->lb & 0xFF) | (v << 8)); if (!(c->crb & 1)) c->tb = c->lb; break;
    case 13: if (v & 0x80) c->mask |= v & 0x1F; else c->mask &= (uint8_t)~(v & 0x1F); break;
    case 14: if (v & 0x10) c->ta = c->la; c->cra = (uint8_t)(v & ~0x10); break;
    case 15: if (v & 0x10) c->tb = c->lb; c->crb = (uint8_t)(v & ~0x10); break;
    default: break;
    }
}

/* --- VIC-II raster only [S12 Appendix N]: $D011 bit 7 + $D012 = line;
 * writes set the compare line; $D019 bit 0 = raster match, $D01A bit 0 =
 * its IRQ enable; writing a 1 to $D019 clears. PAL: 63 cycles x 312 lines. */
static uint16_t raster, rcmp;
static uint8_t vic_irr, vic_imr, vic_d011;
static unsigned rcyc;
static void vic_tick(void)
{
    if (++rcyc < 63) return;
    rcyc = 0;
    if (++raster == 312) raster = 0;
    if (raster == rcmp) vic_irr |= 1;
}

/* --- the bus -------------------------------------------------------------- */
static uint8_t cia2_pa_in(void)
{
    uint8_t pin = 0x3F;   /* PA0-5: whatever is driven, read back through DDR */
    if (!(iec_low & C64_CLK)) pin |= 0x40;
    if (!(iec_low & C64_DATA)) pin |= 0x80;
    return pin;
}

static void tick(void) { cyc++; cia_tick(&cia1); cia_tick(&cia2); vic_tick(); }

static uint8_t rd(uint16_t a)
{
    uint8_t p = (uint8_t)((port_dat & port_ddr) | (0x17 & ~port_ddr));   /* inputs pulled up [A] */
    if (a < 2) return a ? p : port_ddr;
    if (a >= 0xA000 && a < 0xC000 && (p & 3) == 3) return bas[a - 0xA000];
    if (a >= 0xE000 && (p & 2)) return kern[a - 0xE000];
    if (a >= 0xD000 && a < 0xE000 && (p & 3)) {
        if (!(p & 4)) return 0xFF;    /* character ROM: not needed */
        if (a < 0xD400) {
            switch (a & 0x3F) {
            case 0x11: return (uint8_t)((vic_d011 & 0x7F) | ((raster >> 1) & 0x80));
            case 0x12: return (uint8_t)raster;
            case 0x19: return (uint8_t)(vic_irr | ((vic_irr & vic_imr) ? 0x80 : 0) | 0x70);
            case 0x1A: return (uint8_t)(vic_imr | 0xF0);
            default: return io[a & 0x3F];
            }
        }
        if (a >= 0xD800 && a < 0xDC00) return cram[a & 0x3FF];
        if (a >= 0xDC00 && a < 0xDD00) {                /* no keys; joystick 2 pulls port A's pins low */
            uint8_t v = cia_rd(&cia1, (uint8_t)a, 0xFF, 0xFF);
            return (a & 15) == 0 ? (uint8_t)(v & ~joy2) : v;
        }
        if (a >= 0xDD00 && a < 0xDE00) return cia_rd(&cia2, (uint8_t)a, cia2_pa_in(), 0xFF);
        return io[a & 0x3FF];
    }
    return ram[a];
}

static void wr(uint16_t a, uint8_t v)
{
    uint8_t p = (uint8_t)((port_dat & port_ddr) | (0x17 & ~port_ddr));
    if (a == 0) port_ddr = v;
    else if (a == 1) port_dat = v;
    if (a >= 0xD000 && a < 0xE000 && (p & 3) && (p & 4)) {
        if (a < 0xD400) {
            switch (a & 0x3F) {
            case 0x11: vic_d011 = v; rcmp = (uint16_t)((rcmp & 0xFF) | ((v & 0x80) << 1)); break;
            case 0x12: rcmp = (uint16_t)((rcmp & 0x100) | v); break;
            case 0x19: vic_irr &= (uint8_t)~(v & 0x0F); break;
            case 0x1A: vic_imr = v & 0x0F; break;
            default: io[a & 0x3F] = v; break;
            }
        } else if (a >= 0xD800 && a < 0xDC00) cram[a & 0x3FF] = v;
        else if (a >= 0xDC00 && a < 0xDD00) cia_wr(&cia1, (uint8_t)a, v);
        else if (a >= 0xDD00 && a < 0xDE00) cia_wr(&cia2, (uint8_t)a, v);
        else io[a & 0x3FF] = v;
        return;
    }
    ram[a] = v;                       /* RAM under ROM takes writes */
}

static inline uint8_t BUS_RD(uint16_t a) { tick(); return rd(a); }
static inline void BUS_WR(uint16_t a, uint8_t v) { tick(); wr(a, v); }
static inline int IRQ_ASSERTED(void) { return cia_irq(&cia1) || ((vic_irr & vic_imr) != 0); }
#include "ref/cpu6502.h"

static cpu6502_t C;

void c64_init(const uint8_t *kernal, const uint8_t *basic)
{
    kern = kernal; bas = basic;
    memset(ram, 0, sizeof ram); memset(io, 0, sizeof io); memset(cram, 0, sizeof cram);
    memset(&cia1, 0, sizeof cia1); memset(&cia2, 0, sizeof cia2);
    cia1.ta = cia1.tb = cia2.ta = cia2.tb = cia1.la = cia1.lb = cia2.la = cia2.lb = 0xFFFF;
    port_ddr = 0; port_dat = 0; cyc = 0; raster = 0; rcmp = 0; rcyc = 0;
    vic_irr = vic_imr = vic_d011 = 0; iec_low = 0;
    cpu_reset(&C, (uint16_t)(kern[0x1FFC] | (kern[0x1FFD] << 8)));
}

void c64_step(void) { cpu_step(&C); }
/* Would the next instruction reach the CIA2's port (the serial bus) at cycle `target` or later? LDA/STA abs and
 * the like do it on their 4th cycle, but a whole instruction runs at once: past the drive's time it would use lines
 * up to 3 cycles old. The caller holds it back until the drive has caught up (AUDIT R36). */
int c64_iec_after(uint64_t target)
{
    switch (rd(C.pc)) {
    case 0xAD: case 0xAE: case 0xAC: case 0x8D: case 0x8E: case 0x8C: case 0x2C: case 0xCD: case 0xEC: case 0xCC:
    case 0x0D: case 0x2D: case 0x4D: case 0x6D: case 0xED:
    case 0xBD: case 0xB9: case 0x9D: case 0x99: case 0x1D: case 0x3D: case 0x5D: case 0x7D: case 0xDD: case 0xD9:
        break;
    default:
        return 0;
    }
    return rd((uint16_t)(C.pc + 2)) == 0xDD && cyc + 3 >= target;
}
int c64_irq_off(void) { return (C.p >> 2) & 1; }
uint64_t c64_cycles(void) { return cyc; }
uint8_t *c64_ram(void) { return ram; }
uint16_t c64_pc(void) { return C.pc; }
/* where the VIC reads the text screen: bank (CIA2 PA0-1, inverted [S12]) + $D018 bits 4-7 x $400 */
uint8_t *c64_colour(void) { return cram; }
uint16_t c64_screen(void)
{
    uint8_t pa = (uint8_t)((cia2.pra & cia2.ddra) | ~cia2.ddra);
    return (uint16_t)((3 - (pa & 3)) * 0x4000 + ((io[0x18] >> 4) & 15) * 0x400);
}
void c64_jump(uint16_t pc) { C.pc = pc; }
void c64_iec_in(uint8_t low) { iec_low = low; }

uint8_t c64_iec_out(void)
{
    uint8_t pin = (uint8_t)((cia2.pra & cia2.ddra) | ~cia2.ddra);   /* undriven pins float high [A] */
    uint8_t low = 0;
    if (pin & 0x08) low |= C64_ATN;
    if (pin & 0x10) low |= C64_CLK;
    if (pin & 0x20) low |= C64_DATA;
    return low;
}

uint8_t c64_vic(int reg) { return io[reg & 0x3F]; }
const uint8_t *c64_cram(void) { return cram; }
