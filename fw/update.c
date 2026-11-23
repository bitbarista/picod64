/* PicoD64 -- a firmware update from the card: see update.h */
#include <string.h>
#include <stdio.h>
#include "update.h"
#include "fatfs/ff.h"

/* UF2 [S22]: a block is 32 bytes of header, 476 of data (the first payload_size used), and a final magic */
#define UF2_MAGIC0  0x0A324655u
#define UF2_MAGIC1  0x9E5D5157u
#define UF2_MAGICE  0x0AB16F30u
#define UF2_NOFLASH 0x00000001u           /* flags: not for the main flash */
#define UF2_FAMILY  0x00002000u           /* flags: the file-size field is a family ID */
#define FAM_RP2350_ARM_S 0xE48BFF59u      /* [S22] */
#define FAM_ABSOLUTE     0xE48BFF57u      /* [S22]: the block picotool puts first for RP2350-E10; not part of the image */
#define FLASH_BASE  0x10000000u

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }


/* one pass over the file: check every block; with io->stage_write, also fill the staging area.
 * Returns the image's length, or 0 with why[] set */
static uint32_t pass(FIL *f, const upd_io_t *io, int stage, int *same, uint32_t *hash, char *why, int why_n)
{
    UINT got;
    uint8_t *sect = io->work, *blk = io->work + UPD_SECTOR;
    uint32_t n = 0, total = 0, at = 0;
    const char *mark = UPD_MARK; size_t ml = strlen(mark), mk = 0;
    int found = 0;
    *same = 1; *hash = 2166136261u;
    if (f_lseek(f, 0) != FR_OK) { snprintf(why, (size_t)why_n, "Can't read"); return 0; }
    for (;;) {
        if (f_read(f, blk, 512, &got) != FR_OK) { snprintf(why, (size_t)why_n, "Can't read"); return 0; }
        if (got == 0) break;
        if (got != 512 || le32(blk) != UF2_MAGIC0 || le32(blk + 4) != UF2_MAGIC1 || le32(blk + 508) != UF2_MAGICE) {
            snprintf(why, (size_t)why_n, "Not a UF2"); return 0;
        }
        uint32_t flags = le32(blk + 8), addr = le32(blk + 12), len = le32(blk + 16), no = le32(blk + 20), cnt = le32(blk + 24), fam = le32(blk + 28);
        if ((flags & UF2_FAMILY) && fam == FAM_ABSOLUTE) continue;
        if (!(flags & UF2_FAMILY) || fam != FAM_RP2350_ARM_S || (flags & UF2_NOFLASH)) { snprintf(why, (size_t)why_n, "Not for Pico 2"); return 0; }
        if (len != 256 || no != n || (n && cnt != total) || addr != FLASH_BASE + 256 * n) { snprintf(why, (size_t)why_n, "Out of order"); return 0; }
        if (!n) total = cnt;
        if (total == 0 || (uint64_t)total * 256 > UPD_MAX) { snprintf(why, (size_t)why_n, "Too big"); return 0; }
        const uint8_t *d = blk + 32;
        for (int i = 0; i < 256; i++) {
            *hash = (*hash ^ d[i]) * 16777619u;
            if (!found) { mk = d[i] == (uint8_t)mark[mk] ? mk + 1 : d[i] == (uint8_t)mark[0]; found = mk == ml; }
        }
        if (*same && memcmp(io->running + 256 * n, d, 256)) *same = 0;
        if (stage) {
            memcpy(sect + at, d, 256); at += 256;
            if (at == UPD_SECTOR) {
                if (!io->stage_write(256 * (n + 1) - UPD_SECTOR, sect)) { snprintf(why, (size_t)why_n, "Write failed"); return 0; }
                at = 0;
            }
        }
        n++;
    }
    if (!n || n != total) { snprintf(why, (size_t)why_n, n ? "Cut short" : "Empty file"); return 0; }
    if (!found) { snprintf(why, (size_t)why_n, "Not PicoD64"); return 0; }
    if (stage && at) {
        memset(sect + at, 0xFF, UPD_SECTOR - at);
        if (!io->stage_write(256 * n - at, sect)) { snprintf(why, (size_t)why_n, "Write failed"); return 0; }
    }
    return 256 * n;
}

uint32_t upd_hash;

int upd_check(const upd_io_t *io, uint32_t *size, char *why, int why_n)
{
    FIL f;
    int same; uint32_t hash;
    why[0] = 0; *size = 0;
    if (f_open(&f, UPD_FILE, FA_READ) != FR_OK) return UPD_NONE;
    uint32_t n = pass(&f, io, 0, &same, &hash, why, why_n);   /* all of it checked before any flash is touched */
    if (n && same) {
        f_close(&f);
        f_unlink(UPD_DONE);
        if (f_rename(UPD_FILE, UPD_DONE) != FR_OK) { snprintf(why, (size_t)why_n, "Can't rename"); return UPD_BAD; }
        *size = n;
        return UPD_SAME;
    }
    uint32_t h2 = 0;
    if (n) n = pass(&f, io, 1, &same, &hash, why, why_n);
    f_close(&f);
    if (!n) return UPD_BAD;
    for (uint32_t i = 0, h = 2166136261u; i <= n; i++) {     /* the staging area read back */
        if (i == n) { h2 = h; break; }
        h = (h ^ io->stage[i]) * 16777619u;
    }
    if (h2 != hash) { snprintf(why, (size_t)why_n, "Check failed"); return UPD_BAD; }
    *size = n; upd_hash = hash;
    return UPD_STAGED;
}
