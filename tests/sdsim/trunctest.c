/* trunctest.c - test storage_truncate() and storage_list() of storage_pico.c on the
 * simulated SD card.
 *
 * Author: Thomas Dzubin.
 *
 * A text file written a little at a time grows in blocks of 64 KB; truncate must cut it
 * to its exact length and give the clusters back. Run from the folder that holds sd.img,
 * after fatput has put some files in /hp2000access/ (see README.txt).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "storage.h"
#include "fat32.h"

static int failures;

static void check(const char *what, int ok)
{
    printf("%-4s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

static uint64_t free_bytes(void)
{
    uint64_t f = 0;

    (void)fat32_get_free_space(&f);
    return f;
}

int main(void)
{
    static const char *const suffixes[] = { ".TXT", ".BAS" };
    static char names[20][48];
    hp_store_t *s;
    uint8_t data[1000], back[1000];
    uint64_t free0, free1, free2;
    unsigned i, n;
    int found_txt = 0, found_bas = 0, found_other = 0;

    if (storage_mount() != 0)
        return 2;
    for (i = 0; i < sizeof data; i++)
        data[i] = (uint8_t)(i * 7 + 1);

    free0 = free_bytes();
    s = storage_create("/hp2000access/CAPTURE.TXT");
    check("create", s != NULL);
    check("write 1000 bytes", storage_write(s, 0, data, sizeof data) == 0);
    printf("     size after the write: %u\n", (unsigned)storage_size(s));
    check("the file grew in a block", storage_size(s) > sizeof data);
    free1 = free_bytes();
    check("truncate to 1000", storage_truncate(s, sizeof data) == 0);
    check("the size is exact", storage_size(s) == sizeof data);
    storage_close(s);
    free2 = free_bytes();
    printf("     free bytes: before %llu, after the write %llu, after the cut %llu\n",
           (unsigned long long)free0, (unsigned long long)free1, (unsigned long long)free2);
    check("the clusters after the first were given back", free2 > free1);
    check("only one cluster is kept for 1000 bytes", free0 - free2 <= 4096u * 1u);

    s = storage_open("/hp2000access/CAPTURE.TXT", 0);
    check("reopen", s != NULL);
    check("the size is still exact", storage_size(s) == sizeof data);
    memset(back, 0, sizeof back);
    check("the data is intact", storage_read(s, 0, back, sizeof back) == 0 && memcmp(data, back, sizeof data) == 0);
    storage_close(s);

    /* a second file, a longer one: cut in the middle of a later cluster */
    {
        static uint8_t big[20000], rb[20000];

        for (i = 0; i < sizeof big; i++)
            big[i] = (uint8_t)(i * 13 + 5);
        s = storage_create("/hp2000access/LONGER.BAS");
        check("create a second file", s != NULL);
        check("write 20000 bytes", storage_write(s, 0, big, sizeof big) == 0);
        check("truncate to 20000", storage_truncate(s, sizeof big) == 0 && storage_size(s) == sizeof big);
        storage_close(s);
        s = storage_open("/hp2000access/LONGER.BAS", 0);
        check("it reads back", s != NULL && storage_size(s) == sizeof big &&
                                   storage_read(s, 0, rb, sizeof rb) == 0 && memcmp(big, rb, sizeof big) == 0);
        storage_close(s);
    }

    /* the file made last must not have overwritten the first one's clusters */
    s = storage_open("/hp2000access/CAPTURE.TXT", 0);
    memset(back, 0, sizeof back);
    check("the first file is still intact",
          s != NULL && storage_read(s, 0, back, sizeof back) == 0 && memcmp(data, back, sizeof data) == 0);
    storage_close(s);

    /* the listing */
    s = storage_create("/hp2000access/NOTES.DAT");
    storage_write(s, 0, data, 10);
    storage_close(s);
    n = storage_list("/hp2000access/", suffixes, 2, &names[0][0], sizeof names[0], 20);
    for (i = 0; i < n; i++) {
        printf("     listed: %s\n", names[i]);
        if (strcmp(names[i], "CAPTURE.TXT") == 0 || strcmp(names[i], "RUNLOG.TXT") == 0)
            found_txt++;
        else if (strcmp(names[i], "LONGER.BAS") == 0)
            found_bas++;
        else
            found_other++;
    }
    check("the listing has the .TXT files and the .BAS file and nothing else", found_txt >= 1 && found_bas == 1 && found_other == 0);
    check("a folder that does not exist lists nothing", storage_list("/nowhere/", suffixes, 2, &names[0][0], sizeof names[0], 20) == 0);

    printf("\n%d failure(s)\n", failures);
    return failures;
}
