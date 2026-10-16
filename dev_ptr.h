/* dev_ptr.h - 12597A paper tape reader interface (select code 13 in the 2000E).
 *
 * Author: Thomas Dzubin.
 *
 * Reads the bytes of a paper tape image held in memory. STC starts reading the
 * next frame; when the frame is in the buffer the flag is set. After the end of
 * the image the reader returns blank frames for a while (the trailer) and then
 * stops responding, like a tape that has run out.
 */

#ifndef DEV_PTR_H
#define DEV_PTR_H

#include "hp21xx.h"

typedef struct {
    hp_card_t      card;
    hp_event_t     event;
    uint8_t        data;                /* the frame read */
    const uint8_t *tape;                /* tape image, or NULL if no tape */
    uint32_t       length;
    uint32_t       position;
    uint32_t       trailer;             /* blank frames returned after the end */
} dev_ptr_t;

void dev_ptr_init(dev_ptr_t *p, unsigned select_code);
void dev_ptr_load_tape(dev_ptr_t *p, const uint8_t *image, uint32_t length);

/* Put the basic binary loader in the top 64 words of memory, set for this
 * reader's select code, and point P at it. */
void dev_ptr_boot(dev_ptr_t *p);

#endif
