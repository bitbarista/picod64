/* PicoD64 -- what fw/bus.h and the platform's PLAT_* macros share (include first). */
#ifndef FF_BUS_PLAT_H
#define FF_BUS_PLAT_H
#include <stdint.h>
/* while set, the drive is OFF the bus: it sees bus_replay_lines, drives nothing,
 * and runs as fast as it can (boot, the fast-loader replay) */
static volatile uint8_t bus_replaying, bus_replay_lines;
static volatile uint8_t bus_drive_pulls;       /* what the drive pulls (its last PLAT_IEC_OUT) */
/* the drive's ATN gate (core/drive1541.h PLAT_IEC_GATE): what it pulls for a given ATN, at once */
static volatile uint8_t bus_drive_gate;
static inline uint8_t bus_gate_pulls(uint8_t atn_low)
{
    uint8_t g = bus_drive_gate;
    return (uint8_t)(((g & 1) ? 2 : 0) | (((g >> (atn_low ? 2 : 1)) & 1) ? 4 : 0));   /* IECD_CLK 2, IECD_DATA 4 */
}
/* the drive marks half-tracks it wrote: core 0 writes them back (track = ht / 2 + 1) */
static volatile uint8_t bus_dirty[42];
static volatile uint8_t bus_track_ready;      /* core 0 has brought in the track the head is on */
#define PLAT_TRACK_WRITTEN(ht) (bus_dirty[(ht) >> 1] = 1)
#endif
