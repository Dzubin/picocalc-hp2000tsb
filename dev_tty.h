/* dev_tty.h - 12531C buffered teleprinter interface (system console).
 *
 * Author: Thomas Dzubin.
 *
 * The operator's console: one card at select code 10 in the 2000E. Output is
 * printed with a delay per character; the keyboard is polled at a fixed
 * interval. Characters are upper-cased and cut to 7 bits, as the real
 * teleprinter did.
 */

#ifndef DEV_TTY_H
#define DEV_TTY_H

#include "hp21xx.h"

typedef struct {
    hp_card_t  card;
    hp_event_t key_event;
    hp_event_t print_event;
    uint8_t    data;                    /* input/output data register */
    uint8_t    shift_in;                /* characters shifted in during output */
    uint8_t    cr_seen;
    uint16_t   mode;                    /* last control word */
} dev_tty_t;

void dev_tty_init(dev_tty_t *t, unsigned select_code);

#endif
