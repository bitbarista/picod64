/* -------------------------------------------------------------------------
 * PicoD64 -- the card services core 0 runs for core 1 (fw/bus.h's st_*_
 * hooks). Portable: the host twin calls them directly.
 * ------------------------------------------------------------------------- */
#include <string.h>
#include "svc.h"
#include "storage.h"
#include "../core/d64make.h"

static uint8_t sec[21 * 256];

int svc_load(const char *path, uint8_t *gcr, disk_t *dk, syncs_t *fr)
{
    return st_load_d64(path, gcr, dk, fr, sec);
}

static int is_prg(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'p' && (n[l - 2] | 32) == 'r' && (n[l - 1] | 32) == 'g';
}

/* A program for the virtual D64: NAME.PRG, or a name with no extension at all, the usual C64 way
 * (Dir Browser's own files are "db64", "tdb64", and its readme says to drop ".prg"). Browse mode
 * serves both as PRG; the virtual disk left the second out, so after its fast loader took over,
 * Dir Browser's "tdb64" got FILE NOT FOUND for its main program (2026-11-02, host/fwtwin). */
static int is_program(const char *n)
{
    return is_prg(n) || !strchr(n, '.');
}

/* The PRGs of folder dir as a D64 (first: the one just served, first in the
 * directory), written to SVC_VIRTUAL and mounted from there. The D64 is built
 * in gcr's memory (174,848 of its 252,004 bytes); the file read back over it.
 * filebuf: a scratch buffer for one PRG (up to 64 KB + 2). */
static const uint8_t *svc_builtin; static long svc_builtin_len;
void svc_set_builtin(const uint8_t *p, long n) { svc_builtin = p; svc_builtin_len = n; }

int svc_virtual(const char *dir, const char *first, uint8_t *gcr, disk_t *dk, syncs_t *fr, uint8_t *filebuf, long fbmax)
{
    uint8_t *d64 = gcr;
    uint8_t nm[16]; int nl = 0;
    const char *fn = strrchr(dir, '/'); fn = (fn && fn[1]) ? fn + 1 : "picod64";
    for (; fn[nl] && nl < 16; nl++) nm[nl] = br_petscii(fn[nl]);
    d64_blank(d64, nm, nl, 'F', 'F');
    fsent_t e; int added = 0;
    /* At the root, the built-in browser too. A fast-load cartridge turns LOAD"*",8 into drive code before
     * anything is served, and this disk is then all there is: without it, no browser. First unless a file
     * was just served. When it runs, its CD// comes back to browsing. */
    int root = dir[0] == '/' && !dir[1];
    if (root && svc_builtin && !first[0]) {
        static const uint8_t bn[7] = { 'B', 'R', 'O', 'W', 'S', 'E', 'R' };
        if (d64_add_prg(d64, bn, 7, svc_builtin, svc_builtin_len)) added++;
    }
    for (int pass = 0; pass < 2; pass++)            /* the file just served first, then the rest */
        for (int i = 0; st_list(dir, i, &e); i++) {
            if (e.dir || !is_program(e.name) || (pass == 0) != !strcmp(e.name, first)) continue;
            char full[BR_PATH + BR_NAME + 2];
            size_t dl = strlen(dir);
            memcpy(full, dir, dl);
            if (dl == 0 || full[dl - 1] != '/') full[dl++] = '/';
            strcpy(full + dl, e.name);
            long len = st_read(full, filebuf, fbmax);
            uint8_t pn[16]; int pl = 0;
            for (; e.name[pl] && pl < 16; pl++) pn[pl] = br_petscii(e.name[pl]);
            if (len > 0 && d64_add_prg(d64, pn, pl, filebuf, len)) added++;
        }
    st_mkdir(SVC_DIR);
    if (!st_write(SVC_VIRTUAL, d64, D64_BYTES)) return 0;
    return added > 0 && st_load_d64(SVC_VIRTUAL, gcr, dk, fr, sec);
}

/* the written tracks of the mounted disk back to its file; dirty[] cleared as done */
/* a new, formatted D64: what d64_blank makes (host/fwtwin.sh compares), without the whole of it in RAM */
int svc_newdisk(const char *path, const uint8_t *name, int nl, uint8_t id1, uint8_t id2)
{
    uint8_t *b = sec, *dir = sec + 256;
    memset(sec, 0, 512);
    b[0] = 18; b[1] = 1; b[2] = 0x41;
    for (int t = 1; t <= D64_TRACKS; t++) {
        int n = d64_sectors(t);
        b[4 * t] = (uint8_t)n;
        for (int s = 0; s < n; s++) b[4 * t + 1 + s / 8] |= (uint8_t)(1 << (s & 7));
    }
    b[4 * 18] -= 2;
    b[4 * 18 + 1] &= (uint8_t)~3;                    /* 18/0 and 18/1 in use */
    memset(b + 144, 0xA0, 27);
    for (int i = 0; i < 16 && i < nl; i++) b[144 + i] = name[i];
    b[162] = id1; b[163] = id2; b[165] = '2'; b[166] = 'A';
    dir[0] = 0; dir[1] = 0xFF;
    return st_new_d64(path, b, dir);
}

int svc_track_poll(const char *path, disk_t *dk, volatile uint8_t *dirty, volatile const uint8_t *head)
{
    return st_track_poll(path, dk, dirty, head, sec);
}

int svc_flush(const char *path, const disk_t *dk, volatile uint8_t *dirty)
{
    int n = 0;
    for (int t = 1; t <= 42; t++) {
        if (!dirty[t - 1]) continue;
        dirty[t - 1] = 0;
        if (path[0]) { int r = st_write_track(path, dk, t, sec); if (r > 0) n += r; }
    }
    return n;
}

/* The drive sounds' setting, on core 0: the second character of the settings file's first line ("1" on,
 * "10" off), the rest of the file left alone. core/browse.h reads and writes the same file for the browser. */
#define SVC_CFG "/.picod64.cfg"
int svc_sound_get(void)
{
    uint8_t t[2];
    return !(st_read(SVC_CFG, t, 2) == 2 && t[1] == '0');
}
int svc_sound_set(int on)
{
    uint8_t t[BR_PATH + BR_NAME + 8];
    long n = st_read(SVC_CFG, t, (long)sizeof t - 2);
    if (n < 1) { memcpy(t, "1\n\n\n", 4); n = 4; }
    uint8_t *e = memchr(t, '\n', (size_t)n);
    if (!e) { t[n] = '\n'; e = t + n++; }
    long k = e - t, want = on ? 1 : 2;
    memmove(t + want, e, (size_t)(n - k));
    if (!on) t[1] = '0';
    return st_write(SVC_CFG, t, n + want - k);
}

