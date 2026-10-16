/* dev_ipl.c - 12875A Processor Interconnect.
 *
 * Author: Thomas Dzubin.
 *
 * Behaviour follows hp2100_ipl.c of the SIMH HP 2100 simulator (Robert M. Supnik,
 * J. David Bryan, MIT licence). SIMH runs the two CPUs as two processes and
 * joins them through shared memory; here both are in one program and the
 * "cables" are the ipl_bus_t structures.
 *
 * A word is sent by OTA/OTB to a card and STC: the card puts the word on its
 * data lines and holds Device Command. The other CPU's poll sees it, takes the
 * word, sets the flag of the card at the other end of the cable and drops the
 * command.
 */

#include "dev_ipl.h"

static hp_word ipl_input(hp_card_t *card);
static void    ipl_output(hp_card_t *card, hp_word v);
static void    ipl_set_control(hp_card_t *card);
static void    ipl_reset(hp_card_t *card);

/* The cable pair for a card: each CPU's card k is joined to the other CPU's
 * card k ^ 1. */
static ipl_line_t *out_line(const dev_ipl_t *d, unsigned index)
{
    return d->is_iop ? &d->bus[index ^ 1u].to_sp : &d->bus[index].to_iop;
}

static ipl_line_t *in_line(const dev_ipl_t *d, unsigned index)
{
    return d->is_iop ? &d->bus[index ^ 1u].to_iop : &d->bus[index].to_sp;
}

void dev_ipl_init(dev_ipl_t *d, unsigned cpu, int is_iop, unsigned select_code,
                  ipl_bus_t *bus)
{
    unsigned i, previous = hp_selected();

    d->cpu = cpu;
    d->is_iop = is_iop;
    d->bus = bus;
    d->hold = 0;
    d->hold_until = 0;
    hp_select(cpu);
    for (i = 0; i < IPL_CARDS; i++) {
        hp_card_t *c = &d->card[i];

        d->ref[i].owner = d;
        d->ref[i].index = i;
        d->output_word[i] = d->input_word[i] = 0;
        c->ctx = &d->ref[i];
        c->input = ipl_input;
        c->output = ipl_output;
        c->set_control = ipl_set_control;
        c->clear_control = NULL;
        c->reset = ipl_reset;
        c->flag_set = NULL;
        c->flag_clear = NULL;
        c->edt = NULL;
        c->flag = 0;
        c->flag_buffer = 1;             /* power-on preset sets the flag buffer */
        c->control = 0;
        hp_card_install(c, select_code + i);
    }
    hp_select(previous);
}

static hp_word ipl_input(hp_card_t *card)
{
    const ipl_card_ref_t *r = card->ctx;

    return r->owner->input_word[r->index];
}

static void ipl_output(hp_card_t *card, hp_word v)
{
    const ipl_card_ref_t *r = card->ctx;

    r->owner->output_word[r->index] = v;
    out_line(r->owner, r->index)->data = v;
}

/* STC: hold Device Command and look for the answer right away. */
static void ipl_set_control(hp_card_t *card)
{
    const ipl_card_ref_t *r = card->ctx;

    out_line(r->owner, r->index)->command = 1;
    r->owner->hold = 0;
}

static void ipl_reset(hp_card_t *card)
{
    const ipl_card_ref_t *r = card->ctx;

    r->owner->output_word[r->index] = 0;
    out_line(r->owner, r->index)->data = 0;
}

void dev_ipl_poll(dev_ipl_t *d)
{
    unsigned previous = hp_selected();
    int index;

    hp_select(d->cpu);
    if (d->hold && (int32_t)(hp.ticks - d->hold_until) < 0) {
        hp_select(previous);
        return;
    }
    d->hold = 0;
    /* the outbound card is looked at first: a status reply must be picked up
     * before the next command from the other CPU is noticed */
    for (index = (int)IPL_OUTBOUND; index >= (int)IPL_INBOUND; index--) {
        ipl_line_t *in = in_line(d, (unsigned)index);

        if (!in->command)
            continue;
        in->command = 0;
        d->input_word[index] = in->data;
        out_line(d, (unsigned)index)->command = 0;
        hp_card_set_flag(&d->card[index]);
        if (index == (int)IPL_OUTBOUND) {
            d->hold = 1;
            d->hold_until = hp.ticks + IPL_HOLD_INSTRUCTIONS;
            break;
        }
    }
    hp_select(previous);
}
