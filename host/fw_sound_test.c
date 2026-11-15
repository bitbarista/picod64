/* The drive's sounds, fw/sound.h, without a board: what comes out for what the drive does.
 *   fw_sound_test            the checks (a -DSND_TEST_NO_BUMP build must fail them)
 * Whether it sounds like a 1541 is for ears: host/sound-preview.sh. */
#include <stdio.h>
#include <string.h>
#include "../fw/sound.h"
static snd_t s; static int fails;
static void check(const char *m, int ok) { printf("%s  %s\n", ok ? "ok  " : "FAIL", m); if (!ok) fails++; }
static uint8_t ph, ht; static int motor, online = 1, busy, on = 1;
/* n samples: the sum of the sound's distance from silence, and its peak */
static long run(int n, int *peak)
{
    long e = 0; int p = 0;
    for (int i = 0; i < n; i++) { uint8_t d = snd_tick(&s, ph, ht, motor, online, busy, on); e += d; if (d > p) p = d; }
    if (peak) *peak = p;
    return e;
}
int main(void)
{
    int p; long e;
    run(100, 0);
    check("nothing happening: exactly silent, so no carrier and no current", run((int)SND_HZ, &p) == 0 && p == 0);

    /* a step: phase then half-track, the order core 1 writes them */
    ph = 1; snd_tick(&s, ph, ht, 0, 1, 0, 1); ht = 1;
    long click = run(100, &p); int cpk = p;
    check("a step sounds", click > 0 && cpk > 20);
    run(3000, 0);
    check("and dies away within 0.2 s", run(100, &p) == 0);

    /* a bump: the stepper turns, the head stays */
    ph = 2; long bump = run(600, &p); int bpk = p;
    check("against the stop: a knock, stronger than a step", bump > click * 2 && bpk > cpk);
    run(6000, 0); check("and dies away within 0.5 s", run(100, 0) == 0);

    /* a rattle at DOS's rate stays under the cap */
    int mx = 0; for (int i = 0; i < 400; i++) { ph = (ph + 3) & 3; run(47, &p); if (p > mx) mx = p; }
    check("the worst rattle stays within the duty limit", mx > 0 && mx <= SND_MAX);
    run(6000, 0);

    /* motor */
    motor = 1; run((int)SND_HZ, 0);
    e = run((int)SND_HZ, &p);
    check("motor on: a quiet continuous sound, well under a step", e > 0 && p < cpk);
    long q[12], lo = 1L << 40, hi = 0; int same = 1;                            
    for (int i = 0; i < 12; i++) { q[i] = run((int)SND_HZ / 20, 0); if (q[i] < lo) lo = q[i]; if (q[i] > hi) hi = q[i]; }   /* a quarter of a turn each */
    for (int i = 0; i < 8; i++) if (q[i] * 10 < q[i + 4] * 9 || q[i + 4] * 10 < q[i] * 9) same = 0;
    printf("      a quarter turn each: "); for (int i = 0; i < 12; i++) printf(" %ld", q[i]); printf("; step peak %d, energy %ld; knock peak %d, energy %ld\n", cpk, click, bpk, bump);
    check("and it swells with the disk's 5 Hz turn", hi * 10 > lo * 13 && same);
    motor = 0; run(6000, 0); check("motor off: silent again", run(100, 0) == 0);

    /* the off setting */
    on = 0; motor = 1; ph = (ph + 1) & 3; ht++;
    check("sound off: silent through a step and the motor", run((int)SND_HZ, 0) == 0);
    on = 1; motor = 0; run(6000, 0);

    /* browse mode: a pretend head and motor while it is busy, nothing when it is not */
    online = 0; ph = (ph + 1) & 3;
    check("browse mode idle: silent, whatever the stale stepper says", run(2000, 0) == 0);
    busy = 1; int ticks = 0, prev = 0;
    for (int i = 0; i < 8000; i++) { uint8_t d = snd_tick(&s, ph, ht, 0, 0, 1, 1); if (d > 25 && prev <= 25) ticks++; prev = d; }
    check("browse mode busy: a seek from rest", ticks >= 4);
    run(40000, &p); check("and the motor keeps going", run(1000, 0) > 0);
    busy = 0; run((int)SND_RUNON + 6000, 0); check("then runs on and stops", run(100, 0) == 0);

    /* back online: no false step from what the emulated drive did meanwhile */
    online = 1; ph = 3; ht = 9; run(1, 0); online = 0; run(10, 0); online = 1;
    check("back to the drive with the head unmoved: no false tick", run(50, 0) == 0);
    printf(fails ? "SOUND FAIL\n" : "SOUND PASS\n");
    return fails != 0;
}
