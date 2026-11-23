/* -------------------------------------------------------------------------
 * PicoD64 -- a firmware update from the card: /picod64.uf2, checked at power-on.
 * Portable: the board (fw/pico_main.c) gives it the flash; host/fw_update_test.c
 * gives it arrays.
 *
 * A UF2 [S22] is 512-byte blocks, each carrying 256 bytes for one flash address.
 * The file is taken only if every block is in order and for an RP2350's flash,
 * and it is PicoD64's firmware (it holds UPD_MARK). Then:
 *   - the same as the firmware running: renamed /picod64.uf2.done, so it is
 *     never installed twice, and the card shows it was;
 *   - different: copied into the staging area of the flash and checked there;
 *     the board then copies it over the running firmware and restarts, and the
 *     new firmware finds the file the same as itself (the first case).
 * A bad file is left alone and reported; the running firmware is untouched.
 * ------------------------------------------------------------------------- */
#ifndef FF_UPDATE_H
#define FF_UPDATE_H
#include <stdint.h>

#define UPD_FILE  "/picod64.uf2"
#define UPD_DONE  "/picod64.uf2.done"
#define UPD_MARK  "PicoD64: ready"        /* in every PicoD64 firmware since the first release */
#define UPD_MAX   (1024 * 1024)           /* the largest image taken: the staging area's size */
#define UPD_SECTOR 4096

typedef struct {
    const uint8_t *running;               /* the running firmware, as the CPU sees the flash (from its start) */
    const uint8_t *stage;                 /* the staging area, the same way */
    /* erase and program one sector of the staging area: off a multiple of UPD_SECTOR, buf UPD_SECTOR bytes; 1 OK */
    int (*stage_write)(uint32_t off, const uint8_t *buf);
    uint8_t *work;                        /* UPD_WORK bytes of RAM to use meanwhile (the board: the disk's, unused yet) */
} upd_io_t;
#define UPD_WORK (UPD_SECTOR + 512)

enum { UPD_NONE, UPD_BAD, UPD_SAME, UPD_STAGED };
/* What's on the card. UPD_STAGED: *size bytes are in the staging area, checked; copy them over the firmware.
 * UPD_SAME: renamed to UPD_DONE (*size its length). UPD_BAD: why[] says what was wrong, for the display. */
int upd_check(const upd_io_t *io, uint32_t *size, char *why, int why_n);
extern uint32_t upd_hash;                 /* UPD_STAGED: a hash of the image (FNV-1a), to know it again */
#endif
