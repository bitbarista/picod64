/* PicoD64 -- the SD card through FatFs: see storage.h */
#include <string.h>
#include "storage.h"
#include "fatfs/ff.h"

static FATFS fs;
static int mounted;

int st_init(void)
{
    mounted = f_mount(&fs, "", 1) == FR_OK;
    st_forget();
    return mounted;
}
static int ext_forget(void);
void st_unmount(void) { ext_forget(); f_unmount(""); mounted = 0; st_forget(); }
int st_ready(void) { return mounted; }

/* --- the folder cache ---------------------------------------------------------
 * Real folders run to thousands of long names (one real folder: 1,931 needing 88 KB), and only 30 KB is
 * free. So all of a folder is held only as a sort key and its place in the directory; full names
 * are read from the card a page at a time, in one pass of the directory.
 *   ent:  2,048 x 11 bytes. A key is the first 12 characters at 6 bits each, order kept.
 *   page: 101 names and sizes in a row of the sorted list.
 * Names the same for 12 characters are sorted fully when their page is read: only places within
 * that run change, so every other entry's number stays good. A run longer than a page stays in
 * directory order. Over 2,048 entries: the first 2,048 found, and the listing says so ("cut"). */
#define ST_MAX_ENTRIES 2048
#define ST_KEY         9
#define ST_PAGE        101
#define ST_NOORD       0x7FFF
typedef struct __attribute__((packed)) { uint8_t key[ST_KEY]; uint16_t ord : 15, dir : 1; } st_ent_t;
typedef struct { char name[BR_NAME]; uint32_t size; } st_slot_t;
static char cache_path[BR_PATH];
static int cache_n, cache_valid, cache_cut;
static char cache_filter[20];
static st_ent_t ent[ST_MAX_ENTRIES];
_Static_assert(sizeof(st_ent_t) == 11, "a key of 9 and 2 bytes");
static st_slot_t page[ST_PAGE];
static int page_at, page_n;            /* page[0] is sorted entry page_at; page_n of them */

void st_forget(void) { cache_valid = 0; page_n = 0; }

static int fold(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int name_cmp(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (fold(*a) != fold(*b)) return fold(*a) - fold(*b);
    return fold(*a) - fold(*b);
}
/* the key: $20-$5F as 0-63, small letters as capitals, anything else last; compares with memcmp */
static void make_key(const char *n, uint8_t *k)
{
    uint32_t acc = 0; int bits = 0, o = 0, end = 0;
    for (int i = 0; i < 12; i++) {
        int c = 0;
        if (!end) {
            c = (unsigned char)n[i];
            if (!c) end = 1;
            else {
                if (c >= 'a' && c <= 'z') c -= 32;
                c = (c >= 0x20 && c <= 0x5F) ? c - 0x20 : 63;
            }
        }
        acc = (acc << 6) | (uint32_t)c; bits += 6;
        while (bits >= 8) { bits -= 8; k[o++] = (uint8_t)(acc >> bits); }
    }
}
static int ent_cmp(const st_ent_t *a, const st_ent_t *b)   /* folders first, the key, then directory order */
{
    if (a->dir != b->dir) return (int)b->dir - (int)a->dir;
    int c = memcmp(a->key, b->key, ST_KEY);
    return c ? c : (int)a->ord - (int)b->ord;
}
static int same_run(int i, int j) { return ent[i].dir == ent[j].dir && !memcmp(ent[i].key, ent[j].key, ST_KEY); }

/* A listing's names stop at BR_NAME - 1 characters. 2,350 of the 31,878 are longer, and the end
 * of a name matters (.d64), so a longer one loses its middle (nm_clip): start + ~ + 4 hex + which disk + end. The middle can be all
 * that differs ("(Disk 1 of 2" and "(Disk 2 of 2": 52 on a large test card clip alike without it), so the hex is a hash
 * of the whole name, which tells real_path() which file it means. */
static void clip_name(char *o, const char *n)
{
    size_t l = strlen(n);
    if (l < BR_NAME) { strcpy(o, n); return; }
    uint32_t h = 2166136261u;                        /* FNV-1a, folded to 16 bits */
    for (size_t i = 0; i < l; i++) h = (h ^ (uint8_t)n[i]) * 16777619u;
    h = (h >> 16) ^ (h & 0xFFFF);
    char mark[6] = "~";
    for (int i = 0; i < 4; i++) mark[1 + i] = "0123456789abcdef"[(h >> (12 - 4 * i)) & 15];
    nm_clip(n, (int)l, BR_NAME - 1, mark, o);
}

/* does name contain the filter? (folded; the browser's own filter, on a folder too big for its RAM) */
static int contains(const char *name, const char *f)
{
    if (!f[0]) return 1;
    for (; *name; name++) {
        int k = 0;
        while (f[k] && name[k] && fold(name[k]) == fold(f[k])) k++;
        if (!f[k]) return 1;
    }
    return 0;
}
static int fill(const char *path)
{
    DIR d; FILINFO fi;
    int ord = -1;
    cache_n = 0; cache_cut = 0; page_n = 0;
    if (f_opendir(&d, path) != FR_OK) return 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (++ord >= ST_NOORD) { cache_cut = 1; break; }
        if (fi.fname[0] == '.' || (fi.fattrib & (AM_HID | AM_SYS))) continue;
        if (!contains(fi.fname, cache_filter)) continue;
        if (cache_n == ST_MAX_ENTRIES) { cache_cut = 1; break; }   /* a filter finds the rest */
        make_key(fi.fname, ent[cache_n].key);
        ent[cache_n].ord = (uint16_t)ord & ST_NOORD;
        ent[cache_n].dir = (fi.fattrib & AM_DIR) != 0;
        cache_n++;
    }
    f_closedir(&d);
    for (int gap = cache_n / 2; gap > 0; gap = gap == 2 ? 1 : gap * 5 / 11)   /* Shell sort: no recursion, no spare RAM */
        for (int i = gap; i < cache_n; i++) {
            st_ent_t t = ent[i];
            int j = i;
            for (; j >= gap && ent_cmp(&t, &ent[j - gap]) < 0; j -= gap) ent[j] = ent[j - gap];
            ent[j] = t;
        }
    strncpy(cache_path, path, BR_PATH - 1); cache_path[BR_PATH - 1] = 0;
    cache_valid = 1;
    return 1;
}

/* the page holding sorted entry idx: 1 if read. A whole run of equal keys is kept in one page where it fits */
static int read_page(const char *path, int idx)
{
    DIR d; FILINFO fi;
    static uint16_t by_ord[ST_PAGE];   /* the page's places, in directory order */
    int at = idx;
    while (at > 0 && idx - at < ST_PAGE - 1 && same_run(at - 1, at)) at--;
    if (at > 0 && same_run(at - 1, at)) at = idx;   /* a run longer than a page */
    int n = cache_n - at; if (n > ST_PAGE) n = ST_PAGE;
    if (at + n < cache_n && same_run(at + n - 1, at + n)) {   /* end at the end of a run: it gets sorted whole, once */
        int e = n;
        while (e > 0 && same_run(at + e - 1, at + n)) e--;
        if (e > 0) n = e;
    }
    for (int i = 0; i < n; i++) {
        int j = i;
        for (; j > 0 && ent[at + by_ord[j - 1]].ord > ent[at + i].ord; j--) by_ord[j] = by_ord[j - 1];
        by_ord[j] = (uint16_t)i;
    }
    page_n = 0;
    if (f_opendir(&d, path) != FR_OK) return 0;
    int ord = -1, got = 0, ok = 1;
    while (got < n && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        ord++;
        int place = by_ord[got];
        if (ord != ent[at + place].ord) continue;
        uint8_t k[ST_KEY];
        make_key(fi.fname, k);
        if (memcmp(k, ent[at + place].key, ST_KEY)) { ok = 0; break; }   /* the folder changed under us */
        clip_name(page[place].name, fi.fname);
        page[place].size = fi.fsize > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)fi.fsize;
        got++;
    }
    f_closedir(&d);
    if (!ok || got != n) { cache_valid = 0; return 0; }
    /* the runs wholly in this page, by their full names */
    for (int r = 0; r < n; ) {
        int e = r + 1;
        while (e < n && same_run(at + r, at + e)) e++;
        int whole = (r > 0 || at == 0 || !same_run(at - 1, at)) && (at + e == cache_n || !same_run(at + e - 1, at + e));
        if (whole)
            for (int i = r + 1; i < e; i++) {
                st_slot_t ts = page[i]; st_ent_t te = ent[at + i];
                int j = i;
                for (; j > r && name_cmp(ts.name, page[j - 1].name) < 0; j--) { page[j] = page[j - 1]; ent[at + j] = ent[at + j - 1]; }
                page[j] = ts; ent[at + j] = te;
            }
        r = e;
    }
    page_at = at; page_n = n;
    return 1;
}

long st_prep(const char *path, const char *filter)
{
    if (!mounted) return -1;
    if (strcmp(cache_filter, filter)) { strncpy(cache_filter, filter, sizeof cache_filter - 1); cache_valid = 0; }
    if (!cache_valid || strcmp(cache_path, path)) { if (!fill(path)) return -1; }
    return cache_n | ((long)cache_cut << 24);
}

int st_list(const char *path, int idx, fsent_t *e)
{
    if (!mounted) return 0;
    for (int tries = 0; tries < 2; tries++) {
        if (!cache_valid || strcmp(cache_path, path)) { if (!fill(path)) return 0; }
        if (idx < 0 || idx >= cache_n) return 0;
        if (idx < page_at || idx >= page_at + page_n) { if (!read_page(path, idx)) continue; }
        const st_slot_t *s = &page[idx - page_at];
        strcpy(e->name, s->name);
        e->size = s->size; e->dir = ent[idx].dir;
        return 1;
    }
    return 0;
}

int st_find(const char *path, const char *name)
{
    if (!mounted) return -1;
    if (!cache_valid || strcmp(cache_path, path)) { if (!fill(path)) return -1; }
    st_ent_t want; fsent_t e;
    make_key(name, want.key); want.ord = 0;
    for (int dir = 1; dir >= 0; dir--) {
        want.dir = (uint16_t)dir;
        int lo = 0, hi = cache_n;
        while (lo < hi) { int m = (lo + hi) / 2; if (ent_cmp(&ent[m], &want) < 0) lo = m + 1; else hi = m; }
        for (; lo < cache_n && ent[lo].dir == dir && !memcmp(ent[lo].key, want.key, ST_KEY); lo++) {
            if (!st_list(path, lo, &e)) return -1;
            if (!strcmp(e.name, name)) return lo;
        }
    }
    return -1;
}

int st_count(const char *path)
{
    if (!mounted) return 0;
    if (!cache_valid || strcmp(cache_path, path)) { if (!fill(path)) return 0; }
    return cache_n;
}

/* the real path of one that came from a listing: a clipped name means the file it was clipped from */
static char long_buf[FF_LFN_BUF + BR_PATH + 2];
static const char *real_path(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!strchr(base, '~')) return path;      /* every clipped name has one */
    size_t dl = (size_t)(base - path);
    char dir[BR_PATH], c[BR_NAME];
    if (dl >= sizeof dir) return path;
    memcpy(dir, path, dl); dir[dl] = 0;
    if (dl > 1) dir[dl - 1] = 0;              /* (not the root's /) */
    DIR d; FILINFO fi;
    const char *r = path;
    if (f_opendir(&d, dir) != FR_OK) return path;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        clip_name(c, fi.fname);
        if (!strcmp(c, base)) {
            memcpy(long_buf, path, dl);
            strcpy(long_buf + dl, fi.fname);
            r = long_buf;
            break;
        }
    }
    f_closedir(&d);
    return r;
}

long st_read(const char *path, uint8_t *buf, long max)
{
    FIL f; UINT got = 0;
    if (!mounted || f_open(&f, real_path(path), FA_READ) != FR_OK) return -1;
    FRESULT r = f_read(&f, buf, (UINT)max, &got);
    f_close(&f);
    return r == FR_OK ? (long)got : -1;
}

int st_write(const char *path, const uint8_t *buf, long len)
{
    FIL f; UINT put = 0;
    if (!mounted || f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    FRESULT r = f_write(&f, buf, (UINT)len, &put);
    if (f_close(&f) != FR_OK) r = FR_DISK_ERR;
    st_forget();
    return r == FR_OK && (long)put == len;
}

/* A new D64, without the 175 KB in RAM (there's a disk in the buffer when the knob asks): zeros, and the
 * two sectors of track 18 given. 1 made, -1 it exists, 0 failed, nothing left behind */
int st_new_d64(const char *path, const uint8_t *bam, const uint8_t *dir)
{
    FIL f; UINT put;
    static const uint8_t zero[256];
    if (!mounted) return 0;
    FRESULT r = f_open(&f, path, FA_WRITE | FA_CREATE_NEW);
    if (r == FR_EXIST) return -1;
    if (r != FR_OK) return 0;
    int ok = 1, o18 = d64_offset(18, 0) / 256;
    for (int s = 0; ok && s < D64_SECTORS; s++) {
        const uint8_t *p = s == o18 ? bam : s == o18 + 1 ? dir : zero;
        ok = f_write(&f, p, 256, &put) == FR_OK && put == 256;
    }
    if (f_close(&f) != FR_OK) ok = 0;
    if (!ok) f_unlink(path);
    st_forget();
    return ok;
}

int st_mkdir(const char *path)
{
    FRESULT r = f_mkdir(path);
    st_forget();
    return r == FR_OK || r == FR_EXIST;
}

/* --- D64 <-> GCR, a track at a time ---------------------------------------- */
/* Tracks past 35: a D64 of up to 42 (a 1541's head goes that far; Lykia and others use them). Their
 * 5 x 6,250 bytes are not in RAM. But tracks 31 on are all that size, so each shares a place with one of
 * 31-35: 36 and 41 with 31, 37 and 42 with 32, 38 with 33, 39 with 34, 40 with 35. When the head is on one
 * not in, core 0 writes back the other if changed and reads this one from the card. Until then the
 * drive sees an unformatted track. Disks of 35 tracks never come here. */
#define EXT_FIRST 31
#define EXT_SLOTS 5
static int ext_tracks = D64_TRACKS;
static uint8_t ext_owner[EXT_SLOTS], ext_id1, ext_id2;
static uint8_t *ext_gcr[EXT_SLOTS];
static syncs_t *ext_sync[EXT_SLOTS];
static FIL ext_f;                     /* kept open: opening searches the folder, thousands of names perhaps */
static int ext_open;
static void ext_close(void) { if (ext_open) { f_close(&ext_f); ext_open = 0; } }
int st_tracks(void) { return ext_tracks; }
static int ext_forget(void) { ext_open = 0; ext_tracks = D64_TRACKS; return 0; }

/* (flatten: core 0's own copy, in flash; the build keeps disk_frame_track itself out of flash for core 1) */
__attribute__((flatten, noinline)) static void ext_frame(const uint8_t *g, uint32_t n, syncs_t *r) { disk_frame_track(g, n, r); }

int st_track_poll(const char *path, disk_t *dk, volatile uint8_t *dirty, volatile const uint8_t *head, uint8_t *sec)
{
    uint8_t ht = *head;
    if (ext_tracks <= D64_TRACKS || (ht & 1) || dk->gcr[ht]) return 0;
    int t = ht / 2 + 1;
    if (t < EXT_FIRST || t > ext_tracks) return 0;
    int s = (t - EXT_FIRST) % EXT_SLOTS, o = ext_owner[s], ho = 2 * (o - 1);
    if (dirty[o - 1]) { dirty[o - 1] = 0; st_write_track(path, dk, o, sec); }
    uint16_t oldlen = dk->len[ho];
    dk->gcr[ho] = 0; dk->sync[ho] = 0; dk->len[ho] = 0;
    __sync_synchronize();
    int ok = *head != ho;                 /* (stepped back meanwhile: leave it be) */
    UINT got;
    int n = d64_sectors(t) * 256;
    if (ok && !ext_open) ok = ext_open = f_open(&ext_f, real_path(path), FA_READ) == FR_OK;
    if (ok) ok = f_lseek(&ext_f, (FSIZE_t)d64_offset(t, 0)) == FR_OK && f_read(&ext_f, sec, (UINT)n, &got) == FR_OK && got == (UINT)n;
    if (!ok) {
        if (*head != ho) ext_tracks = D64_TRACKS;   /* the card failed: don't try for ever */
        dk->sync[ho] = ext_sync[s]; dk->len[ho] = oldlen;
        __sync_synchronize();
        dk->gcr[ho] = ext_gcr[s];
        return 0;
    }
    d64_track_sectors(sec, t, ext_id1, ext_id2, ext_gcr[s]);
    ext_frame(ext_gcr[s], (uint32_t)d64_track_bytes(t) * 8u, ext_sync[s]);
    dk->sync[ht] = ext_sync[s]; dk->len[ht] = (uint16_t)d64_track_bytes(t);
    __sync_synchronize();
    dk->gcr[ht] = ext_gcr[s];
    ext_owner[s] = (uint8_t)t;
    return 1;
}

int st_load_d64(const char *path, uint8_t *gcr, disk_t *dk, syncs_t *pool_, uint8_t *sec)
{
    FIL f; UINT got;
    ext_close();
    ext_tracks = D64_TRACKS;
    if (!mounted || f_open(&f, real_path(path), FA_READ) != FR_OK) return 0;
    int ok = f_size(&f) >= D64_BYTES;                 /* 174,848: 35 tracks; error bytes after are ignored [A] */
    int tracks = D64_TRACKS;
    if (ok) tracks += (int)((f_size(&f) - D64_BYTES) / (17 * 256));   /* (a 40-track's error bytes are under a track) */
    if (tracks > 42) tracks = 42;
    uint8_t id1 = 0, id2 = 0;
    if (ok && f_lseek(&f, (FSIZE_t)d64_offset(18, 0)) == FR_OK && f_read(&f, sec, 256, &got) == FR_OK && got == 256) {
        id1 = sec[162]; id2 = sec[163];               /* the BAM's disk ID [S10 p.37], as d64_to_disk */
    } else ok = 0;
    for (int i = 0; i < DISK_HALFTRACKS; i++) { dk->gcr[i] = 0; dk->len[i] = 0; }
    uint8_t *o = gcr;
    for (int t = 1; ok && t <= D64_TRACKS; t++) {
        UINT n = (UINT)(d64_sectors(t) * 256);
        if (f_lseek(&f, (FSIZE_t)d64_offset(t, 0)) != FR_OK || f_read(&f, sec, n, &got) != FR_OK || got != n) { ok = 0; break; }
        d64_track_sectors(sec, t, id1, id2, o);
        dk->gcr[2 * (t - 1)] = o; dk->len[2 * (t - 1)] = (uint16_t)d64_track_bytes(t);
        o += d64_track_bytes(t);
    }
    f_close(&f);
    if (ok) disk_frame(dk, pool_, D64_TRACKS);
    if (ok && tracks > D64_TRACKS) {
        for (int i = 0; i < EXT_SLOTS; i++) {
            int h = 2 * (EXT_FIRST + i - 1);
            ext_owner[i] = (uint8_t)(EXT_FIRST + i); ext_gcr[i] = dk->gcr[h]; ext_sync[i] = dk->sync[h];
        }
        ext_id1 = id1; ext_id2 = id2;
        ext_tracks = tracks;
    }
    return ok;
}

int st_write_track(const char *path, const disk_t *dk, int t, uint8_t *sec)
{
    FIL f; UINT put;
    uint32_t m = disk_track_to_sectors(dk, t, sec);
    if (!m) return 0;
    ext_close();                          /* (its buffer would go stale) */
    if (!mounted || f_open(&f, real_path(path), FA_WRITE | FA_OPEN_EXISTING) != FR_OK) return -1;
    int n = 0;
    for (int s = 0; s < d64_sectors(t); s++) {
        if (!(m & (1u << s))) continue;
        if (f_lseek(&f, (FSIZE_t)d64_offset(t, s)) != FR_OK || f_write(&f, sec + s * 256, 256, &put) != FR_OK || put != 256) { n = -1; break; }
        n++;
    }
    if (f_close(&f) != FR_OK) n = -1;
    return n;
}
