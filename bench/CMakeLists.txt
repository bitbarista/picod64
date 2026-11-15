# PicoD64 feasibility bench F1 -- Raspberry Pi Pico 2 (RP2350).
#   bash bench/build.sh
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350-arm-s)
include(pico_sdk_import.cmake)
project(picod64_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(ff_bench main.c)
target_include_directories(ff_bench PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(ff_bench PRIVATE -O2 -Wall -Wextra -Wno-unused-function)
# ⚠ Everything runs from SRAM. (copy_to_ram was briefly dropped, wrongly
# blamed for the missing-TinyUSB bring-up failure. The flash build then left
# the out-of-lined cpu_rd in flash, behind a veneer, on every emulated cycle.
# build.sh now fails if any core function lands in flash.)
option(BENCH_XIP "experiment: run from flash via the XIP cache, not SRAM" OFF)
if(NOT BENCH_XIP)
  pico_set_binary_type(ff_bench copy_to_ram)
else()
  target_compile_definitions(ff_bench PRIVATE BENCH_XIP=1)
endif()
pico_enable_stdio_usb(ff_bench 1)
pico_enable_stdio_uart(ff_bench 0)
pico_set_program_description(ff_bench "PicoD64 F1 feasibility bench")
target_link_libraries(ff_bench pico_stdlib pico_multicore hardware_vreg)
pico_add_extra_outputs(ff_bench)

# F5 part 1: the real DOS in real time on the Pico, no SD (bench/f5.c)
add_executable(ff_f5 f5.c)
target_include_directories(ff_f5 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(ff_f5 PRIVATE -O2 -Wall -Wextra -Wno-unused-function)
pico_set_binary_type(ff_f5 copy_to_ram)
pico_enable_stdio_usb(ff_f5 1)
pico_enable_stdio_uart(ff_f5 0)
pico_set_program_description(ff_f5 "PicoD64 F5: real 1541 DOS in real time")
target_link_libraries(ff_f5 pico_stdlib pico_multicore)
pico_add_extra_outputs(ff_f5)
