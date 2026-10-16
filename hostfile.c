/* hostfile.c - move text between a file on the SD card and a terminal (see hostfile.h).
 *
 * Author: Thomas Dzubin.
 */

#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "storage.h"
#include "hostfile.h"

enum { ST_LOAD, ST_TYPE, ST_WAIT, ST_GAP };

static int make_path(char *path, unsigned size, const char *name)
{
    return (unsigned)snprintf(path, size, "%s%s", plat_data_dir(), name) < size;
}

/* ===================================================================== */
/*  Import                                                                */
/* ===================================================================== */

/* The next byte of the file, or -1 at the end. */
static int file_byte(hostfile_import_t *h)
{
    if (h->chunk_at >= h->chunk_len) {
        uint32_t n = h->size - h->pos;

        if (n == 0)
            return -1;
        if (n > sizeof h->chunk)
            n = sizeof h->chunk;
        if (storage_read(h->file, h->pos, h->chunk, n) != 0)
            return -1;
        h->pos += n;
        h->chunk_len = n;
        h->chunk_at = 0;
    }
    return h->chunk[h->chunk_at++];
}

/* Read the next line that holds something into h->line. Returns 0 at the end of the
 * file. Carriage returns are dropped, tabs become spaces, other control characters are
 * skipped, a line longer than TSB takes is cut, and spaces at its end are removed. */
static int read_line(hostfile_import_t *h)
{
    for (;;) {
        int c, got_any = 0;

        h->len = 0;
        h->at = 0;
        while ((c = file_byte(h)) >= 0) {
            got_any = 1;
            if (c == '\n')
                break;
            if (c == '\t')
                c = ' ';
            if (c < 0x20 || c > 0x7E)
                continue;
            if (h->len < HOSTFILE_LINE_MAX)
                h->line[h->len++] = (char)c;
        }
        while (h->len > 0 && h->line[h->len - 1] == ' ')
            h->len--;
        h->line[h->len] = 0;
        if (h->len > 0)
            return 1;
        if (!got_any)
            return 0;                           /* end of the file */
    }
}

int hostfile_import_open(hostfile_import_t *h, const char *name, uint32_t now)
{
    char path[160];

    memset(h, 0, sizeof *h);
    if (!make_path(path, sizeof path, name) || (h->file = storage_open(path, 0)) == NULL)
        return 1;
    h->size = storage_size(h->file);
    while (read_line(h))                        /* count the lines for the progress display */
        h->total++;
    h->pos = h->chunk_len = h->chunk_at = 0;
    if (h->total == 0) {
        hostfile_import_close(h);
        return 2;
    }
    h->active = 1;
    h->state = ST_LOAD;
    h->mark = now;
    return 0;
}

int hostfile_import_next(hostfile_import_t *h, uint32_t now)
{
    if (!h->active)
        return -1;
    switch (h->state) {
    case ST_LOAD:
        if (!read_line(h)) {
            h->active = 0;
            return -1;
        }
        h->state = ST_TYPE;
        h->saw_lf = h->counted_error = 0;
        /* fall through */
    case ST_TYPE:
        if (h->at < h->len)
            return (unsigned char)h->line[h->at++];
        h->state = ST_WAIT;
        h->mark = now;
        h->sent++;
        return HOSTFILE_CR;
    case ST_WAIT:                               /* for the line feed TSB sends back */
        if (h->saw_lf || (uint32_t)(now - h->mark) > HOSTFILE_ECHO_WAIT) {
            h->state = ST_GAP;
            h->mark = now;
        }
        return -1;
    case ST_GAP:                                /* and then time to finish the line */
        if ((uint32_t)(now - h->mark) >= HOSTFILE_LINE_GAP)
            h->state = ST_LOAD;
        return -1;
    }
    return -1;
}

void hostfile_import_see(hostfile_import_t *h, uint8_t c)
{
    if (!h->active)
        return;
    if (h->state == ST_WAIT && c == '\n')
        h->saw_lf = 1;
    h->tail[0] = h->tail[1];
    h->tail[1] = h->tail[2];
    h->tail[2] = (char)c;
    if (!h->counted_error && h->state != ST_TYPE && memcmp(h->tail, HOSTFILE_ERROR_TEXT, 3) == 0) {
        h->errors++;
        h->counted_error = 1;
    }
}

void hostfile_import_close(hostfile_import_t *h)
{
    if (h->file != NULL)
        storage_close(h->file);
    h->file = NULL;
    h->active = 0;
}

/* ===================================================================== */
/*  Capture                                                               */
/* ===================================================================== */

/* Where the text in the file ends: a file that was not cut back (a power cut) ends in
 * zeros, so look back from the end for the last byte that is not one. */
static uint32_t text_end(hp_store_t *store)
{
    uint8_t block[256];
    uint32_t end = storage_size(store);

    while (end > 0) {
        uint32_t start = end > sizeof block ? end - (uint32_t)sizeof block : 0;
        uint32_t n = end - start, i;

        if (storage_read(store, start, block, n) != 0)
            return storage_size(store);
        for (i = n; i > 0; i--)
            if (block[i - 1] != 0)
                return start + i;
        end = start;
    }
    return 0;
}

int hostfile_capture_open(hostfile_capture_t *c, const char *name)
{
    char path[160];

    memset(c, 0, sizeof *c);
    if (!make_path(path, sizeof path, name))
        return 1;
    c->file = storage_open(path, 1);
    if (c->file == NULL)
        c->file = storage_create(path);
    if (c->file == NULL)
        return 1;
    c->size = text_end(c->file);
    c->active = 1;
    return 0;
}

static void flush(hostfile_capture_t *c)
{
    if (c->fill == 0 || c->failed)
        return;
    if (storage_write(c->file, c->size, c->buf, c->fill) != 0)
        c->failed = 1;
    else
        c->size += c->fill;
    c->fill = 0;
}

void hostfile_capture_put(hostfile_capture_t *c, uint8_t ch)
{
    if (!c->active || c->failed)
        return;
    if (ch == 0 || ch == 0x7F || (ch < 0x20 && ch != '\r' && ch != '\n' && ch != '\t'))
        return;                                 /* nothing a text file should hold */
    c->buf[c->fill++] = ch;
    if (c->fill == sizeof c->buf)
        flush(c);
}

uint32_t hostfile_capture_close(hostfile_capture_t *c)
{
    uint32_t size;

    if (c->file == NULL)
        return 0;
    flush(c);
    if (!c->failed && storage_truncate(c->file, c->size) != 0)
        c->failed = 1;
    storage_close(c->file);
    size = c->failed ? 0 : c->size;
    c->file = NULL;
    c->active = 0;
    return size;
}
