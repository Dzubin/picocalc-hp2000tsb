/* fatls.c - list /hp2000access on the simulated SD card with the real FAT32 driver, and
 * try to open each file by its path.
 *
 * Author: Thomas Dzubin.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "sdcard.h"
#include "fat32.h"
#include "fat32.c"

int main(void)
{
    fat32_file_t dir, f;
    fat32_entry_t entry;
    fat32_error_t e;
    char names[64][128];
    unsigned count = 0, i;

    fat32_init();
    if (!fat32_is_ready()) {
        printf("not ready\n");
        return 1;
    }
    e = fat32_open(&dir, "/hp2000access");
    printf("open dir: %s\n", fat32_error_string(e));
    if (e != FAT32_OK)
        return 1;
    while (fat32_dir_read(&dir, &entry) == FAT32_OK && entry.filename[0]) {
        printf("  %-32s %10lu\n", entry.filename, (unsigned long)entry.size);
        if (count < 64)
            snprintf(names[count++], 128, "/hp2000access/%s", entry.filename);
    }
    fat32_close(&dir);
    for (i = 0; i < count; i++) {
        e = fat32_open(&f, names[i]);
        if (e != FAT32_OK)
            printf("CANNOT OPEN %s: %s\n", names[i], fat32_error_string(e));
        else
            fat32_close(&f);
    }
    return 0;
}
