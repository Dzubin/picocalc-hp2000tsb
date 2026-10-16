/* dev_ipl_const.h - constants for the 12875A Processor Interconnect. */

#ifndef DEV_IPL_CONST_H
#define DEV_IPL_CONST_H

#define IPL_CARDS               2u      /* inbound (lower select code) and outbound cards */
#define IPL_INBOUND             0u
#define IPL_OUTBOUND            1u

/* After a status word arrives on the outbound card, wait this many instructions
 * before looking for a command, so that the program can pick the status up first. */
#define IPL_HOLD_INSTRUCTIONS   500u

#define IPL_SLICE_INSTRUCTIONS  16u     /* instructions one CPU runs before the other */

/* The I/O Processor spends nearly all of its time in a loop waiting for work, and it
 * runs as many instructions as the System Processor, so half of the emulation time is
 * spent on it. It is given one slice for every this many slices of the System
 * Processor. (Its time base generator is calibrated on its own instruction count.) */
#define IPL_IOP_EVERY           4u

#endif
