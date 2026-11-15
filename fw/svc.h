#ifndef FF_SVC_H
#define FF_SVC_H
#include "../core/disk.h"
#define SVC_DIR     "/.picod64"
#define SVC_VIRTUAL "/.picod64/virtual.d64"
int svc_load(const char *path, uint8_t *gcr, disk_t *dk, syncs_t *fr);
int svc_virtual(const char *dir, const char *first, uint8_t *gcr, disk_t *dk, syncs_t *fr, uint8_t *filebuf, long fbmax);
int svc_newdisk(const char *path, const uint8_t *name, int nl, uint8_t id1, uint8_t id2);
void svc_set_builtin(const uint8_t *p, long n);    /* the browser, for svc_virtual at the root */
int svc_flush(const char *path, const disk_t *dk, volatile uint8_t *dirty);
int svc_sound_get(void);                           /* the drive sounds, in /.picod64.cfg: core 0 only */
int svc_sound_set(int on);
int svc_track_poll(const char *path, disk_t *dk, volatile uint8_t *dirty, volatile const uint8_t *head);
#endif
