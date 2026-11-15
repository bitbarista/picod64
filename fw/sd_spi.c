/* -------------------------------------------------------------------------
 * PicoD64 -- an SD card in SPI mode, as FatFs's disk (fw/fatfs/diskio.h).
 * The protocol of the SD Physical Layer Simplified Specification, section 7
 * ("SPI Mode") [S16, PROVENANCE; ⚠ written before the copy was in hand: every
 * step below is [A] until checked against it, item by item, in FIRMWARE.md].
 *
 *   power-up: >= 74 clocks with CS high, at 100-400 kHz          [A §6.4.1]
 *   CMD0 (CRC 0x95) -> R1 0x01, idle                               [A §7.2.1]
 *   CMD8 0x1AA (CRC 0x87) -> R7: v2 card, echo 0xAA; or illegal: v1  [A §7.2.1]
 *   ACMD41 (CMD55, CMD41 HCS=1 for v2) until R1 = 0x00, <= 1 s       [A §7.2.1]
 *   CMD58 -> OCR: CCS (bit 30) = block addressing (SDHC/SDXC)        [A §7.2.1]
 *   CMD16 512 for byte-addressed cards; then the fast clock (25 MHz)  [A]
 *   read:  CMD17, wait for token 0xFE, 512 bytes, 2 CRC bytes        [A §7.3.3]
 *   write: CMD24, one 0xFF, token 0xFE, 512 bytes, 2 CRC, data response
 *          xxx0_0101 = accepted, then busy (MISO low) until done      [A §7.3.3]
 * CRCs are not checked in SPI mode after CMD8 (CRC off by default)    [A §7.2.2]
 *
 * The platform gives the SPI: sd_spi_xfer, sd_spi_cs, sd_spi_speed, sd_ms
 * (fw/pico_main.c on the board; host/fw_sd_test.c: a simulated card).
 * ------------------------------------------------------------------------- */
#include <stdint.h>
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

uint8_t  sd_spi_xfer(uint8_t out);
void     sd_spi_cs(int low);
void     sd_spi_speed(int fast);
uint32_t sd_ms(void);
int      sd_present(void);                  /* the socket's card-detect switch */

static int sd_ready, sd_block;              /* initialised; block addressing (SDHC/XC) */
static uint32_t sd_sectors;
unsigned long sd_stats_cmds, sd_stats_errs;

static uint8_t sd_wait_ready(uint32_t ms)  /* the card holds MISO low while busy */
{
    uint32_t t0 = sd_ms();
    uint8_t r;
    do { r = sd_spi_xfer(0xFF); if (r == 0xFF) return 1; } while (sd_ms() - t0 < ms);
    return 0;
}

static void sd_deselect(void) { sd_spi_cs(0); sd_spi_xfer(0xFF); }   /* one more clock to release MISO [A] */
static int sd_select(void) { sd_spi_cs(1); sd_spi_xfer(0xFF); if (sd_wait_ready(500)) return 1; sd_deselect(); return 0; }

/* a command; returns R1 (0x80 bit set: no response) */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg)
{
    if (cmd & 0x80) {                        /* ACMDn = CMD55 + CMDn */
        cmd &= 0x7F;
        uint8_t r = sd_cmd(55, 0);
        if (r > 1) return r;
    }
    sd_stats_cmds++;
    if (cmd != 12) { sd_deselect(); if (!sd_select()) return 0xFF; }
    uint8_t crc = cmd == 0 ? 0x95 : cmd == 8 ? 0x87 : 0x01;   /* only these two are checked [A] */
    sd_spi_xfer((uint8_t)(0x40 | cmd));
    sd_spi_xfer((uint8_t)(arg >> 24)); sd_spi_xfer((uint8_t)(arg >> 16));
    sd_spi_xfer((uint8_t)(arg >> 8)); sd_spi_xfer((uint8_t)arg);
    sd_spi_xfer(crc);
    if (cmd == 12) sd_spi_xfer(0xFF);        /* a stuff byte after STOP_TRANSMISSION [A] */
    uint8_t r;
    for (int i = 0; i < 10; i++) { r = sd_spi_xfer(0xFF); if (!(r & 0x80)) return r; }   /* NCR <= 8 [A] */
    return r;
}

static int sd_init(void)
{
    sd_ready = 0;
    if (!sd_present()) return 0;
    sd_spi_speed(0);
    sd_spi_cs(0);
    for (int i = 0; i < 10; i++) sd_spi_xfer(0xFF);          /* 80 clocks, CS high */
    uint8_t ocr[4], r;
    int v2 = 0;
    if (sd_cmd(0, 0) != 1) { sd_deselect(); return 0; }
    if (sd_cmd(8, 0x1AA) == 1) {
        for (int i = 0; i < 4; i++) ocr[i] = sd_spi_xfer(0xFF);
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) { sd_deselect(); return 0; }   /* voltage not accepted */
        v2 = 1;
    }
    uint32_t t0 = sd_ms();
    do r = sd_cmd(0x80 | 41, v2 ? 0x40000000u : 0); while (r == 1 && sd_ms() - t0 < 1000);
    if (r) { sd_deselect(); return 0; }
    sd_block = 0;
    if (v2 && sd_cmd(58, 0) == 0) {
        for (int i = 0; i < 4; i++) ocr[i] = sd_spi_xfer(0xFF);
        sd_block = (ocr[0] & 0x40) != 0;                       /* CCS */
    }
    if (!sd_block && sd_cmd(16, 512) != 0) { sd_deselect(); return 0; }
    /* capacity from the CSD (CMD9) [A §5.3] */
    sd_sectors = 0;
    if (sd_cmd(9, 0) == 0) {
        uint8_t csd[16], tok; uint32_t t1 = sd_ms();
        do tok = sd_spi_xfer(0xFF); while (tok == 0xFF && sd_ms() - t1 < 200);
        if (tok == 0xFE) {
            for (int i = 0; i < 16; i++) csd[i] = sd_spi_xfer(0xFF);
            sd_spi_xfer(0xFF); sd_spi_xfer(0xFF);
            if ((csd[0] >> 6) == 1) {                          /* CSD v2: C_SIZE 22 bits, (C_SIZE+1) * 512 KB */
                uint32_t cs = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
                sd_sectors = (cs + 1) << 10;
            } else {                                           /* CSD v1 */
                uint32_t rbl = csd[5] & 15, cs = ((uint32_t)(csd[6] & 3) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
                uint32_t mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
                sd_sectors = (cs + 1) << (mult + 2 + rbl - 9);
            }
        }
    }
    sd_deselect();
    sd_spi_speed(1);
    sd_ready = 1;
    return 1;
}

static int sd_read_block(uint8_t *buf)
{
    uint8_t tok; uint32_t t0 = sd_ms();
    do tok = sd_spi_xfer(0xFF); while (tok == 0xFF && sd_ms() - t0 < 200);
    if (tok != 0xFE) return 0;
    for (int i = 0; i < 512; i++) buf[i] = sd_spi_xfer(0xFF);
    sd_spi_xfer(0xFF); sd_spi_xfer(0xFF);                      /* CRC, unchecked */
    return 1;
}

static int sd_write_block(const uint8_t *buf, uint8_t token)
{
    if (!sd_wait_ready(500)) return 0;
    sd_spi_xfer(token);
    for (int i = 0; i < 512; i++) sd_spi_xfer(buf[i]);
    sd_spi_xfer(0xFF); sd_spi_xfer(0xFF);
    uint8_t resp = sd_spi_xfer(0xFF);
    return (resp & 0x1F) == 0x05;
}

/* --- FatFs ----------------------------------------------------------------- */
DSTATUS disk_status(BYTE pdrv) { return (pdrv || !sd_ready) ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE pdrv) { if (pdrv) return STA_NOINIT; sd_init(); return disk_status(pdrv); }

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !sd_ready) return RES_NOTRDY;
    uint32_t a = sd_block ? (uint32_t)sector : (uint32_t)sector * 512u;
    int ok = 1;
    for (UINT i = 0; i < count && ok; i++, a += sd_block ? 1 : 512)   /* CMD17 each: simple first [A: CMD18 later] */
        ok = sd_cmd(17, a) == 0 && sd_read_block(buff + i * 512);
    sd_deselect();
    if (!ok) sd_stats_errs++;
    return ok ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !sd_ready) return RES_NOTRDY;
    uint32_t a = sd_block ? (uint32_t)sector : (uint32_t)sector * 512u;
    int ok = 1;
    for (UINT i = 0; i < count && ok; i++, a += sd_block ? 1 : 512)
        ok = sd_cmd(24, a) == 0 && sd_write_block(buff + i * 512, 0xFE);
    if (ok) ok = sd_wait_ready(500);
    sd_deselect();
    if (!ok) sd_stats_errs++;
    return ok ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv || !sd_ready) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: { int ok = sd_select(); sd_deselect(); return ok ? RES_OK : RES_ERROR; }
    case GET_SECTOR_COUNT: *(LBA_t *)buff = sd_sectors; return sd_sectors ? RES_OK : RES_ERROR;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    }
    return RES_PARERR;
}
