/* dev_ipl.h - 12875A Processor Interconnect, joining the System Processor and
 * the I/O Processor of a two-CPU Time-Shared BASIC system (2000F, Access).
 *
 * Author: Thomas Dzubin.
 *
 * Each CPU has two 16-bit parallel interface cards at adjacent select codes: the
 * inbound card at the lower one and the outbound card at the higher one. The
 * cables join the inbound card of each CPU to the outbound card of the other.
 * Commands go from the System Processor on its outbound card, status comes back
 * on the same pair; the I/O Processor's own commands use the other pair.
 */

#ifndef DEV_IPL_H
#define DEV_IPL_H

#include "hp21xx.h"
#include "dev_ipl_const.h"

typedef struct {
    uint8_t  command;                   /* the sender holds Device Command */
    uint16_t data;                      /* the 16 data lines */
} ipl_line_t;

typedef struct {
    ipl_line_t to_iop;                  /* sent by the System Processor */
    ipl_line_t to_sp;                   /* sent by the I/O Processor */
} ipl_bus_t;

typedef struct dev_ipl dev_ipl_t;

typedef struct {
    dev_ipl_t *owner;
    unsigned   index;                   /* IPL_INBOUND or IPL_OUTBOUND */
} ipl_card_ref_t;

struct dev_ipl {
    hp_card_t      card[IPL_CARDS];
    ipl_card_ref_t ref[IPL_CARDS];
    uint16_t       output_word[IPL_CARDS];
    uint16_t       input_word[IPL_CARDS];
    unsigned       cpu;                 /* which CPU this pair is in */
    int            is_iop;
    ipl_bus_t     *bus;                 /* the two cables, shared by both ends */
    uint32_t       hold_until;          /* ticks before the next look for input */
    uint8_t        hold;
};

/* Install the pair in CPU 'cpu' at select_code (inbound) and the next (outbound).
 * 'bus' is the one shared pair of cables (an array of two ipl_bus_t). */
void dev_ipl_init(dev_ipl_t *d, unsigned cpu, int is_iop, unsigned select_code,
                  ipl_bus_t *bus);

/* Pass words across the cables. Called between the CPUs' time slices. */
void dev_ipl_poll(dev_ipl_t *d);

#endif
