/* growtest.c - test a file that GROWS on the simulated SD card (storage_pico.c).
 *
 * Author: Thomas Dzubin.
 *
 * The SLEEP tape is created empty by storage_create() and then written the way the
 * tape controller writes: tape records (marker, data, pad byte, marker) and runs of
 * four-byte erase gap markers, each as its own small write, with a read back of the
 * most recent record now and then. At the end the file is closed, reopened, and every
 * byte is compared with a copy kept in memory.
 *
 *   growtest.exe NEWTAPEFILE [KILOBYTES]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

static unsigned long seed = 4242;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned)((seed >> 16) & 0x7fffu) % n;
}

int main(int argc, char **argv)
{
    hp_store_t *tape;
    uint8_t *ref, *buf, *back;
    uint32_t limit, pos = 0, i, bad = 0, last = 0, last_len = 0;
    unsigned n = 0;

    if (argc < 2)
        return 2;
    limit = (argc > 2 ? (uint32_t)atoi(argv[2]) : 200u) * 1024u;
    if (storage_mount() != 0) {
        fprintf(stderr, "mount failed\n");
        return 1;
    }
    tape = storage_create(argv[1]);
    if (tape == NULL) {
        fprintf(stderr, "cannot create %s\n", argv[1]);
        return 1;
    }
    ref = calloc(limit + 16384u, 1);
    buf = malloc(8192);
    back = malloc(8192);
    while (pos + 9000 < limit) {
        unsigned len = 20 + rnd(4000), padded = (len + 1) & ~1u, k, gaps = rnd(40);
        uint8_t marker[4], gap[4] = { 0xFE, 0xFF, 0xFF, 0xFF };

        marker[0] = (uint8_t)len; marker[1] = (uint8_t)(len >> 8); marker[2] = 0; marker[3] = 0;
        for (k = 0; k < len; k++)
            buf[k] = (uint8_t)rnd(256);
        buf[len] = 0;
        for (k = 0; k < gaps; k++) {                        /* an erase gap before the record */
            if (storage_write(tape, pos, gap, 4)) {
                fprintf(stderr, "gap write failed at %u\n", pos);
                return 1;
            }
            memcpy(ref + pos, gap, 4);
            pos += 4;
        }
        if (storage_write(tape, pos, marker, 4) || storage_write(tape, pos + 4, buf, len) ||
            (padded != len && storage_write(tape, pos + 4 + len, buf + len, 1)) ||
            storage_write(tape, pos + 4 + padded, marker, 4)) {
            fprintf(stderr, "write failed at record %u, offset %u\n", n, pos);
            return 1;
        }
        memcpy(ref + pos, marker, 4);
        memcpy(ref + pos + 4, buf, padded);
        memcpy(ref + pos + 4 + padded, marker, 4);
        last = pos;
        last_len = padded + 8;
        pos += padded + 8;
        n++;
        if (n % 3 == 0 && storage_read(tape, last, back, last_len) == 0 &&
            memcmp(back, ref + last, last_len) != 0) {
            fprintf(stderr, "read back differs at record %u\n", n);
            bad++;
        }
    }
    storage_close(tape);
    tape = storage_open(argv[1], 0);
    if (tape == NULL) {
        fprintf(stderr, "cannot reopen\n");
        return 1;
    }
    printf("wrote %u records, %u bytes; file size %u\n", n, pos, storage_size(tape));
    for (i = 0; i < pos; i += 4096) {
        uint32_t chunk = pos - i < 4096 ? pos - i : 4096;

        if (storage_read(tape, i, back, chunk) != 0 || memcmp(back, ref + i, chunk) != 0) {
            if (bad < 5)
                printf("MISMATCH in the block at offset %u\n", i);
            bad++;
        }
    }
    printf("%s (%u bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
