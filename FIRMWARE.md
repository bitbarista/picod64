# PicoD64 firmware

How the firmware works, how to build and test it, and what is known to work. For building the board itself, see
[`hardware/BUILD-NOTES.md`](hardware/BUILD-NOTES.md); for the case, [`case/CASE.md`](case/CASE.md).

## Building

```
bash fw/build.sh                 # -> fw/build/picod64.uf2
```

It needs the Raspberry Pi Pico SDK (`PICO_SDK_PATH`, default `~/tools/pico-sdk`), an `arm-none-eabi` toolchain,
CMake and Ninja. The build checks that all of core 1's code is in SRAM and fails if less than 1,500 bytes of RAM are
spare (`RAM_FLOOR`): all RAM is allocated at build time and `malloc` is not linked, so running out is a build error.

Build switches, as environment variables:

| Switch | Default | What |
|---|---|---|
| `SYS_MHZ` | 150 | System clock. 150 MHz is the RP2350's rating and what PicoD64 is tested at |
| `DIAG` | 0 | 1 adds timing diagnostics (serial `p`). Too slow for demanding fast loaders: use for diagnosis only |
| `EAGER` | 0 | 1 turns the lazy disk off (see Timing), for comparison |
| `OPT` | 2 | Compiler optimisation level |

The built-in browser is 6502 code in [`fw/browser/`](fw/browser/). After changing it, run `sh fw/browser/build.sh` (needs ACME); it
regenerates the committed `browser_prg.h`.

**Flashing:** hold BOOTSEL while plugging in USB, then copy `picod64.uf2` to the `RP2350` drive. With the firmware
running, a 1200-baud open of the USB serial port also restarts it into BOOTSEL (`host/hw_bench.py flash` does that).

**The SD card** needs your own 1541 DOS ROM as `/dos1541.bin` (16,384 bytes: 325302-01 + 901229-05). It is
Commodore's and is not supplied. Optional: `/chargen.bin` (4,096 bytes) for the OLED's font.

## Design

**Two cores.** Core 1 owns the IEC pins and nothing else: either *browse mode* (the card as a drive,
[`core/iec_dev.h`](core/iec_dev.h) + [`core/browse.h`](core/browse.h)) or the *emulated 1541* ([`core/drive1541.h`](core/drive1541.h), [`core/cpu6502.h`](core/cpu6502.h),
[`core/via6522.h`](core/via6522.h)), running from SRAM. Core 0 owns the SD card (FatFs), the OLED, the knob, the LEDs and the piezo.
When browse mode needs the card, core 1 holds the bus busy (DATA low), asks core 0 and waits; the C64 waits with it.

**Pins** ([`hardware/gen/gen_sch.py`](hardware/gen/gen_sch.py)): UART0 GP0/1, I²C0 GP4/5 (OLED), SD detect GP7, SPI1 GP8-11 (SD), IEC inputs
GP12-15 ATN CLK DATA RESET (pin high = line released), outputs GP16 CLK / GP17 DATA (pin high = line pulled low),
encoder GP18/19/20, LED GP21, piezo GP22.

**Memory.** The mounted disk's GCR (252 KB) is the big buffer; browse mode borrows it while no disk is in. Mounting
streams the D64 from the card into GCR a track at a time; tracks the drive writes are decoded and written back to the
file when the motor has been off for a second, before a swap or an unmount.

**The ATN acknowledge** is a PIO state machine ([`fw/atn_gate.pio`](fw/atn_gate.pio)): on a real 1541, DATA follows ATN through a gate
within nanoseconds, and software was too slow for some fast loaders. Measured on a real C64's bus: DATA follows an ATN
edge 0.17 µs later when pulled, 0.42 µs when released (the line's slower rise); a 1541 on the same C64, 0.25 µs.

**Fast loaders from browse mode.** A program loaded from the card that then sends drive code (M-W) gets a temporary
disk: the conversation so far is replayed into a freshly booted DOS ([`core/iec_host.h`](core/iec_host.h)) while the bus is held busy,
then the emulated 1541 takes over.

**Back to browsing:** `CD_`, `CD:_` or `CD//` on the command channel while a disk is mounted. A passive copy of the
IEC receiver watches the emulated drive's own view of the bus for them (the *sniffer*). The knob can also eject.

**C64 reset.** As on a real 1541, whose RESET comes from the cable, the whole board restarts: what the drive wrote
goes to the card first, then both cores start again (ready in about 0.9 s; a C64 takes 2–3 s to READY). A disk put
in by the knob stays in, as with a real 1541, and goes back in once the board is up. A disk put in by
the browser (Run first, a program from inside a D64) comes out, and browse mode goes to the card's root, so
`LOAD"*",8` after a reset is always the browser. The reset's falling edge is latched in hardware, so a short pulse is
never missed.

## Timing

The emulated 1541 performs every bus cycle of a real one, in order, dummy cycles included. It runs ahead of real time
and waits for its exact cycle only where the outside world can see it: every VIA1 (IEC) read and write. Between
those it has no inputs, so running ahead changes nothing it does.

- **Writes** do their work up to 6 µs early, and only the pins wait for the cycle: they change 0.3 µs after it.
  Fast loaders leave little room: The Hat's loader reads the bus 13.19 µs after its ATN edge, and a real 1541
  answers by 13.0.
- **Releases lead pulls.** The output transistors (Q1/Q2, 2N3904) let go of a line later than they pull it: on the
  board, serial `O` measures a release reaching the C64 228–242 ns after a pull would (10 runs). A write's releases
  go 0.14 µs before its pulls, so a release reaches the C64 about 0.1 µs after a pull of the same write, the order a
  1541 gives (the Ultimate's 1541: 0.07 µs). The ATN acknowledge (a PIO state machine, below) can't be moved earlier.
- **How these were set.** No published measurement of a real 1541's edges against its cycles was found, and there is
  no real 1541 here to measure. The two figures are the setting that ran every title in
  [`docs/COMPAT-TESTS.md`](docs/COMPAT-TESTS.md) on this board. Others tried broke a title each (Coma Light 13 with
  releases 0.23 µs early, Dutch Breeze with the pins at 0.17 µs). Serial `+`/`-` and `}`/`{` change them for a test.
- **The lazy disk.** While the drive can't see the disk byte by byte (VIA2's CA1 interrupt off, not writing), the
  head's events are not scheduled; they are caught up, exactly, before anything looks: a VIA2 access, or an
  instruction that reads the V flag (SO). Idle work runs only when there is time to spare before the next IEC access.
- **Run ends.** The drive runs in chunks; a chunk ends only when there are 3 µs to spare, so the caller's housekeeping
  never lands between a fast loader's read and its answer. The drive never runs more than 10 ms ahead of real time.
- **Measured** at 150 MHz on The Hat (a demanding demo loader), both disks: 0 of 867,115 timed writes late, worst read
  0.5 µs late. Serial `s` prints `pins late` (writes whose pins were set after their cycle) and `late` (syncs).
- **Deviation:** with VIA1's ATN interrupt enabled and I clear (the standard KERNAL protocol, not fast loaders), an
  ATN edge can reach CA1 up to about 24 cycles late (`DRIVE_LEAD`).

So: a cycle-exact 1541 emulation, synchronised to real time at every IEC access, with bus timing within about 1 µs of
a real drive's.

## The built-in browser

`LOAD"*",8` at the card's root gets a 477-byte bootstrap that fetches the browser over the turbo; `LOAD"BROWSER",8`
is the plain way. In a folder, `LOAD"*",8` loads that folder's first program, the plain way. Keys: cursor keys or
joystick 2, RETURN, DEL (back), F1 (menu), F3 (turbo on/off), typing filters the list.

**The turbo** has no drive code: the firmware is the far end of the cable. With the screen blanked and interrupts
off, for each byte the C64 pulses DATA and reads CLK/DATA four times; the firmware holds each bit pair 8 µs centred
on the read, so PAL and NTSC both fit. ATN is not used, so another drive on the bus is not disturbed. A checksum
ends each file; on a wrong one, or a file that would overwrite the browser's code or the I/O area, the browser falls
back to a plain LOAD.

The turbo is for PRG files on the card only. A program inside a D64 (RETURN on it, or Run first) loads the plain
1541 way: the disk goes in and the browser types the LOAD, so the drive reads the disk itself, as a real one does
(some loaders read through the DOS straight after and need the drive to know the disk).

The menu (F1): for a program, **Run**, **Load**, **Run slow** (no turbo); for a D64, **Open**, **Run first** (its
first program, the plain way), **Insert** (to BASIC).

Channel-15 commands ([`core/browse.h`](core/browse.h)):

| Command | Does |
|---|---|
| `XD:hhhh[,filter]` | A window of the folder from entry hhhh over the turbo (`FFFF` = round the saved position) |
| `XO:hhhh` | Open entry hhhh (a folder, or mount a D64) |
| `XI:hhhh` | Name the D64 that `XM` reads, without mounting it (Run first); it is mounted after the transfer |
| `XA:hhhh` | Point `LOAD"#"` at entry hhhh: a plain LOAD of a long name |
| `XL:#hhhh` / `XL:name` | Prepare a card file for the turbo |
| `XM:name` / `XM:*` | Refused (`62, FILE NOT FOUND`): a file inside a D64 loads the plain way |
| `XB` / `XG` / `XC` | Prepare the browser for the bootstrap / start the transfer / cancel |
| `XR` / `XS:t` | Read / write `/.picod64.cfg`: turbo, sound, folder and position |
| `XN` | Put in the next disk of the set (as the knob's long press); the DOS itself answers with a syntax error |

Long names (over 63 characters) are shortened with a hash and still open. Disks of a set show their number
(`1A`, `0`, `v1.1`), and the knob's long press puts in the next disk of the set.

## Testing

`bash host/check.sh` runs everything on a PC: the CPU against Klaus Dormann's functional test and perfect6502
(bus-identical), the drive against a reference model over nine workloads ([`host/equiv.sh`](host/equiv.sh), with the lazy disk always,
never and partly caught up), a real-DOS read, write and disk swap of a whole D64, browse mode against VICE's 1541 as
an oracle, the board firmware's *twin* ([`host/fwtwin.sh`](host/fwtwin.sh): the firmware's own code with the real C64 KERNAL, FatFs on
an image, the OLED to PNG, over 100 checks), and mutation tests ([`host/mutate.sh`](host/mutate.sh): planted faults the checks must
catch). It expects the ROMs and test binaries where [`host/run.sh`](host/run.sh) says; they are not supplied.

**The fill test** (`host/filltest/make.py OUT`): a D64 filled to 0 blocks free with five self-checking programs,
and the same five as PRGs. RUN prints `FILL TEST FILE n: OK, $count`, or `BAD AT $address` at the first wrong byte.

**On the board,** the USB serial port takes one-letter commands:

| Key | Does |
|---|---|
| `s` | Status: mounts, resets, lateness, pins late, sniffer, browse device, card reads, turbo, stacks |
| `z` | Clear the lateness counters |
| `M/path` + Enter / `E` | Mount a disk or program / eject (as the knob) |
| `c` / `C` | The drive's event log (a loader's command byte, the head's half-track) / clear it |
| `d` | Dump the drive's RAM $0300-$07FF |
| `b k K n N r R` | Speed benchmarks, off the bus (sysclk per drive cycle; 150 is real time at 150 MHz) |
| `X` | Bus off: PicoD64 lets go of every line and answers nothing (another drive can have the bus) |
| `S` | The bus sniffer ([`fw/iec_sniff.pio`](fw/iec_sniff.pio)): every ATN edge, sampled just before and 13 µs after |
| `p` | Timing profile (`DIAG=1` builds) |
| `L` / `l` | Logic analyser on the IEC pins: arm / print |
| `+` / `-`, `}` / `{` | Pins later / earlier (5 sysclk); releases earlier / later than pulls (7 sysclk). Not saved |
| `O` | Line timing: how much later a release reaches the C64 than a pull, with [`fw/linetime/linetime.prg`](fw/linetime/linetime.prg) running on it (no disk in, nothing else on the bus; about 10 s) |
| `A` / `a` | Answer watch: each answer's margin before the C64's read, from our outputs / from the bus lines; a key stops it |
| `B` | Restart into BOOTSEL |

[`host/hw_sniff.py`](host/hw_sniff.py) records a loader through the sniffer, and [`host/sniff_cmp.py`](host/sniff_cmp.py) / `sniff_pauses.py` compare two
recordings, for example PicoD64 against a real-timing drive on the same C64.

## Known to work

On a real PAL C64 at the stock 150 MHz, each from a powered-up C64, to its end: Coma Light 13, Dutch Breeze, Edge of
Disgrace, Uncensored, Lunatico, Shards of Fancy, MATH, Memento Mori, Primitive 7 Sins, Reanim8ed, The Hat, The
Masque, Transwarp, Wonderland XIV (40 tracks) and Mafio-DOS; the 1541 Testsuite (all 219 tests); the stock DOS
(directory, LOAD, SAVE back to a D64 and to the card); the browser and its turbo. The list, with each title's loader:
[`docs/COMPAT-TESTS.md`](docs/COMPAT-TESTS.md). JiffyDOS (KERNAL, and KERNAL plus drive ROM) works on
a real C64, every byte checked. FB64 works on the host twin; not confirmed on a board yet.

## Known issues

- **The Hat**: disk 1 crashed in its loader 7 times in about 80 unattended runs at this firmware's bus timing (on a
  test build with the same timing). Other timings tried each broke another demo.
- **Transwarp v0.86, Menu entry 1**: now and then the C64 crashes during or after the load (a garbled BASIC
  error, or a yellow screen), with the drive's timing clean: 2 of 13 runs on v0.2, 1 of 14 on v0.1.
- **Dutch Breeze** crashes on disk 1, on v0.1 too (v0.2 0 of 1, v0.1 1 of 2).
- A first load very occasionally fails with `23,READ ERROR` (about once in 60 runs of Transwarp's loader).
  Cause not found.
- During a disk change, the drive's reads were measured 12–39 µs late (its no-disk tables are read from flash then).
  Fast loaders that swap disks while timing the bus could notice.
- Taking the SD card out while PicoD64 runs, then loading, locked the C64 once; not seen since a fix to remounting
  the card, but not retested either. Switch PicoD64 off before changing cards.
- A name over 63 characters isn't seen as part of a disk set by the knob's next disk.
- `.g64`, `.d71`, `.d81`, CRT and T64 are listed but can't be run.
- Tested on PAL only: NTSC is designed for but untested.
