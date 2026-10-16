/*
 * platform_desktop.c - platform.h for the Windows / Linux (SDL2) build. The
 * clock and sleep come from the shim's core; there is no BOOTSEL or UF2 Loader
 * on a PC, so both just close the program.
 *
 * Author: Thomas Dzubin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "pico/stdlib.h"            /* the shim's stand-in header */

#include "platform.h"
#include "platform_desktop_const.h"

void plat_init(void)
{
    stdio_init_all();
}

uint32_t plat_now_us(void)
{
    return (uint32_t)to_us_since_boot(get_absolute_time());
}

uint32_t plat_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void plat_sleep_ms(uint32_t ms)
{
    sleep_ms(ms);
}

_Noreturn void plat_bootsel(void)
{
    exit(0);
}

_Noreturn void plat_exit_to_loader(void)
{
    exit(0);
}

/* The data folder sits next to the program, wherever the program is started
 * from; if the program's own folder cannot be found, it is taken relative to the
 * current folder. */
const char *plat_data_dir(void)
{
    static char path[PLAT_DATA_PATH_MAX];
    size_t n;

    if (path[0] != 0)
        return path;
#ifdef _WIN32
    n = GetModuleFileNameA(NULL, path, sizeof path);
    if (n == 0 || n >= sizeof path)
        n = 0;
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        n--;
#else
    {
        ssize_t got = readlink("/proc/self/exe", path, sizeof path - 1);

        n = got > 0 ? (size_t)got : 0;
        while (n > 0 && path[n - 1] != '/')
            n--;
    }
#endif
    if (n + strlen(PLAT_DATA_DIR) + 1 > sizeof path)
        n = 0;
    snprintf(path + n, sizeof path - n, "%s", PLAT_DATA_DIR);
    return path;
}

int plat_usb_read(void)
{
    return -1;                      /* no USB link on a PC build */
}

void plat_usb_write(uint8_t c)
{
    (void)c;
}
