/* lcd.h (desktop override) - a wider screen for the Windows and Linux builds.
 *
 * Author: Thomas Dzubin.
 *
 * The PicoCalc's LCD is 320 pixels wide, 40 columns of the 8 x 10 pixel font.
 * HP terminals show 80 columns, so the desktop builds use a 640 x 320 screen.
 * This header comes first on the desktop include path; it takes the vendored
 * lcd.h and changes only WIDTH. The shim and the programs size everything from
 * WIDTH and HEIGHT, so nothing else changes. The vendored file is not touched.
 */
#ifndef DESKTOP_WIDE_LCD_H
#define DESKTOP_WIDE_LCD_H

#include_next "lcd.h"

#undef WIDTH
#define WIDTH           (640)           /* pixels across the desktop screen */

#endif
