/* hp_dma.c - 12895A direct memory access, two channels.
 *
 * Author: Thomas Dzubin.
 *
 * Behaviour follows hp2100_dma.c of the SIMH HP 2100 simulator (Robert M. Supnik,
 * J. David Bryan, MIT licence): one word is moved per DMA cycle, cycles
 * interleaved with instructions (a cycle takes no instruction time), channel 1
 * having priority. Completion sets the channel flag, which also stops the
 * transfer; setting the flag by STF aborts it.
 */

#include "hp_dma.h"

static void dma_eval(void);
static void dma_cycle(void);
static void one_cycle(unsigned ch);
static void channel_set_control(hp_card_t *card);
static void channel_reset(hp_card_t *card);
static void channel_flag_set(hp_card_t *card);
static void channel_output(hp_card_t *card, hp_word v);
static hp_word config_input(hp_card_t *card);
static void config_output(hp_card_t *card, hp_word v);

void hp_dma_init(hp_dma_t *d)
{
    unsigned ch;
    static const unsigned main_sc[DMA_CHANNELS] = { DMA_SC_CHANNEL_1, DMA_SC_CHANNEL_2 };
    static const unsigned config_sc[DMA_CHANNELS] = { DMA_SC_CONFIG_1, DMA_SC_CONFIG_2 };

    hp.dma_ctx = d;
    for (ch = 0; ch < DMA_CHANNELS; ch++) {
        hp_dma_channel_t *c = &d->channel[ch];

        c->cw1 = c->cw2 = c->cw3 = 0;
        c->xfer_sc = DMA_INACTIVE;

        c->card.ctx = c;
        c->card.flag = 0;
        c->card.flag_buffer = 1;        /* power-on preset */
        c->card.control = 0;
        c->card.input = NULL;
        c->card.output = channel_output;
        c->card.set_control = channel_set_control;
        c->card.clear_control = NULL;
        c->card.reset = channel_reset;
        c->card.flag_set = channel_flag_set;
        c->card.flag_clear = NULL;
        c->card.edt = NULL;

        c->config.ctx = c;
        c->config.flag = c->config.flag_buffer = c->config.control = 0;
        c->config.input = config_input;
        c->config.output = config_output;
        c->config.set_control = NULL;
        c->config.clear_control = NULL;
        c->config.reset = NULL;
        c->config.flag_set = NULL;
        c->config.flag_clear = NULL;
        c->config.edt = NULL;

        hp_card_install(&c->card, main_sc[ch]);
        hp_card_install(&c->config, config_sc[ch]);
    }
    hp.dma_eval = dma_eval;
    hp.dma_cycle = dma_cycle;
}

/* Control word 1: the device to drive and the options. */
static void channel_output(hp_card_t *card, hp_word v)
{
    hp_dma_channel_t *c = card->ctx;

    c->cw1 = (uint16_t)(v & ~DMA_CW1_PACK);     /* no byte packing on the 2100 */
}

/* STC starts the transfer, unless the flag buffer is set (an abort pending),
 * but a CLF in the same instruction clears it first. */
static void channel_set_control(hp_card_t *card)
{
    hp_dma_channel_t *c = card->ctx;

    if (!card->flag_buffer || hp.iog_clf)
        c->xfer_sc = (uint16_t)(c->cw1 & DMA_CW1_SC_MASK);
}

static void channel_reset(hp_card_t *card)
{
    hp_dma_channel_t *c = card->ctx;

    card->control = 0;
    c->config.control = 0;              /* control word select flip-flop */
    c->xfer_sc = DMA_INACTIVE;
}

/* Setting the flag ends (or aborts) the transfer. */
static void channel_flag_set(hp_card_t *card)
{
    hp_dma_channel_t *c = card->ctx;

    c->xfer_sc = DMA_INACTIVE;
}

/* Control words 2 and 3: with the select flip-flop (the config card's control)
 * clear a write sets the address, set it writes the word count. */
static hp_word config_input(hp_card_t *card)
{
    return ((hp_dma_channel_t *)card->ctx)->cw3;
}

static void config_output(hp_card_t *card, hp_word v)
{
    hp_dma_channel_t *c = card->ctx;

    if (card->control)
        c->cw3 = v;
    else
        c->cw2 = v;
}

/* A channel asks for a cycle when the device it drives has its flag set. */
static void dma_eval(void)
{
    unsigned ch;
    uint8_t req = 0;

    for (ch = 0; ch < DMA_CHANNELS; ch++) {
        hp_dma_t *dma = hp.dma_ctx;
        hp_dma_channel_t *c = &dma->channel[ch];
        hp_card_t *dev;

        if (c->xfer_sc == DMA_INACTIVE)
            continue;
        dev = hp.card[c->xfer_sc & 077];
        if (dev != NULL && dev->flag)
            req |= (uint8_t)(1u << ch);
    }
    hp.dma_request = req;
}

static void dma_cycle(void)
{
    if (hp.dma_request & 1u)
        one_cycle(0);
    if ((hp.dma_request & 3u) == 2u)
        one_cycle(1);
}

/* One word between memory and the device. Input cycles: IOI, CLF (not on the
 * last), then EDT; output cycles: IOO, CLF, with STC and CLC as programmed. */
static void one_cycle(unsigned ch)
{
    hp_dma_t *dma = hp.dma_ctx;
    hp_dma_channel_t *c = &dma->channel[ch];
    hp_card_t *dev = hp.card[c->cw1 & DMA_CW1_SC_MASK];
    int input = (c->cw2 & DMA_CW2_INPUT) != 0;
    uint32_t address = c->cw2 & DMA_CW2_ADDRESS;
    int last = c->cw3 == DMA_LAST_COUNT;
    hp_word data;

    hp.dma_request &= (uint8_t)~(1u << ch);
    if (dev == NULL)
        return;

    if (input) {
        data = dev->input != NULL ? dev->input(dev) : 0;
        hp_write_core(address, data);
    } else {
        data = hp_read_core(address);
        if (dev->output != NULL)
            dev->output(dev, data);
    }
    if ((c->cw1 & DMA_CW1_STC) && (!input || !last)) {
        dev->control = 1;
        if (dev->set_control != NULL)
            dev->set_control(dev);
    }
    if (last && (c->cw1 & DMA_CW1_CLC)) {
        dev->control = 0;
        if (dev->clear_control != NULL)
            dev->clear_control(dev);
    }
    if (!(input && last)) {             /* CLF on every cycle but the last input */
        dev->flag_buffer = 0;
        dev->flag = 0;
        if (dev->flag_clear != NULL)
            dev->flag_clear(dev);
    }

    if (last && dev->edt != NULL)
        dev->edt(dev);                  /* end of the data transfer */

    c->cw2 = (uint16_t)((c->cw2 & DMA_CW2_INPUT) | ((c->cw2 + 1) & DMA_CW2_ADDRESS));
    c->cw3 = (uint16_t)((c->cw3 + 1) & HP_WORD_MASK);
    if (c->cw3 == 0)
        hp_card_set_flag(&c->card);     /* transfer complete */
    else
        hp_irq_eval();
}
