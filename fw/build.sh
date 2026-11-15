#!/bin/sh
# Build the board firmware: fw/build/picod64.uf2
#   bash fw/build.sh
# Toolchain, SDK, TinyUSB and picotool as bench/build.sh (in ~/tools).
set -e
cd "$(dirname "$0")"
export PICO_SDK_PATH=${PICO_SDK_PATH:-$HOME/tools/pico-sdk}
export PICO_TOOLCHAIN_PATH=${PICO_TOOLCHAIN_PATH:-$HOME/tools/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi}
export PATH="$PICO_TOOLCHAIN_PATH/bin:$PATH"
export PICO_TINYUSB_PATH=${PICO_TINYUSB_PATH:-$HOME/tools/tinyusb-sdk2.2.0}
[ -f "$PICO_TINYUSB_PATH/src/tusb.h" ] || { echo "  *** no TinyUSB at $PICO_TINYUSB_PATH"; exit 1; }
PT=${picotool_DIR:-$HOME/tools/picotool}
[ -f "$PT/picotoolConfig.cmake" ] && PTARG="-Dpicotool_DIR=$PT" || PTARG=""
mkdir -p build
cmake -S . -B build -G Ninja $PTARG -DSYS_MHZ=${SYS_MHZ:-150} -DDIAG=${DIAG:-0} -DEAGER=${EAGER:-0} -DOPT=${OPT:-2} > build/cmake.log 2>&1 || { cat build/cmake.log; exit 1; }
grep -A3 -i "warning" build/cmake.log || true
ninja -C build
arm-none-eabi-nm build/picod64.elf | grep -q " tud_task_ext" || { echo "  *** USB stdio missing"; exit 1; }
# ⚠ no emulator code in flash (bench/build.sh)
bad=$(arm-none-eabi-nm build/picod64.elf | grep -E " [tT] " | grep -E " [tT] (cpu_|drive_|via_|head_|plat_|core1_main|BUS_|iec_|iech_|br_|bfs_|bus_|sn_|disk_frame)" | grep -v "^2" || true)
[ -z "$bad" ] || { echo "  *** emulator code in flash:"; echo "$bad"; exit 1; }
# RAM: everything is allocated at build time. Too much fails the link; this keeps a margin for fixes.
if arm-none-eabi-nm build/picod64.elf | grep -qE " T (_?malloc|_malloc_r|calloc|realloc)$"; then
  echo "  *** malloc is linked: the heap is only 256 bytes (PICO_HEAP_SIZE)"; exit 1
fi
RAM_END=$(arm-none-eabi-nm build/picod64.elf | awk '$3=="__bss_end__"{print $1}')
RAM_LIM=$(arm-none-eabi-nm build/picod64.elf | awk '$3=="__HeapLimit"{print $1}')
RAM_SPARE=$((0x$RAM_LIM - 0x$RAM_END))
echo "RAM spare: $RAM_SPARE bytes (floor ${RAM_FLOOR:=1100})"   # (1500 until 2026-11-10: lowered for the lazy T1, ~440 bytes)
[ "$RAM_SPARE" -ge "$RAM_FLOOR" ] || { echo "  *** RAM spare below the floor"; exit 1; }
arm-none-eabi-size build/picod64.elf
ls -la build/picod64.uf2
