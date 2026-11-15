# Licences

Copyright © 2026 Carl Leach.

PicoD64 is free for **private and commercial** use: build it, sell it, change it. **Changes must
be shared under the same terms.** If you modify the design, the firmware or the documents and
distribute the result (boards, kits, programmed drives, files), you must make your modified
source available under the same licence. The project uses one licence written for hardware, one
for code and one for documents, all with that rule.

| What | Where | Licence |
|---|---|---|
| Hardware design: schematic, PCB, footprints, symbols, order files | [`hardware/`](hardware/) (except the scripts and the items below) | [CERN-OHL-W v2](LICENSES/CERN-OHL-W-2.0.txt) |
| Case design: OpenSCAD model, STLs, renders | [`case/`](case/) (except its scripts) | [CERN-OHL-W v2](LICENSES/CERN-OHL-W-2.0.txt) |
| Emulator, firmware, generators, checks and tools | [`core/`](core/), [`fw/`](fw/) (except the third-party folders below), [`host/`](host/), [`bench/`](bench/), [`hardware/gen/`](hardware/gen/), and the `*.py`/`*.sh` scripts in [`case/`](case/) and [`hardware/lib/3d/`](hardware/lib/3d/) | [GPL-3.0-only](LICENSES/GPL-3.0.txt) |
| Documentation | the `*.md` files, unless they say otherwise | [CC BY-SA 4.0](LICENSES/CC-BY-SA-4.0.txt) |
| Footprints adapted from KiCad's library (`EC11E_Switch_Alps`, `RaspberryPi_Pico_SMD_HandSolder_Pin1Rect`) and symbols taken from it | [`hardware/lib/`](hardware/lib/) | CC BY-SA 4.0 with the KiCad library exception (KiCad Libraries Team); the adapted versions are under the same licence |
| FatFs (ChaN) | [`fw/fatfs/`](fw/fatfs/) | FatFs licence (1-clause BSD style); the notice is in its files and `LICENSE.txt` |
| Bescii Mono font | [`fw/fonts/bescii/`](fw/fonts/bescii/) | CC0 1.0 |
| `pico_sdk_import.cmake` | [`bench/`](bench/) | BSD-3-Clause (Raspberry Pi); a copy from the Pico SDK |

**Not in this repository, with their own licences or owners:**
- The Raspberry Pi Pico SDK (BSD-3-Clause), used to build [`fw/`](fw/) and [`bench/`](bench/).
- Klaus Dormann's 6502 functional test and perfect6502, which the host checks run against; [`host/run.sh`](host/run.sh) says where they are expected.
- The makers' 3D models of J1/J2, J3 and SW1. They can't be redistributed, so [`hardware/lib/3d/fetch.sh`](hardware/lib/3d/fetch.sh) downloads them.
- The Commodore 1541 DOS ROM, and the KERNAL, BASIC and character ROMs the host checks use: Commodore's, never in this repository. You supply your own `dos1541.bin`.

[`THIRD-PARTY.md`](THIRD-PARTY.md) has the details and sources.

**Why these three.** CERN-OHL-W is the weakly reciprocal CERN licence: changes to this design
must stay open, but PicoD64 can be used alongside, or built into, other hardware without that
hardware coming under the licence. GPL-3.0 is stronger for the code: anyone who distributes the
firmware, changed or not, in a drive or as a file, must make its complete source available under
GPL-3.0. "Only" means version 3, not any later version the FSF may publish, so the terms cannot
change without the author. The source is given away free, so whoever builds on it gives theirs
away too. CC BY-SA 4.0 is the share-alike Creative Commons licence, for the documents.

Source location (CERN-OHL-W v2, section 4.3.d): https://github.com/bitbarista/picod64
