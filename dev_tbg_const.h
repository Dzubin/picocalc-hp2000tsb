/* dev_tbg_const.h - constants for the time base generator (dev_tbg.c). */

#ifndef DEV_TBG_CONST_H
#define DEV_TBG_CONST_H

/* Instructions per millisecond at the real 2100's speed (0.634 microseconds
 * per instruction, the average used by the SIMH HP 2100 simulator). The
 * program calibrates this against the host clock while it runs. */
#define TBG_DEFAULT_INSTR_PER_MS    1577u

#define TBG_RATE_MASK               07u
#define TBG_STATUS_LOST_TICK        0000020u

/* Tick period in milliseconds for each rate setting (0 means 100 microseconds,
 * handled as one tenth of a millisecond), and how many 100 ms base ticks make
 * one interrupt for the slower rates. */
#define TBG_RATES                   8

#endif
