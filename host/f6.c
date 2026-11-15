/* -------------------------------------------------------------------------
 * F6: browse mode against the real C64 KERNAL (host/c64.c), with a folder on
 * the PC standing in for the SD card.
 *
 * The bus is shared by three parties: the C64; the browse-mode device
 * (core/iec_dev.h + core/browse.h), which owns the bus until an image is
 * mounted; and the emulated 1541 (core/drive1541.h, the real DOS), booted once
 * at the start OFF the bus and kept as a snapshot (drive_save). A mount
 * restores the snapshot, inserts the image and puts the drive on the bus; a
 * "CD_" sent to channel 15 while it is there (seen by a PASSIVE copy of the
 * device engine) takes it off again.
 *
 *   f6 <dos.rom> <kernal> <basic> <root folder> <outdir> command...
 * Commands as host/f4c.c: NAME | load:NAME, cmd:TEXT, and err: (read the
 * error channel); "$" lists. Outputs <outdir>/<n>.bin (loads) and <n>.txt.
 * To drive a program (FB64) as a person would:
 *   type:TEXT   keys through the KERNAL's keyboard buffer ($0277, count $C6
 *               [S12 memory map]); | = RETURN, ~ = CRSR down, ^ = CRSR up,
 *               < = DEL, a-z as the unshifted letters
 *   wait:SECS   let both machines run
 *   screen:     print the 40 x 25 screen at $0400 as text
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include "c64.h"

static uint8_t drive_low, c64_low, dev_low, ctl_low;
static int online;                                 /* the emulated drive is on the bus */
#define PLAT_CYCLE()      do { } while (0)
#define PLAT_SYNC(c, k)   do { } while (0)
#define PLAT_SLACK(c)     1
#define PLAT_LA_BUDGET    0
#define PLAT_LEAD(c, n)   do { } while (0)
#define PLAT_IEC_IN()     ((uint8_t)(drive_low | ctl_low | (online ? c64_low : 0)))
#define PLAT_IEC_OUT(p)   (drive_low = (uint8_t)(p))
#include "../core/drive1541.h"
#include "../core/browse.h"
#include "../core/d64make.h"
#include "../core/iec_host.h"

static uint8_t dos[16384], kernal[8192], basic[8192];
static uint8_t d64[D64_BYTES], d64_orig[D64_BYTES], gcr[D64_GCR_BYTES];
static disk_t disk, nodisk;
static syncs_t frames[D64_TRACKS];
static char mounted[BR_PATH];
static uint64_t drive_base; static uint32_t drive_last;
static const char *root;

static void load(const char *path, uint8_t *b, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f || fread(b, 1, n, f) != n) { perror(path); exit(2); }
    fclose(f);
}

/* --- the "SD card": a folder on the PC ---------------------------------- */
static int ent_cmp(const void *a, const void *b)
{
    const fsent_t *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;          /* folders first [A] */
    return strcmp(x->name, y->name);
}
static int fs_list(void *ctx, const char *path, int idx, fsent_t *e)
{
    (void)ctx;
    static fsent_t ents[512]; static int n; static char last[BR_PATH + 512];
    char full[BR_PATH + 512];
    snprintf(full, sizeof full, "%s%s", root, path);
    if (strcmp(full, last)) {                              /* (re)read the folder */
        strcpy(last, full); n = 0;
        DIR *dp = opendir(full);
        struct dirent *de;
        while (dp && (de = readdir(dp)) && n < 512) {
            if (de->d_name[0] == '.') continue;
            char p[BR_PATH + 1024]; struct stat st;
            snprintf(p, sizeof p, "%s/%s", full, de->d_name);
            if (stat(p, &st)) continue;
            strncpy(ents[n].name, de->d_name, BR_NAME - 1); ents[n].name[BR_NAME - 1] = 0;
            ents[n].size = (uint32_t)st.st_size; ents[n].dir = S_ISDIR(st.st_mode);
            n++;
        }
        if (dp) closedir(dp);
        qsort(ents, (size_t)n, sizeof *ents, ent_cmp);
    }
    if (idx >= n) return 0;
    *e = ents[idx];
    return 1;
}
static long fs_read(void *ctx, const char *path, uint8_t *buf, long max)
{
    (void)ctx;
    char full[BR_PATH + 512];
    snprintf(full, sizeof full, "%s%s", root, path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    long n = (long)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}
static int pending_mount, pending_unmount, virtual_disk;
/* the conversation since the last OPEN of a file, and its copy taken at the M-W */
static iecl_t convo, replay;
static int fs_mount(void *ctx, const char *path)
{
    (void)ctx;
    char full[BR_PATH + 512];
    snprintf(full, sizeof full, "%s%s", root, path);
    FILE *f = fopen(full, "rb");
    if (!f || fread(d64, 1, D64_BYTES, f) != D64_BYTES) { if (f) fclose(f); return 0; }
    fclose(f);
    memcpy(d64_orig, d64, D64_BYTES);
    strncpy(mounted, full, BR_PATH - 1);
    d64_to_disk(d64, gcr, &disk, frames);
    pending_mount = 1;                                 /* switch once ATN is released */
    return 1;
}
static drive_snap_t booted;
static uint8_t fs_peek(void *ctx, uint16_t a)
{
    (void)ctx;
    if (a >= 0xC000) return dos[a - 0xC000];
    if (a < 0x0800) return booted.d.ram[a];
    return (uint8_t)(a >> 8);                      /* [A] open bus, as the core's map */
}
/* M-W in browse mode: a virtual D64 of the folder's PRGs (the one just served first),
 * the drive code poked into the booted drive's RAM, the emulated 1541 on the bus */
static int fs_drivecode(void *ctx, const char *path, const char *first, uint16_t addr, const uint8_t *data, int n)
{
    (void)ctx;
    static uint8_t f[65536];
    uint8_t nm[16]; int nl = 0;
    const char *fn = strrchr(path, '/'); fn = (fn && fn[1]) ? fn + 1 : "picod64";
    for (; fn[nl] && nl < 16; nl++) nm[nl] = br_petscii(fn[nl]);
    d64_blank(d64, nm, nl, 'F', 'F');
    fsent_t e; int added = 0;
    for (int pass = 0; pass < 2; pass++)            /* the file just served first, then the rest */
        for (int i = 0; fs_list(0, path, i, &e); i++) {
            int l = (int)strlen(e.name);
            int isprg = !e.dir && l > 4 && e.name[l - 4] == '.' && (e.name[l - 3] | 32) == 'p' &&
                        (e.name[l - 2] | 32) == 'r' && (e.name[l - 1] | 32) == 'g';
            if (!isprg || (pass == 0) != !strcmp(e.name, first)) continue;
            char full[BR_PATH + 64]; snprintf(full, sizeof full, "%s/%s", strcmp(path, "/") ? path : "", e.name);
            long len = fs_read(0, full, f, sizeof f);
            uint8_t pn[16]; int pl = 0;
            for (; e.name[pl] && pl < 16; pl++) pn[pl] = br_petscii(e.name[pl]);
            if (len > 0 && d64_add_prg(d64, pn, pl, f, len)) added++;
        }
    memcpy(d64_orig, d64, D64_BYTES);
    d64_to_disk(d64, gcr, &disk, frames);
    (void)addr; (void)data; (void)n;                 /* the M-W itself is in the conversation */
    replay = convo;
    snprintf(mounted, sizeof mounted, "(virtual: %d PRGs of %s)", added, path);
    virtual_disk = 1; pending_mount = 1;
    return 1;
}
static const fs_ops_t fsops = { fs_list, fs_read, fs_mount, fs_peek, fs_drivecode };

static iecdev_t dev, sniff;
static void tr_cmd(iecdev_t *d, uint8_t c) { (void)d; printf("  [atn %02X]\n", c); }
static browse_t br;

/* F6TRACE=1: every name / command and every TALK, as the browse device saw them */
static void tr_unlisten(iecdev_t *d)
{
    browse_t *b = d->ctx;
    /* F6LISTING=file: serve that file's bytes as "$" (a test probe: is it the listing or the bus?) */
    if (getenv("F6LISTING") && d->opening && b->nlen == 1 && b->name[0] == '$') {
        browse_ops.unlisten(d);
        FILE *f = fopen(getenv("F6LISTING"), "rb");
        if (f) { b->blen = (long)fread(b->buf, 1, (size_t)b->bmax, f); fclose(f); }
        return;
    }
    if (getenv("F6TRACE") && (b->nlen || d->opening)) {
        printf("  [bus: (%u bytes sent so far) %s sa %d \"", d->bytes_out, d->sa == 15 ? "command" : "open", d->sa);
        for (int k = 0; k < b->nlen; k++) { uint8_t c = b->name[k]; putchar(c >= 0x20 && c < 0x7F ? c : '.'); }
        printf("\" (");
        for (int k = 0; k < b->nlen; k++) printf("%02X", b->name[k]);
        printf(")]\n");
    }
    browse_ops.unlisten(d);
    if (getenv("F6TRACE") && (d->sa == 15 || d->opening)) {
        printf("  [   -> ");
        for (int k = 0; k < b->elen && b->err[k] != '\r'; k++) putchar(b->err[k] >= 0x20 && b->err[k] < 0x7F ? b->err[k] : '.');
        printf("]\n");
    }
}
static int tr_talk(iecdev_t *d, uint8_t sa, uint8_t *c, int *last)
{
    static int prev = -1;
    if (getenv("F6TRACE") && prev != sa) { printf("  [bus: talk sa %d]\n", sa); }
    prev = *last ? -1 : sa;
    int r = browse_ops.talk_byte(d, sa, c, last);
    if (!r) prev = -1;
    else iecl_add(&convo, IECL_READ, 0, 0);
    return r;
}
static iecdev_ops_t trace_ops;
static void log_cmd(iecdev_t *d, uint8_t c)
{
    if (getenv("F6TRACE") && atoi(getenv("F6TRACE")) > 1) printf("  [atn %02X]\n", c);
    if ((c & 0xF0) == 0xF0 && (c & 0x0F) != 15 && d->listening) {   /* OPEN of a file: a new conversation */
        convo.n = 0; convo.over = 0;
        iecl_add(&convo, IECL_ATN, (uint8_t)(0x20 | d->dev), 0);
    }
    if ((c & 0xF0) == 0xE0 && (c & 0x0F) != 15 && d->listening) {   /* CLOSE of a file: nothing left open */
        convo.n = 0; convo.over = 0;
        return;
    }
    iecl_add(&convo, IECL_ATN, c, 0);
}
static void log_atn_end(iecdev_t *d) { (void)d; iecl_add(&convo, IECL_ATN_END, 0, 0); }
static void log_byte(iecdev_t *d, uint8_t sa, uint8_t c, int eoi) { iecl_add(&convo, IECL_DATA, c, (uint8_t)eoi); browse_ops.listen_byte(d, sa, c, eoi); }

static uint8_t buf[200000];

/* the passive copy: only channel-15 text, for "CD_" / "CD:_" / "CD//" */
static uint8_t scmd[48]; static int sn;
static void sn_open(iecdev_t *d, uint8_t sa) { (void)d; if (sa == 15) sn = 0; }
static void sn_byte(iecdev_t *d, uint8_t sa, uint8_t c, int eoi) { (void)d; (void)eoi; if (sa == 15 && sn < 48) scmd[sn++] = c; }
static void sn_unlisten(iecdev_t *d)
{
    if (d->sa == 15 && sn >= 3 && scmd[0] == 'C' && scmd[1] == 'D' &&
        (scmd[2] == 0x5F || (sn >= 4 && scmd[2] == ':' && scmd[3] == 0x5F) || (sn >= 4 && scmd[2] == '/' && scmd[3] == '/')))
        pending_unmount = 1;
    sn = 0;
}
static int sn_talk(iecdev_t *d, uint8_t sa, uint8_t *b, int *l) { (void)d; (void)sa; (void)b; (void)l; return 0; }
static const iecdev_ops_t sniff_ops = { sn_byte, sn_open, 0, sn_unlisten, sn_talk };

/* --- time and the bus --------------------------------------------------- */
static uint64_t drive_now(void) { drive_base += (uint32_t)(D.cycles - drive_last); drive_last = D.cycles; return drive_base; }
static uint32_t us_now(void) { return (uint32_t)(c64_cycles() * 1000000u / C64_HZ); }

static void switch_modes(void)
{
    if (pending_mount && !(c64_low & C64_ATN)) {
        drive_load(&booted);                           /* a freshly booted 1541 ... */
        drive_last = D.cycles; drive_base = c64_cycles() * 1000000u / C64_HZ;
        drive_insert(&disk);                           /* ... with the image in it */
        if (virtual_disk && replay.n) {                /* replay the conversation into the DOS, off the bus */
            static iech_t h;
            memset(&h, 0, sizeof h); h.log = &replay; h.e.dev = 8;
            uint32_t limit = D.cycles + 5000000u;
            while (!h.done && (int32_t)(limit - D.cycles) > 0) {
                ctl_low = iech_poll(&h, D.cycles, (uint8_t)(drive_low | ctl_low));
                drive_run(D.cycles + 1);
                static int pi = -1; static uint8_t pln = 0xFF;
                if (getenv("F6REPLAY") && (h.i != pi || (uint8_t)(drive_low | ctl_low) != pln ||
                    (D.cycles > 4225840 && D.cycles < 4226400 && D.cycles % 10 == 0))) {
                    printf("    [replay t %u op %d/%d type %d b %02X | ctl %X drive %X | pc %d tx %d rx %d]\n", D.cycles, h.i, replay.n,
                           h.i < replay.n ? replay.op[h.i].type : -1, h.i < replay.n ? replay.op[h.i].b : 0, ctl_low, drive_low, h.pc, h.e.tpc, h.e.rpc);
                    pi = h.i; pln = (uint8_t)(drive_low | ctl_low);
                }
            }
            ctl_low = 0;
            printf("  [replayed %d steps into the DOS (%s)]\n", replay.n, h.done ? "done" : "TIMED OUT");
            drive_last = D.cycles;                     /* the replay took no C64 time */
            replay.n = 0;
        }
        online = 1; pending_mount = 0; dev_low = 0;
        memset(&sniff, 0, sizeof sniff); sniff.dev = 8; sniff.passive = 1; sniff.ops = &sniff_ops;
        if (getenv("F6TRACE") && atoi(getenv("F6TRACE")) > 1) sniff.trace = tr_cmd;
        printf("  [mounted %s: the emulated 1541 has the bus]\n", virtual_disk ? mounted : strrchr(mounted, '/') + 1);
    }
    if (pending_unmount && !(c64_low & C64_ATN)) {
        static uint8_t now[D64_BYTES];
        memcpy(now, d64, D64_BYTES);
        int n = disk_to_d64(&disk, now);
        if (!virtual_disk && memcmp(now, d64_orig, D64_BYTES)) {   /* written to: back to the "SD card" */
            FILE *f = fopen(mounted, "wb");
            if (f) { fwrite(now, 1, D64_BYTES, f); fclose(f); }
            printf("  [written back: %d sectors]\n", n);
        }
        online = 0; pending_unmount = 0; drive_low = 0; virtual_disk = 0;
        dev.pc = dev.rpc = dev.tpc = 0; dev.listening = dev.talking = 0; dev.atn_prev = 0;
        br_error(&br, 0, " OK");
        printf("  [unmounted: browse mode has the bus]\n");
    }
}

static void step(void)
{
    if (!online || c64_cycles() * 1000000u <= drive_now() * (uint64_t)C64_HZ) {
        c64_iec_in((uint8_t)(c64_low | (online ? drive_low : dev_low)));
        c64_step();
        c64_low = c64_iec_out();
        uint8_t lines = (uint8_t)(c64_low | (online ? drive_low : dev_low));
        if (online) iec_poll(&sniff, us_now(), lines);
        else {
            dev_low = iec_poll(&dev, us_now(), lines);
            static int lv; static uint8_t pl = 0xFF; static int ppc = -1;
            if (!lv) lv = getenv("F6TRACE") ? atoi(getenv("F6TRACE")) : -1;
            if (lv >= 3 && (lines != pl || dev.pc != ppc))
                printf("  [t %u: C64 %X dev %X -> lines %X; dev pc %d rx %d tx %d L%d T%d; C64 pc $%04X]\n",
                       us_now(), c64_low, dev_low, (unsigned)(c64_low | dev_low), dev.pc, dev.rpc, dev.tpc,
                       dev.listening, dev.talking, c64_pc());
            pl = lines; ppc = dev.pc;
            static uint8_t pst;
            uint8_t st = c64_ram()[0x90];
            if (lv >= 2 && st != pst)
                printf("  [status $90 -> $%02X at t %u; dev pc %d tx %d rx %d; bytes out %u; C64 pc $%04X]\n",
                       st, us_now(), dev.pc, dev.tpc, dev.rpc, dev.bytes_out, c64_pc());
            pst = st;
        }
        switch_modes();
    } else {
        drive_run(D.cycles + 1);
    }
}

static int run_until_pc(uint16_t pc, uint64_t limit)
{
    uint64_t end = c64_cycles() + limit;
    while (c64_pc() != pc) {
        if (c64_cycles() > end || CPU.jammed) return 0;
        step();
    }
    return 1;
}

/* --- the driver at $C000, as host/f4c.c ----------------------------------- */
static uint8_t *code; static uint16_t pc;
static void op(int n, ...) { va_list ap; va_start(ap, n); while (n--) code[pc++ & 0xFFFF] = (uint8_t)va_arg(ap, int); va_end(ap); }
#define JSR(a) op(3, 0x20, (a) & 0xFF, (a) >> 8)
enum { SETNAM = 0xFFBD, SETLFS = 0xFFBA, LOAD = 0xFFD5, OPEN = 0xFFC0, CLOSE = 0xFFC3,
       CHKIN = 0xFFC6, CHRIN = 0xFFCF, CLRCHN = 0xFFCC };

static uint16_t build(uint8_t *m, char kind, size_t len)
{
    code = m; pc = 0xC000;
    op(6, 0xA9, (int)len, 0xA2, 0x00, 0xA0, 0xC1); JSR(SETNAM);
    if (kind == 'L') {
        op(6, 0xA9, 1, 0xA2, 8, 0xA0, 0); JSR(SETLFS);
        op(6, 0xA9, 0, 0xA2, 0x00, 0xA0, 0x10); JSR(LOAD);
    } else if (kind == 'C') {
        op(6, 0xA9, 15, 0xA2, 8, 0xA0, 15); JSR(SETLFS); JSR(OPEN);
        op(2, 0xA9, 15); JSR(CLOSE);
    } else {                                           /* E: read the error channel to $C300 */
        op(6, 0xA9, 15, 0xA2, 8, 0xA0, 15); JSR(SETLFS); JSR(OPEN);
        op(2, 0xA2, 15); JSR(CHKIN);
        op(2, 0xA0, 0x00);
        uint16_t loop = pc;
        JSR(CHRIN); op(3, 0x99, 0x00, 0xC3); op(1, 0xC8);          /* STA $C300,Y; INY */
        op(2, 0xA5, 0x90); op(2, 0xD0, 0x04);                       /* LDA $90; BNE out */
        op(2, 0xC0, 0x40); op(2, 0x90, (uint8_t)(loop - (pc + 2)));  /* CPY #64; BCC loop */
        op(3, 0x8C, 0x05, 0xC2);                                    /* out: STY $C205 */
        JSR(CLRCHN); op(2, 0xA9, 15); JSR(CLOSE);
    }
    op(6, 0x8E, 0x00, 0xC2, 0x8C, 0x01, 0xC2);
    op(3, 0x8D, 0x02, 0xC2); op(2, 0x08, 0x68); op(3, 0x8D, 0x03, 0xC2);
    op(5, 0xA5, 0x90, 0x8D, 0x04, 0xC2);
    uint16_t done = pc; op(3, 0x4C, done & 0xFF, done >> 8);
    return done;
}
#define READY_LOOP 0xE5CD

static void advance(double secs)
{
    uint64_t end = c64_cycles() + (uint64_t)(secs * C64_HZ);
    while (c64_cycles() < end) step();
}

static void type_keys(const char *t)
{
    uint8_t *m = c64_ram();
    while (*t) {
        uint64_t limit = c64_cycles() + 20u * C64_HZ;
        while (m[0xC6] && c64_cycles() < limit) step();        /* the buffer is empty */
        int n = 0;
        while (*t && n < 10) {
            char c = *t++;
            uint8_t k = c == '|' ? 0x0D : c == '~' ? 0x11 : c == '^' ? 0x91 : c == '<' ? 0x14 :
                        (c >= 'a' && c <= 'z') ? (uint8_t)(c - 32) : (uint8_t)c;
            m[0x277 + n++] = k;
        }
        m[0xC6] = (uint8_t)n;
    }
    advance(0.2);
}

/* F6SHOT=prefix: each screen: also as a picture, prefixN.ppm, drawn from the real character
 * ROM (F6CHARGEN) with colour RAM and the border/background colours: to SHOW a run */
static void screen_shot(void)
{
    static int k;
    static uint8_t cg[4096];
    const char *pre = getenv("F6SHOT"), *cgp = getenv("F6CHARGEN");
    if (!pre || !cgp) return;
    FILE *f = fopen(cgp, "rb");
    if (!f || fread(cg, 1, sizeof cg, f) != sizeof cg) { if (f) fclose(f); return; }
    fclose(f);
    static const uint8_t pal[16][3] = {   /* [A] a common VIC-II palette approximation */
        {0,0,0},{255,255,255},{136,57,50},{103,182,189},{139,63,150},{85,160,73},{64,49,141},{191,206,114},
        {139,84,41},{87,66,0},{184,105,98},{80,80,80},{120,120,120},{148,224,137},{120,105,196},{159,159,159} };
    char name[512]; snprintf(name, sizeof name, "%s%02d.ppm", pre, k++);
    f = fopen(name, "wb"); if (!f) return;
    const int B = 32, W = 320 + 2 * B, H = 200 + 2 * B;
    fprintf(f, "P6 %d %d 255\n", W, H);
    const uint8_t *m = c64_ram(), *cr = c64_cram();
    uint16_t base = c64_screen();
    int bord = c64_vic(0x20) & 15, bg = c64_vic(0x21) & 15, cset = (c64_vic(0x18) & 2) ? 0x800 : 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int c = bord, sx = x - B, sy = y - B;
        if (sx >= 0 && sx < 320 && sy >= 0 && sy < 200) {
            int i = (sy / 8) * 40 + sx / 8;
            uint8_t ch = m[(uint16_t)(base + i)];
            int on = (cg[cset + ch * 8 + (sy & 7)] >> (7 - (sx & 7))) & 1;
            c = on ? cr[i] & 15 : bg;
        }
        fwrite(pal[c], 1, 3, f);
    }
    fclose(f);
}

static void screen_dump(void)
{
    screen_shot();
    const uint8_t *m = c64_ram();
    uint16_t base = c64_screen();
    printf("    +----------------------------------------+ screen at $%04X\n", base);
    for (int r = 0; r < 25; r++) {
        printf("    |");
        for (int c = 0; c < 40; c++) {
            uint8_t v = m[(uint16_t)(base + r * 40 + c)] & 0x7F;           /* reverse video shown plain */
            putchar(v == 0 ? '@' : v < 0x1B ? 'A' + v - 1 : v >= 0x20 && v < 0x40 ? v : v == 0x1E ? '^' : v == 0x1F ? '<' : '.');
        }
        printf("|\n");
    }
    printf("    +----------------------------------------+\n");
}

int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "usage: f6 <dos> <kernal> <basic> <root> <outdir> command...\n"); return 2; }
    load(argv[1], dos, sizeof dos); load(argv[2], kernal, sizeof kernal); load(argv[3], basic, sizeof basic);
    root = argv[4];
    /* the 1541, booted once, off the bus */
    drive_init(dos, &nodisk);
    drive_run(2500000);
    drive_save(&booted);
    drive_last = D.cycles; drive_base = 0;
    browse_init(&br, &dev, &fsops, 0, buf, sizeof buf);
    trace_ops = browse_ops; trace_ops.unlisten = tr_unlisten; trace_ops.talk_byte = tr_talk;
    trace_ops.listen_byte = log_byte;
    dev.ops = &trace_ops;
    dev.trace = log_cmd;                              /* the conversation, for a replay */
    dev.atn_end = log_atn_end;
    c64_init(kernal, basic);
    if (!run_until_pc(READY_LOOP, 5000000)) { printf("C64 never reached READY\n"); return 1; }
    int fail = 0;
    for (int i = 6; i < argc; i++) {
        uint8_t *m = c64_ram();
        char kind = 'L';
        const char *a = argv[i];
        if (!strncmp(a, "type:", 5)) { type_keys(a + 5); printf("%s\n", argv[i]); continue; }
        if (!strncmp(a, "wait:", 5)) { advance(atof(a + 5)); continue; }
        if (!strcmp(a, "screen:")) { screen_dump(); continue; }
        if (!strncmp(a, "peek:", 5)) {                 /* the emulated drive's RAM (a test probe) */
            unsigned ad = (unsigned)strtoul(a + 5, 0, 16);
            printf("  [drive RAM $%04X = $%02X%s]\n", ad, D.ram[ad & 0x7FF], online ? "" : " (drive not on the bus)");
            continue;
        }
        if (!strcmp(a, "state:")) {
            printf("  [state: C64 pc $%04X; lines (low) %s%s%s; C64 pulls %X, device %X; device pc %d L%d T%d sa %d]\n",
                   c64_pc(), (c64_low | dev_low) & 1 ? "ATN " : "", (c64_low | dev_low) & 2 ? "CLK " : "",
                   (c64_low | dev_low) & 4 ? "DATA" : "", c64_low, dev_low, dev.pc, dev.listening, dev.talking, dev.sa);
            continue;
        }
        if (!strncmp(a, "load:", 5)) a += 5;
        else if (!strncmp(a, "cmd:", 4)) { kind = 'C'; a += 4; }
        else if (!strncmp(a, "cmdhex:", 7)) {           /* a command as hex bytes (binary: M-W, M-E, M-R) */
            kind = 'C'; static char hx[64]; int hn = 0;
            for (const char *q = a + 7; q[0] && q[1] && hn < 63; q += 2) { unsigned v; sscanf(q, "%2x", &v); hx[hn++] = (char)v; }
            hx[hn] = 0; a = hx;
            memcpy(m + 0xC100, hx, (size_t)hn);
            uint16_t done = build(m, kind, (size_t)hn);
            c64_jump(0xC000);
            int ok = run_until_pc(done, 30u * C64_HZ);
            printf("%s: %s\n", argv[i], ok ? "done" : "TIMEOUT");
            c64_jump(READY_LOOP);
            continue;
        }
        else if (!strncmp(a, "err:", 4)) { kind = 'E'; a += 4; }
        size_t len = strlen(a);
        for (size_t k = 0; k < len; k++)   /* ASCII to PETSCII; '_' is the left arrow ($5F) as typed */
            m[0xC100 + k] = (uint8_t)(a[k] >= 'a' && a[k] <= 'z' ? a[k] - 32 : a[k]);
        memset(m + 0x1000, 0, 0x9000); memset(m + 0xC200, 0xEE, 6); memset(m + 0xC300, 0, 64);
        uint16_t done = build(m, kind, len);
        uint64_t t0 = c64_cycles();
        c64_jump(0xC000);
        int ok = run_until_pc(done, 300u * C64_HZ);
        unsigned end = m[0xC200] | (m[0xC201] << 8);
        int err = (m[0xC203] & 1) ? m[0xC202] : 0;
        printf("%s: %s", argv[i], ok ? "done" : "TIMEOUT");
        if (kind == 'L') printf(" end=$%04X bytes=%u", end, end >= 0x1000 ? end - 0x1000 : 0);
        printf(" err=%d status=$%02X time=%.2fs\n", err, m[0xC204], (double)(c64_cycles() - t0) / C64_HZ);
        char path[512]; FILE *f;
        if (kind == 'L' && ok && !err && end > 0x1000) {
            snprintf(path, sizeof path, "%s/%d.bin", argv[5], i - 5);
            if ((f = fopen(path, "wb"))) { fwrite(m + 0x1000, 1, end - 0x1000, f); fclose(f); }
            if (len == 1 && a[0] == '$') {               /* print the listing as text */
                const uint8_t *b = m + 0x1000; int k = 0;
                while (k + 4 < (int)(end - 0x1000) && (b[k] | b[k + 1])) {
                    printf("    %u ", b[k + 2] | (b[k + 3] << 8)); k += 4;
                    while (b[k]) { uint8_t c = b[k++]; putchar(c >= 0xC1 && c <= 0xDA ? c - 0x80 : c == 0x12 ? ' ' : c < 0x20 || c > 0x7E ? '.' : c); }
                    putchar('\n'); k++;
                }
            }
        }
        if (kind == 'E') {
            int n = m[0xC205] == 0xEE ? 0 : m[0xC205];
            printf("    error channel: \"");
            for (int k = 0; k < n; k++) { uint8_t c = m[0xC300 + k]; putchar(c == '\r' ? '|' : c < 0x20 || c > 0x7E ? '.' : c); }
            printf("\" hex");
            for (int k = 0; k < n && k < 16; k++) printf(" %02X", m[0xC300 + k]);
            printf("\n");
        }
        if (!ok) fail = 1;
        c64_jump(READY_LOOP);    /* the driver ends in JMP *: back to BASIC's key loop, where it came from */
    }
    return fail;
}
