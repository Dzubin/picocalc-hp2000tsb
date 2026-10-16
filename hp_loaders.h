/* hp_loaders.h - put the bootstrap loaders of a two-CPU system into memory.
 *
 * Author: Thomas Dzubin.
 *
 * Both work on the CPU that is currently selected (hp_select()) and put the
 * 64-word loader in the top of its memory, as the front panel does.
 */

#ifndef HP_LOADERS_H
#define HP_LOADERS_H

#include "hp21xx.h"

/* The I/O Processor's binary loader, which reads an absolute tape from the
 * Processor Interconnect at interconnect_sc. Sets P to its first word. */
void hp_load_iop_loader(unsigned interconnect_sc);

/* The 7900 disc bootstrap for the disc whose data channel is at disc_sc. Sets P to
 * its first word; it reads the boot extension into memory, runs it, and the system
 * loads and stops with HLT 77. */
void hp_load_disc_bootstrap(unsigned disc_sc);

/* The 2883 disc bootstrap (BMDL-2883) for the 12565A whose data channel is at
 * disc_sc. It reads cylinder 0, head 0, sector 0 into memory and jumps to the boot
 * extension; the system loads and stops with HLT 77. Sets P to its first word. */
void hp_load_dq_bootstrap(unsigned disc_sc);

/* The magnetic tape loader (BMTL) for the 13183B whose data channel is at tape_sc.
 * Set S first (0 = read the file under the head). It reads an absolute file and
 * stops with HLT 77. Sets P to its first word. */
void hp_load_tape_loader(unsigned tape_sc);

#endif
