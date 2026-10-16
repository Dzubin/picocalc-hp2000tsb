/* hostfile.h - move text between a file on the SD card and a terminal of the emulated
 * machine, without a serial cable.
 *
 * Author: Thomas Dzubin.
 *
 * Import: a text file is typed into a terminal one line at a time, the way a person
 * would type it. Each line is followed by a wait for TSB's echo of the line feed and
 * then for a gap of instructions, so TSB has finished the line before the next arrives.
 * The program calls hostfile_import_next() when the terminal can take a character, and
 * hostfile_import_see() for every character the terminal prints.
 *
 * Capture: everything a terminal prints is copied into a file, appended to what is
 * there, and the file is cut back to the exact length when the capture ends.
 *
 * Portable: it uses only storage.h and platform.h.
 */

#ifndef HOSTFILE_H
#define HOSTFILE_H

#include <stdint.h>
#include "storage.h"
#include "hostfile_const.h"

typedef struct {
    hp_store_t *file;
    uint32_t    size, pos;                  /* the file, and how much of it is read */
    int         active;                     /* a file is being typed */
    int         state;
    char        line[HOSTFILE_LINE_MAX + 1];
    unsigned    len, at;                    /* the line being typed, and how much is typed */
    uint32_t    mark;                       /* instruction count at the last event */
    int         saw_lf, counted_error;
    char        tail[3];                    /* the last characters printed, to spot ??? */
    unsigned    total, sent, errors;        /* lines in the file, lines sent, lines TSB refused */
    uint8_t     chunk[HOSTFILE_CHUNK];
    unsigned    chunk_len, chunk_at;
} hostfile_import_t;

typedef struct {
    hp_store_t *file;
    uint32_t    size;                       /* bytes in the file (all written) */
    unsigned    fill;                       /* bytes waiting in buf */
    uint8_t     buf[HOSTFILE_CAPTURE_BUF];
    int         active, failed;
} hostfile_capture_t;

/* Start typing the file name (in the data folder). Returns 0 on success, otherwise 1 if
 * the file cannot be opened or 2 if it holds no lines. now is the instruction count. */
int  hostfile_import_open(hostfile_import_t *h, const char *name, uint32_t now);

/* The next character to type, or -1 if there is nothing to type just now. When the file
 * is finished active becomes 0. */
int  hostfile_import_next(hostfile_import_t *h, uint32_t now);

/* Tell the importer what the terminal printed. */
void hostfile_import_see(hostfile_import_t *h, uint8_t c);

/* Stop (or finish) and close the file. */
void hostfile_import_close(hostfile_import_t *h);

/* Start copying output into the file name (in the data folder), after what it holds.
 * Returns 0 on success. */
int  hostfile_capture_open(hostfile_capture_t *c, const char *name);
void hostfile_capture_put(hostfile_capture_t *c, uint8_t ch);
/* Write what is waiting, cut the file to its exact length and close it. Returns the
 * length of the file, or 0 if something failed. */
uint32_t hostfile_capture_close(hostfile_capture_t *c);

#endif
