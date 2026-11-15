/* PicoD64 -- how much of a stack has been used: paint it, and later look for the highest word
 * that changed. Each core has 3 KB (core 1's was 4 KB until 2026-11-11; it had used 808 bytes at most) and neither
 * the linker nor anything else checks it. */
#ifndef STACKMARK_H
#define STACKMARK_H
#include <stdint.h>
#define SM_PAINT 0x5AC4DB17u
static inline void sm_paint(uint32_t *lo, uint32_t *hi) { while (lo < hi) *lo++ = SM_PAINT; }
/* bytes from hi down to the lowest word disturbed */
static inline unsigned sm_used(const uint32_t *lo, const uint32_t *hi)
{
    while (lo < hi && *lo == SM_PAINT) lo++;
    return (unsigned)(hi - lo) * 4u;
}
#endif
