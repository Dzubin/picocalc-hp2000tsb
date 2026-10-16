/* dev_ptr.c - 12597A paper tape reader interface.
 *
 * Author: Thomas Dzubin.
 *
 * Behaviour follows hp2100_pt.c of the SIMH HP 2100 simulator (MIT licence).
 */

#include "dev_ptr.h"
#include "dev_ptr_const.h"
#include "ptr_loader_const.h"

static hp_word ptr_input(hp_card_t *card);
static void    ptr_set_control(hp_card_t *card);
static void    ptr_clear_control(hp_card_t *card);
static void    ptr_reset(hp_card_t *card);
static void    ptr_frame_ready(void *arg);

void dev_ptr_init(dev_ptr_t *p, unsigned select_code)
{
    p->card.ctx = p;
    p->card.input = ptr_input;
    p->card.output = NULL;
    p->card.set_control = ptr_set_control;
    p->card.clear_control = ptr_clear_control;
    p->card.reset = ptr_reset;
    p->card.flag_set = NULL;
    p->card.flag_clear = NULL;
    p->card.edt = NULL;
    p->card.flag = 0;
    p->card.flag_buffer = 1;            /* power-on preset sets the flag buffer */
    p->card.control = 0;
    p->event.active = 0;
    p->data = 0;
    p->tape = NULL;
    p->length = 0;
    p->position = 0;
    p->trailer = 0;
    hp_card_install(&p->card, select_code);
}

void dev_ptr_load_tape(dev_ptr_t *p, const uint8_t *image, uint32_t length)
{
    p->tape = image;
    p->length = length;
    p->position = 0;
    p->trailer = 0;
}

static hp_word ptr_input(hp_card_t *card)
{
    return ((dev_ptr_t *)card->ctx)->data;
}

static void ptr_set_control(hp_card_t *card)
{
    dev_ptr_t *p = card->ctx;

    hp_event_after(&p->event, PTR_FRAME_INSTRUCTIONS, ptr_frame_ready, p);
}

static void ptr_clear_control(hp_card_t *card)
{
    (void)card;
}

static void ptr_reset(hp_card_t *card)
{
    dev_ptr_t *p = card->ctx;

    hp_event_cancel(&p->event);
}

/* A frame has been read: put it in the buffer and raise the flag. If the tape
 * is missing or has run out, nothing happens and the program waits forever. */
static void ptr_frame_ready(void *arg)
{
    dev_ptr_t *p = arg;

    if (p->tape == NULL)
        return;
    if (p->position < p->length) {
        p->data = p->tape[p->position++];
        p->trailer = 0;
    } else if (p->trailer < PTR_TRAILER_FRAMES) {
        p->trailer++;
        p->data = 0;
    } else {
        return;
    }
    hp_card_set_flag(&p->card);
}

void dev_ptr_boot(dev_ptr_t *p)
{
    static const hp_word loader[PTR_LOADER_WORDS] = PTR_LOADER_INIT;
    uint32_t base = (hp.mem_size - 1) & ~(uint32_t)(PTR_LOADER_WORDS - 1);
    unsigned i;
    hp_word w;

    for (i = 0; i < PTR_LOADER_WORDS; i++) {
        w = loader[i];
        /* an I/O instruction (not a halt) naming a plug-in card follows the reader */
        if ((w & IOG_TEST_MASK) == IOG_TEST_VALUE &&
            (w & HP_SC_MASK) >= PTR_LOADER_LISTING_SC &&
            (w & 0172700u) != 0102000u)
            w = (hp_word)((w + (p->card.select_code - PTR_LOADER_LISTING_SC)) &
                          HP_WORD_MASK);
        else if (i == PTR_LOADER_FWA_INDEX)
            w = (hp_word)((~base + 1) & HP_WORD_MASK);
        hp_write(base + i, w);
    }
    hp.p = (hp_word)(base + PTR_LOADER_START_INDEX);
}
