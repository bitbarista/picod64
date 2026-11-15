/* -------------------------------------------------------------------------
 * fw/ui.c on the host: the knob decoder against quadrature sequences (with
 * bounce and a missed state), the long press, and the screens over a FAT image
 * (fw/storage.c), each frame written as a picture through the SH1106 page
 * layout (oled_page), so what is drawn is what the board would send.
 *   fw_ui_test <card.img> <outprefix>
 * ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw/ui.h"
#include "../fw/svc.h"
#include "../fw/storage.h"
#include "../fw/font5x7.h"
#include "../fw/font_pet57.h"

int diskio_host_open(const char *path);

static int fails;
#define CHECK(c, msg) do { if (c) printf("ok    %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while (0)

/* --- the knob --------------------------------------------------------------- */
static enc_t k; static uint32_t t;
static int feed(int a, int b, int sw) { t += 1; return enc_update(&k, a, b, sw, t); }
/* one detent clockwise from rest A=B: (0,0)->(0,1)->(1,1) [A: direction naming] */
static int turn(int dir, int *a, int *b, int bounce)
{
    int ev = EV_NONE, seq[2][2];
    int s = (*a << 1) | *b;
    static const int nxt[4] = { 1, 3, 0, 2 }, prv[4] = { 2, 0, 3, 1 };   /* Gray order 00 01 11 10 */
    int s1 = dir > 0 ? nxt[s] : prv[s], s2 = dir > 0 ? nxt[s1] : prv[s1];
    seq[0][0] = s1 >> 1; seq[0][1] = s1 & 1; seq[1][0] = s2 >> 1; seq[1][1] = s2 & 1;
    int r;
    if ((r = feed(seq[0][0], seq[0][1], 1))) ev = r;
    if (bounce) {                                            /* back and forth on the first edge */
        if ((r = feed(*a, *b, 1))) ev = r;
        if ((r = feed(seq[0][0], seq[0][1], 1))) ev = r;
    }
    if ((r = feed(seq[1][0], seq[1][1], 1))) ev = r;
    *a = seq[1][0]; *b = seq[1][1];
    return ev;
}

/* --- the screen as a picture ----------------------------------------------------- */
static void shot(const ui_t *u, const char *name)
{
    uint8_t cmd[3], data[132];
    static uint8_t img[64][132];
    for (int p = 0; p < 8; p++) {
        oled_page(u, p, cmd, data);
        for (int x = 0; x < 132; x++) for (int r = 0; r < 8; r++) img[p * 8 + r][x] = (data[x] >> r) & 1;
    }
    FILE *f = fopen(name, "wb");
    const int S = 4;                                     /* 4x, columns 2..129 as the panel shows */
    fprintf(f, "P6 %d %d 255\n", 128 * S, 64 * S);
    for (int y = 0; y < 64 * S; y++) for (int x = 0; x < 128 * S; x++) {
        int on = img[y / S][x / S + 2];
        unsigned char c[3] = { (unsigned char)(on ? 120 : 8), (unsigned char)(on ? 200 : 12), (unsigned char)(on ? 255 : 30) };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

/* --- a fake platform: the UI's actions recorded --------------------------------- */
static char mounted[BR_PATH]; static int online, ejected;
static void io_mount(const char *p) { strncpy(mounted, p, BR_PATH - 1); online = 1; }
static void io_eject(void) { ejected++; online = 0; mounted[0] = 0; }
static char prg_path[BR_PATH], prg_name[BR_NAME];
static void io_mount_prg(const char *p) { strncpy(prg_path, p, BR_PATH - 1); strncpy(prg_name, strrchr(p, '/') + 1, BR_NAME - 1); mounted[0] = 0; online = 1; }
static int io_list(const char *p, int i, fsent_t *e) { return st_list(p, i, e); }
static int io_newdisk(const char *p, const uint8_t *n, int nl, uint8_t i1, uint8_t i2) { return svc_newdisk(p, n, nl, i1, i2); }
static int snd_state = 1;
static int io_sound(int toggle) { if (toggle) snd_state = !snd_state; return snd_state; }

int main(int argc, char **argv)
{
    if (argc < 3 || !diskio_host_open(argv[1]) || !st_init()) { fprintf(stderr, "usage / image\n"); return 2; }
    static uint8_t cgrom[4096];                        /* argv[3]: the C64 font, to see it (never shipped) */
    if (argc > 3) { FILE *cf = fopen(argv[3], "rb"); if (cf && fread(cgrom, 1, 4096, cf) == 4096) ui_font(cgrom); if (cf) fclose(cf); }
    const char *pre = argv[2];
    char name[512];

    /* a list row's name */
    { char o[32], nm[80];
      ui_row_name("Compilation 02.d64", 1, 19, 0, o); CHECK(!strcmp(o, "Compilation 02"), "row: .d64 hidden");
      ui_row_name("Harbour Run.prg", 0, 19, 0, o); CHECK(!strcmp(o, "Harbour Run.prg"), "row: another extension stays");
      ui_row_name("Starfall (Disk 2 of 3).d64", 1, 19, 0, o); CHECK(!strcmp(o, "Starfall..Disk 2"), "row: long: start, and which disk");
      ui_row_name("Crystal Caverns of Doom side b.d64", 1, 19, 0, o); CHECK(!strcmp(o, "Crystal Cav..side b"), "row: long: which side");
      ui_row_name("The Unreasonably Long Adventure 7.d64", 1, 19, 0, o); CHECK(!strcmp(o, "The Unreaso..ture 7"), "row: long, no disk word: start and end");
      /* every length, both widths, every moment: never too wide; sliding reaches the end of the name and comes back */
      int bad = 0, noend = 0, noback = 0;
      for (int w = 14; w <= 19; w += 5) for (int len = 1; len <= 59; len++) {
          for (int i = 0; i < len; i++) nm[i] = (char)('a' + i % 26);
          strcpy(nm + len, ".d64");
          int end = 0, back = 0, moved = 0;
          for (uint32_t held = 0; held < 20000; held += 100) {
              ui_row_name(nm, 1, w, held, o);
              if ((int)strlen(o) > w || strstr(o, ".d64")) bad++;
              if (len > w && held >= UI_SLIDE_WAIT_MS) {
                  if (!strncmp(o, nm + len - w, (size_t)w)) { end = 1; moved = 1; }
                  if (moved && !strncmp(o, nm, (size_t)w)) back = 1;
              }
          }
          if (len > w && !end) noend++;
          if (len > w && !back && len - w < 40) noback++;
      }
      CHECK(!bad, "row: never over the width or showing .d64, at any length or moment");
      CHECK(!noend, "row: sliding reaches the name's end, at every length");
      CHECK(!noback, "row: and comes back to the start");
      char c1[32], lng[] = "A name long enough that it has to move along.d64";
      ui_row_name(lng, 1, 19, UI_SLIDE_WAIT_MS - 1, c1);
      ui_row_name(lng, 1, 19, UI_SLIDE_WAIT_MS + UI_SLIDE_STEP_MS, o);
      CHECK(!strcmp(o, " name long enough t") && strcmp(c1, o), "row: selected: waits, then slides a character a step");
      CHECK(UI_SLIDE_WAIT_MS + (BR_NAME - 1 - 4 - 19) * UI_SLIDE_STEP_MS < UI_IDLE_MS, "row: the longest name finishes sliding within the idle time");
    }

    /* knob */
    int a = 0, b = 0, cw = 0, ccw = 0;
    for (int i = 0; i < 30; i++) cw += turn(+1, &a, &b, i % 3 == 0) == EV_CW;
    CHECK(cw == 30, "knob: 30 detents clockwise (every third with contact bounce) = 30 CW");
    for (int i = 0; i < 30; i++) ccw += turn(-1, &a, &b, i % 4 == 0) == EV_CCW;
    CHECK(ccw == 30, "knob: 30 back = 30 CCW");
    int r1 = feed(!a, !b, 1); int r2 = feed(a, b, 1);       /* both lines at once (a state missed), and back */
    CHECK(r1 == EV_NONE && r2 == EV_NONE, "knob: a missed state gives no step");
    int r3 = turn(+1, &a, &b, 0);
    CHECK(r3 == EV_CW, "knob: and the next detent counts normally");
    int ev = 0; t += 100; feed(a = 0, b = 0, 0); t += 100; ev = feed(0, 0, 1);
    CHECK(ev == EV_PRESS, "push: 100 ms = a press");
    t += 100; feed(0, 0, 0); int got_long = 0;
    for (int i = 0; i < 1000; i += 10) { t += 10; if (enc_update(&k, 0, 0, 0, t) == EV_LONG) got_long++; }
    t += 10; int after = enc_update(&k, 0, 0, 1, t);
    CHECK(got_long == 1 && after == EV_NONE, "push: held 1 s = one long press, no press on release");

    /* screens */
    static ui_t u; static ui_io_t io;
    io.card = 1; io.browse_dir = "/"; io.disk = mounted; io.mount = io_mount; io.eject = io_eject; io.list = io_list;
    io.newdisk = io_newdisk; io.sound = io_sound; io.mount_prg = io_mount_prg; io.prg = prg_name;
    ui_init(&u, &io);
    uint32_t ms = 1000;
    io.online = online; ui_draw(&u, ms); snprintf(name, sizeof name, "%s01_status.ppm", pre); shot(&u, name);
    ui_event(&u, EV_CW, ms); ui_draw(&u, ms); snprintf(name, sizeof name, "%s02_files.ppm", pre); shot(&u, name);
    CHECK(u.screen == 1, "turn on STATUS: FILES");
    /* find /sets and go in; pick Game-1.d64 */
    fsent_t e; int sets = -1;
    for (int i = 0; st_list("/", i, &e); i++) if (!strcmp(e.name, "sets")) sets = i;
    for (int i = 0; i < sets; i++) ui_event(&u, EV_CW, ms);
    ui_event(&u, EV_PRESS, ms); ui_draw(&u, ms); snprintf(name, sizeof name, "%s03_sets.ppm", pre); shot(&u, name);
    CHECK(!strcmp(u.dir, "/sets"), "press on a folder: into it");
    ui_event(&u, EV_CW, ms);                                  /* past ".." */
    ui_event(&u, EV_PRESS, ms); io.online = online;
    CHECK(!strcmp(mounted, "/sets/Game-1.d64") && u.screen == 0, "press on a D64: inserted, back to STATUS");
    ui_draw(&u, ms); snprintf(name, sizeof name, "%s04_inserted.ppm", pre); shot(&u, name);
    ms += 3000; ui_event(&u, EV_LONG, ms); io.online = online;
    CHECK(!strcmp(mounted, "/sets/Game-2.d64") && u.set_pos == 2 && u.set_count == 2, "hold on STATUS: next disk of the set (2/2)");
    ms += 3000; ui_draw(&u, ms); snprintf(name, sizeof name, "%s05_next.ppm", pre); shot(&u, name);
    ui_event(&u, EV_CW, ms); ui_draw(&u, ms); snprintf(name, sizeof name, "%s06_files_mounted.ppm", pre); shot(&u, name);
    CHECK(u.screen == 1 && !strcmp(u.dir, "/sets"), "FILES while a disk is in: opens in its folder");
    /* the cursor starts on the disk that's in, so a second press changes nothing */
    { fsent_t cur; int want = -1;
      for (int i = 0; st_list("/sets", i, &cur); i++) if (!strcmp(cur.name, "Game-2.d64")) want = i;
      CHECK(want >= 0 && u.sel == want + 2, "FILES while a disk is in: the cursor on that disk (past Eject, ..)");
      int sp = u.set_pos, sc = u.set_count; int unmount0 = ejected; char was[BR_PATH]; strcpy(was, mounted); u.msg[0] = 0;
      ui_event(&u, EV_PRESS, ms); io.online = online;
      CHECK(ejected == unmount0 && !strcmp(mounted, was) && online, "press on the disk that's in: not ejected, not changed");
      CHECK(u.screen == 0 && u.msg[0] == 0 && u.set_pos == sp && u.set_count == sc, "and back to STATUS, no message, its place in the set kept");
      ui_event(&u, EV_CW, ms); }
    /* Settings: the last line, one turn up from the top; Back first; the cursor kept; both ways out */
    { int mid = u.sel;
      for (int g = 0; g < 500 && u.sel > 0; g++) ui_event(&u, EV_CCW, ms);
      ui_event(&u, EV_CCW, ms);
      CHECK(u.sel == u.n - 1, "the list goes round: up from the top is its last line");
      ui_draw(&u, ms); snprintf(name, sizeof name, "%s06b_list_end.ppm", pre); shot(&u, name);
      ui_event(&u, EV_PRESS, ms);
      CHECK(u.screen == 2 && u.set_sel == 0, "last line: Settings, the cursor on Back");
      ui_draw(&u, ms); snprintf(name, sizeof name, "%s06c_settings.ppm", pre); shot(&u, name);
      ui_event(&u, EV_PRESS, ms);
      CHECK(u.screen == 1 && u.sel == u.n - 1, "a second press: back to the list, same place");
      ui_event(&u, EV_PRESS, ms); ui_event(&u, EV_CCW, ms);
      int was_snd = snd_state; ui_event(&u, EV_PRESS, ms);
      CHECK(u.screen == 2 && snd_state != was_snd, "Sound (the last row): toggled, Settings stays");
      ui_event(&u, EV_PRESS, ms);
      ui_event(&u, EV_LONG, ms);
      CHECK(u.screen == 0, "hold in Settings: STATUS");
      ui_event(&u, EV_CW, ms); for (int g = 0; g < 500 && u.sel != u.n - 1; g++) ui_event(&u, EV_CW, ms);
      ui_event(&u, EV_PRESS, ms); uint32_t idle = ms + UI_IDLE_MS + 10; ui_event(&u, EV_NONE, idle);
      CHECK(u.screen == 0, "Settings untouched 15 s: STATUS");
      ms = idle; ui_event(&u, EV_CW, ms); (void)mid; }
    ui_event(&u, EV_CW, ms); for (int g = 0; g < 500 && u.sel > 0; g++) ui_event(&u, EV_CCW, ms);   /* up to the top line: Eject */
    ui_event(&u, EV_PRESS, ms); io.online = online;
    CHECK(ejected == 1 && !online, "press on Eject: ejected");
    ui_event(&u, EV_CW, ms); ms += UI_IDLE_MS + 10; ui_event(&u, EV_NONE, ms);
    CHECK(u.screen == 0, "FILES untouched 15 s: back to STATUS");
    /* New disk: from the list, on the card, put in; the next number when it's taken; and with a disk in */
    ms += 3000; ui_event(&u, EV_CW, ms);                      /* FILES at the root, no disk: up to Settings, New disk */
    ui_event(&u, EV_CCW, ms); ui_event(&u, EV_PRESS, ms); ui_event(&u, EV_CW, ms);
    ui_event(&u, EV_PRESS, ms); io.online = online;
    CHECK(!strcmp(mounted, "/New disk 01.d64") && u.screen == 0 && online, "New disk: made and put in");
    ms += 3000; ui_event(&u, EV_CW, ms);                      /* FILES: Eject, ..., the disk that's in, ..., Settings */
    CHECK(u.sel >= 1, "New disk's disk: the cursor on it too");
    for (int g = 0; g < 500 && u.sel != u.n - 1; g++) ui_event(&u, EV_CW, ms);
    ui_event(&u, EV_PRESS, ms); ui_event(&u, EV_CW, ms);
    ui_event(&u, EV_PRESS, ms); io.online = online;
    CHECK(!strcmp(mounted, "/New disk 02.d64"), "New disk again, a disk in: the next number");
    { fsent_t e2; int have = 0;
      for (int i = 0; st_list("/", i, &e2); i++) if (!strcmp(e2.name, "New disk 01.d64") && e2.size == 174848) have = 1;
      CHECK(have, "New disk: on the card, 174,848 bytes"); }
    /* a program by the knob: its folder's PRGs as a disk, it first (2026-11-06) */
    { io_eject(); io.online = online; io.browse_dir = "/games"; u.screen = 0; u.msg[0] = 0;
      ui_event(&u, EV_CW, ms);                                /* FILES in /games: ".." first */
      fsent_t e3; int at = -1;
      for (int i = 0; st_list("/games", i, &e3); i++) if (!strcmp(e3.name, "pong.prg")) at = i;
      for (int i = 0; i < 1 + at; i++) ui_event(&u, EV_CW, ms);
      ui_event(&u, EV_PRESS, ms); io.online = online;
      CHECK(at >= 0 && !strcmp(prg_path, "/games/pong.prg") && u.screen == 0 && online && !strcmp(u.msg, "LOAD\"*\",8,1"),
            "press on a PRG: its folder as a disk, it first; says LOAD\"*\",8,1");
      ui_draw(&u, ms); snprintf(name, sizeof name, "%s06b_prg.ppm", pre); shot(&u, name);
      io.browse_dir = "/"; }
    io.card = 0; ui_draw(&u, ms); snprintf(name, sizeof name, "%s07_nocard.ppm", pre); shot(&u, name);
    /* the fonts: the C64-style 5x7 unless the card has another; the file we offer is our plain one */
    { static uint8_t f[UI_FONT57_SIZE + 1], deflt[sizeof u.fb], pet[sizeof u.fb], plain[sizeof u.fb];
      FILE *ff = fopen("../fw/fonts/font5x7.bin", "rb"); size_t got = ff ? fread(f, 1, sizeof f, ff) : 0; if (ff) fclose(ff);
      CHECK(got == UI_FONT57_SIZE && !memcmp(f, font5x7, UI_FONT57_SIZE), "font5x7.bin: right size, the plain font's glyphs");
      ui_draw(&u, ms); memcpy(deflt, u.fb, sizeof u.fb);
      ui_font57(&font_pet57[0][0]); ui_draw(&u, ms); memcpy(pet, u.fb, sizeof u.fb);
      ui_font57(f); ui_draw(&u, ms); memcpy(plain, u.fb, sizeof u.fb); snprintf(name, sizeof name, "%s08_plain.ppm", pre); shot(&u, name);
      CHECK(!memcmp(deflt, pet, sizeof u.fb), "font: the default is the C64-style 5x7");
      CHECK(memcmp(deflt, plain, sizeof u.fb), "font: the file from the card is what's drawn");
      ui_font57(0); }
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
