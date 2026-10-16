/* dev_tty.c - 12531C buffered teleprinter interface.
 *
 * Author: Thomas Dzubin.
 *
 * Behaviour follows hp2100_tty.c of the SIMH HP 2100 simulator (MIT licence):
 * the same output and polling model, including the trick the real card plays
 * of shifting a key pressed during output into the data register so a driver
 * can see it.
 */

#include "dev_tty.h"
#include "dev_tty_const.h"
#include "console.h"

static hp_word tty_input(hp_card_t *card);
static void    tty_output(hp_card_t *card, hp_word v);
static void    tty_set_control(hp_card_t *card);
static void    tty_reset(hp_card_t *card);
static void    keyboard_service(void *arg);
static void    print_service(void *arg);
static void    print_character(dev_tty_t *t, uint8_t c);

void dev_tty_init(dev_tty_t *t, unsigned select_code)
{
    t->card.ctx = t;
    t->card.input = tty_input;
    t->card.output = tty_output;
    t->card.set_control = tty_set_control;
    t->card.clear_control = NULL;
    t->card.reset = tty_reset;
    t->card.flag_set = NULL;
    t->card.flag_clear = NULL;
    t->card.edt = NULL;
    t->card.flag = 0;
    t->card.flag_buffer = 1;            /* power-on preset */
    t->card.control = 0;
    t->data = 0;
    t->shift_in = TTY_MARK;
    t->cr_seen = 0;
    t->mode = TTY_CTL_INPUT;
    t->print_event.active = 0;
    hp_card_install(&t->card, select_code);
    hp_event_after(&t->key_event, TTY_POLL_INSTRUCTIONS, keyboard_service, t);
}

static hp_word tty_input(hp_card_t *card)
{
    dev_tty_t *t = card->ctx;
    hp_word v = t->data;

    if (!(t->mode & TTY_CTL_INPUT) && hp_event_active(&t->print_event))
        v |= TTY_STATUS_BUSY;
    return v;
}

static void tty_output(hp_card_t *card, hp_word v)
{
    dev_tty_t *t = card->ctx;

    if (v & TTY_CTL_CONTROL)
        t->mode = v;
    t->data = (uint8_t)(v & 0377);
}

/* STC in output mode prints the character in the data register. */
static void tty_set_control(hp_card_t *card)
{
    dev_tty_t *t = card->ctx;

    if (!(t->mode & TTY_CTL_INPUT) && !hp_event_active(&t->print_event))
        hp_event_after(&t->print_event, TTY_PRINT_INSTRUCTIONS, print_service, t);
}

static void tty_reset(hp_card_t *card)
{
    dev_tty_t *t = card->ctx;

    card->flag_buffer = 1;
    t->mode = TTY_CTL_INPUT;
    t->shift_in = TTY_MARK;
    t->cr_seen = 0;
}

/* The teleprinter is upper case only and 7-bit. */
static uint8_t to_teleprinter(int c)
{
    c &= TTY_CHAR_MASK;
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    return (uint8_t)c;
}

static void print_character(dev_tty_t *t, uint8_t c)
{
    if ((c & TTY_CHAR_MASK) == 0)
        return;                         /* a teleprinter prints nothing for NUL */
    if ((t->mode & TTY_CTL_PRINT) || (t->mode & TTY_CTL_PUNCH))
        console_write(CONSOLE_OPERATOR, to_teleprinter(c));
}

static void keyboard_service(void *arg)
{
    dev_tty_t *t = arg;
    int key;
    uint8_t input;

    hp_event_after(&t->key_event, TTY_POLL_INSTRUCTIONS, keyboard_service, t);

    key = console_read(CONSOLE_OPERATOR);
    if (key < 0)
        return;
    input = to_teleprinter(key);
    t->cr_seen = (input == '\r');

    if (t->mode & TTY_CTL_INPUT) {
        t->data = input;
        t->shift_in = TTY_MARK;
        hp_card_set_flag(&t->card);
        if (t->mode & (TTY_CTL_PRINT | TTY_CTL_PUNCH))
            print_character(t, input);      /* echo */
    } else {
        t->shift_in = input;                /* seen by the driver after output */
    }
}

static void print_service(void *arg)
{
    dev_tty_t *t = arg;

    print_character(t, t->data);
    t->data = t->shift_in;
    t->shift_in = TTY_MARK;
    hp_card_set_flag(&t->card);
}
