/* hostfile_const.h - constants for typing a file into a terminal and capturing a terminal's
 * output into a file (hostfile.c). */

#ifndef HOSTFILE_CONST_H
#define HOSTFILE_CONST_H

#define HOSTFILE_LINE_MAX       119u        /* longest line TSB takes; a longer one is cut */
#define HOSTFILE_CHUNK          256u        /* bytes of the file read at a time */
#define HOSTFILE_NAME_MAX       64u         /* longest file name */

/* Time is the System Processor's instruction count (hp_cpus[HP_SP].ticks), so a fast PC
 * is not slowed down to the speed of a PicoCalc. */
#define HOSTFILE_LINE_GAP       300000u     /* instructions TSB gets to finish a line (0.6 s at 0.5 MIPS) */
#define HOSTFILE_ECHO_WAIT      8000000u    /* longest wait for the line feed that answers a line */

#define HOSTFILE_CAPTURE_BUF    512u        /* output is written to the card in pieces this big */

#define HOSTFILE_CR             13
#define HOSTFILE_ERROR_TEXT     "???"       /* what TSB prints for a line it does not accept */

#endif
