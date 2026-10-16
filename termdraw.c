/* termdraw.c - draw one row of a terminal on the LCD (see termdraw.h).
 *
 * Author: Thomas Dzubin.
 */

#include "lcd.h"
#include "termdraw.h"

void termdraw_row(const char *text, unsigned row, uint16_t colour, int cursor_col)
{
    lcd_set_foreground(colour);
    lcd_putstr(0, (uint8_t)row, text);
    if (cursor_col >= 0) {
        lcd_set_reverse(true);
        lcd_putc((uint8_t)cursor_col, (uint8_t)row, (uint8_t)text[cursor_col]);
        lcd_set_reverse(false);
    }
}
