/* terminal.c - a scrolling text terminal held in memory (see terminal.h).
 *
 * Author: Thomas Dzubin.
 */

#include <string.h>
#include "terminal.h"

#define ALL_ROWS (~0ull)

static void line_feed(terminal_t *t);

void terminal_init(terminal_t *t)
{
    unsigned r;

    for (r = 0; r < TERM_ROWS; r++) {
        memset(t->text[r], ' ', TERM_COLS);
        t->text[r][TERM_COLS] = 0;
    }
    t->row = t->col = 0;
    t->dirty = ALL_ROWS;
}

static void line_feed(terminal_t *t)
{
    unsigned r;

    if (t->row + 1 < TERM_ROWS) {
        t->row++;
        return;
    }
    for (r = 1; r < TERM_ROWS; r++)             /* scroll up one line */
        memcpy(t->text[r - 1], t->text[r], TERM_COLS);
    memset(t->text[TERM_ROWS - 1], ' ', TERM_COLS);
    t->dirty = ALL_ROWS;
}

void terminal_put(terminal_t *t, uint8_t c)
{
    switch (c) {
    case TERM_CR:
        t->col = 0;
        break;
    case TERM_LF:
        line_feed(t);
        break;
    case TERM_BS:
        if (t->col > 0)
            t->col--;
        break;
    case TERM_TAB_CHAR:
        t->col = (t->col / TERM_TAB + 1) * TERM_TAB;
        if (t->col >= TERM_COLS) {
            t->col = 0;
            line_feed(t);
        }
        break;
    default:
        if (c < 0x20 || c > 0x7E)
            break;                              /* other control characters */
        if (t->col >= TERM_COLS) {              /* wrap */
            t->col = 0;
            line_feed(t);
        }
        t->text[t->row][t->col++] = (char)c;
        t->dirty |= 1ull << t->row;
        break;
    }
}
