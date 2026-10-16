/* termdraw.h - draw one row of a terminal on the LCD.
 *
 * Author: Thomas Dzubin.
 *
 * A row is TERM_COLS characters of the LCD driver's 8 x 10 pixel font: 40 on the
 * PicoCalc, 80 on the desktop builds, whose screen is 640 pixels wide.
 */

#ifndef TERMDRAW_H
#define TERMDRAW_H

#include <stdint.h>

/* Draw text (TERM_COLS characters) as text row 'row' in the given colour. If
 * cursor_col is not negative that cell is drawn reversed. */
void termdraw_row(const char *text, unsigned row, uint16_t colour, int cursor_col);

#endif
