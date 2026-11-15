/* -------------------------------------------------------------------------
 * F4a: boots the real 1541 DOS ROM [S9] on the core, with no C64 on the bus,
 * and prints the drive's state at a point in the idle loop chosen to match
 * VICE's (host/f4a.sh): the first time after `from` cycles that PC = `pc`
 * and X = `x`, within 60 cycles (the idle loop decrements X every ~20, so
 * that state is unique there). Drive RAM goes to <ram.bin>.
 *
 *   dos_boot <rom.bin> <ram.bin> <from> <pc> <x>
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static uint8_t bus_low;
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   do { } while (0)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     (bus_low)
#define PLAT_IEC_OUT(p)   (bus_low = (uint8_t)(p))
#include "../core/drive1541.h"
#include "../bench/track.h"

static uint8_t rom[16384], trk[BENCH_TRACK_BYTES];
static disk_t disk;

static unsigned t1(uint16_t base)   /* T1 counter; reading T1L-L clears the T1 flag, so only at the end */
{ uint8_t h = drive_via_rd(base + 5); return (unsigned)(h << 8 | drive_via_rd(base + 4)); }

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: dos_boot <rom.bin> <ram.bin> <from> <pc> <x>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { perror(argv[1]); return 2; }
    fclose(f);
    uint32_t from = (uint32_t)strtoul(argv[3], 0, 0);
    uint16_t pc = (uint16_t)strtoul(argv[4], 0, 16);
    uint8_t x = (uint8_t)strtoul(argv[5], 0, 16);
    bench_track(trk);
    bench_disk(&disk, trk);
    drive_init(rom, &disk);
    drive_run(from);
    while (!(CPU.pc == pc && CPU.x == x)) {
        if (CPU.jammed || D.cycles > from + 60) { fprintf(stderr, "state not reached\n"); return 1; }
        drive_run(D.cycles + 1);
    }
    printf("cycles=%u pc=%04X a=%02X x=%02X y=%02X sp=%02X p=%02X\n",
           D.cycles, CPU.pc, CPU.a, CPU.x, CPU.y, CPU.s, CPU.p);
    printf("via1 ddrb=%02X ddra=%02X  via2 orb=%02X ddrb=%02X ddra=%02X\n",
           D.via1.ddrb, D.via1.ddra, D.via2.orb, D.via2.ddrb, D.via2.ddra);
    printf("via2 t1=%04X\n", t1(0x1C00));
    f = fopen(argv[2], "wb");
    if (!f || fwrite(D.ram, 1, sizeof D.ram, f) != sizeof D.ram) { perror(argv[2]); return 2; }
    fclose(f);
    return 0;
}
