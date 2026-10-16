/* storage_stdio.c - storage.h on top of the C library's stdio files.
 *
 * Author: Thomas Dzubin.
 *
 * Used by the desktop builds and by the host test programs.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

struct hp_store {
    FILE    *file;
    int      writable;
};

const char *storage_last_error(void)
{
    return strerror(errno);
}

unsigned storage_failures(char *text, unsigned size)
{
    if (size > 0)
        text[0] = 0;
    return 0;
}

void storage_stats(uint32_t *milliseconds, uint32_t *transfers)
{
    *milliseconds = 0;
    *transfers = 0;
}

int storage_mount(void)
{
    return 0;                           /* the host file system is always there */
}

hp_store_t *storage_open(const char *path, int writable)
{
    hp_store_t *s = malloc(sizeof *s);

    if (s == NULL)
        return NULL;
    s->file = fopen(path, writable ? "r+b" : "rb");
    s->writable = writable;
    if (s->file == NULL) {
        free(s);
        return NULL;
    }
    return s;
}

hp_store_t *storage_create(const char *path)
{
    hp_store_t *s = malloc(sizeof *s);

    if (s == NULL)
        return NULL;
    s->file = fopen(path, "w+b");
    s->writable = 1;
    if (s->file == NULL) {
        free(s);
        return NULL;
    }
    return s;
}

int storage_reserve(hp_store_t *store, uint32_t bytes)
{
    (void)store;
    (void)bytes;
    return 0;                           /* stdio files grow cheaply */
}

int storage_truncate(hp_store_t *store, uint32_t bytes)
{
    if (!store->writable)
        return 1;
    if (fflush(store->file) != 0)
        return 1;
#ifdef _WIN32
    return _chsize(_fileno(store->file), (long)bytes) != 0;
#else
    return ftruncate(fileno(store->file), (off_t)bytes) != 0;
#endif
}

static int name_ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name), k = strlen(suffix);

    if (k > n)
        return 0;
    name += n - k;
    while (*suffix != 0)
        if (toupper((unsigned char)*name++) != toupper((unsigned char)*suffix++))
            return 0;
    return 1;
}

unsigned storage_list(const char *folder, const char *const *suffixes, unsigned suffix_count,
                      char *names, unsigned name_size, unsigned max)
{
    DIR *d = opendir(folder);
    struct dirent *e;
    unsigned count = 0, i;

    if (d == NULL)
        return 0;
    while (count < max && (e = readdir(d)) != NULL) {
        for (i = 0; i < suffix_count; i++)
            if (name_ends_with(e->d_name, suffixes[i]) && strlen(e->d_name) < name_size) {
                strcpy(names + (size_t)count * name_size, e->d_name);
                count++;
                break;
            }
    }
    closedir(d);
    return count;
}

int storage_delete(const char *path)
{
    return remove(path) != 0;
}

void storage_close(hp_store_t *store)
{
    if (store == NULL)
        return;
    fclose(store->file);
    free(store);
}

uint32_t storage_size(hp_store_t *store)
{
    long n;

    fseek(store->file, 0, SEEK_END);
    n = ftell(store->file);
    return n < 0 ? 0 : (uint32_t)n;
}

int storage_read(hp_store_t *store, uint32_t offset, void *buffer, uint32_t length)
{
    size_t got;

    if (fseek(store->file, (long)offset, SEEK_SET) != 0)
        return 1;
    got = fread(buffer, 1, length, store->file);
    if (got < length) {
        if (ferror(store->file)) {
            clearerr(store->file);
            return 1;
        }
        memset((char *)buffer + got, 0, length - got);
    }
    return 0;
}

int storage_write(hp_store_t *store, uint32_t offset, const void *buffer, uint32_t length)
{
    if (!store->writable)
        return 1;
    if (fseek(store->file, (long)offset, SEEK_SET) != 0)
        return 1;
    if (fwrite(buffer, 1, length, store->file) != length)
        return 1;
    return fflush(store->file) != 0;
}
