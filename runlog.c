/* runlog.c - a log file on the card and a quick SD speed test (see runlog.h).
 *
 * Author: Thomas Dzubin.
 */

#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "storage.h"
#include "runlog.h"
#include "runlog_const.h"

static int make_path(char *path, unsigned size, const char *name)
{
    return (unsigned)snprintf(path, size, "%s%s", plat_data_dir(), name) < size;
}

/* Where the log ends: the first zero byte (the file is padded with zeros). */
static uint32_t log_end(hp_store_t *store)
{
    uint8_t block[256];
    uint32_t at, size = storage_size(store);

    for (at = 0; at < size && at < RUNLOG_MAX_BYTES; at += sizeof block) {
        unsigned i;

        if (storage_read(store, at, block, sizeof block) != 0)
            return RUNLOG_MAX_BYTES;
        for (i = 0; i < sizeof block; i++)
            if (block[i] == 0)
                return at + i;
    }
    return at < size ? at : size;
}

void runlog_append(const char *line)
{
    char path[160];
    char text[RUNLOG_LINE_MAX + 2];
    hp_store_t *store;
    uint32_t end, length;

    if (!make_path(path, sizeof path, RUNLOG_FILE))
        return;
    length = (uint32_t)snprintf(text, sizeof text, "%s\r\n", line);
    if (length > sizeof text - 1)
        length = sizeof text - 1;
    store = storage_open(path, 1);
    if (store == NULL)
        store = storage_create(path);
    if (store == NULL)
        return;
    end = log_end(store);
    if (end + length > RUNLOG_MAX_BYTES) {          /* full: start again */
        storage_close(store);
        store = storage_create(path);
        if (store == NULL)
            return;
        end = 0;
    }
    (void)storage_write(store, end, text, length);
    storage_close(store);
}

void sdtest_run(char *text, unsigned size)
{
    static uint8_t buffer[SDTEST_CHUNK];
    char path[160], line[RUNLOG_LINE_MAX];
    hp_store_t *store;
    uint32_t t0, read_ms = 0, write_ms = 0, done, fails_before, fails_after;
    char last[24];
    int bad = 0;

    fails_before = storage_failures(last, sizeof last);
    if (!make_path(path, sizeof path, SDTEST_READ_FILE) ||
        (store = storage_open(path, 0)) == NULL) {
        snprintf(text, size, "NO %s", SDTEST_READ_FILE);
        return;
    }
    t0 = plat_now_ms();
    for (done = 0; done < SDTEST_READ_BYTES && done < storage_size(store); done += SDTEST_CHUNK)
        bad |= storage_read(store, done, buffer, SDTEST_CHUNK);
    read_ms = plat_now_ms() - t0;
    storage_close(store);

    memset(buffer, 0x5A, sizeof buffer);
    if (!make_path(path, sizeof path, SDTEST_FILE) || (store = storage_create(path)) == NULL) {
        snprintf(text, size, "CANNOT CREATE %s", SDTEST_FILE);
        return;
    }
    t0 = plat_now_ms();
    for (done = 0; done < SDTEST_WRITE_BYTES; done += SDTEST_CHUNK)
        bad |= storage_write(store, done, buffer, SDTEST_CHUNK);
    storage_close(store);
    write_ms = plat_now_ms() - t0;

    fails_after = storage_failures(last, sizeof last);
    if (read_ms == 0)
        read_ms = 1;
    if (write_ms == 0)
        write_ms = 1;
    snprintf(text, size, "R %uK/S W %uK/S FAIL %u%s",
             (unsigned)(SDTEST_READ_BYTES / 1024u * 1000u / read_ms),
             (unsigned)(SDTEST_WRITE_BYTES / 1024u * 1000u / write_ms),
             (unsigned)(fails_after - fails_before), bad ? " ERR" : "");
    snprintf(line, sizeof line, "SDTEST read %u KB in %u ms, write %u KB in %u ms, fails %u%s",
             (unsigned)(SDTEST_READ_BYTES / 1024u), (unsigned)read_ms,
             (unsigned)(SDTEST_WRITE_BYTES / 1024u), (unsigned)write_ms,
             (unsigned)(fails_after - fails_before), bad ? ", ERRORS" : "");
    runlog_append(line);
}
