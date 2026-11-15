# PicoD64 board firmware (rev 1) -- Raspberry Pi Pico 2 (RP2350).
#   bash fw/build.sh
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350-arm-s)
include(../bench/pico_sdk_import.cmake)
project(picod64_fw C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(picod64 pico_main.c storage.c svc.c ui.c diskset.c sd_spi.c fatfs/ff.c fatfs/ffunicode.c)
# optimisation: bash fw/build.sh with OPT=3 (or s) to compare (2026-11-07); 2 is the default
if(NOT DEFINED OPT)
  set(OPT 2)
endif()
target_compile_options(picod64 PRIVATE -O${OPT} -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-stringop-truncation -Wno-format-truncation)
set_source_files_properties(fatfs/ff.c fatfs/ffunicode.c PROPERTIES COMPILE_OPTIONS "-w")
# ⚠ the drive runs from SRAM (bench/CMakeLists.txt). The whole image no longer fits
# beside the disk's GCR (252 KB), so it runs from flash and the core's per-cycle code is
# placed in SRAM (FF_HOT -> .time_critical); build.sh fails if any of it lands in flash.
# Core 1's other code is __not_in_flash too (fw/pico_main.c).
# stacks in the scratch banks: core 1 1.34 KB (816 bytes the most used, serial 's'; 3 KB until 2026-11-13, when the
# bank's other 2.6 KB took more of core 1's rarely run code, FF_SCRATCH); core 0 3 KB (FatFs keeps a FIL with its 512-byte buffer on the stack; the
# most measured is 1.7 KB, serial 's'), the bank's other 1 KB holds the logic analyser / sniffer ring (fw/pico_main.c
# la_t, 1 KB-aligned for its DMA: in main RAM its padding cost up to 1 KB, 2026-11-08)
target_compile_definitions(picod64 PRIVATE PICO_STACK_SIZE=0xC00 PICO_CORE1_STACK_SIZE=0x560 PICO_HEAP_SIZE=0x100)
# the system clock (fw/pico_main.c SYS_MHZ): bash fw/build.sh with SYS_MHZ=200 for the out-of-spec test build
if(DEFINED SYS_MHZ)
  target_compile_definitions(picod64 PRIVATE SYS_MHZ=${SYS_MHZ})
endif()
# the lazy disk off (core/drive1541.h DRIVE_EAGER): bash fw/build.sh with EAGER=1, a test build (2026-11-07)
if(EAGER)
  target_compile_definitions(picod64 PRIVATE DRIVE_EAGER=1)
endif()
if(DEFINED DIAG)
  target_compile_definitions(picod64 PRIVATE DIAG=${DIAG})
endif()
pico_enable_stdio_usb(picod64 1)
pico_enable_stdio_uart(picod64 0)
pico_set_program_description(picod64 "PicoD64: a cycle-exact 1541 on a Pico 2")
pico_generate_pio_header(picod64 ${CMAKE_CURRENT_LIST_DIR}/atn_gate.pio)
pico_generate_pio_header(picod64 ${CMAKE_CURRENT_LIST_DIR}/iec_sniff.pio)
target_link_libraries(picod64 pico_stdlib pico_multicore hardware_spi hardware_i2c hardware_pio hardware_dma hardware_pwm hardware_irq hardware_flash)
pico_add_extra_outputs(picod64)
