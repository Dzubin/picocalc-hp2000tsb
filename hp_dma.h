/* hp_dma.h - 12895A direct memory access (two channels), 2100 CPU.
 *
 * Author: Thomas Dzubin.
 *
 * Channel 1 uses select codes 6 (control word 1, start, completion flag) and 2
 * (control words 2 and 3: address and negative word count); channel 2 uses 7
 * and 3. A device on the channel is driven one word per cycle whenever its flag
 * is set, between two instructions.
 */

#ifndef HP_DMA_H
#define HP_DMA_H

#include "hp21xx.h"
#include "hp_dma_const.h"

typedef struct {
    hp_card_t card;                     /* select code 6 or 7 */
    hp_card_t config;                   /* select code 2 or 3 */
    uint16_t  cw1, cw2, cw3;
    uint16_t  xfer_sc;                  /* select code being driven, or inactive */
} hp_dma_channel_t;

typedef struct {
    hp_dma_channel_t channel[DMA_CHANNELS];
} hp_dma_t;

/* Install both channels in the card cage and hook them into the CPU. */
void hp_dma_init(hp_dma_t *d);

#endif
