/* -------------------------------------------------------------------------
 * PicoD64 -- the knob and the OLED (core 0). Portable: the host draws the
 * frame buffer to a picture (host/fw_ui_test.c).
 *
 * Knob: Alps EC11E15244G1, 15 pulses / 30 detents [D: Alps EC11E catalogue,
 * AUDIT A5]: one detent = half a quadrature cycle, A and B equal at rest [A:
 * check on the part]. Push: a short press < UI_LONG_MS, a long press >= it.
 *
 * Screens (FILES and SETTINGS go round at the ends):
 *   STATUS  what has the bus: BROWSE + folder, or the disk + its place in its
 *           set ("2/3"). Turn or press: FILES. Hold: next disk (F9).
 *   FILES   the card's folders and files, 6 lines; ".." to go up; "Eject" at
 *           the top while a disk is in, and the cursor starts on that disk. Press: open a
 *           folder / insert a D64 / a program (its folder's PRGs as a disk, it first:
 *           LOAD"*",8,1) / eject. Hold, or 15 s untouched: back to STATUS.
 *   SETTINGS the last line of FILES: Back, New disk (in that folder), Sound. Hold, or 15 s: STATUS.
 *   DEVICE  held down at power-on: turn to pick 8-11, press to save.
 * ------------------------------------------------------------------------- */
#ifndef FF_UI_H
#define FF_UI_H
#include <stdint.h>
#include "../core/browse.h"

#define UI_W 128
#define UI_H 64
#define UI_LONG_MS   700        /* [A] to be tried on the real knob */
#define UI_IDLE_MS 15000
#define UI_ROWS 6               /* lines of the FILES list */
#define UI_FONT57_SIZE 475      /* ui_font57(): 95 glyphs, 5 column bytes each, bit 0 on top */
#define UI_SLIDE_WAIT_MS 1000   /* a long name stays selected this long, then slides */
#define UI_SLIDE_STEP_MS 200    /* a character (so the longest name is seen before UI_IDLE_MS) */
#define UI_SLIDE_REST 12        /* steps of the cycle not moving: half at the end, half at the start */

enum { EV_NONE, EV_CW, EV_CCW, EV_PRESS, EV_LONG };

typedef struct {                /* the knob's decoder */
    uint8_t ab, acc;            /* last A/B state; transitions since the last detent (signed, in acc) */
    int8_t  step;
    uint8_t sw, long_sent;
    uint32_t sw_since, sw_last_change;
} enc_t;
/* a, b, sw: pin levels (1 = high; the switch closes to ground: 0 = pressed) */
int enc_update(enc_t *k, int a, int b, int sw, uint32_t ms);

/* what the UI shows and asks for: the platform fills in / acts */
typedef struct {
    /* status, read each frame */
    int card;                   /* a card is in and mounted */
    int online;                 /* the emulated 1541 has the bus */
    const char *disk;           /* card path of the D64 in the drive ("" = virtual) */
    const char *prg;            /* virtual: the program put first (knob or browser); "" or null = a fast loader's */
    const char *browse_dir;     /* browse mode's folder */
    int device;                 /* the IEC device number, 8-11 (0: shown as 8) */
    int activity;               /* the bus is busy now (LED) */
    /* actions */
    void (*mount)(const char *path);     /* insert a D64: browse mode mounts, or a swap if one is in */
    void (*eject)(void);
    void (*mount_prg)(const char *path); /* a program: its folder's PRGs as a disk, it first. Null: "Not a disk" */
    int  (*list)(const char *path, int idx, fsent_t *e);
    int  (*count)(const char *path);        /* entries in the folder: may be null */
    /* a new formatted D64: 1 made, -1 it exists, 0 failed. Null: no "New disk" in Settings */
    int  (*newdisk)(const char *path, const uint8_t *name, int nl, uint8_t id1, uint8_t id2);
    /* the drive sounds: 1 on, 0 off; toggle = 1 changes and saves them. Null: not in Settings */
    int  (*sound)(int toggle);
} ui_io_t;

typedef struct {
    ui_io_t *io;
    int screen;                 /* 0 STATUS, 1 FILES, 2 SETTINGS */
    int set_sel;                /* SETTINGS' cursor */
    char dir[BR_PATH];          /* FILES' folder */
    int sel, top, n;
    uint32_t last_ms;
    char msg[24]; uint32_t msg_until;
    int set_pos, set_count;     /* the mounted disk's place in its set (F9) */
    uint8_t fb[UI_H / 8][UI_W]; /* the SH1106's pages: byte = 8 rows, bit 0 on top */
} ui_t;

void ui_init(ui_t *u, ui_io_t *io);
void ui_font(const uint8_t *chargen);   /* the C64 character ROM (4096 bytes) from the card: 16 a line, or null */
void ui_font57(const uint8_t *glyphs);  /* a 5x7 from the card, or null: the built-in PETSCII-style one. 21 a line */
void ui_next_disk(ui_t *u, uint32_t ms);   /* the next disk of the set, as the knob's long press */
void ui_event(ui_t *u, int ev, uint32_t ms);
void ui_draw(ui_t *u, uint32_t ms);
void ui_row_name(const char *n, int hide_ext, int w, uint32_t held, char *o);   /* w <= 28; o: w + 1 */
int  ui_device_step(int dev, int ev);    /* the next number for a knob event, 8-11, wrapping */
void ui_draw_device(ui_t *u, int dev);   /* the DEVICE screen */

/* the SH1106 over I2C [D: Sino Wealth SH1106 V2.3]: the bytes to send, as
 * (control, data...) transfers; the platform does the I2C writes */
#define OLED_ADDR 0x3C          /* SA0 low: 0111100 [D p.13]; modules strap it [A] */
int  oled_init_seq(const uint8_t **cmds);               /* one command transfer: its length */
void oled_page(const ui_t *u, int page, uint8_t *cmd3, uint8_t *data132);
#endif
