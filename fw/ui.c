/* PicoD64 -- the knob and the OLED: see ui.h */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "diskset.h"
#include "font_pet57.h"

/* --- knob ------------------------------------------------------------------- */
/* Gray order of (A,B): 00 01 11 10. A step +1 one way, -1 the other; a detent is
 * two steps in one direction landing on A == B (15 pulses, 30 detents). */
static int gray_pos(int ab) { static const int8_t p[4] = { 0, 1, 3, 2 }; return p[ab & 3]; }
int enc_update(enc_t *k, int a, int b, int sw, uint32_t ms)
{
    int ev = EV_NONE;
    int ab = (a << 1) | b;
    if (ab != k->ab) {
        int d = (gray_pos(ab) - gray_pos(k->ab)) & 3;
        if (d == 1) k->step++; else if (d == 3) k->step--;        /* 2: a missed state, ignore */
        k->ab = (uint8_t)ab;
        if (a == b) {                                              /* at rest */
            if (k->step >= 2) ev = EV_CW; else if (k->step <= -2) ev = EV_CCW;
            k->step = 0;
        }
    }
    /* push, debounced 20 ms [A] */
    int pressed = !sw;
    if (pressed != k->sw && ms - k->sw_last_change >= 20) {
        k->sw = (uint8_t)pressed; k->sw_last_change = ms;
        if (pressed) { k->sw_since = ms; k->long_sent = 0; }
        else if (!k->long_sent && ev == EV_NONE) ev = EV_PRESS;
    }
    if (k->sw && !k->long_sent && ms - k->sw_since >= UI_LONG_MS && ev == EV_NONE) { k->long_sent = 1; ev = EV_LONG; }
    return ev;
}

/* --- drawing ---------------------------------------------------------------- */
static void px(ui_t *u, int x, int y, int on)
{
    if (x < 0 || x >= UI_W || y < 0 || y >= UI_H) return;
    if (on) u->fb[y >> 3][x] |= (uint8_t)(1u << (y & 7)); else u->fb[y >> 3][x] &= (uint8_t)~(1u << (y & 7));
}
/* The font: the PETSCII-style 5x7, 6 px, 21 a line, because a file's name has to be readable (2026-11-04,
 * after trying 16 a line in host/panel-demo). From the card, if the user wants: /font5x7.bin, another 5x7
 * (ours is fw/fonts/font5x7.bin); or /chargen.bin, the user's C64 character ROM, not shipped, 8 px, 16 a line
 * (its lower/upper case set [S12: the second 2 KB]) */
static const uint8_t *cg;          /* the C64 character ROM (4096), or null */
static const uint8_t *card57;      /* a 5x7 from the card (UI_FONT57_SIZE), or null */
void ui_font(const uint8_t *chargen) { cg = chargen; }
void ui_font57(const uint8_t *glyphs) { card57 = glyphs; }
static int CW(void) { return cg ? 8 : 6; }
static int COLS(void) { return UI_W / CW(); }
static uint8_t screen_code(int c)   /* ASCII -> a screen code of the lower/upper case set */
{
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 1);
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 65);
    if (c == '@') return 0;
    if (c == '[') return 27; if (c == ']') return 29; if (c == '^') return 30; if (c == '\\') return 77;
    if (c == '_') return 100; if (c == '`') return 39; if (c == '{') return 27; if (c == '}') return 29;
    if (c == '|') return 93; if (c == '~') return 30;
    return (uint8_t)(c >= 32 && c < 64 ? c : '?');
}
static int text(ui_t *u, int x, int y, const char *s, int inv)
{
    int w = CW();
    for (; *s && x < UI_W; s++, x += w) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        for (int col = 0; col < w; col++) {
            if (cg) {
                const uint8_t *g = cg + 0x800 + screen_code(c) * 8;
                for (int r = 0; r < 8; r++) px(u, x + col, y + r, ((g[r] >> (7 - col)) & 1) != inv);
            } else {
                uint8_t bits = col >= 5 ? 0 : card57 ? card57[(c - 32) * 5 + col] : font_pet57[c - 32][col];
                for (int r = 0; r < 8; r++) px(u, x + col, y + r, ((bits >> r) & 1) != inv);
            }
        }
    }
    return x;
}
static void bar(ui_t *u, int y, int inv) { for (int x = 0; x < UI_W; x++) for (int r = 0; r < 9; r++) px(u, x, y + r, inv); }
static void line(ui_t *u, int y) { for (int x = 0; x < UI_W; x++) px(u, x, y, 1); }

/* the end of a long name: at most n characters, "..." in front if cut */
static const char *tail(const char *s, int n, char *buf)
{
    int l = (int)strlen(s);
    if (l <= n) return s;
    strcpy(buf, "..."); strcpy(buf + 3, s + l - (n - 3));
    return buf;
}
static const char *base(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

/* A list row's name in w characters. A D64's ".d64" goes: the row's mark says it. If it still doesn't
 * fit: the start, "..", and what tells it from its neighbours: which disk it is if it says, or else its
 * end. Once the row has been selected a moment (held ms), it slides to show the whole name, waits, returns. */
void ui_row_name(const char *n, int hide_ext, int w, uint32_t held, char *o)
{
    int l = (int)strlen(n), s, e, h;
    if (hide_ext) l -= 4;
    if (l <= w) { memcpy(o, n, (size_t)l); o[l] = 0; return; }
    if (held >= UI_SLIDE_WAIT_MS) {
        int span = l - w, pos = (int)((held - UI_SLIDE_WAIT_MS) / UI_SLIDE_STEP_MS) % (span + UI_SLIDE_REST);
        memcpy(o, n + (pos > span ? span : pos), (size_t)w); o[w] = 0;
        if (pos >= span + UI_SLIDE_REST / 2) { memcpy(o, n, (size_t)w); }  /* back at the start for the rest of the wait */
        return;
    }
    int tl = 0, ts = 0;
    if (nm_token(n, l, &s, &e)) {                      /* "(Disk 1 of 3)" -> "Disk 1" */
        if (n[s] == '(') s++;
        if (e > s && n[e - 1] == ')') e--;
        for (int i = s; i + 3 < e; i++) if (!strncmp(n + i, " of ", 4)) { e = i; break; }
        h = w - 2 - (e - s);
        if (h > s) h = s;
        if (h >= 4) { ts = s; tl = e - s; }
    }
    if (!tl) { tl = w / 3; ts = l - tl; h = w - 2 - tl; }
    while (h > 1 && strchr(" (-_[", n[h - 1])) h--;    /* no gap before the dots */
    memcpy(o, n, (size_t)h); memcpy(o + h, "..", 2); memcpy(o + h + 2, n + ts, (size_t)tl); o[h + 2 + tl] = 0;
}

/* --- the FILES list: "Eject" and ".." ahead of the folder, "Settings" after it --- */
static int extra(ui_t *u) { return (strcmp(u->dir, "/") ? 1 : 0) + (u->io->online ? 1 : 0); }
static int has_settings(ui_t *u) { return u->io->newdisk || u->io->sound; }
static int entry(ui_t *u, int i, fsent_t *e, int *kind)   /* kind: 0 file/dir, 1 "..", 2 eject, 5 settings */
{
    if (has_settings(u) && i == u->n - 1) { *kind = 5; strcpy(e->name, "Settings"); e->dir = 1; return 1; }
    if (u->io->online) { if (i == 0) { *kind = 2; strcpy(e->name, "Eject disk"); e->dir = 0; return 1; } i--; }
    if (strcmp(u->dir, "/")) { if (i == 0) { *kind = 1; strcpy(e->name, ".."); e->dir = 1; return 1; } i--; }
    *kind = 0;
    return u->io->list(u->dir, i, e);
}
static int count(ui_t *u)
{
    int s = has_settings(u) ? 1 : 0;
    if (u->io->count) return u->io->count(u->dir) + extra(u) + s;
    fsent_t e; int n = 0;
    while (u->io->list(u->dir, n, &e)) n++;
    return n + extra(u) + s;
}
/* --- the SETTINGS screen: Back first, so that a second press of the knob is harmless --- */
static int settings_n(ui_t *u) { return 1 + (u->io->newdisk ? 1 : 0) + (u->io->sound ? 1 : 0); }
static int settings_row(ui_t *u, int i, char *name)       /* kind: 0 back, 3 new disk, 4 sound */
{
    if (i == 0) { strcpy(name, "Back"); return 0; }
    if (u->io->newdisk && --i == 0) { strcpy(name, "New disk"); return 3; }
    strcpy(name, u->io->sound(0) ? "Sound: on" : "Sound: off"); return 4;
}

static int is_d64(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'd' && n[l - 2] == '6' && n[l - 1] == '4';
}

/* a program, as the browser and the virtual disk take one (svc.c): NAME.PRG, or no extension */
static int is_prog(const char *n)
{
    size_t l = strlen(n);
    if (l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'p' && (n[l - 2] | 32) == 'r' && (n[l - 1] | 32) == 'g') return 1;
    return !strchr(n, '.');
}

static void say(ui_t *u, const char *m, uint32_t ms) { strncpy(u->msg, m, sizeof u->msg - 1); u->msg[sizeof u->msg - 1] = 0; u->msg_until = ms + 2000; }

void ui_init(ui_t *u, ui_io_t *io)
{
    memset(u, 0, sizeof *u);
    u->io = io; strcpy(u->dir, "/");
}

static void enter_files(ui_t *u)
{
    u->screen = 1;
    if (u->io->online && u->io->disk[0]) {            /* start in the mounted disk's folder */
        const char *s = strrchr(u->io->disk, '/');
        size_t l = s ? (size_t)(s - u->io->disk) : 0;
        if (l == 0) strcpy(u->dir, "/"); else { memcpy(u->dir, u->io->disk, l); u->dir[l] = 0; }
    } else if (u->io->browse_dir) strncpy(u->dir, u->io->browse_dir, BR_PATH - 1);
    u->sel = 0; u->top = 0; u->n = count(u);
    /* the cursor on the disk that's in, not on Eject: two presses of the knob must not pull the
     * disk out from under a running game */
    if (u->io->online && u->io->disk[0]) {
        fsent_t e; const char *name = base(u->io->disk);
        for (int i = 0; u->io->list(u->dir, i, &e); i++)
            if (!strcmp(e.name, name)) {
                u->sel = extra(u) + i;
                if (u->sel >= UI_ROWS) {                  /* with its neighbours in sight */
                    u->top = u->sel - 2;
                    if (u->top > u->n - UI_ROWS) u->top = u->n - UI_ROWS;
                }
                break;
            }
    }
}

/* "New disk": the game says "insert a blank disk", and the C64 is busy, so this must be here on the
 * drive. New disk 01.d64, or the next number free, in this folder; put in as next disk puts one in, so the
 * DOS sees the change. Each its own ID, so it can't be taken for the last */
static void new_disk(ui_t *u, uint32_t ms)
{
    char path[BR_PATH];
    for (int n = 1; n <= 99; n++) {
        char nm[17];
        int nl = snprintf(nm, sizeof nm, "NEW DISK %02d", n);
        snprintf(path, sizeof path, "%s/New disk %02d.d64", strcmp(u->dir, "/") ? u->dir : "", n);
        int r = u->io->newdisk(path, (const uint8_t *)nm, nl, (uint8_t)('0' + n / 10), (uint8_t)('0' + n % 10));
        if (r < 0) continue;
        if (!r) { say(u, "Can't write", ms); return; }
        u->io->mount(path);
        u->set_pos = u->set_count = 0; u->screen = 0;
        say(u, "New disk in", ms);
        return;
    }
    say(u, "Too many", ms);
}

static void next_disk(ui_t *u, uint32_t ms)
{
    if (!u->io->online || !u->io->disk[0]) { say(u, "No disk in", ms); return; }
    char dir[BR_PATH], nx[BR_NAME], path[BR_PATH];
    const char *s = strrchr(u->io->disk, '/');
    size_t l = s ? (size_t)(s - u->io->disk) : 0;
    if (l == 0) strcpy(dir, "/"); else { memcpy(dir, u->io->disk, l); dir[l] = 0; }
    if (!ds_next(dir, base(u->io->disk), nx, sizeof nx, u->io->list, &u->set_pos, &u->set_count)) { say(u, "No next disk", ms); return; }
    snprintf(path, sizeof path, "%s/%s", strcmp(dir, "/") ? dir : "", nx);
    u->io->mount(path);
    say(u, "Next disk", ms);
}

void ui_next_disk(ui_t *u, uint32_t ms) { next_disk(u, ms); }   /* the knob's long press, asked for over the bus (XN) */

void ui_event(ui_t *u, int ev, uint32_t ms)
{
    if (ev == EV_NONE) {
        if (u->screen != 0 && ms - u->last_ms >= UI_IDLE_MS) u->screen = 0;
        return;
    }
    u->last_ms = ms;
    if (!u->io->card) { say(u, "No card", ms); return; }
    if (u->screen == 0) {
        if (ev == EV_LONG) next_disk(u, ms);
        else enter_files(u);
        return;
    }
    if (u->screen == 2) {
        int n = settings_n(u);
        char nm[16];
        if (ev == EV_CW) u->set_sel = (u->set_sel + 1) % n;
        else if (ev == EV_CCW) u->set_sel = (u->set_sel + n - 1) % n;
        else if (ev == EV_LONG) u->screen = 0;
        else if (ev == EV_PRESS) {
            int kind = settings_row(u, u->set_sel, nm);
            if (kind == 0) u->screen = 1;
            else if (kind == 3) new_disk(u, ms);          /* in the folder the list was showing */
            else u->io->sound(1);
        }
        return;
    }
    switch (ev) {                                         /* the list goes round: Settings is one turn up from the top */
    case EV_CW:  if (u->n > 0) u->sel = (u->sel + 1) % u->n; break;
    case EV_CCW: if (u->n > 0) u->sel = (u->sel + u->n - 1) % u->n; break;
    case EV_LONG: u->screen = 0; break;
    case EV_PRESS: {
        fsent_t e; int kind;
        if (!entry(u, u->sel, &e, &kind)) break;
        if (kind == 2) { u->io->eject(); u->screen = 0; say(u, "Ejected", ms); break; }
        if (kind == 5) { u->screen = 2; u->set_sel = 0; break; }
        if (kind == 1) {
            char *s = strrchr(u->dir, '/');
            if (s && s != u->dir) *s = 0; else strcpy(u->dir, "/");
            u->sel = 0; u->top = 0; u->n = count(u);
            break;
        }
        char path[BR_PATH];
        snprintf(path, sizeof path, "%s/%s", strcmp(u->dir, "/") ? u->dir : "", e.name);
        if (e.dir) { strncpy(u->dir, path, BR_PATH - 1); u->sel = 0; u->top = 0; u->n = count(u); break; }
        if (u->io->online && !strcmp(path, u->io->disk)) { u->screen = 0; break; }   /* it's in already: nothing changes */
        if (is_d64(e.name)) { u->io->mount(path); u->set_pos = u->set_count = 0; u->screen = 0; say(u, "Inserted", ms); break; }
        if (u->io->mount_prg && is_prog(e.name)) {         /* its folder's PRGs as a disk, it first (2026-11-06) */
            u->io->mount_prg(path); u->set_pos = u->set_count = 0; u->screen = 0; say(u, "LOAD\"*\",8,1", ms); break;
        }
        say(u, "Not a disk", ms);
        break;
    }
    }
    if (u->sel < u->top) u->top = u->sel;
    if (u->sel >= u->top + UI_ROWS) u->top = u->sel - (UI_ROWS - 1);
}

int ui_device_step(int dev, int ev)
{
    if (ev == EV_CW) return dev >= 11 ? 8 : dev + 1;
    if (ev == EV_CCW) return dev <= 8 ? 11 : dev - 1;
    return dev;
}
void ui_draw_device(ui_t *u, int dev)
{
    char b[8];
    memset(u->fb, 0, sizeof u->fb);
    bar(u, 0, 1);
    text(u, 2, 1, "Device number", 1);
    snprintf(b, sizeof b, "%d", dev);
    text(u, (UI_W - CW() * (int)strlen(b)) / 2, 22, b, 0);
    text(u, 0, 45, "Turn, press to save", 0);
}
void ui_draw(ui_t *u, uint32_t ms)
{
    char b1[32], b2[32];
    memset(u->fb, 0, sizeof u->fb);
    if (u->screen == 0) {
        bar(u, 0, 1);
        text(u, 2, 1, "PicoD64", 1);
        snprintf(b1, sizeof b1, "#%d", u->io->device ? u->io->device : 8);
        text(u, UI_W - CW() * (int)strlen(b1) - 2, 1, b1, 1);
        if (!u->io->card) {
            text(u, 0, 20, "No SD card", 0);
        } else if (u->io->online) {
            text(u, 0, 14, "DISK", 0);
            if (u->set_count > 1) { snprintf(b1, sizeof b1, "%d/%d", u->set_pos, u->set_count); text(u, UI_W - CW() * (int)strlen(b1), 14, b1, 0); }
            const char *n = u->io->disk[0] ? base(u->io->disk) : u->io->prg && u->io->prg[0] ? u->io->prg : "(fast loader)";
            int l = (int)strlen(n);
            int k = COLS();                                   /* two lines of the name */
            text(u, 0, 26, l > k ? (memcpy(b2, n, (size_t)k), b2[k] = 0, b2) : n, 0);
            if (l > k) text(u, 0, 35, tail(n + k, k, b1), 0);
        } else {
            text(u, 0, 14, "BROWSE", 0);
            text(u, 0, 26, tail(u->io->browse_dir ? u->io->browse_dir : "/", COLS(), b1), 0);
        }
        line(u, 52);
        text(u, 0, 55, u->io->online ? (CW() == 8 ? "hold: next disk" : "turn:files hold:next") : (CW() == 8 ? "turn: files" : "turn/press: files"), 0);
    } else if (u->screen == 2) {
        bar(u, 0, 1);
        text(u, 2, 1, "Settings", 1);
        for (int r = 0; r < settings_n(u); r++) {
            int y = 11 + r * 9, inv = r == u->set_sel;
            if (inv) bar(u, y - 1, 1);
            settings_row(u, r, b1);
            snprintf(b2, sizeof b2, "  %s", b1);
            text(u, 1, y, b2, inv);
        }
    } else {
        bar(u, 0, 1);
        text(u, 2, 1, tail(u->dir, COLS() - 1, b1), 1);
        fsent_t e; int kind;
        for (int r = 0; r < UI_ROWS; r++) {
            int i = u->top + r;
            if (!entry(u, i, &e, &kind)) break;
            int y = 11 + r * 9, inv = i == u->sel;
            if (inv) bar(u, y - 1, 1);
            const char *pre = kind == 2 ? "^ " : e.dir ? "> " : is_d64(e.name) ? "o " : "  ";
            ui_row_name(e.name, kind == 0 && !e.dir && is_d64(e.name), COLS() - 2, inv ? ms - u->last_ms : 0, b1);
            snprintf(b2, sizeof b2, "%s%s", pre, b1);
            text(u, 1, y, b2, inv);
        }
    }
    if (u->msg[0] && (int32_t)(u->msg_until - ms) > 0) {   /* a passing message, boxed, bottom */
        int w = CW() * (int)strlen(u->msg) + 4, x0 = (UI_W - w) / 2;
        for (int x = x0; x < x0 + w; x++) for (int y = 43; y < 54; y++) px(u, x, y, 1);
        text(u, x0 + 2, 45, u->msg, 1);
    }
}

/* --- SH1106 [D: Sino Wealth SH1106 V2.3, "Command Set" pp.19-27] -------------- */
static const uint8_t oled_init[] = {
    0xAE,                   /* 11. display off */
    0xD5, 0x50,             /* 15. clock divide / oscillator (the POR value) */
    0xA8, 0x3F,             /*  9. multiplex 64 */
    0xD3, 0x00,             /* 14. display offset 0 */
    0x40,                   /*  4. start line 0 */
    0xAD, 0x8B,             /* 10. DC-DC on (with display on) */
    0x32,                   /*  3. pump 8.0 V (POR) */
    0xA1,                   /*  6. segment re-map: column 131 -> SEG0 } a module mounted header-up [A]: */
    0xC8,                   /* 13. scan COM63 -> COM0           } check on the board, flip both if upside down */
    0xDA, 0x12,             /* 17. common pads: alternative (POR) */
    0x81, 0x80,             /*  5. contrast 128 (POR) */
    0xA4,                   /*  7. display follows RAM */
    0xA6,                   /*  8. normal (not reversed) */
    0xAF,                   /* 11. display on */
};
int oled_init_seq(const uint8_t **cmds) { *cmds = oled_init; return (int)sizeof oled_init; }

/* page p: the three commands that address it, then 132 data bytes (the RAM is 132
 * wide; a 128-pixel panel shows columns 2..129 [A: the usual 1.3" module]) */
void oled_page(const ui_t *u, int page, uint8_t *cmd3, uint8_t *data132)
{
    cmd3[0] = (uint8_t)(0xB0 | page);   /* 12. page */
    cmd3[1] = 0x00;                     /*  1. column low 0 */
    cmd3[2] = 0x10;                     /*  2. column high 0 */
    memset(data132, 0, 132);
    memcpy(data132 + 2, u->fb[page], UI_W);
}
