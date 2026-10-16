/* sparesim.c - run the spare-copy code (spare.c) on the simulated SD card.
 *
 * Author: Thomas Dzubin.
 *
 * Runs a string of commands, one letter each: S saves the spares (after changing one
 * byte of the live tape to the number of the save, so each generation differs), R
 * restores the newest spares, O restores the older ones, D damages one byte of the
 * first file in slot 1 (the newest after an odd number of saves) to see that a restore refuses it. After R or O the changed byte of the
 * live tape is printed. Run from the folder that holds sd.img, after fatput has put
 * the system files in /hp2000access/; then run fatls to list the folder.
 *
 *   sparesim.exe SSSRO
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "platform.h"
#include "storage.h"
#include "spare.h"

#define TAPE_PATH   "/hp2000access/HIBERNATE-2883.tape"
#define MARK_AT     1000u

const char *plat_data_dir(void)
{
    return "/hp2000access/";
}

uint32_t plat_now_ms(void)
{
    return 0;
}

static void progress(const char *what, unsigned percent)
{
    (void)what;
    (void)percent;
}

static void set_mark(uint8_t value)
{
    hp_store_t *s = storage_open(TAPE_PATH, 1);

    if (s != NULL) {
        (void)storage_write(s, MARK_AT, &value, 1);
        storage_close(s);
    }
}

static int get_mark(const char *path)
{
    hp_store_t *s = storage_open(path, 0);
    uint8_t value = 0;

    if (s == NULL)
        return -1;
    (void)storage_read(s, MARK_AT, &value, 1);
    storage_close(s);
    return value;
}

int main(int argc, char **argv)
{
    const char *names[2] = { "SYSTEM-2883.disc", "HIBERNATE-2883.tape" };
    char detail[64];
    const char *error, *commands = argc > 1 ? argv[1] : "SS";
    unsigned saves = 0;

    for (; *commands; commands++) {
        switch (*commands) {
        case 'S':
            set_mark((uint8_t)(++saves + 100u));
            error = spare_copy(names, 2, SPARE_MODE_SAVE, "SLEEP-2883.tape", progress, detail,
                               sizeof detail);
            printf("save (mark %u): %s [%s]\n", saves + 100u, error ? error : "OK", detail);
            break;
        case 'R':
        case 'O':
            set_mark(1);                    /* so a restore that did nothing shows */
            error = spare_copy(names, 2, *commands == 'R' ? SPARE_MODE_RESTORE
                                                         : SPARE_MODE_RESTORE_OLD,
                               "SLEEP-2883.tape", progress, detail, sizeof detail);
            printf("restore %s: %s [%s]  live tape mark now %d\n",
                   *commands == 'R' ? "newest" : "older", error ? error : "OK", detail,
                   get_mark(TAPE_PATH));
            break;
        case 'D': {
            hp_store_t *s = storage_open("/hp2000access/SP1F0.BIN", 1);
            uint8_t b = 0x5A;

            if (s != NULL) {
                (void)storage_write(s, 5000, &b, 1);
                storage_close(s);
            }
            printf("damaged one byte of SP1F0.BIN\n");
            break;
        }
        default:
            break;
        }
    }
    printf("state %d, older present %d\n", spare_state(names, 2), spare_old_present(names, 2));
    return 0;
}
