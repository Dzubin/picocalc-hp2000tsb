# HP 2000F and HP 2000 Access Time-Shared BASIC - desktop (SDL2) build for Windows and Linux, by Thomas Dzubin.
#
# Builds the same program as the firmware. Instead of the PicoCalc drivers it
# links the small SDL2 shim in this folder, which re-implements the
# picocalc-text-starter driver API (lcd / audio / south-bridge keyboard, by
# Blair Leduc) - see shim.h. The vendored starter's headers and fonts are used
# as-is. The shared build logic (SDL2, output names, copying the result) is in
# shim_desktop.cmake.
#
# Build on Windows (MSYS2 UCRT64 shell with SDL2 installed):
#     cmake -S desktop -B build-windows -G Ninja
#     ninja -C build-windows
# Output: hp2000f-Windows.exe or hp2000access-Windows.exe, copied to the top-level project folder. SDL2 is
# linked statically, so it is one self-contained file.
#
# Build on Linux (needs the SDL2 development package, e.g. libsdl2-dev):
#     cmake -S desktop -B build-linux
#     cmake --build build-linux
# Output: hp2000f-Linux or hp2000access-Linux, copied to the top-level project folder together with
# libSDL2-2.0.so.0 (SDL2 is NOT linked in statically on Linux, so keep the two
# files side by side; the executable finds the library through an $ORIGIN
# run-path).

cmake_minimum_required(VERSION 3.13)

project(picocalc-hp2000tsb-desktop C)

set(CMAKE_C_STANDARD 11)

get_filename_component(TOP ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(STARTER ${TOP}/picocalc-text-starter-main)

# Shared build logic (SDL2, output names, copying the result to the top level).
include(${CMAKE_CURRENT_LIST_DIR}/shim_desktop.cmake)

# Which system to build: F (the HP 2000F, two CPUs) or ACCESS (HP 2000 Access, two
# bigger CPUs):
#     cmake -S desktop -B build-windows-f -DHP_SYSTEM=F ...          (the 2000F)
#     cmake -S desktop -B build-windows-access -DHP_SYSTEM=ACCESS ...  (Access)
set(HP_SYSTEM ACCESS CACHE STRING "System to build: F or ACCESS")
if(HP_SYSTEM STREQUAL "ACCESS")
    set(SYS_NAME hp2000access)
    set(SYS_TITLE "HP 2000 Access Time-Shared BASIC")
    set(SYS_SOURCES ${TOP}/hp2000access.c ${TOP}/dev_ipl.c ${TOP}/hp_pair.c ${TOP}/hp_loaders.c)
elseif(HP_SYSTEM STREQUAL "F")
    set(SYS_NAME hp2000f)
    set(SYS_TITLE "HP 2000F Time-Shared BASIC")
    set(SYS_SOURCES ${TOP}/hp2000f.c ${TOP}/dev_ipl.c ${TOP}/hp_pair.c ${TOP}/hp_loaders.c)
else()
    message(FATAL_ERROR "HP_SYSTEM must be F or ACCESS")
endif()
add_compile_definitions(PLAT_DATA_DIR="${SYS_NAME}/" PATH_MAX_LEN=520 WIDE_TERMINAL=1)

picocalc_desktop_app(
    NAME     ${SYS_NAME}
    TITLE    "${SYS_TITLE}"
    SOURCES
        ${SYS_SOURCES}
        ${CMAKE_CURRENT_LIST_DIR}/platform_desktop.c
        ${TOP}/storage_stdio.c
        ${TOP}/hp21xx.c
        ${TOP}/hp21xx_fp.c
        ${TOP}/hp21xx_iop.c
        ${TOP}/hp_dma.c
        ${TOP}/dev_ptr.c
        ${TOP}/dev_tty.c
        ${TOP}/dev_tbg.c
        ${TOP}/dev_disc.c
        ${TOP}/dev_dq.c
        ${TOP}/dev_mux.c
        ${TOP}/dev_tape.c
        ${TOP}/tape_image.c
        ${TOP}/runlog.c
        ${TOP}/spare.c
        ${TOP}/hostfile.c
        ${TOP}/terminal.c
        ${TOP}/termdraw.c
        ${TOP}/autotype.c
        ${STARTER}/drivers/font-8x10.c
        ${STARTER}/drivers/font-5x10.c
    INCLUDES
        ${CMAKE_CURRENT_LIST_DIR}/wide
        ${TOP}
        ${STARTER}/drivers
)
