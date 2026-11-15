/* -------------------------------------------------------------------------
 * PicoD64 -- NMOS 6502 core, one bus access per cycle.
 *
 * CLEAN ROOM. Written from the bus-cycle tables in 64doc [S3] and the MCS6500
 * documentation [S1]; checked against perfect6502 [S4] and Klaus Dormann's
 * functional test [S5] as black boxes (host/). No other emulator's CPU code
 * was opened. See PROVENANCE.md.
 *
 * This is a TEMPLATE, not a library. cpu_run() keeps the CPU state in a
 * local copy (registers), and the includer's bus state in locals too, so a
 * plain RAM or ROM access touches no global. The includer defines, before
 * including (all may use `c`, the CPU state pointer, inside cpu_run):
 *
 *   BUS_LOCALS        declarations of the bus-side locals, loaded from memory
 *   BUS_SAVE()        write them back (cpu_run's exit)
 *   BUS_RD(a)         expression: one full read cycle, value of the byte
 *   BUS_WR(a, v)      statement: one full write cycle
 *   BUS_IRQ           expression: /IRQ low, now
 *   INSN_END()        after every instruction
 *   RUN_STOP          expression: return from cpu_run (checked after INSN_END)
 *
 * Every bus cycle, dummy accesses included, goes through BUS_RD/BUS_WR, so the
 * includer sees exactly one access per 1 MHz cycle, in bus order. That is the
 * whole timing model: the drive's clock lives in those hooks, not here. The
 * includer may set V (SO) by c->p |= F_V within an access.
 * ------------------------------------------------------------------------- */
#ifndef FF_CPU6502_H
#define FF_CPU6502_H
#include <stdint.h>

/* FF_HOT places the per-cycle code; the Pico bench puts it in SRAM. */
#ifndef FF_HOT
#define FF_HOT
#endif
/* FF_PROF(bit): profiling hook, marks what ran in the current cycle. */
#ifndef FF_PROF
#define FF_PROF(b) do { } while (0)
#endif

enum { F_C = 0x01, F_Z = 0x02, F_I = 0x04, F_D = 0x08,
       F_B = 0x10, F_U = 0x20, F_V = 0x40, F_N = 0x80 };

typedef struct {
    uint16_t pc;
    uint8_t  a, x, y, s, p;   /* p never holds B; bit 5 is kept set */
    uint8_t  ip, ip_prev;     /* IRQ poll samples: this cycle, previous cycle */
    uint8_t  jammed;          /* a KIL/JAM opcode stopped the CPU */
} cpu6502_t;

/* ⚠ THE IRQ POLL. An NMOS 6502 takes an interrupt after an instruction if /IRQ
 * was low (and I clear) when sampled in the instruction's PENULTIMATE cycle
 * [S3]. Each access records one sample, so at the end of an instruction the
 * penultimate cycle's sample is ipp. I is read when the sample is taken,
 * which is what makes CLI/PLP late and SEI/RTI immediate, with no special
 * cases. ⚠ The sample-to-cycle alignment is NOT yet checked against
 * perfect6502 (FEASIBILITY.md, F2); only the bus traces are.
 * ip and ipp are cpu_run() locals. */
#define CPU_SAMPLE() do { ipp = ip; ip = (uint8_t)((BUS_IRQ) && !(c->p & F_I)); } while (0)

/* CPU_VSYNC(): before V is read; CPU_VOVER(): before V is overwritten whatever it was (the includer may hold back
 * SO: it applies it at the first, and at the second only notes that SO up to now no longer matters) */
#ifndef CPU_VSYNC
#define CPU_VSYNC() do { } while (0)
#endif
#ifndef CPU_VOVER
#define CPU_VOVER() CPU_VSYNC()
#endif
/* CPU_IFLAG(): I has just changed (the includer may hold back what a masked IRQ can't show) */
#ifndef CPU_IFLAG
#define CPU_IFLAG() do { } while (0)
#endif
#define RD(a)    ({ uint16_t a_ = (uint16_t)(a); CPU_SAMPLE(); BUS_RD(a_); })
#define WR(a, v) do { uint16_t a_ = (uint16_t)(a); uint8_t v_ = (uint8_t)(v); \
                      CPU_SAMPLE(); BUS_WR(a_, v_); } while (0)
#define PUSH(v)  WR(0x100 | c->s--, (v))
#define NZ(v)    (c->p = (uint8_t)((c->p & ~(F_N | F_Z)) | ((v) & F_N) | ((uint8_t)(v) ? 0 : F_Z)))
#define SETF(f, cond) (c->p = (uint8_t)((cond) ? (c->p | (f)) : (c->p & ~(f))))

static inline void cpu_reset(cpu6502_t *c, uint16_t pc)
{
    c->pc = pc; c->a = c->x = c->y = 0; c->s = 0xFD;
    c->p = F_U | F_I; c->ip = c->ip_prev = 0; c->jammed = 0;
}

/* --- ALU ---------------------------------------------------------------- */

static inline __attribute__((always_inline)) void op_adc(cpu6502_t *c, uint8_t v)
{
    unsigned cin = c->p & F_C;
    if (!(c->p & F_D)) {
        unsigned r = c->a + v + cin;
        SETF(F_V, ~(c->a ^ v) & (c->a ^ r) & 0x80);
        SETF(F_C, r > 0xFF);
        c->a = (uint8_t)r; NZ(c->a);
        return;
    }
    /* NMOS decimal [S3]: Z comes from the BINARY sum; N and V come from the
     * sum after the low-nibble adjust but before the high one; C from the
     * high adjust. */
    unsigned lo = (c->a & 0x0F) + (v & 0x0F) + cin;
    if (lo > 9) lo += 6;
    unsigned r = (c->a & 0xF0) + (v & 0xF0) + (lo > 0x0F ? 0x10 : 0) + (lo & 0x0F);
    SETF(F_Z, !((c->a + v + cin) & 0xFF));
    SETF(F_N, r & 0x80);
    SETF(F_V, ~(c->a ^ v) & (c->a ^ r) & 0x80);
    if ((r & 0x1F0) > 0x90) r += 0x60;
    SETF(F_C, (r & 0xFF0) > 0xF0);
    c->a = (uint8_t)r;
}

static inline __attribute__((always_inline)) void op_sbc(cpu6502_t *c, uint8_t v)
{
    unsigned bin = (c->p & F_C) ? 0 : 1;
    unsigned r = (unsigned)c->a - v - bin;
    /* NMOS: all four flags come from the binary subtraction, decimal or not [S3]. */
    SETF(F_V, (c->a ^ v) & (c->a ^ r) & 0x80);
    SETF(F_C, r < 0x100);
    NZ((uint8_t)r);
    if (!(c->p & F_D)) { c->a = (uint8_t)r; return; }
    unsigned lo = (c->a & 0x0F) - (v & 0x0F) - bin;
    unsigned hi = (c->a & 0xF0) - (v & 0xF0);
    if (lo & 0x10) { lo -= 6; hi -= 0x10; }
    if (hi & 0x100) hi -= 0x60;
    c->a = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
}

static inline __attribute__((always_inline)) void op_cmp(cpu6502_t *c, uint8_t r, uint8_t v)
{
    unsigned t = (unsigned)r - v;
    SETF(F_C, r >= v); NZ((uint8_t)t);
}

static inline __attribute__((always_inline)) uint8_t op_asl(cpu6502_t *c, uint8_t v) { SETF(F_C, v & 0x80); v <<= 1; NZ(v); return v; }
static inline __attribute__((always_inline)) uint8_t op_lsr(cpu6502_t *c, uint8_t v) { SETF(F_C, v & 1); v >>= 1; NZ(v); return v; }
static inline __attribute__((always_inline)) uint8_t op_rol(cpu6502_t *c, uint8_t v)
{ uint8_t ci = c->p & F_C; SETF(F_C, v & 0x80); v = (uint8_t)((v << 1) | ci); NZ(v); return v; }
static inline __attribute__((always_inline)) uint8_t op_ror(cpu6502_t *c, uint8_t v)
{ uint8_t ci = (c->p & F_C) << 7; SETF(F_C, v & 1); v = (uint8_t)((v >> 1) | ci); NZ(v); return v; }

static inline __attribute__((always_inline)) uint8_t op_dec(cpu6502_t *c, uint8_t v) { v--; NZ(v); return v; }
static inline __attribute__((always_inline)) uint8_t op_inc(cpu6502_t *c, uint8_t v) { v++; NZ(v); return v; }
/* undocumented read-modify-write: the shift or step, then the ALU op [S6] */
static inline __attribute__((always_inline)) uint8_t op_slo(cpu6502_t *c, uint8_t v) { v = op_asl(c, v); c->a |= v; NZ(c->a); return v; }
static inline __attribute__((always_inline)) uint8_t op_rla(cpu6502_t *c, uint8_t v) { v = op_rol(c, v); c->a &= v; NZ(c->a); return v; }
static inline __attribute__((always_inline)) uint8_t op_sre(cpu6502_t *c, uint8_t v) { v = op_lsr(c, v); c->a ^= v; NZ(c->a); return v; }
static inline __attribute__((always_inline)) uint8_t op_rra(cpu6502_t *c, uint8_t v) { v = op_ror(c, v); op_adc(c, v); return v; }
static inline __attribute__((always_inline)) uint8_t op_dcp(cpu6502_t *c, uint8_t v) { v--; op_cmp(c, c->a, v); return v; }
static inline __attribute__((always_inline)) uint8_t op_isc(cpu6502_t *c, uint8_t v) { v++; op_sbc(c, v); return v; }

/* --- the run loop -------------------------------------------------------- */

/* Executes instructions (and interrupt entries) until RUN_STOP. */
static FF_HOT void cpu_run(cpu6502_t *cin)
{
    cpu6502_t L = *cin, *c = &L;      /* local: GCC keeps the fields in registers */
    uint8_t ip = L.ip, ipp = L.ip_prev;
    BUS_LOCALS
    for (;;) {
    if (c->jammed) { RD(0xFFFF); goto insn_end; }   /* holds the bus; still one cycle */

    /* Interrupt entry: 7 cycles [S3]. The opcode fetched in cycle 1 is
     * discarded and PC is not incremented. ⚠ Not modelled: an IRQ arriving
     * during BRK's first cycles takes over the BRK [S3]; no NMI (the 1541
     * does not use it [A]). */
    if (ipp) {
        FF_PROF(64);
        RD(c->pc); RD(c->pc);
        PUSH(c->pc >> 8); PUSH(c->pc & 0xFF); CPU_VSYNC(); PUSH((c->p & ~F_B) | F_U);
        c->p |= F_I; CPU_IFLAG();
        uint8_t lo_ = RD(0xFFFE); c->pc = (uint16_t)(lo_ | (RD(0xFFFF) << 8));
        ipp = 0;   /* this entry consumed the poll */
        goto insn_end;
    }

    {
    uint8_t op = RD(c->pc++);
    FF_PROF(32);   /* the dispatch is charged to the cycle after the fetch */
    uint16_t ea; uint8_t v, lo, hi, t;

    /* Effective-address helpers. Each emits exactly the dummy cycles of its
     * addressing mode [S3]. R = read, W = write or read-modify-write: indexed
     * modes always spend the fix-up cycle on a write, only on a page cross on
     * a read. */
#define AM_ZP()   (ea = RD(c->pc++))
#define AM_ZPI(r) do { t = RD(c->pc++); RD(t); ea = (uint8_t)(t + (r)); } while (0)
#define AM_ABS()  do { lo = RD(c->pc++); hi = RD(c->pc++); ea = (uint16_t)(lo | (hi << 8)); } while (0)
#define AM_ABI(r, W) do { lo = RD(c->pc++); hi = RD(c->pc++); \
        unsigned s_ = lo + (r); ea = (uint16_t)(((hi << 8) + s_) & 0xFFFF); \
        if ((W) || s_ > 0xFF) RD((hi << 8) | (s_ & 0xFF)); } while (0)
#define AM_IZX()  do { t = RD(c->pc++); RD(t); t = (uint8_t)(t + c->x); \
        lo = RD(t); hi = RD((uint8_t)(t + 1)); ea = (uint16_t)(lo | (hi << 8)); } while (0)
#define AM_IZY(W) do { t = RD(c->pc++); lo = RD(t); hi = RD((uint8_t)(t + 1)); \
        unsigned s_ = lo + c->y; ea = (uint16_t)(((hi << 8) + s_) & 0xFFFF); \
        if ((W) || s_ > 0xFF) RD((hi << 8) | (s_ & 0xFF)); } while (0)
    /* read-modify-write tail: read, write the old value back, write the new */
#define RMW(f) do { v = RD(ea); WR(ea, v); v = f(c, v); WR(ea, v); } while (0)
#define RMWV(f) do { v = RD(ea); WR(ea, v); CPU_VOVER(); v = f(c, v); WR(ea, v); } while (0)   /* f sets V */
#define IMPLIED() RD(c->pc)
#define BRANCH(cond) do { int8_t d_ = (int8_t)RD(c->pc++); if (cond) { \
        uint8_t keep_ = ipp; RD(c->pc); uint16_t n_ = (uint16_t)(c->pc + d_); \
        if ((n_ ^ c->pc) & 0xFF00) RD((c->pc & 0xFF00) | (n_ & 0xFF)); \
        else ipp = keep_;   /* taken, no cross: polls as if untaken. ⚠ [A], no source yet; F2 */ \
        c->pc = n_; } } while (0)

    switch (op) {
    /* ---- loads / stores */
    case 0xA9: c->a = RD(c->pc++); NZ(c->a); break;
    case 0xA5: AM_ZP();      c->a = RD(ea); NZ(c->a); break;
    case 0xB5: AM_ZPI(c->x); c->a = RD(ea); NZ(c->a); break;
    case 0xAD: AM_ABS();     c->a = RD(ea); NZ(c->a); break;
    case 0xBD: AM_ABI(c->x, 0); c->a = RD(ea); NZ(c->a); break;
    case 0xB9: AM_ABI(c->y, 0); c->a = RD(ea); NZ(c->a); break;
    case 0xA1: AM_IZX();     c->a = RD(ea); NZ(c->a); break;
    case 0xB1: AM_IZY(0);    c->a = RD(ea); NZ(c->a); break;

    case 0xA2: c->x = RD(c->pc++); NZ(c->x); break;
    case 0xA6: AM_ZP();      c->x = RD(ea); NZ(c->x); break;
    case 0xB6: AM_ZPI(c->y); c->x = RD(ea); NZ(c->x); break;
    case 0xAE: AM_ABS();     c->x = RD(ea); NZ(c->x); break;
    case 0xBE: AM_ABI(c->y, 0); c->x = RD(ea); NZ(c->x); break;

    case 0xA0: c->y = RD(c->pc++); NZ(c->y); break;
    case 0xA4: AM_ZP();      c->y = RD(ea); NZ(c->y); break;
    case 0xB4: AM_ZPI(c->x); c->y = RD(ea); NZ(c->y); break;
    case 0xAC: AM_ABS();     c->y = RD(ea); NZ(c->y); break;
    case 0xBC: AM_ABI(c->x, 0); c->y = RD(ea); NZ(c->y); break;

    case 0x85: AM_ZP();      WR(ea, c->a); break;
    case 0x95: AM_ZPI(c->x); WR(ea, c->a); break;
    case 0x8D: AM_ABS();     WR(ea, c->a); break;
    case 0x9D: AM_ABI(c->x, 1); WR(ea, c->a); break;
    case 0x99: AM_ABI(c->y, 1); WR(ea, c->a); break;
    case 0x81: AM_IZX();     WR(ea, c->a); break;
    case 0x91: AM_IZY(1);    WR(ea, c->a); break;
    case 0x86: AM_ZP();      WR(ea, c->x); break;
    case 0x96: AM_ZPI(c->y); WR(ea, c->x); break;
    case 0x8E: AM_ABS();     WR(ea, c->x); break;
    case 0x84: AM_ZP();      WR(ea, c->y); break;
    case 0x94: AM_ZPI(c->x); WR(ea, c->y); break;
    case 0x8C: AM_ABS();     WR(ea, c->y); break;

    /* ---- ALU, read group: ORA AND EOR ADC CMP SBC */
#define READ_GROUP(base, body) \
    case base + 0x09: v = RD(c->pc++); body; break; \
    case base + 0x05: AM_ZP();      v = RD(ea); body; break; \
    case base + 0x15: AM_ZPI(c->x); v = RD(ea); body; break; \
    case base + 0x0D: AM_ABS();     v = RD(ea); body; break; \
    case base + 0x1D: AM_ABI(c->x, 0); v = RD(ea); body; break; \
    case base + 0x19: AM_ABI(c->y, 0); v = RD(ea); body; break; \
    case base + 0x01: AM_IZX();     v = RD(ea); body; break; \
    case base + 0x11: AM_IZY(0);    v = RD(ea); body; break;
    READ_GROUP(0x00, { c->a |= v; NZ(c->a); })
    READ_GROUP(0x20, { c->a &= v; NZ(c->a); })
    READ_GROUP(0x40, { c->a ^= v; NZ(c->a); })
    READ_GROUP(0x60, { CPU_VOVER(); op_adc(c, v); })
    READ_GROUP(0xC0, op_cmp(c, c->a, v))
    READ_GROUP(0xE0, { CPU_VOVER(); op_sbc(c, v); })
    case 0xEB: v = RD(c->pc++); CPU_VOVER(); op_sbc(c, v); break;   /* illegal SBC #imm [S6] */

    case 0xE0: v = RD(c->pc++); op_cmp(c, c->x, v); break;
    case 0xE4: AM_ZP();  v = RD(ea); op_cmp(c, c->x, v); break;
    case 0xEC: AM_ABS(); v = RD(ea); op_cmp(c, c->x, v); break;
    case 0xC0: v = RD(c->pc++); op_cmp(c, c->y, v); break;
    case 0xC4: AM_ZP();  v = RD(ea); op_cmp(c, c->y, v); break;
    case 0xCC: AM_ABS(); v = RD(ea); op_cmp(c, c->y, v); break;

    case 0x24: AM_ZP();  v = RD(ea); goto bit;
    case 0x2C: AM_ABS(); v = RD(ea);
    bit: CPU_VOVER(); c->p = (uint8_t)((c->p & ~(F_N | F_V | F_Z)) | (v & (F_N | F_V)) | ((c->a & v) ? 0 : F_Z)); break;

    /* ---- shifts and INC/DEC, memory */
#define RMW_GROUP(base, f) \
    case base + 0x06: AM_ZP();      RMW(f); break; \
    case base + 0x16: AM_ZPI(c->x); RMW(f); break; \
    case base + 0x0E: AM_ABS();     RMW(f); break; \
    case base + 0x1E: AM_ABI(c->x, 1); RMW(f); break;
    RMW_GROUP(0x00, op_asl)
    RMW_GROUP(0x20, op_rol)
    RMW_GROUP(0x40, op_lsr)
    RMW_GROUP(0x60, op_ror)
    RMW_GROUP(0xC0, op_dec)
    RMW_GROUP(0xE0, op_inc)

    case 0x0A: IMPLIED(); c->a = op_asl(c, c->a); break;
    case 0x2A: IMPLIED(); c->a = op_rol(c, c->a); break;
    case 0x4A: IMPLIED(); c->a = op_lsr(c, c->a); break;
    case 0x6A: IMPLIED(); c->a = op_ror(c, c->a); break;

    /* ---- register ops */
    case 0xAA: IMPLIED(); c->x = c->a; NZ(c->x); break;
    case 0xA8: IMPLIED(); c->y = c->a; NZ(c->y); break;
    case 0x8A: IMPLIED(); c->a = c->x; NZ(c->a); break;
    case 0x98: IMPLIED(); c->a = c->y; NZ(c->a); break;
    case 0xBA: IMPLIED(); c->x = c->s; NZ(c->x); break;
    case 0x9A: IMPLIED(); c->s = c->x; break;
    case 0xE8: IMPLIED(); c->x++; NZ(c->x); break;
    case 0xC8: IMPLIED(); c->y++; NZ(c->y); break;
    case 0xCA: IMPLIED(); c->x--; NZ(c->x); break;
    case 0x88: IMPLIED(); c->y--; NZ(c->y); break;

    /* ---- flags: the change lands after the cycle's sample, see CPU_SAMPLE */
    case 0x18: IMPLIED(); c->p &= ~F_C; break;
    case 0x38: IMPLIED(); c->p |= F_C; break;
    case 0x58: IMPLIED(); c->p &= ~F_I; CPU_IFLAG(); break;
    case 0x78: IMPLIED(); c->p |= F_I; CPU_IFLAG(); break;
    case 0xB8: IMPLIED(); CPU_VOVER(); c->p &= ~F_V; break;
    case 0xD8: IMPLIED(); c->p &= ~F_D; break;
    case 0xF8: IMPLIED(); c->p |= F_D; break;

    /* ---- branches */
    case 0x10: BRANCH(!(c->p & F_N)); break;
    case 0x30: BRANCH(c->p & F_N); break;
    case 0x50: BRANCH(({ CPU_VSYNC(); !(c->p & F_V); })); break;
    case 0x70: BRANCH(({ CPU_VSYNC(); (c->p & F_V); })); break;
    case 0x90: BRANCH(!(c->p & F_C)); break;
    case 0xB0: BRANCH(c->p & F_C); break;
    case 0xD0: BRANCH(!(c->p & F_Z)); break;
    case 0xF0: BRANCH(c->p & F_Z); break;

    /* ---- jumps, stack */
    case 0x4C: lo = RD(c->pc++); hi = RD(c->pc); c->pc = (uint16_t)(lo | (hi << 8)); break;
    case 0x6C: AM_ABS();   /* the pointer's high byte does not carry [S1][S3] */
        lo = RD(ea); hi = RD((ea & 0xFF00) | ((ea + 1) & 0xFF));
        c->pc = (uint16_t)(lo | (hi << 8)); break;
    case 0x20: lo = RD(c->pc++); RD(0x100 | c->s);
        PUSH(c->pc >> 8); PUSH(c->pc & 0xFF);
        hi = RD(c->pc); c->pc = (uint16_t)(lo | (hi << 8)); break;
    case 0x60: IMPLIED(); RD(0x100 | c->s);
        lo = RD(0x100 | ++c->s); hi = RD(0x100 | ++c->s);
        c->pc = (uint16_t)(lo | (hi << 8)); RD(c->pc++); break;
    case 0x40: IMPLIED(); RD(0x100 | c->s);
        v = RD(0x100 | ++c->s); CPU_VOVER(); c->p = (uint8_t)((v & ~F_B) | F_U); CPU_IFLAG();
        lo = RD(0x100 | ++c->s); hi = RD(0x100 | ++c->s);
        c->pc = (uint16_t)(lo | (hi << 8)); break;
    case 0x00: RD(c->pc++);
        PUSH(c->pc >> 8); PUSH(c->pc & 0xFF); CPU_VSYNC(); PUSH(c->p | F_B | F_U);
        c->p |= F_I; CPU_IFLAG();
        lo = RD(0xFFFE); c->pc = (uint16_t)(lo | (RD(0xFFFF) << 8)); break;
    case 0x48: IMPLIED(); PUSH(c->a); break;
    case 0x08: IMPLIED(); CPU_VSYNC(); PUSH(c->p | F_B | F_U); break;
    case 0x68: IMPLIED(); RD(0x100 | c->s); c->a = RD(0x100 | ++c->s); NZ(c->a); break;
    case 0x28: IMPLIED(); RD(0x100 | c->s);
        v = RD(0x100 | ++c->s); CPU_VOVER(); c->p = (uint8_t)((v & ~F_B) | F_U); CPU_IFLAG(); break;

    /* ---- NOPs, documented and not [S6] */
    case 0xEA: case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        IMPLIED(); break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: RD(c->pc++); break;
    case 0x04: case 0x44: case 0x64: AM_ZP(); RD(ea); break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: AM_ZPI(c->x); RD(ea); break;
    case 0x0C: AM_ABS(); RD(ea); break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: AM_ABI(c->x, 0); RD(ea); break;

    /* ---- stable undocumented opcodes [S6]. ⚠ Not yet checked against an
     * oracle (FEASIBILITY.md, F2); the 1541 DOS does not use them. */
    case 0xA7: AM_ZP();      c->a = c->x = RD(ea); NZ(c->a); break;   /* LAX */
    case 0xB7: AM_ZPI(c->y); c->a = c->x = RD(ea); NZ(c->a); break;
    case 0xAF: AM_ABS();     c->a = c->x = RD(ea); NZ(c->a); break;
    case 0xBF: AM_ABI(c->y, 0); c->a = c->x = RD(ea); NZ(c->a); break;
    case 0xA3: AM_IZX();     c->a = c->x = RD(ea); NZ(c->a); break;
    case 0xB3: AM_IZY(0);    c->a = c->x = RD(ea); NZ(c->a); break;
    case 0x87: AM_ZP();      WR(ea, c->a & c->x); break;               /* SAX */
    case 0x97: AM_ZPI(c->y); WR(ea, c->a & c->x); break;
    case 0x8F: AM_ABS();     WR(ea, c->a & c->x); break;
    case 0x83: AM_IZX();     WR(ea, c->a & c->x); break;
#define RMW_ILL(base, f) \
    case base + 0x07: AM_ZP();      RMW(f); break; \
    case base + 0x17: AM_ZPI(c->x); RMW(f); break; \
    case base + 0x0F: AM_ABS();     RMW(f); break; \
    case base + 0x1F: AM_ABI(c->x, 1); RMW(f); break; \
    case base + 0x1B: AM_ABI(c->y, 1); RMW(f); break; \
    case base + 0x03: AM_IZX();     RMW(f); break; \
    case base + 0x13: AM_IZY(1);    RMW(f); break;
    RMW_ILL(0x00, op_slo)
    RMW_ILL(0x20, op_rla)
    RMW_ILL(0x40, op_sre)
#undef RMW
#define RMW RMWV                     /* RRA and ISC set V (ADC, SBC) */
    RMW_ILL(0x60, op_rra)
    RMW_ILL(0xE0, op_isc)
#undef RMW
#define RMW(f) do { v = RD(ea); WR(ea, v); v = f(c, v); WR(ea, v); } while (0)
    RMW_ILL(0xC0, op_dcp)
    case 0x0B: case 0x2B: c->a &= RD(c->pc++); NZ(c->a); SETF(F_C, c->a & 0x80); break; /* ANC */
    case 0x4B: c->a &= RD(c->pc++); c->a = op_lsr(c, c->a); break;    /* ALR */
    case 0xCB: v = RD(c->pc++); t = c->a & c->x;                       /* SBX */
        SETF(F_C, t >= v); c->x = (uint8_t)(t - v); NZ(c->x); break;
    case 0xAB: c->a = c->x = (uint8_t)((c->a | 0xEE) & RD(c->pc++)); NZ(c->a); break;   /* LXA ("OAL" [S6]): (A | $EE) & #imm
                 to A and X. A demo's loader uses LXA #$00 on the drive (The Hat, first board, 2026-11-06): it jammed here */
    case 0x6B: {   /* ARR [S3 "$6B ARR"]: AND #imm, then ROR with C in. A demo's drive code (The Hat, 2026-11-06) */
        unsigned t = c->a & RD(c->pc++), ci;
        CPU_VOVER(); ci = c->p & F_C;
        uint8_t r = (uint8_t)((t >> 1) | (ci << 7));
        if (!(c->p & F_D)) {         /* binary: N, Z as ROR; C = bit 6, V = bit 6 ^ bit 5 of the result */
            c->a = r; NZ(r); SETF(F_C, r & 0x40); SETF(F_V, (r ^ (r << 1)) & 0x40);
        } else {                     /* decimal: N = the old C, Z from the ROR, V = bit 6 changed, then the BCD fix-ups */
            SETF(F_N, ci); SETF(F_Z, !r); SETF(F_V, (t ^ r) & 0x40);
            if ((t & 15) + (t & 1) > 5) r = (uint8_t)((r & 0xF0) | ((r + 6) & 0x0F));
            unsigned hi = t >> 4; SETF(F_C, hi + (hi & 1) > 5);
            if (c->p & F_C) r = (uint8_t)(r + 0x60);
            c->a = r;
        }
        break;
    }
    /* The unstable ones [S3 "6510/8502 Undocumented Commands"; S6 agrees, 2026-11-09]. The 1541 Testsuite's
     * pc64 disks jammed at ANE on the board until these were in. On a C64 the VIC-II upsets ANE's constant and
     * the SH* "AND (high + 1)"; a 1541 has no video DMA, so the stable form is the one taken. */
    case 0x8B: c->a = (uint8_t)((c->a | 0xEE) & c->x & RD(c->pc++)); NZ(c->a); break;   /* ANE: (A | $EE) & X & #imm */
    /* SHA/SHX/SHY/SHS: store reg & (operand high + 1); on a page cross the stored value is also the address's high
     * byte. One copy of the addressing and the store for all five: each RD/WR is the whole bus path, and five
     * copies overflowed the board's RAM by 244 bytes (2026-11-09). */
    case 0x93: AM_IZY(1); t = c->a & c->x; goto sh;                           /* SHA (zp),y */
    case 0x9F: case 0x9E: case 0x9B: case 0x9C:                               /* SHA abs,y; SHX abs,y; SHS abs,y; SHY abs,x */
        AM_ABI(op == 0x9C ? c->x : c->y, 1);
        t = op == 0x9E ? c->x : op == 0x9C ? c->y : c->a & c->x;
        if (op == 0x9B) c->s = t;                                             /* SHS (TAS): S = A & X, then as SHA */
    sh: v = (uint8_t)(t & (hi + 1));
        if ((ea >> 8) != hi) ea = (uint16_t)((v << 8) | (ea & 0xFF));
        WR(ea, v); break;
    case 0xBB: AM_ABI(c->y, 0); c->a = c->x = c->s = RD(ea) & c->s; NZ(c->a); break;   /* LAS abs,y: M & S to A, X, S */

    default:   /* KIL/JAM and the unimplemented few */
        c->jammed = 1; break;
    }
    }
insn_end:
    INSN_END();
    if (RUN_STOP) break;
    }
    L.ip = ip; L.ip_prev = ipp;
    BUS_SAVE();
    *cin = L;
}
#undef AM_ZP
#undef AM_ZPI
#undef AM_ABS
#undef AM_ABI
#undef AM_IZX
#undef AM_IZY
#undef RMW
#undef IMPLIED
#undef BRANCH
#undef READ_GROUP
#undef RMW_GROUP
#undef RMW_ILL
#undef PUSH
#undef RD
#undef WR
#undef CPU_SAMPLE
#undef NZ
#undef SETF
#endif
