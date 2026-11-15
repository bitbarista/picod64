# Compatibility tests

What has been run on PicoD64, and the result.

## How

- A real **PAL** C64, with the stock firmware (150 MHz) as in [`firmware/`](../firmware/). Not tested on NTSC.
- PicoD64 the **only device on the serial bus**. Some fast loaders fail with another drive or interface on the bus,
  on a real 1541 too.
- Each title started from a **freshly powered-up C64**, from PicoD64's browser, with disks changed by the knob's
  long press. Pass: it runs to its end (or its own pass screen) as it does in VICE.

## Results

| Title | Group | Loader | Disks | Result |
|---|---|---|---|---|
| 1541 Testsuite r192 (pc64_1, pc64_2) | The Dreams | (the drive's CPU: opcode tests) | 2 | ✅ 115/115 and 104/104 |
| Coma Light 13 | Oxyron | Krill's loader | 2 | ✅ |
| Dutch Breeze | Blackmail | its own | 1 | ✅ |
| Edge of Disgrace | Booze Design | Booze Design's | 1a, 1b | ✅ |
| Lunatico | Lft | Spindle | 2 | ✅ |
| Mafio-DOS v2.1 (Mafiosino Trackloader) | | its own | 1 | ✅ (its menu) |
| MATH | | its own | 2 | ✅ |
| Memento Mori | Genesis Project | Sparkle | 3 | ✅ |
| Primitive 7 Sins | Albion Crew | its own (steps the head with the motor off) | 1 | ✅ |
| Reanim8ed | Hitmen | Dasheles' (Smash Designs) | 2 | ✅ |
| Shards of Fancy | Lft | Spindle | 1 | ✅ |
| The Hat | | its own | 2 | ✅ |
| The Masque | Oxyron | its own (last part computed on the drive's CPU) | 1 | ✅ |
| Transwarp v0.86: Benchmark | Krill, Plush | Transwarp | 1 | ✅ (save 369, load 400 Bps, as VICE) |
| Transwarp v0.86: Menu, entry 1 | Krill, Plush | Transwarp | 1 | ✅ |
| Uncensored | Booze Design | Booze Design's | 2 | ✅ |
| Wonderland XIV | Censor | its own (40 tracks) | 4 | ✅ |

Also: the browser and its turbo, loading PRGs and D64s from the card; a D64's directory art shown as a C64 listing
shows it.

## Not yet tested

NTSC C64s; the other entries of the Transwarp menu; games beyond those above. Please report what you find
(README, Compatibility).
