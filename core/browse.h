/* -------------------------------------------------------------------------
 * PicoD64 browse mode (F6): a small DOS over the SD card, behind the IEC
 * device engine (core/iec_dev.h). Not the 1541 DOS: the Pico answers itself.
 *
 * What it does, after sd2iec's README [S15] (its documentation only,
 * 2026-10-29; its code unread):
 *   LOAD"$"      the current folder as a 1541-style listing (32-byte lines, as the
 *                1541's own: FB64 relies on it); folders as DIR,
 *                every file as PRG with its extension (sd2iec, extension
 *                hiding off), blocks of 254 bytes [S15]
 *   LOAD"name"   a file, byte for byte (a .PRG keeps its load address)
 *   LOAD"*"      at the card's root: the browser built into the firmware, if there is one
 *                (also listed there, as BROWSER, and loaded by that name unless the card
 *                has a file of that name: the card comes first)
 *   CD:name / CD/name   into a folder;  CD:_ / CD_   up (_ = PETSCII left
 *                arrow);  CD//   to the root;  CD:image.d64   MOUNT it: the
 *                emulated 1541 takes over the bus [S15: "change into them as
 *                if they were a directory"]
 *   XL:name      (the built-in browser's turbo) read a file into the buffer; the error channel
 *                answers 00,TURBO,aaaa,llll: its load address and length after it, in hex.
 *   XG           the transfer starts when ATN ends (fw/bus.h bus_turbo)
 *                Not on a 1541: the browser sees any other answer and does a plain LOAD.
 *   the error channel: "00, OK,00,00" or the last error (62 FILE NOT FOUND,
 *                31 SYNTAX ERROR, 39 "PATH NOT FOUND" [A: our choice of code])
 * Names: SD names are ASCII; the C64 sees them in PETSCII (a-z -> $41-$5A,
 * A-Z -> $C1-$DA, as sd2iec does [A: inferred, the README does not say]),
 * truncated to 16 [A: sd2iec uses the 8.3 name instead]. Matching folds case
 * and takes * (the rest) and ? (one character).
 *
 * The platform supplies the SD card through fs_ops_t and acts on mount().
 * ------------------------------------------------------------------------- */
#ifndef FF_BROWSE_H
#define FF_BROWSE_H
#ifndef FF_C1
#define FF_C1          /* core 1 code: the board puts it in SRAM */
#endif
#include <stdint.h>
#include <string.h>
#include "iec_dev.h"

#define BR_PATH 256
#define BR_NAME 64

typedef struct { char name[BR_NAME]; uint32_t size; uint8_t dir; } fsent_t;
typedef struct {
    /* the idx-th entry of folder path (sorted); 0 when there are no more */
    int (*list)(void *ctx, const char *path, int idx, fsent_t *e);
    /* read a whole file into buf; its length, or -1 */
    long (*read)(void *ctx, const char *path, uint8_t *buf, long max);
    /* mount a D64: the platform switches the bus to the emulated 1541 after the
     * current command; 0 if it cannot */
    int (*mount)(void *ctx, const char *path);
    /* M-R: a byte of the 1541's memory (the booted drive's RAM, the DOS ROM) */
    uint8_t (*peek)(void *ctx, uint16_t addr);
    /* M-W: a program is sending drive code (a fast loader). The platform hands the
     * bus to the emulated 1541 with these bytes in its RAM and, as its disk, a
     * virtual D64 of the PRGs of folder `path` (`first` first: the one just loaded);
     * 0 if it cannot (2026-10-29: serve PRGs here; go cycle-exact only when
     * drive code arrives) */
    int (*drivecode)(void *ctx, const char *path, const char *first, uint16_t addr, const uint8_t *data, int n);
    /* SAVE: write a whole file (create or replace); 1 if OK. May be null: no saving */
    int (*write)(void *ctx, const char *path, const uint8_t *buf, long len);
    /* CD:name.prg: mount the virtual D64 of folder path's PRGs with that one first (FB64
     * Turbo "changes into" a PRG, then its fast loader loads the disk's first file; seen on
     * the bus, host/fwtwin.c, 2026-10-30). May be null */
    int (*mount_prg)(void *ctx, const char *path, const char *first);
    /* the browser's listing: the folder's cache through a filter: entries, | 1<<24 if cut short. May be null */
    long (*prep)(void *ctx, const char *path, const char *filter);
    /* an entry's number in the listing, or -1, not going through the folder. May be null */
    int (*find)(void *ctx, const char *path, const char *name);
    /* a new, formatted D64 at path: 1 made, -1 it exists, 0 it couldn't. May be null */
    int (*newdisk)(void *ctx, const char *path, const uint8_t *name, int nl, uint8_t id1, uint8_t id2);
} fs_ops_t;

typedef struct {
    const fs_ops_t *fs; void *ctx;
    char cwd[BR_PATH];
    uint8_t name[48]; int nlen;          /* name / command being received */
    uint8_t *buf; long bmax, blen, bpos; int bsa;   /* the one data channel's bytes */
    uint8_t err[48]; int elen, epos;     /* the error channel: text, or M-R's bytes */
    char last[BR_NAME];                  /* the last file served (for drivecode) */
    uint8_t got_cmd;
    int wsa; char wpath[BR_PATH];        /* a file being SAVEd: its channel (-1: none) and card path */
    const uint8_t *builtin; long builtin_len;   /* the built-in browser, or null */
    const uint8_t *boot; long boot_len;         /* its bootstrap: what LOAD"*" gets (fw/browser/boot.asm) */
    long turbo_len; uint8_t turbo_go;           /* XL: the file's bytes in buf; XG: send them */
    uint8_t turbo_remount;                      /* XM: mount img again when the transfer ends */
    char img[BR_PATH];                          /* the D64 last mounted */
    char sel[BR_NAME];                          /* the entry the browser's cursor should come back to */
    char alias[BR_PATH];                        /* XA: what LOAD"#" reads */
    uint8_t cfg_read, cfg_turbo, cfg_sound;     /* the saved settings */
} browse_t;

/* --- PETSCII ------------------------------------------------------------ */
static FF_C1 inline uint8_t br_petscii(char c)
{
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 0x41);
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 0xC1);
    return (uint8_t)c;
}
static FF_C1 inline uint8_t br_fold(uint8_t p) { return (p >= 0xC1 && p <= 0xDA) ? (uint8_t)(p - 0x80) : p; }

/* does PETSCII pattern pat[0..pl) match SD name nm? */
static FF_C1 int br_match_n(const uint8_t *pat, int pl, const char *nm, int nl)
{
    int i = 0, j = 0;
    if (nl > 16) nl = 16;
    for (; i < pl; i++, j++) {
        if (pat[i] == '*') return 1;
        if (j >= nl) return 0;
        if (pat[i] != '?' && br_fold(pat[i]) != br_fold(br_petscii(nm[j]))) return 0;
    }
    return j == nl;
}
static FF_C1 int br_match(const uint8_t *pat, int pl, const char *nm) { return br_match_n(pat, pl, nm, (int)strlen(nm)); }

static FF_C1 void br_error(browse_t *b, int code, const char *msg)
{
    int n = 0;
    b->err[n++] = (uint8_t)('0' + code / 10); b->err[n++] = (uint8_t)('0' + code % 10); b->err[n++] = ',';
    while (*msg && n < 30) b->err[n++] = (uint8_t)*msg++;
    memcpy(b->err + n, ",00,00\r", 7);
    b->elen = n + 7; b->epos = 0;
}

static FF_C1 int br_is_d64(const char *nm)
{
    int l = (int)strlen(nm);
    return l > 4 && nm[l - 4] == '.' && (nm[l - 3] | 32) == 'd' && nm[l - 2] == '6' && nm[l - 1] == '4';
}

static FF_C1 int br_is_prg(const char *nm)
{
    int l = (int)strlen(nm);
    return l > 4 && nm[l - 4] == '.' && (nm[l - 3] | 32) == 'p' && (nm[l - 2] | 32) == 'r' && (nm[l - 1] | 32) == 'g';
}

/* Where a long name says which disk it is: "(Disk 1 of 2 Side A)", "Side B", "disk 2". 1 if it does:
 * n[*s..*e) is that part. A name is cut in the middle to fit, and this is what must survive */
static FF_C1 int nm_alnum(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static FF_C1 int nm_word(const char *n, int l, int i)
{
    if (i + 4 >= l) return 0;
    int a = n[i] | 32, b = n[i + 1] | 32, c = n[i + 2] | 32, d = n[i + 3] | 32;
    if (!((a == 'd' && b == 'i' && c == 's' && (d == 'k' || d == 'c')) || (a == 's' && b == 'i' && c == 'd' && d == 'e'))) return 0;
    if (i > 0 && nm_alnum(n[i - 1])) return 0;
    int x = n[i + 4];
    return x == ' ' || x == '_' || x == '-' || (x >= '0' && x <= '9');
}
static FF_C1 int nm_token(const char *n, int l, int *s, int *e)
{
    for (int i = 0; i < l; i++) {
        if (!nm_word(n, l, i)) continue;
        if (i > 0 && n[i - 1] == '(') {
            int k = i;
            while (k < l && n[k] != ')') k++;
            if (k >= l || k - i > 24) continue;
            *s = i - 1; *e = k + 1;
            if (*e < l && n[*e] == '(' && nm_word(n, l, *e + 1)) {   /* "(Disk 1)(Side A)" */
                int m = *e + 1;
                while (m < l && n[m] != ')') m++;
                if (m < l && m - i <= 24) *e = m + 1;
            }
        } else {
            int k = i + 4;
            if (n[k] == ' ' || n[k] == '_' || n[k] == '-') k++;
            while (k < l && nm_alnum(n[k])) k++;
            *s = i; *e = k;
        }
        return 1;
    }
    return 0;
}
/* n, l long, into o in max characters: start, mark, which disk if it says, end. A hash goes after
 * the disk, so that names sorted by this form keep Side A before Side B */
static FF_C1 void nm_clip(const char *n, int l, int max, const char *mark, char *o)
{
    int ml = (int)strlen(mark), s, e, head = 30, tail = l - (max - ml - 30), ts = 0, te = 0;
    if (nm_token(n, l, &s, &e)) {
        int t = e <= l - 4 ? l - 4 : e;              /* then the extension */
        int h = max - ml - (e - s) - (l - t);
        if (h > s) h = s;
        if (h >= 8) { head = h; ts = s; te = e; tail = t; }
    }
    memcpy(o, n, (size_t)head); o += head;
    if (ml == 1 || te == ts) { memcpy(o, mark, (size_t)ml); o += ml; }
    memcpy(o, n + ts, (size_t)(te - ts)); o += te - ts;
    if (ml > 1 && te > ts) { memcpy(o, mark, (size_t)ml); o += ml; }
    memcpy(o, n + tail, (size_t)(l - tail)); o += l - tail;
    *o = 0;
}

/* find the entry of the current folder matching pat; its index or -1 */
static FF_C1 int br_find(browse_t *b, const uint8_t *pat, int pl, fsent_t *e)
{
    for (int i = 0; b->fs->list(b->ctx, b->cwd, i, e); i++) {
        if (pl < 0) {                                /* an ASCII card name, case folded */
            const char *x = (const char *)pat, *y = e->name;
            while (*x && *y && (*x | 32) == (*y | 32)) { x++; y++; }
            if (!*x && !*y) return i;
        } else if (br_match(pat, pl, e->name)) return i;
    }
    /* SAVE"NAME" makes name.prg: LOAD"NAME" and VERIFY"NAME" find it again, as on a 1541 (2026-11-14). Only when
     * nothing matches the name as it is, so a file called NAME still wins */
    if (pl >= 0)
        for (int i = 0; b->fs->list(b->ctx, b->cwd, i, e); i++)
            if (!e->dir && br_is_prg(e->name) && br_match_n(pat, pl, e->name, (int)strlen(e->name) - 4)) return i;
    return -1;
}

static FF_C1 void br_join(browse_t *b, char *out, const char *nm)
{
    size_t l = strlen(b->cwd);
    memcpy(out, b->cwd, l + 1);
    if (l > 1) out[l++] = '/';
    else if (l == 0) out[l++] = '/';
    strncpy(out + l, nm, BR_PATH - l - 1); out[BR_PATH - 1] = 0;
}

/* --- the listing: a BASIC program at $0401, 1541 style ------------------- */
static FF_C1 void br_put(browse_t *b, uint8_t c) { if (b->blen < b->bmax) b->buf[b->blen++] = c; }
static FF_C1 void br_line(browse_t *b, unsigned num, const uint8_t *text, int n)
{
    br_put(b, 1); br_put(b, 1);                       /* link: rebuilt by the KERNAL's LOAD */
    br_put(b, (uint8_t)num); br_put(b, (uint8_t)(num >> 8));
    for (int i = 0; i < n; i++) br_put(b, text[i]);
    br_put(b, 0);
}

static FF_C1 void br_listing(browse_t *b)
{
    uint8_t t[40]; int n;
    fsent_t e;
    b->blen = 0; b->bpos = 0;
    br_put(b, 0x01); br_put(b, 0x04);                 /* load address $0401 */
    /* header: the folder's name, ID "FF", "2A" as a 1541's [the DOS's own format, F4c] */
    const char *fn = strrchr(b->cwd, '/');
    fn = (fn && fn[1]) ? fn + 1 : "picod64";        /* (lower case: unshifted PETSCII, reads as capitals) */
    n = 0; t[n++] = 0x12; t[n++] = '"';
    for (int i = 0; i < 16; i++) t[n++] = fn[i] && i < (int)strlen(fn) ? br_petscii(fn[i]) : ' ';
    t[n++] = '"'; t[n++] = ' '; t[n++] = 'F'; t[n++] = 'F'; t[n++] = ' '; t[n++] = '2'; t[n++] = 'A';
    br_line(b, 0, t, n);
    if (b->builtin && !strcmp(b->cwd, "/")) {           /* the built-in browser, first, at the root */
        static const uint8_t bl[] = "  \"BROWSER\"          PRG      ";
        br_line(b, (unsigned)((b->builtin_len + 253) / 254), bl, 27);
    }
    for (int i = 0; b->fs->list(b->ctx, b->cwd, i, &e); i++) {
        unsigned blocks = e.dir ? 0 : (e.size + 253) / 254;
        int l = (int)strlen(e.name); if (l > 16) l = 16;
        n = 0;
        int sp = blocks < 10 ? 3 : blocks < 100 ? 2 : 1;
        while (sp--) t[n++] = ' ';
        t[n++] = '"';
        for (int k = 0; k < l; k++) t[n++] = br_petscii(e.name[k]);
        t[n++] = '"';
        for (int k = l; k < 17; k++) t[n++] = ' ';
        const char *ty = e.dir ? "DIR" : "PRG";
        for (int k = 0; k < 3; k++) t[n++] = (uint8_t)ty[k];
        /* ⚠ 27 bytes of text, every line 32 bytes, as the 1541's own (fewer leading spaces for
         * bigger block counts, more trailing). FB64 steps through the listing in 32-byte
         * records: with 29-31 it read only the header and re-read for ever (2026-10-29) */
        while (n < 27) t[n++] = ' ';
        br_line(b, blocks, t, n);
    }
    static const char fr[] = "BLOCKS FREE.             ";
    br_line(b, 664, (const uint8_t *)fr, (int)sizeof fr - 1);   /* [A] a fixed figure: the card is not a disk */
    br_put(b, 0); br_put(b, 0);
}

/* --- the built-in browser ------------------------------------------------- */
static int brx_hexv(uint8_t c) { return c >= 'A' ? (c - 'A' + 10) & 15 : (c - '0') & 15; }
static int brx_hex4(const uint8_t *c) { return (brx_hexv(c[0]) << 12) | (brx_hexv(c[1]) << 8) | (brx_hexv(c[2]) << 4) | brx_hexv(c[3]); }

/* "00,TURBO,aaaa,llll": fl bytes in buf, ready for XG; aaaa the first two */
static void brx_turbo_ready(browse_t *b, long fl)
{
    static const char hex[] = "0123456789ABCDEF", head[] = "00,TURBO,";
    uint16_t ad = (uint16_t)(b->buf[0] | (b->buf[1] << 8)); long pl = fl - 2;
    int k = 0;
    b->bsa = -1; b->blen = 0; b->turbo_len = fl;
    for (; head[k]; k++) b->err[k] = (uint8_t)head[k];
    for (int sh = 12; sh >= 0; sh -= 4) b->err[k++] = (uint8_t)hex[(ad >> sh) & 15];
    b->err[k++] = ',';
    for (int sh = 12; sh >= 0; sh -= 4) b->err[k++] = (uint8_t)hex[(pl >> sh) & 15];
    b->err[k++] = '\r';
    b->elen = k; b->epos = 0;
}

/* XD:hhhh[,filter] a window of the folder, for the browser (FFFF: the one round b->sel). In buf:
 *   2 bytes unused (the turbo's load address)
 *   total lo hi, start lo hi, n lo hi, flags (1 = the folder has more than the cache holds), sel lo hi
 *   cwd: length, bytes (its last 60)
 *   n entries: type (1 = folder), blocks lo hi, length, name in ASCII as on the card (at most 46: a
 *   longer one keeps its start and its end)
 * The names are not the 16 characters of a 1541 listing: they tell millie_and_molly.d64 from .t64. */
#define BR_XWIN 100     /* 0.5 s of names at the turbo's speed: the rest a window at a time, as the cursor gets there */
#define BR_XWIN_F 100   /* while filtering a big folder: quick, a letter at a time */
#define BR_XNAME 46
static void brx_xlist(browse_t *b, const uint8_t *a, int an)
{
    char f[20]; int fl = 0;
    int start = an >= 4 ? brx_hex4(a) : 0;
    if (an > 5 && a[4] == ',') {
        for (int i = 5; i < an && fl < (int)sizeof f - 1; i++) {
            uint8_t ch = br_fold(a[i]);
            f[fl++] = (ch >= 0x41 && ch <= 0x5A) ? (char)(ch + 32) : (char)ch;
        }
    }
    f[fl] = 0;
    long pr = b->fs->prep(b->ctx, b->cwd, f);
    if (pr < 0) { strcpy(b->cwd, "/"); pr = b->fs->prep(b->ctx, b->cwd, f); }
    if (pr < 0) { br_error(b, 74, "DRIVE NOT READY"); return; }
    int total = (int)(pr & 0xFFFFFF), cut = (int)(pr >> 24) & 1, sel = 0;
    fsent_t e;
    if (b->sel[0] && !fl && start == 0xFFFF) {           /* (only that window starts round it) */
        if (b->fs->find) { int i = b->fs->find(b->ctx, b->cwd, b->sel); if (i > 0) sel = i; }
        else for (int i = 0; i < total; i++) if (b->fs->list(b->ctx, b->cwd, i, &e) && !strcmp(e.name, b->sel)) { sel = i; break; }
    }
    if (start == 0xFFFF) start = sel > 20 ? sel - 20 : 0;
    if (start > total) start = 0;
    int n = total - start;
    if (n > (fl ? BR_XWIN_F : BR_XWIN)) n = fl ? BR_XWIN_F : BR_XWIN;
    long o = 2;
    uint8_t *p = b->buf;
    p[0] = p[1] = 0;
    p[o++] = (uint8_t)total; p[o++] = (uint8_t)(total >> 8);
    p[o++] = (uint8_t)start; p[o++] = (uint8_t)(start >> 8);
    p[o++] = (uint8_t)n; p[o++] = (uint8_t)(n >> 8);
    p[o++] = (uint8_t)cut;
    p[o++] = (uint8_t)sel; p[o++] = (uint8_t)(sel >> 8);
    int cl = (int)strlen(b->cwd), cs = cl > 60 ? cl - 60 : 0;
    p[o++] = (uint8_t)(cl - cs);
    for (int i = cs; i < cl; i++) p[o++] = (uint8_t)b->cwd[i];
    for (int i = 0; i < n; i++) {
        if (!b->fs->list(b->ctx, b->cwd, start + i, &e)) { e.name[0] = 0; e.dir = 0; e.size = 0; }
        int l = (int)strlen(e.name);
        unsigned bl = e.dir ? 0 : (e.size + 253) / 254;
        if (bl > 65535) bl = 65535;
        p[o++] = e.dir; p[o++] = (uint8_t)bl; p[o++] = (uint8_t)(bl >> 8);
        if (l <= BR_XNAME) {
            p[o++] = (uint8_t)l;
            for (int k = 0; k < l; k++) p[o++] = (uint8_t)e.name[k];
        } else {
            char c[BR_XNAME + 1];
            nm_clip(e.name, l, BR_XNAME, "~", c);
            int cl2 = (int)strlen(c);
            p[o++] = (uint8_t)cl2;
            for (int k = 0; k < cl2; k++) p[o++] = (uint8_t)c[k];
        }
    }
    brx_turbo_ready(b, o);
}

/* The browser's settings, on the card: turbo on or off, the folder, the entry */
#define BR_CFG "/.picod64.cfg"
static char brx_cfg[BR_PATH + BR_NAME + 8];
/* in flash, as the comment on the commands below says: core 1, the bus held busy, nothing timed */
static __attribute__((noinline)) void brx_cfg_read(browse_t *b)
{
    if (b->cfg_read) return;
    b->cfg_read = 1; b->cfg_turbo = 1; b->cfg_sound = 1;
    char *t = brx_cfg;
    long n = b->fs->read(b->ctx, BR_CFG, (uint8_t *)t, (long)sizeof brx_cfg - 1);
    if (n < 2) return;
    t[n] = 0;
    b->cfg_turbo = t[0] == '1';
    b->cfg_sound = t[1] != '0';                 /* first line "1" or "10": drive sounds off */
    char *c = strchr(t, '\n'); if (!c) return;
    char *v = strchr(++c, '\n'); if (!v) return;
    *v++ = 0;
    char *e = strchr(v, '\n'); if (e) *e = 0;
    if (c[0] == '/') { strncpy(b->cwd, c, BR_PATH - 1); b->cwd[BR_PATH - 1] = 0; }
    strncpy(b->sel, v, BR_NAME - 1); b->sel[BR_NAME - 1] = 0;
}
static __attribute__((noinline)) void brx_cfg_write(browse_t *b)
{
    if (!b->fs->write) return;
    char *t = brx_cfg;
    int n = 0;
    t[n++] = b->cfg_turbo ? '1' : '0'; if (!b->cfg_sound) t[n++] = '0'; t[n++] = '\n';
    for (const char *c = b->cwd; *c; c++) t[n++] = *c;
    t[n++] = '\n';
    for (const char *c = b->sel; *c; c++) t[n++] = *c;
    t[n++] = '\n';
    b->fs->write(b->ctx, BR_CFG, (const uint8_t *)t, n);
}
/* A disk going in, whichever way the browser (or the knob) loads it next: the folder and the entry are saved now, so
 * the browser reopens there after a power-off. Only once the settings have been read (the browser's XR), so a turbo
 * set off isn't saved back as on */
static __attribute__((noinline)) void brx_cfg_note(browse_t *b) { if (b->cfg_read) brx_cfg_write(b); }

/* The built-in browser's commands (fw/browser/). A 1541 says SYNTAX ERROR to all of them. In flash: nothing
 * here is timed, the bus is held busy meanwhile. 1 if it was one of them. */
static int brx_command(browse_t *b, const uint8_t *c, int n)
{
    /* --- the built-in browser's commands (fw/browser/). A 1541 says SYNTAX ERROR to all of them. --- */
    if (n >= 3 && c[0] == 'X' && c[2] == ':' && c[1] == 'D' && b->fs->prep) { brx_xlist(b, c + 3, n - 3); return 1; }
    if (n >= 7 && c[0] == 'X' && c[2] == ':' && c[1] == 'O') {     /* XO:hhhh open entry: a folder or a D64 */
        fsent_t e; char path[BR_PATH];
        if (!b->fs->list(b->ctx, b->cwd, brx_hex4(c + 3), &e)) { br_error(b, 62, " FILE NOT FOUND"); return 1; }
        if (b->fs->prep) b->fs->prep(b->ctx, b->cwd, "");     /* (an index is only good for the listing it came from) */
        br_join(b, path, e.name);
        if (e.dir) { strncpy(b->cwd, path, BR_PATH - 1); b->sel[0] = 0; br_error(b, 0, " OK"); return 1; }
        if (br_is_d64(e.name) && b->fs->mount && b->fs->mount(b->ctx, path)) { brx_cfg_note(b); br_error(b, 0, " OK"); return 1; }
        br_error(b, 39, " PATH NOT FOUND");
        return 1;
    }
    if (n >= 7 && c[0] == 'X' && c[2] == ':' && c[1] == 'A') {     /* XA:hhhh LOAD"#" reads this entry */
        fsent_t e;
        if (!b->fs->list(b->ctx, b->cwd, brx_hex4(c + 3), &e)) { br_error(b, 62, " FILE NOT FOUND"); return 1; }
        br_join(b, b->alias, e.name);
        strncpy(b->sel, e.name, BR_NAME - 1); b->sel[BR_NAME - 1] = 0;
        br_error(b, 0, " OK");
        return 1;
    }
    if (n >= 8 && c[0] == 'X' && c[1] == 'L' && c[2] == ':' && c[3] == '#') {   /* XL:#hhhh the turbo: entry into the buffer */
        fsent_t e; char path[BR_PATH];
        b->turbo_len = 0;
        if (!b->fs->list(b->ctx, b->cwd, brx_hex4(c + 4), &e) || e.dir) { br_error(b, 62, " FILE NOT FOUND"); return 1; }
        br_join(b, path, e.name);
        long fl = b->fs->read(b->ctx, path, b->buf, b->bmax);
        if (fl < 3) { br_error(b, 62, " FILE NOT FOUND"); return 1; }
        strncpy(b->sel, e.name, BR_NAME - 1); b->sel[BR_NAME - 1] = 0;
        strncpy(b->last, e.name, BR_NAME - 1); b->last[BR_NAME - 1] = 0;
        brx_turbo_ready(b, fl);
        return 1;
    }
    if (n >= 7 && c[0] == 'X' && c[1] == 'I' && c[2] == ':') {     /* XI:hhhh this D64 is the one XM reads, nothing mounted:
                                                                       * the browser's RUN FIRST (2026-11-07: a mount and CD_ at
                                                                       * once raced on the board); XM's end mounts it */
        fsent_t e; char path[BR_PATH];
        if (!b->fs->list(b->ctx, b->cwd, brx_hex4(c + 3), &e) || e.dir || !br_is_d64(e.name)) { br_error(b, 62, " FILE NOT FOUND"); return 1; }
        if (b->fs->prep) b->fs->prep(b->ctx, b->cwd, "");
        br_join(b, path, e.name);
        strncpy(b->img, path, BR_PATH - 1); b->img[BR_PATH - 1] = 0;
        { const char *sl = strrchr(path, '/'); if (sl) { strncpy(b->sel, sl + 1, BR_NAME - 1); b->sel[BR_NAME - 1] = 0; } }
        brx_cfg_note(b);
        br_error(b, 0, " OK");
        return 1;
    }
    if (n >= 4 && c[0] == 'X' && c[1] == 'M' && c[2] == ':') {     /* XM:name (or *) a file from the D64 last mounted */
        /* Refused (2026-11-17): the disk goes back in and the browser types a plain LOAD, so the drive
         * loads the file itself, as a real 1541. Sent from the image, the drive was left never having
         * read the disk (its ID 00 00), and a loader's own DOS job then failed with 29 (Shards of Fancy) */
        b->turbo_len = 0; b->turbo_remount = 1;
        br_error(b, 62, " FILE NOT FOUND");           /* (the platform mounts it again: see turbo_remount) */
        return 1;
    }
    if (n == 2 && c[0] == 'X' && c[1] == 'R') {                    /* XR the settings: 00,CFG,t */
        brx_cfg_read(b);
        static const char h[] = "00,CFG,";
        int k = 0;
        for (; h[k]; k++) b->err[k] = (uint8_t)h[k];
        b->err[k++] = (uint8_t)('0' + b->cfg_turbo); b->err[k++] = '\r';
        b->elen = k; b->epos = 0;
        return 1;
    }
    if (n == 4 && c[0] == 'X' && c[1] == 'S' && c[2] == ':') {     /* XS:t save the settings and where we are */
        b->cfg_turbo = c[3] == '1';
        brx_cfg_write(b);
        br_error(b, 0, " OK");
        return 1;
    }
    if (n == 2 && c[0] == 'X' && c[1] == 'B' && b->builtin) {      /* XB the browser, for its bootstrap */
        memcpy(b->buf, b->builtin, (size_t)b->builtin_len);
        brx_turbo_ready(b, b->builtin_len);
        return 1;
    }
    if (n == 2 && c[0] == 'X' && c[1] == 'C') { b->turbo_len = 0; br_error(b, 0, " OK"); return 1; }   /* never mind */
    if (n == 2 && c[0] == 'X' && c[1] == 'G') {
        if (b->turbo_len > 0) { b->turbo_go = 1; br_error(b, 0, " OK"); return 1; }
        br_error(b, 31, " SYNTAX ERROR"); return 1;
    }
    return 0;
}

/* --- commands (channel 15) ------------------------------------------------ */
/* N:NAME,ID with no disk in. On a 1541 that means "give me a fresh disk, ready to use", so here it makes
 * NAME.D64 in this folder and mounts it, and SAVE goes into it next. It belongs to the drive, not to any
 * browser: BASIC, a cartridge's wedge or anything else can send it. Not hot, so left in flash. */
__attribute__((noinline)) static void nd_make(browse_t *b, const uint8_t *c, int n)
{
    int i = 1;
    while (i < n && c[i] != ':') i++;
    if (i >= n || !b->fs->newdisk || !b->fs->mount) { br_error(b, 31, " SYNTAX ERROR"); return; }
    const uint8_t *nm = c + i + 1;
    int rest = n - i - 1, nl = 0;
    while (nl < rest && nm[nl] != ',') nl++;
    uint8_t id1 = '0', id2 = '1';
    if (nl + 1 < rest) { id1 = nm[nl + 1]; if (nl + 2 < rest) id2 = nm[nl + 2]; }
    if (!nl) { br_error(b, 31, " SYNTAX ERROR"); return; }
    char f[BR_NAME]; int k = 0;
    for (int j = 0; j < nl && k < BR_NAME - 5; j++) {
        uint8_t ch = br_fold(nm[j]);
        char a = (ch >= 0x41 && ch <= 0x5A) ? (char)(ch - 0x41 + 'a') : (char)ch;
        if (a == '/' || a == '\\' || a == ':' || a == '*' || a == '?' || a == '"' || a < 0x20 || a > 0x7E) a = '_';
        f[k++] = a;
    }
    f[k] = 0;
    if (!(k > 4 && !strcmp(f + k - 4, ".d64"))) strcat(f, ".d64");
    char path[BR_PATH];
    size_t dl = strlen(b->cwd);                      /* (br_join's in RAM; a call to it from flash needs a veneer in flash) */
    if (dl + (size_t)k + 6 >= sizeof path) { br_error(b, 31, " SYNTAX ERROR"); return; }
    memcpy(path, b->cwd, dl);
    if (dl == 0 || path[dl - 1] != '/') path[dl++] = '/';
    strcpy(path + dl, f);
    int r = b->fs->newdisk(b->ctx, path, nm, nl, id1, id2);
    if (r < 0) { br_error(b, 63, " FILE EXISTS"); return; }
    if (!r) { br_error(b, 26, " WRITE PROTECT ON"); return; }
    if (b->fs->mount(b->ctx, path)) br_error(b, 0, " OK");
    else br_error(b, 39, " PATH NOT FOUND");
}

#ifndef BR_CMD_LOG
#define BR_CMD_LOG(c, n) do { } while (0)   /* the platform may log the commands (fw/pico_main.c, serial x) */
#endif
static FF_C1 void br_command(browse_t *b, const uint8_t *c, int n)
{
    BR_CMD_LOG(c, n);
    if (n >= 2 && c[0] == 'N' && (c[1] == ':' || c[1] == '0' || c[1] == 'E')) { nd_make(b, c, n); return; }
    /* trailing spaces off a typed command, but not off XM:name, the browser's exact name of a file in a D64: names can
     * end in a space (Uncensored's " - UNCENSORED - " went the plain way, FILE NOT FOUND, and its place wasn't saved:
     * 2026-11-14) */
    int exact = n >= 3 && c[0] == 'X' && c[1] == 'M' && c[2] == ':';
    while (n && (c[n - 1] == '\r' || (!exact && c[n - 1] == ' '))) n--;
    if (n >= 2 && c[0] == 'C' && c[1] == 'D') {
        const uint8_t *a = c + 2; int an = n - 2;
        if (an >= 2 && a[0] == '/' && a[1] == '/') {          /* CD//[path]: from the root */
            strcpy(b->cwd, "/"); a += 2; an -= 2;
            if (!an) { br_error(b, 0, " OK"); return; }
        } else if (an && (a[0] == ':' || a[0] == '/')) { a++; an--; }
        if (an == 1 && a[0] == 0x5F) {                       /* left arrow: up */
            char *s = strrchr(b->cwd, '/');
            if (s && s[1]) { strncpy(b->sel, s + 1, BR_NAME - 1); b->sel[BR_NAME - 1] = 0; }
            if (s && s != b->cwd) *s = 0; else strcpy(b->cwd, "/");
            br_error(b, 0, " OK"); return;
        }
        fsent_t e; char path[BR_PATH];
        if (an <= 0 || br_find(b, a, an, &e) < 0) { br_error(b, 62, " FILE NOT FOUND"); return; }
        br_join(b, path, e.name);
        if (e.dir) { strncpy(b->cwd, path, BR_PATH - 1); br_error(b, 0, " OK"); return; }
        if (br_is_d64(e.name) && b->fs->mount && b->fs->mount(b->ctx, path)) { br_error(b, 0, " OK"); return; }
        if (br_is_prg(e.name) && b->fs->mount_prg && b->fs->mount_prg(b->ctx, b->cwd, e.name)) {
            strncpy(b->last, e.name, BR_NAME - 1); b->last[BR_NAME - 1] = 0;
            br_error(b, 0, " OK"); return;
        }
        br_error(b, 39, " PATH NOT FOUND");
        return;
    }
    if (n >= 2 && c[0] == 'X' && brx_command(b, c, n)) return;   /* the built-in browser's */
    if (n == 2 && c[0] == 'U' && (c[1] == 'I' || c[1] == 'J' || c[1] == '9' || c[1] == ':')) {   /* reset: keep the folder */
        br_error(b, 73, "PICOD64 BROWSE");
        return;
    }
    if (n == 1 && c[0] == 'I') { br_error(b, 0, " OK"); return; }
    /* M-R addr-lo addr-hi [count]: the bytes come back on the error channel [A: the 1541's
     * reply, as commonly described; checked against the real DOS in f6.sh] */
    if (n >= 5 && c[0] == 'M' && c[1] == '-' && c[2] == 'R' && b->fs->peek) {
        int cnt = n >= 6 ? c[5] : 1;
        if (cnt < 1) cnt = 1;
        if (cnt > (int)sizeof b->err) cnt = (int)sizeof b->err;
        for (int i = 0; i < cnt; i++) b->err[i] = b->fs->peek(b->ctx, (uint16_t)(c[3] + (c[4] << 8) + i));
        b->elen = cnt; b->epos = 0;
        return;
    }
    /* M-W addr-lo addr-hi count data: drive code. From here the real DOS answers. */
    if (n >= 6 && c[0] == 'M' && c[1] == '-' && c[2] == 'W' && b->fs->drivecode) {
        int cnt = c[5] < n - 6 ? c[5] : n - 6;
        if (b->fs->drivecode(b->ctx, b->cwd, b->last, (uint16_t)(c[3] + (c[4] << 8)), c + 6, cnt)) { br_error(b, 0, " OK"); return; }
    }
    br_error(b, 31, " SYNTAX ERROR");
}

/* --- the engine's callbacks ----------------------------------------------- */
static FF_C1 void br_open(iecdev_t *d, uint8_t sa) { browse_t *b = d->ctx; (void)sa; b->nlen = 0; }
/* CLOSE: a file being saved goes to the card now (the bytes were collected in buf) */
static FF_C1 void br_close(iecdev_t *d, uint8_t sa)
{
    browse_t *b = d->ctx;
    if (sa == b->wsa) {
        int ok = b->fs->write && b->fs->write(b->ctx, b->wpath, b->buf, b->blen);
        if (ok) br_error(b, 0, " OK"); else br_error(b, 25, " WRITE ERROR");   /* [A: our choice of code] */
        b->wsa = -1; b->blen = 0;
        return;
    }
    if (sa == b->bsa) { b->blen = 0; b->bsa = -1; }
}
static FF_C1 void br_listen_byte(iecdev_t *d, uint8_t sa, uint8_t c, int eoi)
{
    browse_t *b = d->ctx; (void)eoi;
    if ((d->opening || sa == 15) && b->nlen < (int)sizeof b->name) b->name[b->nlen++] = c;
    else if (!d->opening && sa == b->wsa) br_put(b, c);   /* SAVE's data */
    if (sa == 15) b->got_cmd = 1;
}
/* the built-in browser as the file being read */
static FF_C1 void br_builtin(browse_t *b)
{
    memcpy(b->buf, b->builtin, (size_t)b->builtin_len);
    b->blen = b->builtin_len; b->bpos = 0;
    br_error(b, 0, " OK");
}
static FF_C1 void br_unlisten(iecdev_t *d)
{
    browse_t *b = d->ctx;
    uint8_t sa = d->sa;
    if (sa == 15) { if (b->nlen) br_command(b, b->name, b->nlen); b->nlen = 0; return; }
    if (!d->opening) { b->nlen = 0; return; }       /* the end of a block of SAVE's data */
    const uint8_t *nm = b->name; int nl = b->nlen;
    int replace = nl >= 1 && nm[0] == '@';                                                    /* "@0:name" */
    if (replace) { nm++; nl--; }
    if (nl >= 2 && nm[1] == ':' && (nm[0] == '0' || nm[0] == '1')) { nm += 2; nl -= 2; }   /* "0:name" */
    int write = sa == 1;                             /* the KERNAL's SAVE opens channel 1 [S12] */
    for (int i = 0; i < nl; i++) if (nm[i] == ',') {                                           /* ",P,R" / ",P,W" */
        for (int k = i; k < nl; k++) if (nm[k] == 'W') write = 1;
        nl = i; break;
    }
    if (write) {
        /* SAVE: a new file in the current folder, NAME.PRG unless it has an extension; an
         * existing one only with "@" [the 1541's "file exists" rule, S10] */
        b->nlen = 0;
        fsent_t e; char nm8[BR_NAME]; int k = 0, dot = 0;
        if (nl <= 0 || !b->fs->write) { br_error(b, 31, " SYNTAX ERROR"); return; }
        for (int i = 0; i < nl && k < BR_NAME - 5; i++) {
            uint8_t c = br_fold(nm[i]);
            char a = (c >= 0x41 && c <= 0x5A) ? (char)(c - 0x41 + 'a') : (char)c;   /* PETSCII -> lower-case ASCII */
            if (a == '/' || a == '\\' || a == ':' || a == '*' || a == '?' || a == '"' || a < 0x20 || a > 0x7E) a = '_';
            if (a == '.') dot = 1;
            nm8[k++] = a;
        }
        nm8[k] = 0;
        if (!dot) strcat(nm8, ".prg");
        if (!replace && br_find(b, (const uint8_t *)nm8, -1, &e) >= 0) { br_error(b, 63, " FILE EXISTS"); return; }
        br_join(b, b->wpath, nm8);
        b->wsa = sa; b->blen = 0;
        br_error(b, 0, " OK");
        return;
    }
    b->bsa = sa; b->blen = 0; b->bpos = 0;
    b->nlen = 0;                                     /* (nm points into it; it is not reused before return) */
    if (nl == 1 && nm[0] == '$') { br_listing(b); br_error(b, 0, " OK"); return; }
    fsent_t e; char path[BR_PATH];
    if (nl == 1 && nm[0] == '#' && b->alias[0]) {       /* the browser's plain LOAD of a long name */
        long fl = b->fs->read(b->ctx, b->alias, b->buf, b->bmax);
        if (fl < 0) { br_error(b, 62, " FILE NOT FOUND"); return; }
        b->blen = fl; b->bpos = 0;
        br_error(b, 0, " OK");
        return;
    }
    if (b->boot && !strcmp(b->cwd, "/") && nl == 1 && nm[0] == '*') {
        memcpy(b->buf, b->boot, (size_t)b->boot_len);
        b->blen = b->boot_len; b->bpos = 0;
        br_error(b, 0, " OK");
        return;
    }
    int found = nl > 0 && br_find(b, nm, nl, &e) >= 0;
    static const uint8_t bn[] = "BROWSER";
    if (!found && b->builtin && !strcmp(b->cwd, "/") && nl == 7 && !memcmp(nm, bn, 7)) { br_builtin(b); return; }
    if (!found || e.dir) { br_error(b, 62, " FILE NOT FOUND"); return; }
    br_join(b, path, e.name);
    long n = b->fs->read(b->ctx, path, b->buf, b->bmax);
    if (n < 0) { br_error(b, 62, " FILE NOT FOUND"); return; }
    b->blen = n;
    strncpy(b->last, e.name, BR_NAME - 1); b->last[BR_NAME - 1] = 0;
    br_error(b, 0, " OK");
}
/* ⚠ every name is used once: FB64 sends its CD as DATA to channel 15 (PRINT#15), and the
 * "$" left from the last OPEN made it "$CD:GAMES", a syntax error (2026-10-29) */
static FF_C1 int br_talk_byte(iecdev_t *d, uint8_t sa, uint8_t *c, int *last)
{
    browse_t *b = d->ctx;
    if (sa == 15) {                                  /* the error channel, then back to 00, OK */
        *c = b->err[b->epos++];
        *last = b->epos >= b->elen;
        if (*last) br_error(b, 0, " OK");
        return 1;
    }
    if (sa != b->bsa || b->bpos >= b->blen) return 0;
    *c = b->buf[b->bpos++];
    *last = b->bpos >= b->blen;
    return 1;
}
static const iecdev_ops_t browse_ops = { br_listen_byte, br_open, br_close, br_unlisten, br_talk_byte };

static FF_C1 void browse_init(browse_t *b, iecdev_t *d, const fs_ops_t *fs, void *ctx, uint8_t *buf, long bmax)
{
    memset(b, 0, sizeof *b);
    b->fs = fs; b->ctx = ctx; b->buf = buf; b->bmax = bmax; b->bsa = -1; b->wsa = -1;
    strcpy(b->cwd, "/");
    br_error(b, 73, "PICOD64 BROWSE");            /* a 1541 says "73,CBM DOS V2.6 1541" at power-up */
    memset(d, 0, sizeof *d);
    d->dev = 8; d->ops = &browse_ops; d->ctx = b;
}
#endif
