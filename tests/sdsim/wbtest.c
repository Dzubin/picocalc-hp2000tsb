/* wbtest.c - test the storage layer (storage_pico.c) on the simulated SD card.
 *
 * Author: Thomas Dzubin.
 *
 * It writes tape-style records (marker, data, pad byte, marker) into one file
 * while reading random disc sectors from another, with the card refusing every
 * Nth write (env SD_FAIL_EVERY), then closes, reopens and compares the whole
 * tape file with a copy kept in memory, and compares every disc read with the
 * original file. Run from the folder that holds the image (see README.txt).
 *
 *   wbtest.exe TAPEFILE DISCFILE DISC_ORIGINAL_ON_PC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

#define TAPE_BYTES   (3u * 1024u * 1024u)
#define RECORDS      900u

static unsigned long seed = 12345;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned)((seed >> 16) & 0x7fffu) % n;
}

int main(int argc, char **argv)
{
    hp_store_t *tape, *disc;
    uint8_t *ref, *orig, *buf;
    FILE *f;
    long disc_size;
    uint32_t pos = 0, i, bad = 0;
    unsigned n, reads = 0;

    if (argc != 4)
        return 2;
    if (storage_mount() != 0) {
        fprintf(stderr, "mount failed\n");
        return 1;
    }
    tape = storage_open(argv[1], 1);
    disc = storage_open(argv[2], 1);
    f = fopen(argv[3], "rb");
    if (tape == NULL || disc == NULL || f == NULL) {
        fprintf(stderr, "cannot open files\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    disc_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    orig = malloc((size_t)disc_size);
    if (fread(orig, 1, (size_t)disc_size, f) != (size_t)disc_size)
        return 1;
    fclose(f);
    ref = malloc(TAPE_BYTES);
    buf = malloc(8192);
    if (storage_size(tape) < TAPE_BYTES) {
        fprintf(stderr, "tape file too small\n");
        return 1;
    }
    for (i = 0; i < TAPE_BYTES; i++)                        /* the reference starts as the file is */
        if (i % 4096 == 0 && storage_read(tape, i, ref + i, TAPE_BYTES - i < 4096 ? TAPE_BYTES - i : 4096) != 0)
            return 1;

    for (n = 0; n < RECORDS && pos + 9000 < TAPE_BYTES; n++) {
        unsigned len = 100 + rnd(4000), padded = (len + 1) & ~1u, k;
        uint8_t marker[4];

        marker[0] = (uint8_t)len; marker[1] = (uint8_t)(len >> 8); marker[2] = 0; marker[3] = 0;
        for (k = 0; k < len; k++)
            buf[k] = (uint8_t)rnd(256);
        buf[len] = 0;
        if (storage_write(tape, pos, marker, 4) || storage_write(tape, pos + 4, buf, len) ||
            (padded != len && storage_write(tape, pos + 4 + len, buf + len, 1)) ||
            storage_write(tape, pos + 4 + padded, marker, 4)) {
            fprintf(stderr, "write failed at record %u\n", n);
            return 1;
        }
        memcpy(ref + pos, marker, 4);
        memcpy(ref + pos + 4, buf, len);
        if (padded != len)
            ref[pos + 4 + len] = 0;
        memcpy(ref + pos + 4 + padded, marker, 4);
        pos += padded + 8;
        for (k = 0; k < 6; k++) {                           /* disc reads in between */
            uint32_t at = (uint32_t)(rnd(30000) * 256u + 0) % (uint32_t)(disc_size - 256);

            if (storage_read(disc, at, buf, 256) != 0 || memcmp(buf, orig + at, 256) != 0) {
                fprintf(stderr, "disc read mismatch at %u\n", (unsigned)at);
                bad++;
            }
            reads++;
        }
    }
    storage_close(tape);                                    /* flushes the waiting sector */
    storage_close(disc);
    tape = storage_open(argv[1], 0);
    for (i = 0; i < TAPE_BYTES; i += 4096) {
        uint32_t take = TAPE_BYTES - i < 4096 ? TAPE_BYTES - i : 4096;

        if (storage_read(tape, i, buf, take) != 0 || memcmp(buf, ref + i, take) != 0) {
            fprintf(stderr, "tape mismatch in the 4 KB at %u\n", (unsigned)i);
            bad++;
        }
    }
    printf("%u records (%u bytes), %u disc reads, %u mismatches\n", n, (unsigned)pos, reads, (unsigned)bad);
    return bad != 0;
}
