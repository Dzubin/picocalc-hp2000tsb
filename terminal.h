/* terminal.h - a scrolling text terminal held in memory.
 *
 * Author: Thomas Dzubin.
 *
 * Portable: it only keeps the characters, the cursor and which rows changed. The
 * program draws the rows on the screen. It behaves like a printing terminal that
 * scrolls: carriage return, line feed, backspace and tab are acted on, other
 * control characters are ignored, and a line that runs off the right edge wraps.
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include "terminal_const.h"

typedef struct {
    char     text[TERM_ROWS][TERM_COLS + 1];
    unsigned row, col;                  /* cursor */
    uint64_t dirty;                     /* bit n set: row n changed since drawn */
} terminal_t;

void terminal_init(terminal_t *t);
void terminal_put(terminal_t *t, uint8_t c);

#endif
