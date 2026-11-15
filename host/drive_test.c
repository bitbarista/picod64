/* -------------------------------------------------------------------------
 * Runs the bench workload on the PC, with no real-time wait, and prints the
 * same fingerprint the Pico prints: cycles, passes, IRQs, bytes read and a
 * CRC-32 of drive RAM. The two must match exactly; that is the proof the
 * Pico ran the same emulation, cycle for cycle, that the PC did.
 *
 *   drive_test <workload.bin> <cycles>
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static uint8_t bus_low;   /* lines this drive pulls; nobody else on the bus */
#define PLAT_CYCLE()      do { } while (0)   /* host/ref/ model */
#define PLAT_SYNC(c, k)   do { } while (0)   /* core/ model */
/* idle-time head lookahead: always taken, or (-DLA_FORCED) never, so the
 * forced path at the lower bound is exercised too */
#ifdef LA_FORCED
#define PLAT_SLACK(c)     0
#else
#define PLAT_SLACK(c)     1
#endif
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     (bus_low)
#define PLAT_IEC_OUT(p)   (bus_low = (uint8_t)(p))
#include "../core/drive1541.h"
#include "../bench/track.h"
#include "../bench/fingerprint.h"

static uint8_t rom[16384], trk[BENCH_TRACK_BYTES];
static disk_t disk;

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: drive_test <workload.bin> <cycles>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { perror(argv[1]); return 2; }
    fclose(f);
    uint32_t n = (uint32_t)strtoul(argv[2], 0, 0);
    bench_track(trk);
    bench_disk(&disk, trk);
    drive_init(rom, &disk);
    clock_t t0 = clock();
    drive_run(n);
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    print_fingerprint(printf);
    printf("host speed: %.1f emulated MHz\n", s > 0 ? D.cycles / s / 1e6 : 0.0);
    return CPU.jammed ? 1 : 0;
}
