/* console.h - the terminals as the emulated machine sees them.
 *
 * Author: Thomas Dzubin.
 *
 * The device cards talk to a terminal only through these two functions. The
 * program provides them: on the PicoCalc and in the desktop build they draw on
 * the screen and read the keyboard; the host test programs print to stdout and
 * read a script. Terminal 0 is the operator's console (the teleprinter card),
 * terminal 1 and up are the time-sharing user terminals (multiplexer lines).
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

#define CONSOLE_OPERATOR        0
#define CONSOLE_USER            1

/* console_read returns this for the BREAK key (the character codes are 0 to 255). */
#define CONSOLE_BREAK           0x100

/* Send one character (7-bit ASCII, already upper-cased by the card) to a terminal. */
void console_write(unsigned terminal, uint8_t character);

/* The next key typed on a terminal, or -1 if none is waiting. */
int  console_read(unsigned terminal);

#endif
