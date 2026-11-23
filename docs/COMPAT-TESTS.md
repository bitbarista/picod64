# Compatibility tests

What has been run on PicoD64, and the result.

## How

- A real **PAL** C64, with the firmware in [`firmware/`](../firmware/) (v0.2, 150 MHz). Not tested on NTSC.
- PicoD64 the **only device on the serial bus**. Some fast loaders fail with another drive or interface on the bus,
  on a real 1541 too.
- Each title started from a **reset C64** with its RAM set to a power-up pattern, then `LOAD"*",8,1` and `RUN`;
  the next disk put in as the knob's long press does. Pass: it runs to its end (or its own pass screen) as it does
  in VICE. Transwarp's Menu and the Testsuite ran unattended, judged from the screen.
- **Runs** counts every run on this firmware, passed or not.

## Results

| Title | Group | Loader | Disks | Runs | Result |
|---|---|---|---|---|---|
| 1541 Testsuite r192 (pc64_1, pc64_2) | The Dreams | (the drive's CPU: opcode tests) | 2 | 1 | ✅ 115/115 and 104/104 |
| Coma Light 13 | Oxyron | Krill's loader | 2 | 1 * | ✅ |
| Dutch Breeze | Blackmail | its own | 1 | 1 * | ❌ crashed on disk 1 (fails on v0.1 too: see Known issues) |
| Edge of Disgrace | Booze Design | Booze Design's | 1a, 1b | 1 * | ✅ |
| Lunatico | Lft | Spindle | 2 | 1 * | ✅ |
| Mafio-DOS v2.1 (Mafiosino Trackloader) | | its own | 1 | 1 | ✅ (its menu) |
| MATH | | its own | 2 | 1 * | ✅ |
| Memento Mori | Genesis Project | Sparkle | 3 | 1 * | ✅ |
| Primitive 7 Sins | Albion Crew | its own (steps the head with the motor off) | 1 | 1 * | ✅ |
| Reanim8ed | Hitmen | Dasheles' (Smash Designs) | 2 | 1 * | ✅ |
| Shards of Fancy | Lft | Spindle | 1 | 1 * | ✅ |
| The Hat | | its own | 2 | 0 | not run on v0.2: see Known issues |
| The Masque | Oxyron | its own (last part computed on the drive's CPU) | 1 | 1 | ✅ |
| Transwarp v0.86: Benchmark | Krill, Plush | Transwarp | 1 | 1 | ✅ (save 369, load 400 Bps, as VICE) |
| Transwarp v0.86: Menu, entry 1 | Krill, Plush | Transwarp | 1 | 13 | ✅ 11 of 13 (see Known issues); 3 of 4 * |
| Uncensored | Booze Design | Booze Design's | 2 | 1 * | ✅ |
| Wonderland XIV | Censor | its own (40 tracks) | 4 | 1 | ✅ |

\* Run on a pre-release v0.2 build that differed only in how a track with no SYNC is read. That build crashed
Wonderland XIV 3 runs of 3, so it was not released; v0.2 reads such tracks as v0.1 does.

Also: the browser, loading PRGs (over its turbo) and D64s (the plain 1541 way) from the card; a D64's directory art is
shown as a C64 listing shows it.

## Copy protection: D64 error bytes

A D64 that ends in error bytes has them built into the disk, so a protection that reads them finds them. Each title
below stops at READY, or on its title screen, with the error bytes removed. Loaded with `LOAD"*",8,1` from a freshly
reset C64.

| Title | Errors its protection reads | Runs | Result |
|---|---|---|---|
| Galactic Controller (Commodore) | 21, 22 on track 1 | 1 | ✅ runs by itself; plays |
| Summer Games II, Side A (Epyx) | 21, 23 on track 2 | 1 (JiffyDOS) | ✅ its own fast loader, then the events; plays |
| Rainbow Islands (Taito) | 21, 23 on track 22 | 1 (JiffyDOS) | ✅ plays |
| Adventureland (Scott Adams) | 21 on track 33 | 1 | ✅ RUN after the load; starts |

## JiffyDOS

JiffyDOS 6.01 KERNAL in the C64. "Both sides" means JiffyDOS's 1541 ROM on the card as `dos1541.bin` as well.
The fill test: self-checking programs of 154 blocks on one D64; each checks every byte it loaded.

| Test | JiffyDOS on | Result |
|---|---|---|
| Fill test, 2 files | C64 only (stock drive ROM) | ✅ every byte right, 98-101 s a file (stock speed) |
| Fill test, 4 files | both sides | ✅ every byte right, 18-20 s a file |
| Transwarp v0.86: Menu, entry 1 | both sides | ✅ 3 of 3 |
| The browser; Ghosts'n Goblins from a D64 | both sides | ✅ |
| Summer Games II, Rainbow Islands (error bytes) | both sides | ✅ load |

## Not yet tested

NTSC C64s; G64 images (not supported yet); the other entries of the Transwarp menu; games beyond those above. Please report what you find
(README, Compatibility).
