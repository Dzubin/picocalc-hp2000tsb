/* dev_dq_const.h - constants for the 12565A disc interface with 2883 drives
 * (dev_dq.c).
 *
 * Author: Thomas Dzubin.
 */

#ifndef DEV_DQ_CONST_H
#define DEV_DQ_CONST_H

/* Geometry of the HP 2883 disc drive (about 24 MB) */
#define DQ_WORDS_PER_SECTOR     128u
#define DQ_SECTORS              23u         /* per track */
#define DQ_SURFACES             20u         /* tracks per cylinder */
#define DQ_CYLINDERS            203u
#define DQ_DRIVES               2u
#define DQ_IMAGE_BYTES \
    (DQ_SURFACES * DQ_CYLINDERS * DQ_SECTORS * DQ_WORDS_PER_SECTOR * 2u)

/* Timing in instructions (the "fast" times of the SIMH simulator) */
#define DQ_SEEK_PER_CYL         100u        /* seek time per cylinder */
#define DQ_COMMAND_TIME         100u        /* start of a data command */
#define DQ_WORD_TIME            3u          /* per word transferred */
#define DQ_CHANNEL_TIME         2u          /* data channel response */

/* Command word: function in bits 15-12, drive in bit 0 */
#define DQ_CMD_SHIFT            12
#define DQ_CMD_MASK             017u
#define DQ_DRIVE_MASK           01u
#define DQ_FNC_STATUS           001u
#define DQ_FNC_RECALIBRATE      002u
#define DQ_FNC_SEEK             003u
#define DQ_FNC_READ             004u
#define DQ_FNC_WRITE            005u
#define DQ_FNC_READ_ADDRESS     006u
#define DQ_FNC_WRITE_ADDRESS    007u
#define DQ_FNC_CHECK            010u
#define DQ_FNC_LOAD_ADDRESS     013u
#define DQ_FNC_ADDRESS_SKIP     014u
/* internal states of a command in progress */
#define DQ_FNC_SEEK1            020u
#define DQ_FNC_SEEK2            021u
#define DQ_FNC_SEEK3            022u
#define DQ_FNC_CHECK1           023u
#define DQ_FNC_LOAD_ADDRESS1    024u

/* Disc address words */
#define DQ_CYL_MASK             0377u
#define DQ_HEAD_SHIFT           8
#define DQ_HEAD_MASK            037u
#define DQ_SECTOR_MASK          037u
#define DQ_CHECK_MASK           0777u       /* sector count of Check */

/* Drive status word */
#define DQ_STA_DRIVE_ID         0000200u    /* drive 1 */
#define DQ_STA_NOT_READY        0000100u
#define DQ_STA_END_OF_CYL       0000040u
#define DQ_STA_ADDRESS_ERROR    0000020u
#define DQ_STA_FLAGGED          0000010u
#define DQ_STA_BUSY             0000004u    /* seeking */
#define DQ_STA_DATA_ERROR       0000002u
#define DQ_STA_ERROR            0000001u    /* any error */
#define DQ_STA_ANY_ERROR \
    (DQ_STA_NOT_READY | DQ_STA_END_OF_CYL | DQ_STA_ADDRESS_ERROR | \
     DQ_STA_FLAGGED | DQ_STA_DATA_ERROR)

/* The 12992A style bootstrap (BMDL-2883) of SIMH, 64 words at the top of memory.
 * The tape half (first 050 words) reads absolute paper tape and is not used;
 * the disc half starts at index 050, reads cylinder 0, head 0, sector 0 into
 * 2011 octal and jumps to 2055. The listing uses select code 10 for the data
 * channel and 11 for the command channel. */
#define DQ_LOADER_WORDS         64u
#define DQ_LOADER_START_INDEX   050u
#define DQ_LOADER_DMA_INDEX     076u        /* 120000 + the data channel select code */
#define DQ_LOADER_LISTING_SC    010u
#define DQ_LOADER_INIT \
    { 0002701, 0063722, 0002307, 0102077, 0017735, 0007307, 0027702, 0077733, \
      0017735, 0017735, 0074000, 0077734, 0067734, 0047777, 0002040, 0102055, \
      0017735, 0040001, 0177734, 0037734, 0000040, 0037733, 0027714, 0017735, \
      0054000, 0027701, 0102011, 0000000, 0000000, 0000000, 0006600, 0103710, \
      0102310, 0027740, 0106410, 0002041, 0127735, 0005767, 0027737, 0177600, \
      0063775, 0102611, 0103711, 0102311, 0027753, 0067776, 0106606, 0067732, \
      0106602, 0102702, 0067747, 0106602, 0001000, 0106711, 0102611, 0103710, \
      0103706, 0103711, 0102311, 0027772, 0127717, 0020000, 0120010, 0100100 }

#endif
