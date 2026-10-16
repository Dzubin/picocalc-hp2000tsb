/* tape_image.h - a magnetic tape held in an image file (SIMH tape format).
 *
 * Author: Thomas Dzubin.
 *
 * The image is a string of records. A record is a 32-bit little-endian length,
 * the data (padded to an even number of bytes), and the length again. A length
 * of zero is a tape mark, 0xFFFFFFFE is a piece of erased gap and 0xFFFFFFFF
 * marks the end of the medium; bit 31 of a length flags a record read in error.
 * This is the format of the SIMH simulator (Robert M. Supnik, MIT licence), so
 * its tape images can be used directly. Only the standard format is handled.
 */

#ifndef TAPE_IMAGE_H
#define TAPE_IMAGE_H

#include <stdint.h>
#include "storage.h"

typedef enum {
    TAPE_OK = 0,
    TAPE_MARK,                          /* a tape mark */
    TAPE_EOM,                           /* end of the medium */
    TAPE_BOT,                           /* backed into the beginning of tape */
    TAPE_TOO_LONG,                      /* the record is longer than the buffer */
    TAPE_RECORD_ERROR,                  /* the record was read in error */
    TAPE_WRITE_PROTECTED,
    TAPE_IO_ERROR,
    TAPE_NOT_MOUNTED
} tape_status_t;

typedef struct {
    hp_store_t *store;                  /* NULL if no tape is mounted */
    uint32_t    position;               /* byte offset into the image */
    uint8_t     write_protect;
    uint8_t     position_not_updated;   /* the last operation did not move the tape */
} tape_image_t;

void tape_mount(tape_image_t *t, hp_store_t *store, int write_protect);
void tape_unmount(tape_image_t *t);
void tape_rewind(tape_image_t *t);
int  tape_at_bot(const tape_image_t *t);

/* Read the next record forward into buffer (at most max bytes). On TAPE_OK, *length
 * is the record length; on TAPE_MARK it is 0. */
tape_status_t tape_read_forward(tape_image_t *t, uint8_t *buffer, uint32_t *length,
                                uint32_t max);

/* Write a record at the present position, a tape mark, or an erase gap of the
 * given number of bytes (as erased tape markers). Nothing after the write is
 * removed. */
tape_status_t tape_write_record(tape_image_t *t, const uint8_t *data, uint32_t length);
/* A record written in error (a partial record when a write was cut short). */
tape_status_t tape_write_bad_record(tape_image_t *t, const uint8_t *data, uint32_t length);
tape_status_t tape_write_mark(tape_image_t *t);
tape_status_t tape_write_gap(tape_image_t *t, uint32_t bytes);

/* Skip a record (forward or backward); *length is the record length skipped. */
tape_status_t tape_space_forward(tape_image_t *t, uint32_t *length);
tape_status_t tape_space_reverse(tape_image_t *t, uint32_t *length);

#endif
