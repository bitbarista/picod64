/* -------------------------------------------------------------------------
 * PicoD64 front panel demo: fw/ui.c and fw/diskset.c themselves, compiled for a
 * browser (or natively, for check.sh). This file stands in for main()'s loop in
 * fw/pico_main.c, the board's pins and the card. Keep its order of events the
 * same as main(): knob, io.online, ui_event, card, draw every 100 ms.
 * ------------------------------------------------------------------------- */
#include "../../fw/ui.c"
#define is_d64 ds_is_d64            /* (a static in both files) */
#include "../../fw/diskset.c"
#include "../../fw/font5x7.h"
#undef is_d64

/* --- the sample card ---------------------------------------------------------- */
typedef struct { const char *dir; char name[BR_NAME]; uint8_t isdir; } node_t;
#define MAXN 160
static node_t fs[MAXN]; static int nfs;
static char made[BR_NAME];
static void add(const char *dir, const char *name, int isdir)
{
    if (nfs == MAXN) return;
    fs[nfs].dir = dir; strncpy(fs[nfs].name, name, BR_NAME - 1); fs[nfs].isdir = (uint8_t)isdir; nfs++;
}
static void card_init(void)
{
    static char names[40][24];
    nfs = 0; made[0] = 0;
    add("/", "Collection", 1); add("/", "Demos", 1); add("/", "Games", 1); add("/", "Utilities", 1);
    add("/", "dos1541.bin", 0); add("/", "font5x7.bin", 0); add("/", "Quick Note.txt", 0); add("/", "Tile Shuffle.d64", 0);
    add("/Games", "Starfall (Disk 1 of 3).d64", 0); add("/Games", "Starfall (Disk 2 of 3).d64", 0); add("/Games", "Starfall (Disk 3 of 3).d64", 0);
    add("/Games", "Crystal Cave side a.d64", 0); add("/Games", "Crystal Cave side b.d64", 0);
    add("/Games", "Moonbase.d64", 0); add("/Games", "Harbour Run.prg", 0);
    add("/Games", "The Unreasonably Long Name of an Adventure Game.d64", 0);
    add("/Games", "Saved games", 1); add("/Games/Saved games", "Slot one.d64", 0);
    add("/Demos", "Copper Dreams.d64", 0); add("/Demos", "Raster Party 1a.d64", 0); add("/Demos", "Raster Party 1b.d64", 0); add("/Demos", "Raster Party 2.d64", 0);
    add("/Utilities", "Disk Tools.d64", 0); add("/Utilities", "Sprite Editor.prg", 0);
    for (int i = 0; i < 40; i++) { snprintf(names[i], sizeof names[i], "Compilation %02d.d64", i + 1); add("/Collection", names[i], 0); }
}
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int before(const node_t *a, const node_t *b)   /* folders first, then the name without case: fw/storage.c */
{
    if (a->isdir != b->isdir) return a->isdir;
    const char *p = a->name, *q = b->name;
    while (*p && lc(*p) == lc(*q)) p++, q++;
    return lc(*p) < lc(*q);
}
static int card_in = 1, has_rom = 1, has_font, sound_on = 1, device = 8;
static int fs_list(const char *path, int idx, fsent_t *e)
{
    if (!card_in) return 0;
    const node_t *sel[MAXN]; int n = 0;
    for (int i = 0; i < nfs; i++) if (!strcmp(fs[i].dir, path)) {
        if (!has_rom && !strcmp(fs[i].name, "dos1541.bin")) continue;
        if (!has_font && !strcmp(fs[i].name, "font5x7.bin")) continue;
        int j = n++;
        while (j > 0 && before(&fs[i], sel[j - 1])) { sel[j] = sel[j - 1]; j--; }
        sel[j] = &fs[i];
    }
    if (idx < 0 || idx >= n) return 0;
    strcpy(e->name, sel[idx]->name); e->dir = sel[idx]->isdir; e->size = 0;
    return 1;
}
static int fs_count(const char *path) { fsent_t e; int n = 0; while (fs_list(path, n, &e)) n++; return n; }
static int file_exists(const char *path)
{
    const char *s = strrchr(path, '/');
    char dir[BR_PATH]; size_t l = (size_t)(s - path);
    if (l == 0) strcpy(dir, "/"); else { memcpy(dir, path, l); dir[l] = 0; }
    for (int i = 0; i < nfs; i++) if (!strcmp(fs[i].dir, dir) && !strcmp(fs[i].name, s + 1)) return 1;
    return 0;
}
static int fs_newdisk(const char *path, const uint8_t *nm, int nl, uint8_t a, uint8_t b)
{
    (void)nm; (void)nl; (void)a; (void)b;
    if (!card_in || nfs == MAXN) return 0;
    if (file_exists(path)) return -1;
    static char dirs[12][BR_PATH]; static int nd;
    const char *s = strrchr(path, '/'); size_t l = (size_t)(s - path);
    const char *dir = "/";
    if (l) {
        char d[BR_PATH]; memcpy(d, path, l); d[l] = 0;
        for (int i = 0; i < nfs && dir[1] == 0; i++) if (!strcmp(fs[i].dir, d)) dir = fs[i].dir;
        if (dir[1] == 0) { if (nd == 12) return 0; strcpy(dirs[nd], d); dir = dirs[nd++]; }  /* a folder with nothing in it yet */
    }
    add(dir, s + 1, 0);
    strncpy(made, s + 1, BR_NAME - 1);
    return 1;
}

/* --- the board: fw/pico_main.c, fw/bus.h ---------------------------------------- */
enum { ST_PICK_RELEASE, ST_PICK, ST_WAIT, ST_RUN };
static int st; static ui_t ui; static ui_io_t io; static enc_t knob;
static int a, b, sw, online;                     /* A, B: the pins; sw: 1 = pushed */
static char mounted[BR_PATH], cwd[BR_PATH] = "/", swap_path[BR_PATH];
static int swap_on; static uint32_t swap_at, last_draw, wait_at, now, pick_since; static int pick_dev, mount_req, eject_req;
static char mount_path[BR_PATH], prg_name[BR_NAME]; static int prg_req;

static void plat_mount(const char *path)
{
    if (online) {
        if (!strcmp(path, mounted) || swap_on) return;
        strncpy(swap_path, path, BR_PATH - 1); swap_on = 1; swap_at = now;
    } else { strncpy(mount_path, path, BR_PATH - 1); mount_req = 1; }
}
static void plat_eject(void) { if (online) eject_req = 1; }
static void plat_mount_prg(const char *path)        /* out with the disk in, then its folder's PRGs as a disk */
{
    if (swap_on) return;
    strncpy(mount_path, path, BR_PATH - 1); prg_req = 1; if (online) eject_req = 1;
}
static int plat_sound(int toggle) { if (toggle) sound_on = !sound_on; return sound_on; }

static void run_event(int ev)
{
    if (st == ST_PICK) {
        if (ev == EV_PRESS) {
            device = pick_dev; io.device = device;
            st = ST_WAIT; wait_at = now - 1000;
            return;
        }
        int nx = ui_device_step(pick_dev, ev);
        if (nx != pick_dev) { pick_dev = nx; ui_draw_device(&ui, pick_dev); }
    } else if (st == ST_RUN) {
        io.online = online;
        ui_event(&ui, ev, now);
        if (ev != EV_NONE) { io.activity = 0; ui_draw(&ui, now); last_draw = now; }
    }
}

/* power on: held = the knob is down */
void sim_power(int held, uint32_t ms)
{
    now = ms;
    memset(&knob, 0, sizeof knob);
    a = b = 0; sw = held;
    online = 0; mounted[0] = 0; strcpy(cwd, "/"); swap_on = mount_req = eject_req = prg_req = 0; prg_name[0] = 0;
    memset(&io, 0, sizeof io);
    io.device = device; io.sound = plat_sound; io.disk = mounted; io.browse_dir = cwd;
    io.mount = plat_mount; io.eject = plat_eject; io.mount_prg = plat_mount_prg; io.prg = prg_name; io.list = fs_list; io.count = fs_count; io.newdisk = fs_newdisk;
    ui_init(&ui, &io);
    ui_font57(0);
    pick_dev = device;
    if (held) { st = ST_PICK_RELEASE; ui_draw_device(&ui, pick_dev); }   /* at once, as device_choose() */
    else { st = ST_WAIT; wait_at = ms - 1000; }
}

/* a pass of main()'s loop. pushed: the knob is down now. */
void sim_tick(uint32_t ms, int pushed)
{
    now = ms; sw = pushed;
    if (st == ST_PICK_RELEASE) {
        if (!sw) { st = ST_PICK; pick_since = ms; }
        return;
    }
    if (st == ST_PICK && ms - pick_since < 50) return;
    if (st == ST_WAIT) {
        if (ms - wait_at < 1000) return;
        wait_at = ms;
        io.card = card_in;
        if (io.card && has_rom) { st = ST_RUN; ui.msg[0] = 0; ui_font57(has_font ? &font5x7[0][0] : 0); ui_draw(&ui, ms); last_draw = ms; return; }
        strcpy(ui.msg, io.card ? "Need /dos1541.bin" : "Insert SD card"); ui.msg_until = ms + 1000000;
        ui_draw(&ui, ms);
        return;
    }
    int ev = enc_update(&knob, a, b, !sw, ms);
    run_event(ev);
    if (st != ST_RUN) return;
    /* core 1's side: a mount, an eject, the 1.4 s until a swap's new disk is in, 1.6 s to the end */
    if (mount_req) { mount_req = 0; online = 1; prg_name[0] = 0; strncpy(mounted, mount_path, BR_PATH - 1); }
    if (eject_req) { eject_req = 0; online = 0; mounted[0] = 0; prg_name[0] = 0; }
    if (prg_req && !online) { prg_req = 0; online = 1; mounted[0] = 0; strncpy(prg_name, strrchr(mount_path, '/') + 1, BR_NAME - 1); }
    if (swap_on == 1 && ms - swap_at >= 1400) { strncpy(mounted, swap_path, BR_PATH - 1); swap_on = 2; }
    if (swap_on == 2 && ms - swap_at >= 1600) swap_on = 0;
    io.card = io.card && card_in;
    if (ms - last_draw >= 100) { ui_draw(&ui, ms); last_draw = ms; }
}

/* one detent: two Gray steps on the pins, through the real decoder */
void sim_turn(int dir, uint32_t ms)
{
    static const int nxt[4] = { 1, 3, 0, 2 }, prv[4] = { 2, 0, 3, 1 };
    if (st == ST_PICK_RELEASE || st == ST_WAIT) return;  /* main() isn't reading the knob */
    now = ms;
    for (int i = 0; i < 2; i++) {
        int s = (a << 1) | b; s = dir > 0 ? nxt[s] : prv[s];
        a = s >> 1; b = s & 1;
        run_event(enc_update(&knob, a, b, !sw, ms));
    }
}
void sim_card(int in, uint32_t ms)
{
    now = ms;
    if (in == card_in) return;
    card_in = in;
    if (st == ST_RUN) { io.card = in; ui_draw(&ui, ms); last_draw = ms; }
}
void sim_rom(int has) { has_rom = has; }
void sim_font_file(int has) { has_font = has; }
void sim_card_reset(void) { card_init(); card_in = has_rom = sound_on = 1; has_font = 0; device = 8; }

/* what the panel shows */
int sim_online(void) { return online; }
int sim_screen(void) { return st == ST_RUN ? ui.screen : st == ST_WAIT ? 3 : 2; }
int sim_booted(void) { return st == ST_RUN; }
int sim_swapping(void) { return swap_on != 0; }
int sim_sound(void) { return sound_on; }
int sim_device(void) { return device; }
int sim_card_in(void) { return card_in; }
const char *sim_mounted(void) { return mounted; }
const char *sim_dir(void) { return ui.dir; }
const char *sim_made(void) { return made; }
/* the picture through oled_page(): the bytes the board would send */
static uint8_t page_out[8][132];
const uint8_t *sim_fb(void)
{
    uint8_t cmd[3];
    for (int p = 0; p < 8; p++) oled_page(&ui, p, cmd, page_out[p]);
    return &page_out[0][0];
}
