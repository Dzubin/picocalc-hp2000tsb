/* hp_pair.h - run two CPUs (System Processor and I/O Processor) side by side.
 *
 * Author: Thomas Dzubin.
 *
 * SIMH needs "execution interlocking" to keep its two processes close in step.
 * Here both CPUs are run by one thread in short alternating slices, so they are
 * always within a few instructions of each other and the result is repeatable.
 */

#ifndef HP_PAIR_H
#define HP_PAIR_H

#include "hp21xx.h"
#include "dev_ipl.h"

#define HP_SP   0u
#define HP_IOP  1u

/* Set up the pair run: the two interconnect ends to poll. */
void hp_pair_init(dev_ipl_t *sp_ipl, dev_ipl_t *iop_ipl);

/* Make a stopped CPU runnable again (after the program started it). */
void hp_pair_resume(unsigned cpu);

/* Keep a CPU from running until hp_pair_resume() (before it has a program). */
void hp_pair_hold(unsigned cpu);

/* Run each running CPU for about 'instructions', in alternating slices. Returns 0
 * when that many were run, or 1 + the CPU number of the first one to stop (HLT or
 * error); the stop status is in hp_pair_status(cpu). A stopped CPU stays stopped
 * until hp_pair_resume(). The System Processor is selected on return. */
int  hp_pair_run(uint32_t instructions);
int  hp_pair_status(unsigned cpu);
int  hp_pair_stopped(unsigned cpu);

#endif
