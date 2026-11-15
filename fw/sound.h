/* -------------------------------------------------------------------------
 * PicoD64 -- the drive's sounds on the piezo (BZ1, GP22). FIRMWARE.md, "Drive sounds".
 *
 * One call per sample, SND_HZ, on core 0. It only reads what the drive is doing: the
 * cycle-exact code on core 1 does not know this exists. What a 1541 sounds like:
 *   - a tick on every half-track step, so a long seek becomes a fast rattle by itself;
 *   - a harder, lower knock when the head is stepped against its stop (the head bump);
 *   - the motor's quiet whirr, with the disk's 5 Hz turn in it.
 * Browse mode has no head to follow, so while it is busy a pretend one moves.
 *
 * The result is a PWM duty, 0..SND_MAX of 255: unipolar, because the pin only drives
 * 0..3.3 V, so silence is exactly 0 (no carrier, no current). Integers only.
 *
 * Tuned to the piezo (first board, 2026-11-06): the first sounds, a small swing round a pedestal
 * with ticks about 1 ms long, were "almost inaudible", while a full-swing 3.9 kHz test tone was clear.
 * So every sound is now a square wave between 0 and its loudness, near BZ1's resonance (3.9 kHz:
 * 4 samples a period), and lasts long enough to hear: a step ~10 ms, the knock ~30 ms at 1.95 kHz
 * (lower, as a knock is), the motor a soft hiss.
 * ------------------------------------------------------------------------- */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

#define SND_HZ 15625u
/* First board (2026-11-06): "almost inaudible" at the first levels (click 42, bump 62, motor 9, duty
 * capped at 128): ~0.4 V of signal on a part rated at 3 V. Now ~2.5x and the full swing. R14 still limits
 * each edge to 33 mA for about a microsecond (the piezo's ~10 nF [A]) */
#define SND_MAX 255
#define SND_CLICK (240 << 8)        /* amplitudes, 8.8 (2026-11-06: "still a little quiet": up from 200 / 48) */
#define SND_BUMP  (255 << 8)
#define SND_MOTOR (72 << 8)
#define SND_RUNON SND_HZ            /* browse mode: the pretend motor's run-on, 1 s */
#define SND_SEEK_GAP 203            /* browse mode: a half-track per 13 ms, the real DOS's pace on the twin */
#define SND_TRACK_GAP 39062u        /* browse mode: on to the next track every 2.5 s */

typedef struct {
    uint32_t lfsr;
    uint16_t click, bump, motor;    /* envelopes, 8.8 */
    int16_t  lp;                    /* (unused since the piezo tuning) */
    uint16_t am, tone;              /* phases: the disk's turn; the step's ring */
    uint16_t hold, gap, walk;       /* browse mode: motor run-on; to the next tick; to the next track */
    uint8_t  ph, ht, pend, seek;    /* the stepper as last seen; a step not yet judged; pretend steps left */
} snd_t;

static inline int8_t snd_noise(snd_t *s)
{
    uint32_t x = s->lfsr ? s->lfsr : 0x1F2E3D4Cu;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->lfsr = x;
    return (int8_t)x;
}

/* phase, ht: the stepper. motor: the spindle (VIA2 PB2). online: the emulated drive has the bus
 * (otherwise phase, ht and motor mean nothing). busy: browse mode is talking or listening. */
static inline uint8_t snd_tick(snd_t *s, uint8_t phase, uint8_t ht, int motor, int online, int busy, int on)
{
    int n = snd_noise(s);
    if (online) {
        /* judged one sample later: core 1 writes the phase just before the half-track */
        if (s->pend) {
            s->pend = 0;
#ifndef SND_TEST_NO_BUMP
            if (ht == s->ht) s->bump = SND_BUMP;     /* it moved the stepper, not the head: the stop */
            else
#endif
                s->click = SND_CLICK;
            s->ht = ht;
        }
        if (phase != s->ph) { s->ph = phase; s->pend = 1; }
        s->hold = 0; s->seek = 0; s->walk = 0;
    } else {
        s->ph = phase; s->ht = ht; s->pend = 0; motor = 0;
        if (busy) {
            if (!s->hold) { s->seek = (uint8_t)(6 + (n & 31)); s->gap = 0; s->walk = SND_TRACK_GAP; }  /* from rest: seek */
            s->hold = SND_RUNON;
            if (!--s->walk) { s->seek = 2; s->walk = SND_TRACK_GAP; }
        }
        if (s->seek) {
            if (s->gap) s->gap--;
            else { s->click = SND_CLICK; s->gap = SND_SEEK_GAP; s->seek--; }
        }
        if (s->hold) { s->hold--; motor = 1; }
    }
    /* the motor winds up and down in about a quarter of a second */
    if (motor) { s->motor = (uint16_t)(s->motor + 5 > SND_MOTOR ? SND_MOTOR : s->motor + 5); }
    else s->motor = (uint16_t)(s->motor > 5 ? s->motor - 5 : 0);
    if (!on) { s->click = s->bump = 0; return 0; }
    if (!(s->click | s->bump | s->motor)) return 0;

    int d = 0;
    s->tone = (uint16_t)(s->tone + 1);               /* sample count: bit 1 = 3.9 kHz, bit 2 = 1.95 kHz */
    if (s->click) {                                  /* a step: 3.9 kHz, ~10 ms */
        if (s->tone & 2) d += s->click >> 8;
        s->click = (uint16_t)(s->click - (s->click >> 6) - 1 * (s->click < 64));
    }
    if (s->bump) {                                   /* the stop: 1.95 kHz, ~30 ms, with a rasp of noise */
        if ((s->tone & 4) && (n & 7)) d += s->bump >> 8;
        s->bump = (uint16_t)(s->bump - (s->bump >> 8) - 1 * (s->bump < 256));
    }
    if (s->motor) {                                  /* a soft hiss, swelling with the disk's 5 Hz turn */
        s->am = (uint16_t)(s->am + 21);              /* 5 Hz: 65536 / 3125 */
        int tri = (s->am & 0x8000) ? (int)(s->am & 0x7FFF) : (int)(0x7FFF - s->am);
        int a = (s->motor * (20 + (tri >> 11))) >> 13;   /* 0.56 .. 1.0 of full */
        if (n & 1) d += a;
    }
    return (uint8_t)(d < 0 ? 0 : d > SND_MAX ? SND_MAX : d);
}
#endif
