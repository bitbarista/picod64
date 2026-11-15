/* PicoD64 -- FatFs's disk layer on the host: a FAT image file as the SD card
 * (made by mkfs.vfat / mkfs.exfat). diskio_host_open() before st_init(). */
#include <stdio.h>
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

static FILE *img;
static LBA_t sectors;
unsigned long diskio_host_reads, diskio_host_writes;

int diskio_host_open(const char *path)
{
    if (img) fclose(img);
    img = fopen(path, "r+b");
    if (!img) return 0;
    fseek(img, 0, SEEK_END);
    sectors = (LBA_t)(ftell(img) / 512);
    return 1;
}
void diskio_host_close(void) { if (img) fclose(img); img = 0; }

DSTATUS disk_status(BYTE pdrv) { return (pdrv || !img) ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE pdrv) { return disk_status(pdrv); }

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !img) return RES_NOTRDY;
    if (fseek(img, (long)sector * 512, SEEK_SET) || fread(buff, 512, count, img) != count) return RES_ERROR;
    diskio_host_reads += count;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv || !img) return RES_NOTRDY;
    if (fseek(img, (long)sector * 512, SEEK_SET) || fwrite(buff, 512, count, img) != count) return RES_ERROR;
    diskio_host_writes += count;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv || !img) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: fflush(img); return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = sectors; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    }
    return RES_PARERR;
}
