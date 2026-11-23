# PROVENANCE — where everything in PicoD64 came from

**Rule (2026-10-25): this project is white-coat.** Every line of the emulator is written here, from
primary documentation. Other implementations may be run **as black-box oracles** in
test benches. Nothing from them is read for reuse, and nothing from them goes into what
is shipped.

Evidence tags:

| Tag | Meaning |
|---|---|
| **[D]** | Datasheet or primary source |
| **[M]** | Measured by this project; the artefact is named |
| **[C]** | Cross-checked against an independent source |
| **[A]** | Assumption, not yet verified. Must be cleared or cut |

The citation rule: every factual claim carries a source key.
A search-engine summary is not a source. Where a second source exists, corroborate.

---

## Not opened, and not to be opened

These are implementations of the same thing. **None has been opened for this project.**
Product pages, reviews and README feature lists were seen in web searches on
2026-10-25 as market information only.

| Project | Licence | Status |
|---|---|---|
| Pi1541 (and pottendo-Pi1541 fork) | GPL-3.0 | ❌ unread |
| VICE drive and CPU source | GPL-2.0 | ❌ unread. The VICE *binary* may be used as a black-box oracle |
| MiSTer / FPGA64 / C128_MiSTer C1541, MEGA65 C64 core | various | ❌ unread |
| 1541 Ultimate / Ultimate 64 | closed | ❌ unread |
| sd2iec, arm2iec, uno2iec, Meatloaf, IECDevice | GPL / various | ❌ unread. Not cycle-level emulators anyway |

**Exception (2026-10-29):** the sd2iec **README** may be read as documentation of its
command set (`CD`, image mounting, etc.), so browse mode works with FB64. Its source stays unread.

**Exception (2026-10-30):** the source of **FB64 Turbo** (CBM Filebrowser V1.6 11x Turbo
Pi1541 Edition, DeeKay & TSM, CSDb 223021) may be read: a C64 program we test against for
compatibility, not a drive emulator, and not shipped. Nothing from it goes into PicoD64.
| 1541-rePico, 1541-rebuild | various | ❌ unread |

## Sources

| Key | Source | Held at | Used for |
|---|---|---|---|
| **[S1]** | MOS **MCS6500** family documentation | ⚠ **not obtained**. 6502.org refuses scripted downloads (HTTP 404 to curl) and that was not worked around | Nothing yet. Claims cited [S1] in the code are also cited [S3], so currently rest on [S3] and the [S4] oracle |
| **[S2]** | **Rockwell R6522 VIA** data sheet, text transcription by F. Kontros, corrected by W. Lorenz (zimmers.net `chipdata/6522-VIA.txt`); scans of the data sheet `chipdata/6522.zip`, 24 pages | `datasheets/zips/` | Register map, IFR/IER, port reads, timer modes; T1 one-shot IRQ **N+1.5 cycles** (Fig. 15, line 775), free-run period **N+2** (line 846) |
| **[S3]** | **64doc**, J. West and M. Mäkelä, rev 1.8 1994 (zimmers.net `chipdata/64doc`). ⚠ Documentation written by the authors of an emulator (x64, VICE's ancestor). It is a description of the chip measured on hardware, not code, and is used as such. **Facts only; its example C code was not copied** | `datasheets/64doc.txt` | Per-cycle bus activity of every addressing mode; IRQ "must occur before the last cycle" (l. 890–893); interrupt entry 7 cycles; BRK takeover (l. 895–899); JMP (ind) page wrap (l. 792–795); -SO sets V (l. 186–196); NMOS decimal-mode flags |
| **[S4]** | **perfect6502**, M. Steil, B. & B. Silverman, MIT licence: a simulation of the 6502's transistor netlist from visual6502's die photographs. Run as a cycle-trace build of it | perfect6502 | **Oracle.** Bus-cycle equivalence, [`host/diff.py`](host/diff.py) |
| **[S5]** | **Klaus Dormann**, `6502_functional_test.bin` (GPL-3.0, *test program*, run not copied). Its published binary | Klaus Dormann's repository | **Oracle.** Functional coverage of documented opcodes and decimal mode |
| **[S6]** | A. Vardy, *Extra Instructions of the 65XX Series CPU*, 1996 (zimmers.net `chipdata/6502-NMOS.extra.opcodes`) | `datasheets/zips/` | Undocumented opcodes. Agrees with [S3] on ANE, SHA/SHX/SHY/SHS, LAS; those are oracle-checked (perfect6502, and the 1541 Testsuite on the twin and in VICE, 2026-11-09) |
| **[S7]** | Commodore **1540/1541 Service Manual**, PN 314002-01, Nov 1985 (zimmers.net). © Commodore; held for reference, not to be redistributed | `datasheets/` | 1 MHz clock from 16 MHz ÷ 16; zone dividers 13/14/15/16; RAM `$0000-$07FF`; ROM `$C000-$FFFF`; two VIAs at `$1800` and `$1C00`; ATN on PB7 and CA1; ATNA on PB4, XOR'd onto DATA; BYTE READY |
| **[S9]** | Commodore 1541 DOS ROM **325302-01 + 901229-05**, the ROM image shipped in the VICE 3.9 source tree's `data/DRIVES/` (ROM data, not VICE code), md5 `a0ce8439d1b8dcf2e1430461f7233a72`. © Commodore; test use only, never shipped (the user supplies their own on the SD card) | `~/src/vice-3.9/data/DRIVES/` | F4: the real DOS, run on our core |
| **[S10]** | R. Immers, G. Neufeld, *Inside Commodore DOS*, Datamost 1985, OCR scan (devili.iki.fi `pub/Commodore/docs/books/Inside_Commodore_DOS_OCR.pdf`). © the authors; held for reference, not to be redistributed. ⚠ Not Commodore's own document, but the standard reference written from the DOS; the DOS ROM running on our core is the check (F4b) | `datasheets/` | Track/sector layout, zones, GCR table, header/data blocks, job queue ([`core/disk.h`](core/disk.h), [`host/f4b.c`](host/f4b.c)) |
| **[S11]** | VICE `c1541` 3.9 run as a **black box** (`-block t s`): the D64 sector order | — | [`core/disk.h`](core/disk.h) D64 offsets, checked on 10 sectors across all zones |
| **[S18]** | VICE **x64sc** 3.9 with true drive emulation, run as a **black box** (2026-11-06), through its remote monitor: breakpoints and memory reads only; its source unread, nothing from it ships | — | The Hat (a demo with its own loader) runs on it; the command bytes its drive receives compared with the twin's (FIRMWARE.md, "First board") |
| **[S19]** | *1541 Testsuite* r192, Ninja / The Dreams, 2008 (CSDb 73533; SourceForge rrtools): disk images of test programs that run on the drive's own 6502 (Lorenz's opcode tests, ported). Run as a **black box** on the board, the twin and VICE [S18]; only their pass/fail screens read. Not shipped (`~/PicoD64-compat/`) | — | ANE, SHA/SHX/SHY/SHS, LAS checked (2026-11-09): all 219 (pc64_1 115, pc64_2 104) on the board and the twin; docs/COMPAT-TESTS.md |
| **[S21]** | The **VICE 3.9 manual**, "The D64 disk image format": its variations (error bytes after the sectors, one per sector; the file sizes) and its error-code table (contributed by P. Schepers, partly from [S10]). Documentation of the file format, not code (`doc/vice.texi`) | `~/src/vice-3.9/doc/` | [`core/disk.h`](core/disk.h) `d64_track_sectors_err`, [`fw/storage.c`](fw/storage.c): which byte is which sector, and what each code means. How each is built into the GCR is [A], checked against the real DOS (host/errbytes_test.c) |
| **[S22]** | Raspberry Pi **Pico SDK** `boot/uf2.h` (BSD-3-Clause): the UF2 block layout, its magic numbers and flags, and the family IDs (RP2350 Arm Secure `0xE48BFF59`; `0xE48BFF57` the "absolute" block picotool adds) | `~/tools/pico-sdk/src/common/boot_uf2_headers/` | [`fw/update.c`](fw/update.c): reading `/picod64.uf2` from the card |
| **[S12]** | Commodore 64 **Programmer's Reference Guide**, 1982 (Project 64 etext). © Commodore; held for reference | `datasheets/c64-programmers-reference-guide.txt` | The C64 test fixture [`host/c64.c`](host/c64.c): memory map, 6510 port, CIA2 serial bits, 6526 (App. M), VIC raster (App. N), KERNAL entry points; serial bus timing table (p.365) |
| **[S13]** | C64 **KERNAL 901227-03** (md5 `39065497630802346bce17963f13c092`) and **BASIC 901226-01** (md5 `57af4ae21d4b705c2991d98ed5c1f7b8`), from the VICE install's ROM directory (ROM data, not VICE code). © Commodore; test use only | `~/.local/share/vice/C64/` | F4c: the real KERNAL's serial code drives our drive |
| **[S14]** | M. Steil, *Commodore Peripheral Bus*, pagetable.com, part 2 (TALK/LISTEN: command bytes LISTEN $20+pa, UNLISTEN $3F, TALK $40+pa, UNTALK $5F, SECOND $60+sa, CLOSE $E0+sa, OPEN $F0+sa) and part 4 (Standard Serial: the line protocol, EOI, turnaround, timings). Documentation, read 2026-10-29; it cites the KERNAL source, which was not read | web | [`core/iec_dev.h`](core/iec_dev.h) (browse mode). ⚠ Its data-bit polarity is disputed in its comments: the real KERNAL [S13] settles it in host/f6.sh |
| **[S15]** | sd2iec **README** (github.com/cbmeeks/sd2iec), documentation only, by exception (2026-10-29); the source unread | web | [`core/browse.h`](core/browse.h): CD forms (CD:name, CD/name, CD_ / CD:_ up, CD// root, CD:image mounts), listing conventions (blocks of 254, extensions shown) |
| **[S16]** | SD Association, *Physical Layer Simplified Specification* (Part 1), §7 SPI mode. ⚠ **not yet in hand**: behind a click-through licence that must be accepted (2026-10-30); until then every step of [`fw/sd_spi.c`](fw/sd_spi.c) is [A] | sdcard.org | [`fw/sd_spi.c`](fw/sd_spi.c) |
| **[S17]** | Sino Wealth **SH1106** datasheet V2.3 (`datasheets/parts/sinowealth_SH1106_v2.3.pdf`, via Crystalfontz) | PDF | [`fw/ui.c`](fw/ui.c): I²C address 0x3C, control bytes (Co, D/C), command set and POR values, page/column addressing (132 columns) |
| **[L1]** | **FatFs** R0.15a, ChaN (elm-chan.org), 1-clause BSD ([`fw/fatfs/LICENSE.txt`](fw/fatfs/LICENSE.txt)). A third-party *library*, not an implementation of anything we emulate, so outside the white-coat rule; used unmodified except `ffconf.h` (LFN, exFAT, CP437, no RTC) | elm-chan.org | [`fw/storage.c`](fw/storage.c) (the SD card's filesystem) |
| **[L2]** | **BESCII** v2.0, Damian Vila (github.com/damianvila/font-bescii), **CC0 1.0** ([`fw/fonts/bescii/LICENCE`](fw/fonts/bescii/LICENCE)): an 8x8 font in the PETSCII style, the OLED's default (2026-10-30). A font, not anything we emulate | github | [`fw/font_bescii.h`](fw/font_bescii.h) (rasterised by [`fw/tools/mkbescii.py`](fw/tools/mkbescii.py)) |
| — | The plain OLED font ([`fw/tools/font5x7.txt`](fw/tools/font5x7.txt) → [`fw/font5x7.h`](fw/font5x7.h), [`fw/fonts/font5x7.bin`](fw/fonts/font5x7.bin)) is PicoD64's own, drawn 2026-10-30; not built in, an optional file for the card | — | [`fw/ui.c`](fw/ui.c) (`ui_font57`) |
| — | The PETSCII-style 5x7 ([`fw/font_pet57.h`](fw/font_pet57.h), [`fw/fonts/pet57.txt`](fw/fonts/pet57.txt)): BESCII [L2] squeezed to 5x7 by [`fw/tools/mkpet57.py`](fw/tools/mkpet57.py), 16 glyphs redrawn by hand ([`fw/fonts/pet57_fixes.txt`](fw/fonts/pet57_fixes.txt)), 2026-10-30. CC0 like its source | — | [`fw/ui.c`](fw/ui.c) (`ui_font_small(1)`) |
| **[S8]** | Commodore schematic **251748 rev E** (short board), two sheets (zimmers.net) | `datasheets/` | ⚠ **Obtained, not yet traced.** Needed to clear the [A] items below |

## Open findings

**F-1 ⚠ [S7] contradicts itself, or the common map.** The service manual places UC3, the
*serial* VIA, at **`$1C00`** and UC2, the *read/write* VIA, at **`$1800`** (circuit-theory
pages). The commonly published 1541 map has the opposite. The code follows the common map
and marks it [A]. **Trace UC7's decoding on [S8] to settle it.** It does not affect F1's
timing measurement. The same page also lists the third speed zone as "15-30", which must
be 25-30 given the other three rows.

**F-1 resolved (2026-10-29).** [S8] traced: UC2 (disk VIA) PA0-7 = YB0-7, PB0 STP1, PB1
STP0, PB2 MTR, PB3 ACT, PB4 WPS, PB5-6 density, PB7 SYNC, CA1 BYTE READY, CA2 SOE, CB2
MODE; UC3 (serial VIA) PB0 DATA IN, PB1 DATA OUT, PB2 CLK IN, PB3 CLK OUT, PB4 ATNA, PB7 and
CA1 ATN IN. The real DOS, which addresses the serial VIA at $1800, talks to the real KERNAL
through our drive (F4c), so the code's map stands and [S7]'s text is in error.

## [A] items in the code, to be cleared

| Item | Where | Clears by |
|---|---|---|
| ~~VIA1 PB bit assignment~~ | [`core/drive1541.h`](core/drive1541.h) | ✅ [S8] traced 2026-10-29 (device jumpers PB5-6 not yet) |
| ~~VIA2 pins~~ | [`core/drive1541.h`](core/drive1541.h) | ✅ [S8] traced 2026-10-29. Still [A]: step direction, write-mode polarity (both confirmed by the DOS working, F4b/F4d) |
| SYNC = ten consecutive 1 bits | [`core/drive1541.h`](core/drive1541.h) | [S8]; [S7] mentions only the 8-bit stage |
| Address-decode mirroring, open-bus value | [`core/drive1541.h`](core/drive1541.h) | [S8] trace |
| IRQ poll of a taken, non-crossing branch | [`core/cpu6502.h`](core/cpu6502.h) | perfect6502 with `--irq` (F2) |
| VIA timer phase within the cycle | [`core/via6522.h`](core/via6522.h) | VICE black box, or hardware (F2) |
| 7692-byte zone-3 track | [`bench/track.h`](bench/track.h) | Arithmetic from [S7] at 300 rpm [S7]. The bench only needs it plausible |
