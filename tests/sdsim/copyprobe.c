/* copyprobe.c - copy one file to a new file on the simulated SD card and print how
 * many SD sector reads and writes each 10 percent of the copy cost.
 *
 * Author: Thomas Dzubin.
 *
 * It uses the same storage calls as spare.c, so a copy that gets slower as the file
 * grows shows up as a rising count. Run from the folder that holds sd.img.
 *
 *   copyprobe.exe /hp2000access/SYSTEM-2883.disc /hp2000access/NEW-SYSTEM-2883.disc [CHUNK]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "storage.h"

extern unsigned long sim_reads, sim_writes;

int main(int argc, char **argv)
{
    static uint8_t buffer[65536];
    hp_store_t *in, *out;
    uint32_t size, done = 0, chunk = argc > 3 ? (uint32_t)atoi(argv[3]) : 4096u;
    unsigned next = 10;
    unsigned long r0 = 0, w0 = 0;

    if (argc < 3 || chunk > sizeof buffer || storage_mount() != 0)
        return 2;
    in = storage_open(argv[1], 0);
    out = storage_create(argv[2]);
    if (in == NULL || out == NULL) {
        fprintf(stderr, "cannot open\n");
        return 1;
    }
    size = storage_size(in);
    if (getenv("RESERVE"))
        printf("reserve returned %d\n", storage_reserve(out, size));
    while (done < size) {
        uint32_t n = size - done < chunk ? size - done : chunk;

        if (storage_read(in, done, buffer, n) || storage_write(out, done, buffer, n)) {
            fprintf(stderr, "failed at %u\n", (unsigned)done);
            return 1;
        }
        done += n;
        if ((uint64_t)done * 100u / size >= next) {
            printf("%3u%%  sector reads %8lu  writes %8lu\n", next, sim_reads - r0, sim_writes - w0);
            r0 = sim_reads;
            w0 = sim_writes;
            next += 10;
        }
    }
    storage_close(out);
    out = storage_open(argv[2], 0);
    if (out == NULL || storage_size(out) != size) {
        printf("VERIFY: bad size\n");
        return 1;
    }
    {
        static uint8_t other[65536];
        uint32_t at, bad = 0;

        for (at = 0; at < size; at += chunk) {
            uint32_t n = size - at < chunk ? size - at : chunk;

            if (storage_read(in, at, buffer, n) || storage_read(out, at, other, n) ||
                memcmp(buffer, other, n))
                bad++;
        }
        printf("VERIFY: %u bad blocks\n", (unsigned)bad);
    }
    storage_close(in);
    storage_close(out);
    return 0;
}
