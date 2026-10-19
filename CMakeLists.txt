# HP 2000F and HP 2000 Access Time-Shared BASIC  (by Thomas Dzubin)
#
# Reuses the drivers from ./picocalc-text-starter-main (lcd, audio, southbridge),
# which is picocalc-text-starter by Blair Leduc, plus the Raspberry Pi Pico SDK.
# Build it with the Raspberry Pi Pico VS Code extension, or from a terminal that
# has PICO_SDK_PATH set (see README.md).

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in the Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(picocalc-hp2000tsb C CXX ASM)

pico_sdk_init()

# Location of the picocalc-text-starter drivers we build against
set(STARTER ${CMAKE_CURRENT_LIST_DIR}/picocalc-text-starter-main)

# Which system to build: F (the HP 2000F, two CPUs) or ACCESS (HP 2000 Access, two
# bigger CPUs). Each is a separate program with its own sources, version line and
# output file. The one-CPU 2000E program is archived in archive/hp2000e/.
#     cmake -B build-rp2350-f      -DPICO_BOARD=pico2 -DHP_SYSTEM=F ...        (the 2000F)
#     cmake -B build-rp2350-access -DPICO_BOARD=pico2 -DHP_SYSTEM=ACCESS ...   (Access)
set(HP_SYSTEM ACCESS CACHE STRING "System to build: F or ACCESS")
if(HP_SYSTEM STREQUAL "ACCESS")
    set(SYS_NAME hp2000access)
    set(SYS_SOURCES hp2000access.c dev_ipl.c hp_pair.c hp_loaders.c)
elseif(HP_SYSTEM STREQUAL "F")
    set(SYS_NAME hp2000f)
    set(SYS_SOURCES hp2000f.c dev_ipl.c hp_pair.c hp_loaders.c)
else()
    message(FATAL_ERROR "HP_SYSTEM must be F or ACCESS")
endif()
set(TARGET picocalc-${SYS_NAME})

add_executable(${TARGET}
    ${SYS_SOURCES}
    platform_pico.c
    storage_pico.c
    hp21xx.c
    hp21xx_fp.c
    hp21xx_iop.c            # the CPU core refers to it, so every system links it
    hp_dma.c
    dev_ptr.c
    dev_tty.c
    dev_tbg.c
    dev_disc.c
    dev_dq.c
    dev_mux.c
    dev_tape.c
    tape_image.c
    runlog.c
    spare.c
    hostfile.c
    terminal.c
    termdraw.c
    autotype.c
    # fat32.c is not listed: storage_pico.c includes it (see the note there)
    ${STARTER}/drivers/sdcard.c
    ${STARTER}/drivers/lcd.c
    ${STARTER}/drivers/audio.c
    ${STARTER}/drivers/southbridge.c
    ${STARTER}/drivers/font-8x10.c
    ${STARTER}/drivers/font-5x10.c
)

pico_set_program_name(${TARGET} "${TARGET}")

# The program version comes from the VERSION line in ${SYS_NAME}_const.h, so it is
# written down in one place only (a change there re-runs CMake).
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/${SYS_NAME}_const.h VERSION_LINE
     REGEX "^#define VERSION ")
string(REGEX REPLACE "^#define VERSION \"V?([^\"]*)\".*" "\\1" PROGRAM_VERSION "${VERSION_LINE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/${SYS_NAME}_const.h)
pico_set_program_version(${TARGET} "${PROGRAM_VERSION}")

# Build the audio PIO program (generates audio.pio.h that drivers/audio.c needs)
pico_generate_pio_header(${TARGET} ${STARTER}/drivers/audio.pio)

# USB serial carries the second user terminal (multiplexer channel 1); ~ still goes to BOOTSEL
# The emulator's code is large and its loop is hot; run it from RAM instead of the
# 16 KB flash cache (the program, with its data, fits in the RP2350's 520 KB).
pico_set_binary_type(${TARGET} copy_to_ram)

pico_enable_stdio_uart(${TARGET} 0)
pico_enable_stdio_usb(${TARGET} 1)

target_compile_options(${TARGET} PRIVATE -Wall)

# The folder on the SD card for this system's files (/hp2000f/ or /hp2000access/).
target_compile_definitions(${TARGET} PRIVATE PLAT_DATA_DIR="/${SYS_NAME}/")

target_include_directories(${TARGET} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${STARTER}/drivers
)

target_link_libraries(${TARGET}
    pico_stdlib
    pico_multicore
    hardware_gpio
    hardware_i2c
    hardware_spi
    hardware_pio
    hardware_clocks
    hardware_watchdog
)

# The chip is part of every output file name (hp2000f-RP2040.uf2 /
# hp2000f-RP2350.uf2, or hp2000access-... for Access, and the matching .elf/.bin/.hex), so a downloaded file
# always says which board it was built for.
if(PICO_PLATFORM MATCHES "^rp2040")
    set(CHIP_NAME RP2040)
elseif(PICO_PLATFORM MATCHES "^rp2350")
    set(CHIP_NAME RP2350)
else()
    set(CHIP_NAME ${PICO_PLATFORM})
endif()
set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME ${SYS_NAME}-${CHIP_NAME})

pico_add_extra_outputs(${TARGET})

# Copy the finished .uf2 (same name) to the top-level project folder, so the
# RP2040 and RP2350 builds can sit side by side.
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:${TARGET}>/$<TARGET_FILE_BASE_NAME:${TARGET}>.uf2
            ${CMAKE_CURRENT_LIST_DIR}/
)
