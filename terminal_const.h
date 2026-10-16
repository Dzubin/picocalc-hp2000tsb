/* terminal_const.h - constants for the text terminals (terminal.c). */

#ifndef TERMINAL_CONST_H
#define TERMINAL_CONST_H

#ifdef WIDE_TERMINAL
#define TERM_COLS               80u     /* characters across the desktop's 640 pixel screen */
#else
#define TERM_COLS               40u     /* characters across the PicoCalc's 320 pixel LCD (8 pixel cells) */
#endif
#define TERM_ROWS               31u     /* text rows (the last LCD row is the status line) */
#define TERM_TAB                8u

#define TERM_BS                 0x08
#define TERM_TAB_CHAR           0x09
#define TERM_LF                 0x0A
#define TERM_CR                 0x0D

#endif
