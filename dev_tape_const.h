/* dev_tape_const.h - constants for the 13183B tape interface (dev_tape.c). */

#ifndef DEV_TAPE_CONST_H
#define DEV_TAPE_CONST_H

#define TAPE_DRIVES             4u
#define TAPE_RECORD_MAX         16384u      /* longest record, in bytes */
#define TAPE_GAP_BYTES          (30u * 160u) /* a 3 inch gap at 1600 bpi */

/* Commands (command channel output word, low byte) */
#define TCMD_CLEAR              00110
#define TCMD_GAP                00015
#define TCMD_GAP_MARK           00215
#define TCMD_READ               00023
#define TCMD_WRITE              00031
#define TCMD_FORWARD_RECORD     00003
#define TCMD_BACKWARD_RECORD    00041
#define TCMD_FORWARD_FILE       00203
#define TCMD_BACKWARD_FILE      00241
#define TCMD_REWIND             00101
#define TCMD_REWIND_OFFLINE     00105
#define TCMD_WRITE_MARK         00211
#define TCMD_READ_FILE_FWD      00223       /* diagnostic */
#define TCMD_READ_RECORD_REV    00061       /* diagnostic, not supported */
#define TCMD_COMPLETION         00400       /* internal: completion state */
#define TCMD_SELECT_SHIFT       9
#define TCMD_SELECT_MASK        017u

/* Command flags */
#define TFLAG_MOTION            00001
#define TFLAG_OFFLINE           00004
#define TFLAG_WRITE             00010
#define TFLAG_REVERSE           00040
#define TFLAG_REWIND            00100
#define TFLAG_CHANGE_SELECT     00400
#define TCMD_SELECT_BITS \
    ((TCMD_SELECT_MASK << TCMD_SELECT_SHIFT) | TFLAG_CHANGE_SELECT)

/* Status word */
#define TSTA_PE                 0100000     /* 1600 bpi */
#define TSTA_SELECT_SHIFT       13
#define TSTA_ODD                0004000     /* odd number of bytes */
#define TSTA_REWINDING          0002000
#define TSTA_TRANSPORT_BUSY     0001000
#define TSTA_BUSY               0000400     /* controller busy */
#define TSTA_EOF                0000200
#define TSTA_BOT                0000100
#define TSTA_EOT                0000040
#define TSTA_TIMING             0000020
#define TSTA_REJECT             0000010
#define TSTA_WRITE_LOCKED       0000004
#define TSTA_PARITY             0000002
#define TSTA_LOCAL              0000001
/* bits that are worked out when the status is read */
#define TSTA_DYNAMIC \
    (TSTA_PE | (03u << TSTA_SELECT_SHIFT) | TSTA_TRANSPORT_BUSY | TSTA_BOT | \
     TSTA_EOT | TSTA_WRITE_LOCKED | TSTA_LOCAL)

/* Timing in instructions (the "fast" set of the SIMH simulator) */
#define TTIME_BOT_START         1u
#define TTIME_COMMAND           1000u
#define TTIME_GAP               1u
#define TTIME_INTERRECORD       1u
#define TTIME_REWIND_START      100u
#define TTIME_WORD              10u

/* CRC and LRC characters written after a record */
#define TAPE_CRC_POLY           0474u
#define TAPE_CRC_FINAL          0727u
#define TAPE_PARITY_EVEN        0400u       /* parity bit value used in the CRC */

#endif
