#!/bin/sh
# Build the F1 bench: bench/build/ff_bench.uf2
#   bash bench/build.sh
# The toolchain and SDK: ~/tools (or PICO_SDK_PATH, PICO_TOOLCHAIN_PATH).
set -e
cd "$(dirname "$0")"
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/tools/pico-sdk}
export PICO_TOOLCHAIN_PATH=${PICO_TOOLCHAIN_PATH:-$HOME/tools/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi}
export PATH="$PICO_TOOLCHAIN_PATH/bin:$PATH"
# ⚠ ~/tools/pico-sdk has no TinyUSB submodule.
# Without it the SDK SILENTLY drops USB stdio: the first bench images ran and
# printed to nowhere (2026-10-25). Use TinyUSB 0.18.0, the commit SDK 2.2.0
# pins (86ad6e5), from its own folder; the shared SDK is left untouched.
export PICO_TINYUSB_PATH=${PICO_TINYUSB_PATH:-$HOME/tools/tinyusb-sdk2.2.0}
[ -f "$PICO_TINYUSB_PATH/src/tusb.h" ] || { echo "  *** no TinyUSB at $PICO_TINYUSB_PATH"; exit 1; }
# picotool: a built one at ~/tools/picotool (or picotool_DIR), if present; otherwise the SDK fetches it
PT=${picotool_DIR:-$HOME/tools/picotool}
[ -f "$PT/picotoolConfig.cmake" ] && PTARG="-Dpicotool_DIR=$PT" || PTARG=""
mkdir -p build
# the two workloads, assembled from our own source and embedded as C arrays
acme -DTIMER=10000 -o build/wl_dos.bin workload.asm >/dev/null
acme -DTIMER=150   -o build/wl_stress.bin workload.asm >/dev/null
acme               -o build/wl_timers.bin wl_timers.asm >/dev/null
acme -DATNIRQ=0    -o build/wl_iec.bin wl_iec.asm >/dev/null
acme -DATNIRQ=1    -o build/wl_iecatn.bin wl_iec.asm >/dev/null
acme -DMODE=0      -o build/wl_nop.bin wl_micro.asm >/dev/null
acme -DMODE=1      -o build/wl_ram.bin wl_micro.asm >/dev/null
for w in dos stress timers iec iecatn nop ram; do
  python3 -c "
import sys
b = open(sys.argv[1], 'rb').read(); assert len(b) == 16384, len(b)
rows = ',\n'.join(','.join(str(x) for x in b[i:i+32]) for i in range(0, len(b), 32))
open(sys.argv[2], 'w').write('static uint8_t %s[16384] = {\n%s\n};\n' % (sys.argv[3], rows))
" build/wl_$w.bin build/wl_$w.h wl_$w
done
# F5: the DOS ROM as a C array (test use only, never shipped [S9])
DOS=${DOS1541:-$HOME/src/vice-3.9/data/DRIVES/dos1541-325302-01+901229-05.bin}
python3 -c "
import sys
b = open(sys.argv[1], 'rb').read(); assert len(b) == 16384, len(b)
rows = ',\n'.join(','.join(str(x) for x in b[i:i+32]) for i in range(0, len(b), 32))
open(sys.argv[2], 'w').write('static const uint8_t dos1541[16384] = {\n%s\n};\n' % rows)
" "$DOS" build/dos1541.h
# cmake output is kept: a swallowed warning is how USB went missing
cmake -S . -B build -G Ninja $PTARG -DBENCH_XIP=${BENCH_XIP:-OFF} > build/cmake.log 2>&1 || { cat build/cmake.log; exit 1; }
grep -A3 -i "warning" build/cmake.log || true
ninja -C build
# ⚠ ASSERT USB STDIO IS IN THE IMAGE, not just requested
arm-none-eabi-nm build/ff_bench.elf | grep -q " tud_task_ext" || { echo "  *** USB stdio missing from ff_bench.elf"; exit 1; }
# ⚠ ASSERT NO EMULATOR CODE RUNS FROM FLASH: no symbol of the core or the
# cycle loop at 0x10xxxxxx, and no veneer to one
bad=$(arm-none-eabi-nm build/ff_bench.elf | grep -E " [tT] " | grep -E "cpu_|drive_|via_|head_|plat_|core1_run|BUS_" | grep -v "^2" || true)
[ -z "$bad" ] || [ "${BENCH_XIP:-OFF}" = ON ] || { echo "  *** emulator code in flash:"; echo "$bad"; exit 1; }
arm-none-eabi-nm build/ff_f5.elf | grep -q " tud_task_ext" || { echo "  *** USB stdio missing from ff_f5.elf"; exit 1; }
bad=$(arm-none-eabi-nm build/ff_f5.elf | grep -E " [tT] " | grep -E "cpu_|drive_|via_|head_|plat_|core1_main|BUS_" | grep -v "^2" || true)
[ -z "$bad" ] || { echo "  *** f5: emulator code in flash:"; echo "$bad"; exit 1; }
arm-none-eabi-size build/ff_f5.elf
ls -la build/ff_bench.uf2 build/ff_f5.uf2
