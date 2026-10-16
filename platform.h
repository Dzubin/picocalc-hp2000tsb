/*
 * platform.h - the few things the programs need from the machine and the
 * operating system besides the LCD, audio and keyboard drivers: starting the
 * runtime, a millisecond clock, sleeping, and the two ways of leaving the
 * program. Each platform supplies its own implementation:
 *
 *     PicoCalc firmware   platform_pico.c
 *     Windows / Linux     desktop/platform_desktop.c
 *
 * Author: Thomas Dzubin
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

void     plat_init(void);               /* start the runtime, once at startup  */
uint32_t plat_now_us(void);             /* microseconds since start (wraps)    */
uint32_t plat_now_ms(void);             /* milliseconds since start (wraps)    */
void     plat_sleep_ms(uint32_t ms);    /* wait; the platform keeps running    */

/* Leave the program. Neither one returns. On the PicoCalc, plat_bootsel()
 * opens the USB drive (BOOTSEL) mode and plat_exit_to_loader() goes back to
 * the UF2 Loader menu; on a PC both just exit. */
_Noreturn void plat_bootsel(void);
_Noreturn void plat_exit_to_loader(void);

/* Where the system images live: the folder on the SD card, or a folder next to
 * the program on a PC (with the trailing slash). */
const char *plat_data_dir(void);

/* The USB serial link to a terminal program on another computer (the PicoCalc
 * only; a PC build has none). plat_usb_read() returns a character or -1 if none is
 * waiting; plat_usb_write() drops the character if nobody is connected. */
int      plat_usb_read(void);
void     plat_usb_write(uint8_t c);

#endif /* PLATFORM_H */
