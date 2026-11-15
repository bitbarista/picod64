/* -------------------------------------------------------------------------
 * A minimal C64, host only: a TEST FIXTURE that runs the real KERNAL's serial
 * bus code against the emulated drive (F4c). It is not an exact C64 and is
 * not meant to be; what it must get right is what the KERNAL's IEC routines
 * depend on. See c64.c for what is modelled and what is not.
 * ------------------------------------------------------------------------- */
#ifndef FF_C64_H
#define FF_C64_H
#include <stdint.h>

#define C64_HZ 985248u              /* PAL [A: the PRG gives no figure we hold] */

/* IEC lines, bit set = line LOW; the same bits as core/drive1541.h */
enum { C64_ATN = 1, C64_CLK = 2, C64_DATA = 4 };

void     c64_init(const uint8_t *kernal, const uint8_t *basic);
void     c64_step(void);              /* one instruction (or interrupt entry) */
uint64_t c64_cycles(void);
uint8_t  c64_iec_out(void);           /* lines the C64 pulls low */
void     c64_iec_in(uint8_t low);     /* every line's state now (wired-AND) */
uint8_t *c64_ram(void);
int      c64_iec_after(uint64_t target); /* next instruction reaches the serial port at target or later */
int      c64_irq_off(void);           /* the I flag: no keyboard scan while set */
void     c64_joy2(uint8_t held);      /* joystick 2: 1 up, 2 down, 4 left, 8 right, 16 fire */
uint16_t c64_pc(void);
uint8_t *c64_colour(void);          /* the colour RAM */
uint16_t c64_screen(void);           /* the text screen's address, as the VIC sees it */
void     c64_jump(uint16_t pc);
uint8_t  c64_vic(int reg);            /* a VIC register as last written ($D000 + reg) */
const uint8_t *c64_cram(void);        /* colour RAM, 1000 nybbles */
#endif
