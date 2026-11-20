# Building PicoD64 (rev 1.1)

The whole build, in order. Item codes in brackets point to [`hardware/AUDIT.md`](AUDIT.md), where the reasons are.

## 1. Order the board

[`hardware/fab/picod64-rev1.1-gerbers.zip`](fab/picod64-rev1.1-gerbers.zip), to any fab: **2 layers, 1.6 mm, 1 oz copper**, 100 × 85 mm. Any
surface finish and mask colour. Nothing else to choose: no assembly, no stencil.

## 2. Buy the parts

[`hardware/fab/BUY.md`](fab/BUY.md) is the list of fitted parts, with Farnell numbers; `upload-farnell.csv` in the same
folder loads the whole order at once. **Farnell is the tested source:** the LCSC file
(`upload-lcsc-untested.csv`) has never been ordered and uses equivalents (BUY.md). The Pico must be the plain
**Pico 2** (SC1631, no headers), not the Pico 2 H. Also needed, not from Farnell: a 1.3" SH1106
I²C OLED (4 pins), 4 case screws (step 7) and 4 self-adhesive rubber feet, 10 mm.

## 3. Solder the board

Almost everything is through-hole. The interactive BOM, [`hardware/fab/picod64-ibom.html`](fab/picod64-ibom.html) (open it in a browser, or
online at https://bitbarista.github.io/picod64/hardware/fab/picod64-ibom.html),
shows where each part goes and lets you tick them off. Fit in this order, lowest parts first, so the board lies
flat on the bench:

1. **J3, microSD socket** (GCT MEM2075): the only surface-mount part. Every tail is outside the body, so
   drag-solder with flux. Its two locating posts hold it square (A4).
2. **A1, Pico 2:** flat on the board by its castellated edge. The holes under its USB socket take the solder
   bumps (A2b).
3. **Resistors** R1–R15 lying flat. **R18** stands upright at the left edge, body over the square pad.
   R9, R10 and C3 sit under the OLED: they must go in before it.
4. **RN1, RN2, RN3** (resistor networks): the dot to the square pad. **D4:** its band toward RN3 pin 1.
5. **C2–C5**, then **C1** (electrolytic): + to the square pad.
6. **Q1, Q2** (2N3904): bend the outer legs out to the 2.54 mm holes (the part has 1.27 mm leads), and match
   the flat face to the silk. Pinning is E-B-C (A1).
7. **J5** (3-pin debug header) and **JP1, JP2** (3-pin jumpers), cut from the header strip. Fit the JP1/JP2
   caps in step 4.
8. **J4** (4-pin socket for the OLED), **BZ1** (piezo), **SW1** (encoder), then **J1, J2** (DIN sockets).
9. **D1, D2 (LEDs):** they stand up through the lid, as on a 1541. Thread the leads through a printed LED
   standoff ([`case/stl/picod64-led-standoffs.stl`](../case/stl/picod64-led-standoffs.stl), step 6), one lead each side of its divider, and push the
   LED down onto it, flange on the standoff. The long lead goes in the round pad, the short lead (flat side
   of the LED) in the square pad. Solder, trim. Green D1 is PWR, red D2 is ACT.
10. **Trim every lead** to 2 mm or less under the board: the tray leaves 5 mm, and BZ1's leads come 13 mm
    long.

**Leave as made:** JP3, the solder jumper in J6's power path, is closed by a thin copper bar; it is cut only to
measure the current drawn through J6. The test pads (A C D R S G for ATN, CLK, DATA, RESET, SRQ and GND, and GP28 by the
Pico) are bare pads for a logic analyser. Nothing goes on them.

**Not fitted** on a standard build: J6 + D3 (external 5 V; on J6 power alone the bus pull-ups are off, R39),
Q3 + R16 + R17 (an SRQ output), J7 (spare GPIO header).

## 4. The OLED

- **Set the two jumpers before you plug the display in.** OLED modules don't all put power on the same pins:
  read the four labels on your module's header, left to right. The last two are always SCL and SDA; the
  first two are GND and VCC, in either order. JP1 sets the first pin and JP2 the second: put each cap on the
  **G** side for GND (or VSS) and the **V** side for VCC (or VDD).

  | Your module's labels | JP1 | JP2 |
  |---|---|---|
  | GND VCC SCL SDA (or VSS VDD SCL SDA) | G | V |
  | VCC GND SCL SDA | V | G |

  The module the board was designed around, and the sellers' drawings checked, are the first kind. A wrong setting can't harm the board, but it can damage the display, so check
  before switching on (AUDIT A10, D2).
- **Held by four printed pillars, no screws** ([`case/stl/picod64-oled-pillars.stl`](../case/stl/picod64-oled-pillars.stl), all the same). Each has a
  split peg at one end and a groove at the other that takes the module's PCB edge, so they suit any module's
  hole pattern.
  1. Slide two pillars onto each **side** edge of the module (the edges at the ends of the header row),
     grooves over the PCB and pegs pointing down: one near the header, one near the far end, clear of the glass.
  2. Lower the module onto J4. Slide each pillar along its edge until its peg is over its hole (H5–H8).
  3. Press down on the module's PCB beside each pillar, **not on the glass**, until the pegs click under the
     board.

  To take it out, squeeze each peg's two prongs together from under the board and lift.
- **A different module:** the pillars suit one measured module (D7): PCB 35.55 wide and 1.13 thick, 2.88 from
  the back of its PCB to the top of the glass, header plastic 2.58. That height puts the glass 0.45 under the
  lid's roof. For another module, measure these with calipers and enter them in [`case/picod64-case.scad`](../case/picod64-case.scad)'s
  **OLED module and pillars** tab (Customizer):
  - **thickness:** back of the PCB to the top of the glass, at the thickest place;
  - **PCB width and thickness**;
  - **header plastic:** the PCB plus the header's plastic, minus the PCB.

  Then run `bash case/export.sh`. It stops with a message if the groove won't take the PCB, the jaw would
  reach the lid, or the header pins would hardly reach into J4.
- **Window edge:** from a low front angle, about 1.5 mm of the module's board shows behind the glass at the
  back of the window. If it catches the light, black it out with a marker or black tape.

## 5. Firmware, SD card, first test

Do this before closing the case.

- **Flash:** hold the Pico's BOOTSEL button, plug in USB, and copy [`firmware/picod64.uf2`](../firmware/picod64.uf2) (in the release) onto
  the **RP2350** drive that appears. (Once cased, BOOTSEL is the pinhole on the lid marked ⤓: press it with a paperclip while plugging in.)
  To build the file yourself: `bash fw/build.sh` writes `fw/build/picod64.uf2`.
- **SD card:** any size, FAT32 or exFAT. Disk images (`.d64`) and programs (`.prg`) anywhere, in folders if you
  like. PicoD64 creates `.picod64/` on it for the fast loader's temporary disk.
- **The 1541 DOS ROM** (not included: it is Commodore's): the card setup page, [`fw/tools/card-setup.html`](../fw/tools/card-setup.html) (open it
  in a browser, or online as the README links it), fetches it, checks it and saves it for the card, and can add the
  Commodore font for the OLED. Or `bash fw/tools/get-roms.sh <card folder>` (Linux,
  macOS) or [`fw/tools/get-roms.ps1`](../fw/tools/get-roms.ps1) (Windows; the README shows how to run it) fetches it from the zimmers.net
  archive, joins and checks it, and writes `dos1541.bin` to the card. By hand: `1541-c000.325302-01.bin` +
  `1541-e000.901229-05.bin` from zimmers.net `firmware/drives/new/1541/`, joined in that order as
  **`dos1541.bin`** in the top folder: 16,384 bytes, MD5 `a0ce8439d1b8dcf2e1430461f7233a72`. Missing or the
  wrong size, the OLED shows *Need /dos1541.bin*.
- **Test:** connect PicoD64 to the C64 with an IEC (serial) cable and power it from USB. On the C64,
  `LOAD"*",8` then `RUN` starts the built-in browser; its keys are on its bottom two lines. Load something.
- **Device number** (8 unless you change it): hold the knob down while powering on, release, turn to pick
  8–11, press to save. It is kept in the Pico, not on the card, and shown as `#8` on the OLED.
- Optional, an OLED font: [`fw/fonts/font5x7.bin`](../fw/fonts/font5x7.bin) (plainer, same size) or the C64's character ROM
  (`characters.901225-01.bin` from zimmers.net `firmware/computers/c64/`, 4,096 bytes) renamed **`chargen.bin`**
  (the Commodore font itself; wider, so names get 14 characters instead of 19), in the top folder. If both are
  there, `chargen.bin` wins.
- USB serial (115200): `s` prints the state, `B` reboots to BOOTSEL.

## 6. Print the case

Print **every file in [`case/stl/`](../case/stl/)**, as exported (already in print orientation), with **no brim** (the
slicer's auto brim lands inside the label letters and fills them) and **no supports**:

| Part | File | On the bed |
|---|---|---|
| Tray | `picod64-tray.stl` | flat underside down |
| Lid, front and rear | `picod64-lid-front.stl`, `picod64-lid-rear.stl` | top face down |
| Knob | `picod64-knob.stl` | top face down (nose up) |
| Knob seating tool | `picod64-knob-seater.stl` | base down (a tool for step 7.5, not part of the case) |
| LED standoffs (2) | `picod64-led-standoffs.stl` | as exported |
| OLED pillars (4) | `picod64-oled-pillars.stl` | on their sides |
| Badges | `picod64-badge-pwr.stl`, `-act.stl`, `-name.stl` | letters up. For dark letters on a light badge, change filament at the first letter layer (0.8 mm) |

**Option:** with M3 heat-set inserts (step 7), print [`case/stl/options/picod64-lid-front-insert.stl`](../case/stl/options/picod64-lid-front-insert.stl) and
`picod64-lid-rear-insert.stl` instead of the two lids.

Print the LED standoffs and OLED pillars first: they go in during steps 3 and 4. Changes to the case:
[`case/CASE.md`](../case/CASE.md).

## 7. Assemble the case

1. Lay the board in the tray.
2. Fit the **front lid** first, then lower the **rear lid** over it: the front roof's edge and its side tongues
   go into pockets under the rear part's step wall. Both lids locate on a lip in a rebate round the tray's
   rim. If a print is tight, take a file lightly over the lid's lip.
3. Turn it over and fit **4 screws** from underneath, through the tray and the board (H1–H4) into the lid
   posts: the rear two hold the rear lid, the front two the front lid. Don't overtighten.
   - **Standard lids:** 4 × **M2.6 × 16 countersunk self-tapping** screws, into Ø2.1 mm pilot holes 10.5 deep.
     The heads sit about 1.25 mm below the tray's underside.
   - **Insert lids:** 4 × **M3 × 16 countersunk machine** screws, into M3 heat-set inserts (7 mm long or
     less, the "4.0 mm hole" kind) pressed into the lid posts. Press them in gently: the post wall is 1.5 mm on
     its inner side.
4. Stick a **rubber foot** (10 mm, up to 10.6 mm) over each screw head, centred in the thin ring marked round it
   on the tray's underside. To open the case later, peel the feet off to reach the screws.
5. **Seat the knob on the tool first.** Stand the knob seating tool on the bench, line up the knob's flat with the
   tool's, and press the knob down until it stops on the base. The first push is the firm one; doing it here keeps
   that force off the encoder. Pull it off, then push the **knob** onto the encoder's shaft, which now takes far
   less force.
6. Stick each **badge** into its recess with thin double-sided tape: PWR and ACT go over their LEDs, the name
   on the rear lid.

## Still to confirm on real parts

- The BOOTSEL pinhole over a real Pico 2 (D5).
