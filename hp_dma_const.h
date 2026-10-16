/* hp_dma_const.h - constants for the DMA controller (hp_dma.c). */

#ifndef HP_DMA_CONST_H
#define HP_DMA_CONST_H

#define DMA_CHANNELS            2
#define DMA_SC_CHANNEL_1        6           /* control word 1, flag, control */
#define DMA_SC_CHANNEL_2        7
#define DMA_SC_CONFIG_1         2           /* control words 2 and 3 */
#define DMA_SC_CONFIG_2         3

#define DMA_CW1_STC             0100000u    /* assert STC during each cycle */
#define DMA_CW1_PACK            0040000u    /* byte packing (not on the 2100) */
#define DMA_CW1_CLC             0020000u    /* assert CLC at the end */
#define DMA_CW1_SC_MASK         0000077u
#define DMA_CW2_INPUT           0100000u    /* transfer into memory */
#define DMA_CW2_ADDRESS         0077777u
#define DMA_INACTIVE            0100000u    /* "transfer enable" clear */
#define DMA_LAST_COUNT          0177777u    /* word count -1: the last cycle */

#endif
