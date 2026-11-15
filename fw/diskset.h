#ifndef FF_DISKSET_H
#define FF_DISKSET_H
#include "../core/browse.h"
#define DS_NAME BR_NAME
int ds_token(const char *name, char *key, int keymax, int *val);
/* the D64 after cur in folder dir: 1 and its name in out (pos/count: where it
 * is in its set, 1-based, and the set's size; may be null) */
int ds_next(const char *dir, const char *cur, char *out, int outmax,
            int (*list)(const char *path, int idx, fsent_t *e), int *pos, int *count);
#endif
