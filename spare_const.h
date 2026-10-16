/* spare_const.h - constants for the spare copies of the system files (spare.c). */

#ifndef SPARE_CONST_H
#define SPARE_CONST_H

/* Two spare slots, used in turn. A save writes into the slot that holds the older
 * spare (or an empty one), so the newest spare is never touched until the new one
 * has been written, read back and given its checksum file. No file is ever renamed
 * (the FAT32 driver's rename damages the directory), and the spare files have plain
 * 8.3 names, because the driver loses track of a long name that is deleted and then
 * created again. */
#define SPARE_SLOTS        2u
#define SPARE_FILE_FORMAT  "SP%uF%u.BIN"    /* slot (1 or 2), file number in the set */
#define SPARE_SUMS_FORMAT  "SP%uSUMS.TXT"   /* the slot's checksum file */
#define SPARE_SEQ_WORD     "SEQ"        /* first line of a checksum file: SEQ number */
#define SPARE_STATE_FILE   "SESSION.FLG" /* one byte: R (machine running) or C (ended cleanly) */
#define SPARE_STATE_RUNNING 'R'
#define SPARE_STATE_CLEAN   'C'

#define SPARE_NAME_MAX     96u          /* longest file name, with the prefix */
#define SPARE_CHUNK        4096u        /* bytes per transfer (one buffer, shared) */
#define SPARE_PATH_MAX     160u         /* longest path (the desktop path is long) */
#define SPARE_DETAIL_MAX   40u          /* longest detail text for the menu */
#define SPARE_FILES_MAX    4u           /* most files in one set */
#define SPARE_SUMS_MAX     512u         /* biggest checksum file */
#define SPARE_LABEL_MAX    48u          /* progress text, for example CHECK name */

#define SPARE_CRC_POLY     0xEDB88320u  /* CRC-32 (the zip and Ethernet one), reflected */

#endif
