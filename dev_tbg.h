/* dev_tbg.h - 12539C time base generator interface (select code 17).
 *
 * Author: Thomas Dzubin.
 *
 * The real-time clock. Time-Shared BASIC programs it for a 100 millisecond
 * tick. Ticks are timed in instructions: instr_per_ms says how many
 * instructions pass in a millisecond of real time, and the program adjusts it
 * (dev_tbg_calibrate) from the host clock so the emulated clock keeps real
 * time however fast the host runs.
 */

#ifndef DEV_TBG_H
#define DEV_TBG_H

#include "hp21xx.h"

typedef struct {
    hp_card_t  card;
    hp_event_t event;
    uint8_t    rate;
    uint8_t    lost_tick;
    uint16_t   prescaler;               /* base ticks left before an interrupt */
    uint32_t   instr_per_ms;
} dev_tbg_t;

void dev_tbg_init(dev_tbg_t *t, unsigned select_code);

/* Tell the clock how fast the host is running: instructions executed and the
 * real milliseconds they took. */
void dev_tbg_calibrate(dev_tbg_t *t, uint32_t instructions, uint32_t milliseconds);

#endif
