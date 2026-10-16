/* dev_tty_const.h - constants for the teleprinter card (dev_tty.c). */

#ifndef DEV_TTY_CONST_H
#define DEV_TTY_CONST_H

#define TTY_PRINT_INSTRUCTIONS  200u    /* time to print one character */
#define TTY_POLL_INSTRUCTIONS   5000u   /* keyboard poll period (about 10 ms) */

#define TTY_CTL_CONTROL         0100000u    /* output word is a control word */
#define TTY_CTL_INPUT           0040000u    /* input (not output) mode */
#define TTY_CTL_PRINT           0020000u    /* printer enabled */
#define TTY_CTL_PUNCH           0010000u    /* punch enabled */
#define TTY_STATUS_BUSY         0100000u    /* interface busy printing */
#define TTY_MARK                0377u       /* serial line idle (all ones) */
#define TTY_CHAR_MASK           0177u

#endif
