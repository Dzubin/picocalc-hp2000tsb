/* tape_image.c - a magnetic tape held in an image file (SIMH tape format).
 *
 * Author: Thomas Dzubin.
 *
 * Follows the standard-format handling of sim_tape.c in the SIMH simulator
 * (Robert M. Supnik, J. David Bryan, MIT licence): gaps are skipped when reading,
 * a tape mark has length zero, writing does not truncate, and the position is not
 * updated when a read hits the end of the medium. Tape density and runaway
 * detection are not modelled.
 */

#include <string.h>
#include "tape_image.h"
#include "tape_image_const.h"

static int read_marker(tape_image_t *t, uint32_t at, uint32_t *marker);
static int write_marker(tape_image_t *t, uint32_t at, uint32_t marker);

void tape_mount(tape_image_t *t, hp_store_t *store, int write_protect)
{
    t->store = store;
    t->position = 0;
    t->write_protect = (uint8_t)(write_protect != 0);
    t->position_not_updated = 0;
}

void tape_unmount(tape_image_t *t)
{
    t->store = NULL;
    t->position = 0;
    t->position_not_updated = 0;
}

void tape_rewind(tape_image_t *t)
{
    t->position = 0;
    t->position_not_updated = 0;
}

int tape_at_bot(const tape_image_t *t)
{
    return t->position == 0;
}

/* Read the 32-bit marker at a byte offset. Returns 0 on success, 1 if the offset
 * is at or beyond the end of the file (end of medium), -1 on a read error. */
static int read_marker(tape_image_t *t, uint32_t at, uint32_t *marker)
{
    uint8_t b[4];

    if (at + 4 > storage_size(t->store))
        return 1;
    if (storage_read(t->store, at, b, 4) != 0)
        return -1;
    *marker = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
              ((uint32_t)b[3] << 24);
    return 0;
}

static int write_marker(tape_image_t *t, uint32_t at, uint32_t marker)
{
    uint8_t b[4];

    b[0] = (uint8_t)marker;
    b[1] = (uint8_t)(marker >> 8);
    b[2] = (uint8_t)(marker >> 16);
    b[3] = (uint8_t)(marker >> 24);
    return storage_write(t->store, at, b, 4);
}

/* Find the next record, tape mark or the end, skipping erase gaps. On TAPE_OK the
 * position is left at the start of the data and *next at the byte after the
 * record; *length is the data length. */
static tape_status_t next_object(tape_image_t *t, uint32_t *length, uint32_t *data_at,
                                 uint32_t *next, uint32_t *flags)
{
    uint32_t pos = t->position, m;
    int r;

    for (;;) {
        r = read_marker(t, pos, &m);
        if (r < 0)
            return TAPE_IO_ERROR;
        if (r > 0 || m == TAPE_MARKER_EOM) {
            *length = 0;
            return TAPE_EOM;
        }
        if (m == TAPE_MARKER_MARK) {
            t->position = pos + 4;
            *length = 0;
            return TAPE_MARK;
        }
        if (m == TAPE_MARKER_GAP) {
            pos += 4;
            continue;
        }
        if (m == TAPE_MARKER_HALF_GAP) {
            pos += 2;                   /* resynchronise */
            continue;
        }
        *flags = m & TAPE_ERROR_FLAG;
        *length = m & ~TAPE_ERROR_FLAG;
        *data_at = pos + 4;
        *next = pos + 4 + *length + (*length & 1) + 4;
        return TAPE_OK;
    }
}

tape_status_t tape_read_forward(tape_image_t *t, uint8_t *buffer, uint32_t *length,
                                uint32_t max)
{
    uint32_t data_at = 0, next = 0, flags = 0;
    tape_status_t st;

    if (t->store == NULL)
        return TAPE_NOT_MOUNTED;
    t->position_not_updated = 0;
    st = next_object(t, length, &data_at, &next, &flags);
    if (st == TAPE_EOM) {
        t->position_not_updated = 1;
        return st;
    }
    if (st != TAPE_OK)
        return st;
    if (*length > max) {
        t->position_not_updated = 1;
        return TAPE_TOO_LONG;
    }
    if (storage_read(t->store, data_at, buffer, *length) != 0) {
        t->position_not_updated = 1;
        return TAPE_IO_ERROR;
    }
    t->position = next;
    return flags ? TAPE_RECORD_ERROR : TAPE_OK;
}

static tape_status_t write_record_flagged(tape_image_t *t, const uint8_t *data,
                                          uint32_t length, uint32_t flag)
{
    uint8_t pad = 0;
    uint32_t padded = (length + 1) & ~1u;
    uint32_t marker = length | flag;

    if (t->store == NULL)
        return TAPE_NOT_MOUNTED;
    if (t->write_protect)
        return TAPE_WRITE_PROTECTED;
    t->position_not_updated = 0;
    if (length == 0)
        return TAPE_OK;                 /* a zero-length record is nothing */
    if (write_marker(t, t->position, marker) != 0 ||
        storage_write(t->store, t->position + 4, data, length) != 0 ||
        (padded != length &&
         storage_write(t->store, t->position + 4 + length, &pad, 1) != 0) ||
        write_marker(t, t->position + 4 + padded, marker) != 0)
        return TAPE_IO_ERROR;
    t->position += padded + 8;
    return TAPE_OK;
}

tape_status_t tape_write_record(tape_image_t *t, const uint8_t *data, uint32_t length)
{
    return write_record_flagged(t, data, length, 0);
}

tape_status_t tape_write_bad_record(tape_image_t *t, const uint8_t *data,
                                    uint32_t length)
{
    return write_record_flagged(t, data, length, TAPE_ERROR_FLAG);
}

tape_status_t tape_write_mark(tape_image_t *t)
{
    if (t->store == NULL)
        return TAPE_NOT_MOUNTED;
    if (t->write_protect)
        return TAPE_WRITE_PROTECTED;
    t->position_not_updated = 0;
    if (write_marker(t, t->position, TAPE_MARKER_MARK) != 0)
        return TAPE_IO_ERROR;
    t->position += 4;
    return TAPE_OK;
}

tape_status_t tape_write_gap(tape_image_t *t, uint32_t bytes)
{
    uint32_t n = bytes / 4;

    if (t->store == NULL)
        return TAPE_NOT_MOUNTED;
    if (t->write_protect)
        return TAPE_WRITE_PROTECTED;
    t->position_not_updated = 0;
    if (n == 0)
        n = 1;
    while (n--) {
        if (write_marker(t, t->position, TAPE_MARKER_GAP) != 0)
            return TAPE_IO_ERROR;
        t->position += 4;
    }
    return TAPE_OK;
}

tape_status_t tape_space_forward(tape_image_t *t, uint32_t *length)
{
    uint32_t data_at = 0, next = 0, flags = 0;
    tape_status_t st;

    if (t->store == NULL)
        return TAPE_NOT_MOUNTED;
    if (t->position_not_updated) {      /* the last operation did not move the tape */
        t->position_not_updated = 0;
        *length = 0;
        return TAPE_OK;
    }
    st = next_object(t, length, &data_at, &next, &flags);
    if (st == TAPE_EOM) {
        t->position_not_updated = 1;
        return st;
    }
    if (st == TAPE_OK)
        t->position = next;
    return st;
}

tape_status_t tape_space_reverse(tape_image_t *t, uint32_t *length)
{
    uint32_t pos, m;
    int r;

    if (t->store == NULL)
        return TAPE_NOT_MOUNTED;
    if (t->position_not_updated) {
        t->position_not_updated = 0;
        *length = 0;
        return TAPE_OK;
    }
    pos = t->position;
    for (;;) {
        if (pos == 0) {
            t->position = 0;
            *length = 0;
            return TAPE_BOT;
        }
        r = read_marker(t, pos - 4, &m);
        if (r != 0)
            return TAPE_IO_ERROR;
        pos -= 4;
        if (m == TAPE_MARKER_MARK) {
            t->position = pos;
            *length = 0;
            return TAPE_MARK;
        }
        if (m == TAPE_MARKER_GAP)
            continue;
        *length = m & ~TAPE_ERROR_FLAG;
        pos -= *length + (*length & 1) + 4;
        t->position = pos;
        return TAPE_OK;
    }
}
