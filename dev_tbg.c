/* dev_tbg.c - 12539C time base generator interface.
 *
 * Author: Thomas Dzubin.
 *
 * Behaviour follows hp2100_tbg.c of the SIMH HP 2100 simulator (MIT licence).
 * Rates 0 to 3 tick every 100 microseconds to 100 milliseconds; the slower rates
 * count 100 millisecond base ticks (the jumper positions of the real card are
 * not modelled: seconds, lost-tick bit 4 only).
 */

#include "dev_tbg.h"
#include "dev_tbg_const.h"

static hp_word tbg_input(hp_card_t *card);
static void    tbg_output(hp_card_t *card, hp_word v);
static void    tbg_set_control(hp_card_t *card);
static void    tbg_clear_control(hp_card_t *card);
static void    tbg_reset(hp_card_t *card);
static void    tbg_service(void *arg);
static uint32_t base_period(const dev_tbg_t *t);
static uint32_t prescale_count(unsigned rate);

void dev_tbg_init(dev_tbg_t *t, unsigned select_code)
{
    t->card.ctx = t;
    t->card.input = tbg_input;
    t->card.output = tbg_output;
    t->card.set_control = tbg_set_control;
    t->card.clear_control = tbg_clear_control;
    t->card.reset = tbg_reset;
    t->card.flag_set = NULL;
    t->card.flag_clear = NULL;
    t->card.edt = NULL;
    t->card.flag = 0;
    t->card.flag_buffer = 1;            /* power-on preset */
    t->card.control = 0;
    t->event.active = 0;
    t->rate = 0;
    t->lost_tick = 0;
    t->prescaler = 0;
    t->instr_per_ms = TBG_DEFAULT_INSTR_PER_MS;
    hp_card_install(&t->card, select_code);
}

void dev_tbg_calibrate(dev_tbg_t *t, uint32_t instructions, uint32_t milliseconds)
{
    uint32_t ipm;

    if (milliseconds == 0)
        return;
    ipm = instructions / milliseconds;
    if (ipm == 0)
        ipm = 1;
    t->instr_per_ms = ipm;
}

/* Instructions between service calls for the selected rate. Rates above 100 ms
 * run on a 100 ms base tick and a prescaler. */
static uint32_t base_period(const dev_tbg_t *t)
{
    switch (t->rate & TBG_RATE_MASK) {
    case 0:
        return t->instr_per_ms / 10 ? t->instr_per_ms / 10 : 1;
    case 1:
        return t->instr_per_ms;
    case 2:
        return t->instr_per_ms * 10;
    default:
        return t->instr_per_ms * 100;
    }
}

static uint32_t prescale_count(unsigned rate)
{
    switch (rate & TBG_RATE_MASK) {
    case 4:
        return 10;
    case 5:
        return 100;
    case 6:
        return 1000;
    case 7:
        return 10000;
    default:
        return 1;
    }
}

static hp_word tbg_input(hp_card_t *card)
{
    dev_tbg_t *t = card->ctx;

    return t->lost_tick ? TBG_STATUS_LOST_TICK : 0;
}

static void tbg_output(hp_card_t *card, hp_word v)
{
    dev_tbg_t *t = card->ctx;

    t->rate = (uint8_t)(v & TBG_RATE_MASK);
    card->control = 0;
    hp_event_cancel(&t->event);
}

static void tbg_set_control(hp_card_t *card)
{
    dev_tbg_t *t = card->ctx;

    if (!hp_event_active(&t->event)) {
        hp_event_after(&t->event, base_period(t), tbg_service, t);
        t->prescaler = (uint16_t)prescale_count(t->rate);
    }
    t->lost_tick = 0;
}

static void tbg_clear_control(hp_card_t *card)
{
    dev_tbg_t *t = card->ctx;

    hp_event_cancel(&t->event);
}

static void tbg_reset(hp_card_t *card)
{
    dev_tbg_t *t = card->ctx;

    card->control = 0;
    hp_event_cancel(&t->event);
}

static void tbg_service(void *arg)
{
    dev_tbg_t *t = arg;

    if (!t->card.control)
        return;
    if (t->prescaler > 0)
        t->prescaler--;
    if (t->prescaler == 0) {
        if (t->card.flag) {
            t->lost_tick = 1;           /* the last tick was not handled */
        } else {
            hp_card_set_flag(&t->card);
        }
        t->prescaler = (uint16_t)prescale_count(t->rate);
    }
    hp_event_after(&t->event, base_period(t), tbg_service, t);
}
