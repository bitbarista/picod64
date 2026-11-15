/* -------------------------------------------------------------------------
 * PicoD64 -- the SD card, through FatFs (fw/fatfs, ChaN, BSD-1 [PROVENANCE]).
 * Portable: the same code on the Pico (fw/diskio_sd.c, SPI) and on the host
 * (fw/diskio_host.c, a FAT image file). Core 0 only: FatFs is not re-entrant.
 *
 * Paths are absolute, "/" separated, as core/browse.h uses them.
 * ------------------------------------------------------------------------- */
#ifndef FF_STORAGE_H
#define FF_STORAGE_H
#include <stdint.h>
#include "../core/browse.h"
#include "../core/disk.h"

int  st_init(void);                                  /* mount the card; 1 if OK */
void st_unmount(void);
int  st_ready(void);
long st_prep(const char *path, const char *filter);   /* the folder cache, through a filter: entries, | 1<<24 if cut short */
void st_forget(void);                                /* drop the folder cache (card changed) */

/* the idx-th entry of a folder: folders first, then files, case-folded order;
 * hidden entries (".", names starting with '.') left out */
int  st_list(const char *path, int idx, fsent_t *e);
int  st_find(const char *path, const char *name);    /* its number in the sorted list, or -1: without reading the folder through */
int  st_count(const char *path);                     /* entries in the cache */
long st_read(const char *path, uint8_t *buf, long max);
int  st_write(const char *path, const uint8_t *buf, long len);   /* create/replace */
int  st_new_d64(const char *path, const uint8_t *bam, const uint8_t *dir);   /* 1 made, -1 exists, 0 failed */
int  st_mkdir(const char *path);                     /* 1 if it exists afterwards */

/* Mount: stream a D64 from the card into GCR (gcr: D64_GCR_BYTES) and frame it
 * (pool: D64_TRACKS framings). sec: a scratch buffer of 21 * 256. 1 if OK. */
int  st_load_d64(const char *path, uint8_t *gcr, disk_t *dk, syncs_t *pool, uint8_t *sec);
/* Write track t of a mounted disk back: the sectors that decode are written
 * over the file's, the rest left alone. Returns the sectors written, -1 on error. */
int  st_write_track(const char *path, const disk_t *dk, int t, uint8_t *sec);
int  st_tracks(void);                                /* tracks of the mounted disk: 35 to 42 */
/* the head is on a track past what's in RAM: bring it in; 1 if it came, then tell the drive (drive_retrack) */
int  st_track_poll(const char *path, disk_t *dk, volatile uint8_t *dirty, volatile const uint8_t *head, uint8_t *sec);
#endif
