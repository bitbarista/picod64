# PicoD64 case

A 3D-printed case made for the PicoD64 board, in the style of the C64: ribs, sloped shoulders and printed badges.
It is designed in OpenSCAD ([`picod64-case.scad`](picod64-case.scad), with [`c64-style.scad`](c64-style.scad) and
[`knob-logo.scad`](knob-logo.scad)). Every position comes from the board itself: [`board_dims.scad`](board_dims.scad)
is generated from the PCB by [`hardware/gen/gen_case_dims.py`](../hardware/gen/gen_case_dims.py), so the case follows
any change to the board.

## Printing

Print every file in [`stl/`](stl/) as it is: it's already the right way up. **No brim and no supports.** (A brim
would land inside the letters on the lids and fill them in.)

| Part | File | On the bed |
|---|---|---|
| Tray | `picod64-tray.stl` | flat underside down |
| Lid, front and rear | `picod64-lid-front.stl`, `picod64-lid-rear.stl` | top face down |
| Knob | `picod64-knob.stl` | top face down |
| Knob seating tool | `picod64-knob-seater.stl` | base down. A tool, not part of the case: the knob's first push goes onto it, not the encoder |
| LED standoffs (2) | `picod64-led-standoffs.stl` | as exported |
| OLED pillars (4) | `picod64-oled-pillars.stl` | on their sides |
| Badges (3) | `picod64-badge-pwr.stl`, `-act.stl`, `-name.stl` | letters up. For dark letters on a light badge, change filament where the letters start (0.8 mm) |

**Option:** [`stl/options/`](stl/options/) has two lids for M3 heat-set inserts, for a case you'll open often.
Print them instead of the two standard lids.

The [build guide](../hardware/BUILD-NOTES.md) covers putting it together (steps 6 and 7).

## Changing it

Open [`picod64-case.scad`](picod64-case.scad) in OpenSCAD and use **Window > Customizer**. The defaults are what was
printed and tested. Then run `bash case/export.sh`: it re-exports every part and checks it.

| Tab | What you can change |
|---|---|
| Fit | Clearances that depend on your printer: board to wall, the lid joints, the knob's hole, grip and height, the holes for the encoder, LEDs and BOOTSEL |
| Screws | Self-tapping screws or M3 heat-set inserts, screw length, hole sizes, foot size |
| Shell | Wall, floor and roof thickness |
| Labels | The name on the lid and the underside, label sizes, cut depth, font |
| Knob | Diameter, height, number of ribs, logo on or off |
| OLED module and pillars | Your OLED module's measurements (see the build guide, step 4) |
| Style | `c64` (the default) or `plain` (labels cut into the lids, no badges; ready-made in [`stl/plain/`](stl/plain/)) |
| Two-colour inlay | Letters as a separate part in a second colour, for a multi-colour printer; ready-made in [`stl/inlay/`](stl/inlay/) |

If a combination can't work (a name too long for the lid, a screw too short for its hole, a roof too thin for the
labels), OpenSCAD stops with a message saying why.

**Checks** after a change:
- `bash case/export.sh` also checks that every part is one solid piece, sits flat on the bed, is a closed mesh,
  needs no supports, and that the labels will print cleanly.
- `python3 case/check_fit.py` fits every part against the real board in KiCad (about 10 minutes). Run it after any
  change that could bring the case near a component.
- `bash case/render.sh` draws the pictures in [`render/`](render/).

**The font:** the labels use Eurostile, which is commercial and not included. Without it, OpenSCAD quietly uses
another font and the spacing goes wrong. To use a font you have: install it, run
`bash case/gen_glyphs.sh "Your Font"`, set `LABEL_FONT` in the scad, then run `bash case/check_labels.sh`. Wide,
square fonts suit the case; Michroma and Orbitron are free examples.

## How it works

**A lid in two parts.** The lid is split at the step between the low front and the higher rear. That way each part
prints top face down, flat on the bed, with no supports, and the labels and badge recesses come out crisp. Fit the
front part first, then lower the rear part over it: a lip under the rear part holds the front's edge down, and
small tongues in the side walls keep the two in line. Both parts locate on a lip round the tray's rim, and four
screws from underneath hold everything together.

**The knob.** It sits on the end of the encoder's shaft, not wherever it was pushed to, so it always stands at the
same height, 1 mm above the lid. A cone under the knob sits in a matching dip in the lid. Its hole grips the shaft
with a small springy tongue that presses the shaft against a firm flat side: it grips because the tongue bends,
not because the hole is tight, so it still works on a printer that prints a little over or under size. The first
push onto a shaft is the firm one, which is what the seating tool is for. To make the grip tighter or looser,
change `KNOB_PRELOAD` (default 0.35; try 0.25 or 0.45). The face carries a 5¼" disk logo.

**The OLED.** Four printed pillars hold the module with no screws. Each pillar's head has a groove that slides over
the module's circuit-board edge, and its other end snaps into a hole in PicoD64's board. Because the pillars hold
the edge, not the module's own mounting holes (which vary between makers), they suit any module of the usual size.
The module's thickness sets the pillars' height, so measure yours if it's different (build guide, step 4).

**The LEDs** stand up through the lid, as on a 1541, raised to the right height by two printed standoffs. The
PWR and ACT badges fit round them.

**C64 details.** Sloped shoulders and a band of ribs on the front lid, a frame round the display window, and
recesses for printed badges: PWR and ACT round the LEDs and the name on the rear lid, after the breadbin's badges.
There's no Commodore logo or name anywhere. Every detail is on the outside only: nothing reaches inside the case.

**Openings.** The USB socket sits in a recess in the rear wall that takes the plug, the microSD slot has a finger
scoop across the lid and tray, and the BOOTSEL button is reached through a pinhole marked with a download arrow.

**Inside the front lid** is a maker's mark ("Developed by" and a signature), cut into the underside so it reads
with the lid off. `MAKER_MARK = false` leaves it out.
