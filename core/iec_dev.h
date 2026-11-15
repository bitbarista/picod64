/* -------------------------------------------------------------------------
 * PicoD64 -- an IEC bus DEVICE in software (browse mode, F6): not an
 * emulated 1541, the Pico itself answering the C64.
 *
 * Sources: M. Steil, "Commodore Peripheral Bus", pagetable.com: part 2 (TALK/
 * LISTEN, the command bytes) and part 4 (Standard Serial: the line protocol)
 * [S14]; C64 PRG serial bus timing table p.365 [S12]. The real KERNAL, run
 * against this code in host/f6.c, is the check (as the DOS was for the drive).
 *
 * Lines: bit set = line pulled LOW ("true"), IEC_ATN | IEC_CLK | IEC_DATA, as
 * core/drive1541.h. A data bit is 1 when DATA is released [A: the article's
 * reader comments dispute polarity; the KERNAL settles it, host/f6.c].
 *
 * NON-BLOCKING. iec_poll(d, now_us, lines) runs the state machine as far as
 * it can and returns; d->out is what this device pulls. The machine is a
 * protothread (switch on line numbers): each WAIT returns until its condition
 * holds. A falling ATN restarts it from the top, wherever it was: ATN may come
 * at any time, even mid-byte [S14 part 4].
 *
 * PASSIVE (d->passive): follow the traffic, drive nothing. Used while the
 * emulated 1541 owns the bus, to see a "CD_" that means "back to browsing".
 * ------------------------------------------------------------------------- */
#ifndef FF_IEC_DEV_H
#define FF_IEC_DEV_H
#ifndef FF_C1
#define FF_C1          /* core 1 code: the board puts it in SRAM */
#endif
#include <stdint.h>

enum { IECD_ATN = 1, IECD_CLK = 2, IECD_DATA = 4 };

/* timings, µs [S14 part 4] */
#define IECD_EOI_US     200   /* the sender's pause that means "last byte" */
#define IECD_EOI_ACK_US  70   /* listener holds DATA >= 60 to acknowledge EOI */
#define IECD_HOLD_US     80   /* talker's CLK/DATA holds: >= 60 for a device (1541 ~74). Was 70, shorter than a real
                                * 1541's: a C64 loses up to ~40 us to a bad line, and on the board a LOAD hung now and then,
                                * the C64 never acknowledging a byte the talker had sent on time (2026-11-08) */
#define IECD_BETWEEN_US 100   /* between bytes */
#define IECD_ACK_US    1000   /* frame acknowledge window */

typedef struct iecdev iecdev_t;

/* what the DOS layer (core/browse.h) gives the engine */
typedef struct {
    void (*listen_byte)(iecdev_t *d, uint8_t sa, uint8_t b, int eoi);   /* data to channel sa */
    void (*open)(iecdev_t *d, uint8_t sa);         /* OPEN sa: its name follows as listen bytes */
    void (*close)(iecdev_t *d, uint8_t sa);
    void (*unlisten)(iecdev_t *d);                 /* the end of a LISTEN: act on name/command */
    /* TALK sa: the next byte to send; returns 0 if none (empty stream = not found) */
    int (*talk_byte)(iecdev_t *d, uint8_t sa, uint8_t *b, int *last);
} iecdev_ops_t;

struct iecdev {
    /* set by the user */
    uint8_t dev, passive;
    const iecdev_ops_t *ops;
    void *ctx;
    /* bus */
    uint32_t now; uint8_t in, out, atn_prev;
    /* role */
    uint8_t listening, talking, sa, opening, addressed;
    /* the protothreads: main, receive, transmit */
    int pc, rpc, tpc;
    uint32_t t, rt, tt;
    uint8_t rbyte, rbit, reoi, tbyte, tbit, tlast;
    uint8_t cmd;
    uint32_t bytes_in, bytes_out, errors;
    void (*trace)(iecdev_t *d, uint8_t cmd);       /* optional: every command byte under ATN */
    void (*atn_end)(iecdev_t *d);                  /* optional: the end of each command phase */
};

#define IEC_IN(l)   ((d->in & (l)) != 0)          /* line pulled low by anyone */
#define PULL(l)     (d->out |= (uint8_t)(l))
#define RELEASE(l)  (d->out &= (uint8_t)~(l))
#define PT_BEGIN(pc)        switch (pc) { case 0:
#define PT_WAIT(pc, c, ret) do { pc = __LINE__; /* fallthrough */ case __LINE__: if (!(c)) return ret; } while (0)
#define PT_END(pc)          } pc = 0

/* RECEIVE one byte (we are a listener; we hold DATA on entry = busy).
 * Returns 0 while busy, 1 with a byte in rbyte (reoi = EOI), 2 if ATN went
 * away before a byte began (end of a command phase). */
static FF_C1 int iec_rx(iecdev_t *d, int under_atn)
{
    PT_BEGIN(d->rpc);
    d->reoi = 0;
    /* step 1 first: the sender HOLDS CLK. ⚠ Under a fresh ATN the C64 may not have
     * pulled CLK yet: taking that released CLK for "ready to send" let DATA go at
     * once, and the C64 saw no device (2026-10-29, host/f6.c) */
    PT_WAIT(d->rpc, IEC_IN(IECD_CLK) || (under_atn && !IEC_IN(IECD_ATN)), 0);
    if (under_atn && !IEC_IN(IECD_ATN)) { d->rpc = 0; return 2; }
    /* step 2, sender ready: it releases CLK */
    PT_WAIT(d->rpc, !IEC_IN(IECD_CLK) || (under_atn && !IEC_IN(IECD_ATN)), 0);
    if (under_atn && !IEC_IN(IECD_ATN)) { d->rpc = 0; return 2; }
    RELEASE(IECD_DATA);                             /* step 3: ready for data */
    if (d->passive) {
        /* ⚠ PASSIVE: no timers, the real listener's lines say it all. Its time only moves at the line changes it is
         * fed, so a 200 us EOI timer from the C64's CLK release, and a 70 us acknowledge of its own, came out wrong
         * whenever the drive was slow to be ready or quick to acknowledge: CD// arrived as "CD/" 4 times in 10 (first
         * board, 2026-11-07). Ready = every listener has let DATA go; EOI = a listener pulls DATA again before the
         * talker pulls CLK (its acknowledge), and lets go */
        PT_WAIT(d->rpc, !IEC_IN(IECD_DATA) || (under_atn && !IEC_IN(IECD_ATN)), 0);
        if (under_atn && !IEC_IN(IECD_ATN)) { d->rpc = 0; return 2; }
        PT_WAIT(d->rpc, IEC_IN(IECD_CLK) || IEC_IN(IECD_DATA), 0);
        if (!IEC_IN(IECD_CLK)) {
            d->reoi = 1;
            PT_WAIT(d->rpc, !IEC_IN(IECD_DATA) || IEC_IN(IECD_CLK), 0);
            PT_WAIT(d->rpc, IEC_IN(IECD_CLK), 0);
        }
    } else {
    d->rt = d->now;
    /* step 4, or EOI: no CLK within 200 µs */
    PT_WAIT(d->rpc, IEC_IN(IECD_CLK) || d->now - d->rt >= IECD_EOI_US, 0);
    if (!IEC_IN(IECD_CLK)) {
        d->reoi = 1;
        PULL(IECD_DATA); d->rt = d->now;            /* acknowledge EOI: DATA >= 60 µs */
        PT_WAIT(d->rpc, d->now - d->rt >= IECD_EOI_ACK_US, 0);
        RELEASE(IECD_DATA);
        PT_WAIT(d->rpc, IEC_IN(IECD_CLK), 0);
    }
    }
    d->rbyte = 0;
    for (d->rbit = 0; d->rbit < 8; d->rbit++) {      /* LSB first */
        PT_WAIT(d->rpc, !IEC_IN(IECD_CLK), 0);       /* data valid */
        if (!IEC_IN(IECD_DATA)) d->rbyte |= (uint8_t)(1u << d->rbit);
        PT_WAIT(d->rpc, IEC_IN(IECD_CLK), 0);        /* not valid */
    }
    PULL(IECD_DATA);                                /* step 29: frame acknowledge */
    d->bytes_in++;
    PT_END(d->rpc);
    return 1;
}

/* SEND tbyte (we are the talker; we hold CLK on entry, the listener DATA).
 * tlast: send it with EOI. Returns 0 while busy, 1 done, 2 no acknowledge. */
static FF_C1 int iec_tx(iecdev_t *d)
{
    PT_BEGIN(d->tpc);
    d->tt = d->now;
    PT_WAIT(d->tpc, d->now - d->tt >= IECD_BETWEEN_US, 0);
    RELEASE(IECD_CLK);                              /* step 2: ready to send */
    PT_WAIT(d->tpc, !IEC_IN(IECD_DATA), 0);          /* step 3: listener ready */
    if (d->tlast) {                                 /* EOI: wait; the listener pulses DATA */
        PT_WAIT(d->tpc, IEC_IN(IECD_DATA), 0);
        PT_WAIT(d->tpc, !IEC_IN(IECD_DATA), 0);
    }
    PULL(IECD_CLK); d->tt = d->now;                 /* step 4: data not valid */
    PT_WAIT(d->tpc, d->now - d->tt >= IECD_HOLD_US, 0);
    for (d->tbit = 0; d->tbit < 8; d->tbit++) {
        if (d->tbyte & (1u << d->tbit)) RELEASE(IECD_DATA); else PULL(IECD_DATA);   /* step 5 */
        d->tt = d->now;
        PT_WAIT(d->tpc, d->now - d->tt >= IECD_HOLD_US / 2, 0);
        RELEASE(IECD_CLK); d->tt = d->now;           /* step 6: valid */
        PT_WAIT(d->tpc, d->now - d->tt >= IECD_HOLD_US, 0);
        PULL(IECD_CLK); RELEASE(IECD_DATA); d->tt = d->now;   /* step 7 */
        PT_WAIT(d->tpc, d->now - d->tt >= IECD_HOLD_US / 2, 0);
    }
    d->tt = d->now;                                 /* step 29: the listener acknowledges */
    PT_WAIT(d->tpc, IEC_IN(IECD_DATA) || d->now - d->tt >= IECD_ACK_US, 0);
    if (!IEC_IN(IECD_DATA)) { d->tpc = 0; d->errors++; return 2; }
    d->bytes_out++;
    PT_END(d->tpc);
    return 1;
}

/* one command byte under ATN [S14 part 2] */
static FF_C1 void iec_command(iecdev_t *d, uint8_t c)
{
    uint8_t dev = d->dev;
    if (d->trace) d->trace(d, c);
    if (c == 0x3F) { if (d->listening && d->ops->unlisten) d->ops->unlisten(d); d->listening = 0; d->opening = 0; }
    else if (c == 0x5F) d->talking = 0;
    /* ⚠ LISTEN to us ends our talking, TALK to us our listening: one role at a time. FB64
     * ends a read with UNLISTEN, not UNTALK, then LISTENs to OPEN the next file: still
     * "talking", the device turned the bus round the wrong way (2026-10-29, host/f6.c) */
    else if ((c & 0xE0) == 0x20) { d->listening = (c & 0x1F) == dev; if (d->listening) d->talking = 0;
                                   d->addressed = d->listening; d->opening = 0; }
    else if ((c & 0xE0) == 0x40) { d->talking = (c & 0x1F) == dev; if (d->talking) d->listening = 0;
                                   d->addressed = d->talking; }
    else if (d->addressed && (c & 0xE0) == 0x60) d->sa = c & 0x0F;   /* drives ignore bit 4 [S14 part 2] */
    else if (d->addressed && d->listening && (c & 0xF0) == 0xE0) { d->sa = c & 0x0F; if (d->ops->close) d->ops->close(d, d->sa); }
    else if (d->addressed && d->listening && (c & 0xF0) == 0xF0) { d->sa = c & 0x0F; d->opening = 1; if (d->ops->open) d->ops->open(d, d->sa); }
}

static FF_C1 void iec_run(iecdev_t *d)
{
    int r;
    PT_BEGIN(d->pc);
    if (!IEC_IN(IECD_ATN)) { d->out = 0; PT_WAIT(d->pc, 0, ); }   /* idle until ATN */
    /* ATN: acknowledge within 1 ms by pulling DATA, let go of CLK [S14 part 4] */
    d->out = IECD_DATA;
    d->rpc = 0; d->tpc = 0;
    /* ⚠ ONE FRESH LOOK at the bus first. As a talker we were holding CLK: the lines just
     * sampled still show OUR pull, which the receiver took for the C64's, then its release
     * for "ready to send", and let DATA go: the C64 saw no device (2026-10-29, FB64 after
     * reading a listing, host/f6.c) */
    d->t = d->now;
    PT_WAIT(d->pc, d->now - d->t >= 2, );
    for (;;) {                                       /* command bytes under ATN */
        PT_WAIT(d->pc, (r = iec_rx(d, 1)) != 0, );
        if (r == 2) break;
        iec_command(d, d->rbyte);
        /* a LISTEN or TALK for another device: let go until ATN ends. So the C64 gets no acknowledge
         * for the secondary address that follows, and, with nothing at that number, its KERNAL says
         * DEVICE NOT PRESENT; acknowledging every byte hung it in LOAD. As a real 1541 at another
         * number (VICE true drive, the oracle, 2026-11-02) */
        if (((d->rbyte & 0xE0) == 0x20 || (d->rbyte & 0xE0) == 0x40)
            && (d->rbyte & 0x1F) != 0x1F && (d->rbyte & 0x1F) != d->dev) {
            d->out = 0;
            PT_WAIT(d->pc, !IEC_IN(IECD_ATN), );
            break;
        }
    }
    if (d->atn_end) d->atn_end(d);
    /* ATN released */
    if (d->talking && d->passive) {
        PT_WAIT(d->pc, 0, );                         /* the real device talks; nothing to follow */
    } else if (d->talking) {
        /* turnaround [S14 part 4]: the C64 pulls DATA and lets CLK go; then we pull CLK
         * and let DATA go: we are the talker */
        PT_WAIT(d->pc, !IEC_IN(IECD_CLK), );
        d->out = IECD_CLK;
        /* ⚠ HOLD it: the C64 waits to see our CLK as the turnaround's acknowledgement. An
         * empty stream let it go in the same poll, the C64 never saw it and hung in LOAD
         * instead of reporting FILE NOT FOUND (2026-10-29, host/f6.sh) */
        d->t = d->now;
        PT_WAIT(d->pc, d->now - d->t >= IECD_HOLD_US, );
        for (;;) {
            {
                int last = 0;   /* (a local only between waits: a protothread keeps nothing else) */
                if (!d->ops->talk_byte(d, d->sa, &d->tbyte, &last)) {
                    RELEASE(IECD_CLK);               /* empty stream: never pull CLK again: "not found" */
                    PT_WAIT(d->pc, 0, );
                }
                d->tlast = (uint8_t)last;
            }
            d->tpc = 0;
            PT_WAIT(d->pc, (r = iec_tx(d)) != 0, );
            if (r == 2 || d->tlast) { RELEASE(IECD_DATA); PT_WAIT(d->pc, 0, ); }   /* hold CLK till ATN */
        }
    } else if (d->listening) {
        d->out = IECD_DATA;                          /* busy until the next byte begins */
        for (;;) {
            d->rpc = 0;
            PT_WAIT(d->pc, (r = iec_rx(d, 0)) != 0, );
            if (d->ops->listen_byte) d->ops->listen_byte(d, d->sa, d->rbyte, d->reoi);
        }
    }
    d->out = 0;
    PT_WAIT(d->pc, 0, );
    PT_END(d->pc);
}

/* Poll. now: µs (wraps); lines: the bus now (bit set = low). Returns what this
 * device pulls (always 0 when passive). */
static FF_C1 uint8_t iec_poll(iecdev_t *d, uint32_t now, uint8_t lines)
{
    d->now = now; d->in = lines;
    int atn = (lines & IECD_ATN) != 0;
    if (atn && !d->atn_prev) { d->pc = 0; d->rpc = 0; d->tpc = 0; }   /* ATN wins, always */
    d->atn_prev = (uint8_t)atn;
    if (d->passive) {
        /* follow the traffic as a listener would, but our "pulls" never reach the bus:
         * the real device's handshakes carry it */
        iec_run(d);
        return 0;
    }
    iec_run(d);
    return d->out;
}
#undef IEC_IN
#undef PULL
#undef RELEASE
#endif
