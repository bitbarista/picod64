/* -------------------------------------------------------------------------
 * PicoD64 -- the C64's side of the IEC bus, in software: REPLAY.
 *
 * When a program loaded in browse mode starts sending drive code (M-W), the
 * emulated 1541 takes over the bus -- but that program has usually already
 * OPENed its file through the DOS and read from it, and its drive code carries
 * on from the DOS's open file (FB64's fast loader does: OPEN, two bytes, M-W).
 * So the platform REPLAYS that conversation into the freshly booted DOS, over
 * the DOS's own bus lines, before handing the bus over: the DOS ends up as a
 * real 1541 would be.
 *
 * The log is what the browse device saw since the last OPEN of a file: command
 * bytes under ATN, data bytes it was sent (with EOI), and how many bytes it
 * sent back. The controller plays the C64's part [S14 part 4] with the same
 * byte engine (core/iec_dev.h iec_tx / iec_rx): only ATN and the turnaround
 * are its own.
 * ------------------------------------------------------------------------- */
#ifndef FF_IEC_HOST_H
#define FF_IEC_HOST_H
#ifndef FF_C1
#define FF_C1          /* core 1 code: the board puts it in SRAM */
#endif
#include "iec_dev.h"

enum { IECL_ATN, IECL_DATA, IECL_READ, IECL_ATN_END };
typedef struct { uint8_t type, b, eoi; } iecl_op_t;
#define IECL_MAX 512
typedef struct { iecl_op_t op[IECL_MAX]; int n, over; } iecl_t;

static FF_C1 inline void iecl_add(iecl_t *l, uint8_t type, uint8_t b, uint8_t eoi)
{
    /* consecutive reads fold into one count */
    if (type == IECL_READ && l->n && l->op[l->n - 1].type == IECL_READ && l->op[l->n - 1].b < 255) { l->op[l->n - 1].b++; return; }
    if (l->n == IECL_MAX) { l->over = 1; return; }
    l->op[l->n].type = type; l->op[l->n].b = type == IECL_READ ? 1 : b; l->op[l->n].eoi = eoi; l->n++;
}

typedef struct {
    iecdev_t e;                 /* its byte engine; e.out is what the controller pulls */
    const iecl_t *log;
    int i, k, pc, atn, talk_next, done;
    uint32_t t;
} iech_t;

#define IECH_IN(l) ((h->e.in & (l)) != 0)
/* Poll: returns what the controller pulls; h->done when the log is played. */
static FF_C1 uint8_t iech_poll(iech_t *h, uint32_t now, uint8_t lines)
{
    iecdev_t *d = &h->e;
    int r;
    d->now = now; d->in = lines;
    PT_BEGIN(h->pc);
    for (h->i = 0; h->i < h->log->n; h->i++) {
        if (h->log->op[h->i].type == IECL_ATN_END) {
            /* the C64 let ATN go here: the DOS acts on what it was sent (an OPEN, a
             * command) only now. Turnaround if a TALK was in it */
            if (!h->atn) continue;
            h->atn = 0;
            if (h->talk_next) {
                d->out = IECD_ATN | IECD_CLK | IECD_DATA;
                h->t = d->now;
                PT_WAIT(h->pc, d->now - h->t >= 20, d->out);
                d->out = IECD_DATA;
                /* ⚠ a fresh sample first: the lines just seen still hold OUR CLK, which read as
                 * the device's acknowledgement (as core/iec_dev.h at ATN) */
                h->t = d->now;
                PT_WAIT(h->pc, d->now - h->t >= 2, d->out);
                PT_WAIT(h->pc, IECH_IN(IECD_CLK), d->out);
            } else {
                d->out = 0;                          /* idle until the next phase or data */
                h->t = d->now;
                PT_WAIT(h->pc, d->now - h->t >= 100, d->out);
            }
            continue;
        }
        if (h->log->op[h->i].type == IECL_ATN) {
            if (!h->atn) {                           /* ATN, CLK pulled, DATA let go: devices answer */
                d->out = IECD_ATN | IECD_CLK; h->atn = 1; h->talk_next = 0;
                h->t = d->now;
                PT_WAIT(h->pc, d->now - h->t >= 1000, d->out);
            }
            {
                uint8_t c = h->log->op[h->i].b;
                /* TALK ($40+pa, not $5F UNTALK): a turnaround follows; UNTALK, LISTEN, UNLISTEN cancel it */
                if ((c & 0xE0) == 0x40 && c != 0x5F) h->talk_next = 1;
                else if (c == 0x5F || (c & 0xE0) == 0x20) h->talk_next = 0;
                d->tbyte = c;
            }
            d->tlast = 0; d->tpc = 0;
            PT_WAIT(h->pc, (r = iec_tx(d)) != 0, d->out);
            continue;
        }
        if (h->log->op[h->i].type == IECL_DATA) {
            if (!(d->out & IECD_CLK)) {              /* as the sender of data we hold CLK first */
                d->out |= IECD_CLK;
                h->t = d->now;
                PT_WAIT(h->pc, d->now - h->t >= 20, d->out);
            }
            d->tbyte = h->log->op[h->i].b; d->tlast = h->log->op[h->i].eoi; d->tpc = 0;
            PT_WAIT(h->pc, (r = iec_tx(d)) != 0, d->out);
        } else {                                     /* read this many bytes, as a listener */
            for (h->k = 0; h->k < h->log->op[h->i].b; h->k++) {
                d->rpc = 0;
                PT_WAIT(h->pc, (r = iec_rx(d, 0)) != 0, d->out);
            }
        }
    }
    /* end: let everything go after the last command phase */
    if (h->atn) {
        h->t = d->now;
        PT_WAIT(h->pc, d->now - h->t >= 20, d->out);
    }
    d->out = 0; h->atn = 0;
    h->t = d->now;
    PT_WAIT(h->pc, d->now - h->t >= 100, 0);
    h->done = 1;
    PT_WAIT(h->pc, 0, 0);
    PT_END(h->pc);
    return d->out;
}
#undef IECH_IN
#endif
