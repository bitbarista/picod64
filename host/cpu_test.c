/* -------------------------------------------------------------------------
 * Host checks for core/cpu6502.h, against two black-box oracles.
 *
 *   cpu_test klaus <klaus.hex>
 *       Klaus Dormann's 6502_functional_test [S5]. PASS = PC traps at $3469.
 *   cpu_test trace <mem.hex> <cycles> [lo hi]
 *       One line per cycle, in the format of a perfect6502 cycle-trace build
 *       [S4]: "<cycle> <addr> <rw> <data>", so host/diff.py can align and
 *       compare the two.
 *
 * Neither oracle's code is linked here; perfect6502 runs as its own binary.
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t mem[65536];
static unsigned long cyc;
static int tracing;

/* the cpu6502.h bus interface: a flat 64 KiB, no IRQ, one instruction per
 * cpu_run() call so the harness can watch PC between instructions */
#define BUS_LOCALS
#define BUS_SAVE()      do { } while (0)
#define BUS_RD(a)       ({ uint8_t v__ = mem[a]; \
                           if (tracing) printf("%lu %04X 1 %02X\n", cyc, (unsigned)(a), v__); \
                           cyc++; v__; })
#define BUS_WR(a, v)    do { mem[a] = (v); \
                           if (tracing) printf("%lu %04X 0 %02X\n", cyc, (unsigned)(a), (unsigned)(v)); \
                           cyc++; } while (0)
#define BUS_IRQ         0
#define INSN_END()      do { } while (0)
#define RUN_STOP        1
#include "../core/cpu6502.h"

static void load_hex(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    unsigned v; int i = 0;
    while (i < 65536 && fscanf(f, "%x", &v) == 1) mem[i++] = (uint8_t)v;
    fclose(f);
}

int main(int argc, char **argv)
{
    cpu6502_t c;
    if (argc >= 3 && !strcmp(argv[1], "klaus")) {
        load_hex(argv[2]);
        cpu_reset(&c, 0x0400);   /* where Klaus's build starts [S5] */
        uint16_t last = 0xFFFF;
        for (;;) {
            uint16_t pc = c.pc;
            cpu_run(&c);
            if (c.pc == pc && pc == last) break;   /* a JMP * trap */
            last = pc;
            if (c.jammed) { printf("JAM at $%04X\n", pc); return 1; }
            if (cyc > 200000000UL) { printf("TIMEOUT\n"); return 1; }
        }
        printf("trap at $%04X after %lu cycles: %s\n", c.pc, cyc,
               c.pc == 0x3469 ? "PASS" : "FAIL");
        return c.pc == 0x3469 ? 0 : 1;
    }
    if (argc >= 4 && !strcmp(argv[1], "trace")) {
        load_hex(argv[2]);
        if (argc >= 6) { mem[0xFFFC] = (uint8_t)strtol(argv[4], 0, 0); mem[0xFFFD] = (uint8_t)strtol(argv[5], 0, 0); }
        unsigned long n = strtoul(argv[3], 0, 0);
        /* ⚠ No reset sequence is modelled: the trace starts at the first
         * opcode fetch, and diff.py aligns perfect6502's trace to that fetch.
         * S and P after reset are perfect6502's ($FD, I set) [S4]. */
        cpu_reset(&c, (uint16_t)(mem[0xFFFC] | (mem[0xFFFD] << 8)));
        tracing = 1;
        while (cyc < n) cpu_run(&c);
        return 0;
    }
    fprintf(stderr, "usage: cpu_test klaus <hex> | trace <hex> <cycles> [lo hi]\n");
    return 2;
}
