/* -------------------------------------------------------------------------
 * fw/sd_spi.c against a simulated SD card in SPI mode, the card backed by a
 * FAT image file: the driver's bytes on the wire, through FatFs, with
 * fw/storage.c on top (listing, a D64 streamed and written back).
 *
 * The simulated card is written from the same reading of the spec as the
 * driver [A], so this tests the driver's consistency and its FatFs glue, not
 * the spec itself: the real card on the first board does that (F5-SD).
 * Card types: SDHC (block addressed) and SDSC v2 (byte addressed, CMD16).
 *   fw_sd_test <card.img> <ref.d64> <sdpath of a D64 on the image>
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../fw/storage.h"

static FILE *img; static long img_sectors;
static int sdhc, cs_low, fast;
static uint32_t now_ms;
/* the card's state */
enum { IDLE_WAIT, CMD_IN, RESP, DATA_OUT, WAIT_TOKEN, DATA_IN, DATA_RESP };
static int st = CMD_IN, idle = 1, app = 0, nin, nout, pos;
static uint8_t cmdb[6], out[600], blk[514];
static uint32_t waddr; static int acmd41_tries;
static unsigned long wire_bytes;

static void respond(const uint8_t *b, int n) { memcpy(out, b, (size_t)n); nout = n; pos = 0; st = RESP; }
static void command(void)
{
    uint8_t c = cmdb[0] & 0x3F; uint32_t arg = (uint32_t)cmdb[1] << 24 | cmdb[2] << 16 | cmdb[3] << 8 | cmdb[4];
    uint8_t r1 = idle ? 1 : 0, b[600]; int n = 0;
    b[n++] = 0xFF;                                     /* NCR: one byte of 0xFF first */
    int was_app = app; app = 0;
    if (c == 0) { idle = 1; b[n++] = 1; respond(b, n); return; }
    if ((c == 0 || c == 8) && cmdb[5] != (c == 0 ? 0x95 : 0x87)) { b[n++] = 0x09; respond(b, n); return; }   /* CRC error */
    switch (c) {
    case 8: b[n++] = r1; b[n++] = 0; b[n++] = 0; b[n++] = (uint8_t)((arg >> 8) & 15); b[n++] = (uint8_t)arg; break;
    case 55: app = 1; b[n++] = r1; break;
    case 41:
        if (!was_app) { b[n++] = r1 | 4; break; }
        if (++acmd41_tries >= 3) idle = 0;            /* busy a while, then ready */
        b[n++] = idle ? 1 : 0; break;
    case 58: b[n++] = r1; b[n++] = (uint8_t)(0x80 | (sdhc ? 0x40 : 0)); b[n++] = 0xFF; b[n++] = 0x80; b[n++] = 0; break;
    case 16: b[n++] = r1; break;
    case 9: {
        b[n++] = r1; b[n++] = 0xFF; b[n++] = 0xFE;
        uint8_t csd[16] = { 0 };
        if (sdhc) { uint32_t cs = (uint32_t)(img_sectors / 1024) - 1; csd[0] = 0x40; csd[7] = (uint8_t)(cs >> 16 & 0x3F); csd[8] = (uint8_t)(cs >> 8); csd[9] = (uint8_t)cs; }
        else {                                         /* v1: READ_BL_LEN 9, C_SIZE_MULT 7: blocks = (C_SIZE+1)*512 */
            uint32_t cs = (uint32_t)(img_sectors / 512) - 1;
            csd[5] = 9; csd[6] = (uint8_t)(cs >> 10 & 3); csd[7] = (uint8_t)(cs >> 2); csd[8] = (uint8_t)((cs & 3) << 6);
            csd[9] = 3; csd[10] = 0x80;
        }
        memcpy(b + n, csd, 16); n += 16; b[n++] = 0; b[n++] = 0;
        break;
    }
    case 17: {
        uint32_t s = sdhc ? arg : arg / 512;
        if (!sdhc && arg % 512) { b[n++] = 0x40; break; }   /* address error */
        b[n++] = r1; b[n++] = 0xFF; b[n++] = 0xFF; b[n++] = 0xFE;
        fseek(img, (long)s * 512, SEEK_SET); if (fread(b + n, 1, 512, img) != 512) { b[n - 1] = 0x08; break; }
        n += 512; b[n++] = 0; b[n++] = 0;
        break;
    }
    case 24: waddr = sdhc ? arg : arg / 512; b[n++] = r1; respond(b, n); st = RESP; nin = -1; return;
    default: b[n++] = r1 | 4; break;                  /* illegal command */
    }
    respond(b, n);
}
uint8_t sd_spi_xfer(uint8_t o)
{
    wire_bytes++;
    if (!cs_low) return 0xFF;
    uint8_t r = 0xFF;
    switch (st) {
    case CMD_IN:
        if (nin == 0 && (o & 0xC0) != 0x40) return 0xFF;
        cmdb[nin++] = o;
        if (nin == 6) { nin = 0; command(); }
        return 0xFF;
    case RESP:
        r = pos < nout ? out[pos++] : 0xFF;
        if (pos >= nout) { if (nin == -1) { nin = 0; st = WAIT_TOKEN; } else st = CMD_IN; }
        return r;
    case WAIT_TOKEN:
        if (o == 0xFE) { st = DATA_IN; pos = 0; }
        else if ((o & 0xC0) == 0x40) { st = CMD_IN; cmdb[0] = o; nin = 1; }
        return 0xFF;
    case DATA_IN:
        blk[pos++] = o;
        if (pos == 514) {
            fseek(img, (long)waddr * 512, SEEK_SET); fwrite(blk, 1, 512, img);
            out[0] = 0xE5; out[1] = 0x00; out[2] = 0x00; nout = 3; pos = 0; st = DATA_RESP;   /* accepted, then busy x2 */
        }
        return 0xFF;
    case DATA_RESP:
        r = pos < nout ? out[pos++] : 0xFF;
        if (pos >= nout) st = CMD_IN;
        return r;
    }
    return 0xFF;
}
void sd_spi_cs(int low) { if (low != cs_low && !low && (st == CMD_IN || st == RESP)) { st = CMD_IN; nin = 0; } cs_low = low; }
void sd_spi_speed(int f) { fast = f; }
uint32_t sd_ms(void) { return now_ms++; }
int sd_present(void) { return 1; }

extern unsigned long sd_stats_cmds, sd_stats_errs;
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    int fails = 0;
    for (int type = 0; type < 2; type++) {
        sdhc = type == 0; idle = 1; acmd41_tries = 0; st = CMD_IN; nin = 0;
        img = fopen(argv[1], "r+b"); fseek(img, 0, SEEK_END); img_sectors = ftell(img) / 512;
        int ok = st_init();
        printf("%s: init %s, fast clock %s, %lu commands\n", sdhc ? "SDHC" : "SDSC", ok ? "OK" : "FAILED", fast ? "on" : "off", sd_stats_cmds);
        fsent_t e; int n = 0;
        for (int i = 0; ok && st_list("/", i, &e); i++) n++;
        printf("  root: %d entries\n", n);
        static uint8_t ref[D64_BYTES], gref[D64_GCR_BYTES], g[D64_GCR_BYTES], sec[21 * 256];
        static disk_t dr, dk; static syncs_t fr[D64_TRACKS], fk[D64_TRACKS];
        FILE *f = fopen(argv[2], "rb"); if (!f || fread(ref, 1, D64_BYTES, f) != D64_BYTES) return 2; fclose(f);
        d64_to_disk(ref, gref, &dr, fr);
        int lok = ok && st_load_d64(argv[3], g, &dk, fk, sec) && !memcmp(g, gref, D64_GCR_BYTES);
        int w = lok ? st_write_track(argv[3], &dk, 18, sec) : -1;
        static uint8_t back[D64_BYTES];
        long got = st_read(argv[3], back, D64_BYTES);
        int same = got == D64_BYTES && !memcmp(back, ref, D64_BYTES);
        printf("  D64 over SPI: stream mount %s; track 18 written back (%d sectors); file %s; %lu wire bytes, %lu errors\n",
               lok ? "= d64_to_disk" : "WRONG", w, same ? "unchanged" : "CHANGED", wire_bytes, sd_stats_errs);
        fails += !ok || n < 1 || !lok || w != 19 || !same || sd_stats_errs;
        st_unmount(); fclose(img);
    }
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
