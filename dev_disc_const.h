/* dev_disc_const.h - constants for the 13210A / 7900 disc subsystem. */

#ifndef DEV_DISC_CONST_H
#define DEV_DISC_CONST_H

/* Geometry of the HP 7900 cartridge disc on a 13210A interface */
#define DISC_WORDS_PER_SECTOR   128u
#define DISC_SECTORS            24u         /* per surface */
#define DISC_SURFACES           4u          /* per cylinder */
#define DISC_CYLINDERS          203u
#define DISC_DRIVES             4u
#define DISC_IMAGE_BYTES \
    (DISC_CYLINDERS * DISC_SURFACES * DISC_SECTORS * DISC_WORDS_PER_SECTOR * 2u)

/* Timing in instructions (the "fast" times of the SIMH simulator) */
#define DISC_SEEK_PER_CYL       100u        /* seek time per cylinder */
#define DISC_COMMAND_TIME       100u        /* start of a data command */
#define DISC_WORD_TIME          5u          /* per word transferred */
#define DISC_CHANNEL_TIME       2u          /* data channel response */

/* Command word, bits 15-12 */
#define DCMD_SHIFT              12
#define DCMD_MASK               017u
#define DCMD_STATUS             000u
#define DCMD_WRITE              001u
#define DCMD_READ               002u
#define DCMD_SEEK               003u
#define DCMD_REFINE             005u
#define DCMD_CHECK              006u
#define DCMD_INIT               011u
#define DCMD_ADDRESS            013u
/* internal states of a command in progress */
#define DCMD_SEEK1              020u
#define DCMD_SEEK2              021u
#define DCMD_SEEK3              022u
#define DCMD_CHECK1             023u
#define DCMD_ADDRESS1           024u
#define DUNIT_MASK              03u         /* unit number, bits 1-0 */

/* Disc address words */
#define DADDR_CYL_MASK          0377u
#define DADDR_HEAD_SHIFT        8
#define DADDR_HEAD_MASK         03u
#define DADDR_SECTOR_MASK       037u
#define DADDR_CHECK_MASK        077u        /* sector count of Check Data */

/* Drive status word */
#define DSTA_ATTENTION          0100000u
#define DSTA_FIRST              0040000u
#define DSTA_OVERRUN            0020000u
#define DSTA_RW_UNSAFE          0010000u
#define DSTA_UNSAFE             0004000u
#define DSTA_PROTECTED          0002000u
#define DSTA_SEEK_INCOMPLETE    0001000u
#define DSTA_SEEK_CHECK         0000400u
#define DSTA_NOT_READY          0000100u
#define DSTA_END_OF_CYL         0000040u
#define DSTA_ADDRESS_ERROR      0000020u
#define DSTA_FLAGGED            0000010u
#define DSTA_BUSY               0000004u
#define DSTA_DATA_ERROR         0000002u
#define DSTA_ANY_ERROR          0000001u
/* status bits that set "any error" (13210A) */
#define DSTA_ERROR_SET \
    (DSTA_ATTENTION | DSTA_FIRST | DSTA_OVERRUN | DSTA_RW_UNSAFE | \
     DSTA_UNSAFE | DSTA_SEEK_INCOMPLETE | DSTA_SEEK_CHECK | DSTA_NOT_READY | \
     DSTA_END_OF_CYL | DSTA_ADDRESS_ERROR | DSTA_FLAGGED | DSTA_BUSY | \
     DSTA_DATA_ERROR)
/* status bits that are always zero on the 13210A */
#define DSTA_ZERO_13210         (DSTA_ATTENTION | DSTA_RW_UNSAFE | DSTA_SEEK_INCOMPLETE)
#define DSTA_UNLOADED           (DSTA_NOT_READY | DSTA_BUSY)
/* cleared by every command except Check Status */
#define DSTA_CLEARED_BY_COMMAND \
    (DSTA_OVERRUN | DSTA_RW_UNSAFE | DSTA_UNSAFE | DSTA_END_OF_CYL | \
     DSTA_ADDRESS_ERROR | DSTA_FLAGGED | DSTA_DATA_ERROR)
/* cleared by a control reset */
#define DSTA_CLEARED_BY_RESET   (DSTA_FIRST | DSTA_CLEARED_BY_COMMAND)

#endif
