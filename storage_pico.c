/* storage_pico.c - storage.h on the PicoCalc's SD card (FAT32 driver by Blair
 * Leduc, from picocalc-text-starter).
 *
 * Author: Thomas Dzubin.
 *
 * The images are ordinary files on the card. The driver's own read and write
 * start at the first cluster of the file and follow the cluster chain through
 * the FAT, one SD sector read per link, every time. The disc image is 4.9 MB, so
 * one 256 byte disc sector would cost hundreds of card reads. This layer reads the
 * chain once when a file is opened and then goes straight to the sector. A write
 * that grows a file (a new tape image) first extends it with zeros through the
 * driver, which allocates clusters, in large blocks; the chain is read again and the
 * write itself goes straight to the sector.
 *
 * The driver's source is included here (and not compiled on its own) only so this
 * file can use its sector functions and geometry; the vendored files stay
 * unmodified.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "sdcard.h"
#include "fat32.h"
#include "storage.h"
#include "storage_pico_const.h"

static unsigned failures;               /* SD reads and writes that failed */
static char     failure_text[24];       /* ... and what the last one was */
static uint64_t sd_us;                  /* time spent in SD transfers, for the run report */
static uint32_t sd_transfers;

/* Remember a failed transfer: R or W, the SD sector (0 if not known), the code. */
static void note_failure(char op, uint32_t sector, int code)
{
    failures++;
    snprintf(failure_text, sizeof failure_text, "%c S%lu E%d", op,
             (unsigned long)sector, code);
}

/* The driver switches the card to 25 MHz after it starts it. The PicoCalc's wiring
 * and some cards do not manage that (many reads and writes were refused), so the
 * clock is set to STORAGE_PICO_SPI_HZ after every start of the card. */
static sd_error_t sd_init_paced(void)
{
    sd_error_t err = sd_card_init();

    if (err == SD_OK)
        spi_set_baudrate(SD_SPI, STORAGE_PICO_SPI_HZ);
    return err;
}

/* A card that is still programming a block holds its data line low and ignores commands.
 * The driver waits for that after a write by reading up to 10000 bytes, which is a count
 * and not a time (it takes less real time at a faster clock), and it ignores the result,
 * so the next command can reach a card that is still busy and fail, or be misread, since
 * a busy line reads as the status 0 that means success. Before every transfer, and after
 * every write, this waits (up to STORAGE_PICO_BUSY_US) until the card lets go of the line. */
static void sd_wait_idle(void)
{
    uint8_t out = 0xFF, in = 0;
    uint64_t until = time_us_64() + STORAGE_PICO_BUSY_US;

    gpio_put(SD_CS, 0);
    do {
        spi_write_read_blocking(SD_SPI, &out, &in, 1);
    } while (in != 0xFF && time_us_64() < until);
    gpio_put(SD_CS, 1);
    spi_write_blocking(SD_SPI, &out, 1);        /* eight clocks with the card deselected */
}

/* The card now and then refuses a sector transfer, and the driver gives up at once.
 * Every sector transfer the driver makes goes through these two functions instead
 * (the macros below), so a refused transfer is tried again: a pause, and in the
 * middle of the tries a fresh start of the card. This covers the driver's own writes
 * when a file grows (new clusters, the FAT, the directory entry), which a file
 * that only grew the tape image used to lose silently. Every failed try is counted
 * for the operator console. */
static sd_error_t sd_write_retry(uint32_t block, const uint8_t *buffer)
{
    sd_error_t err = SD_OK;
    unsigned tries;
    uint64_t started = time_us_64();

    sd_transfers++;
    for (tries = 0; tries < STORAGE_PICO_TRIES; tries++) {
        sd_wait_idle();
        err = sd_write_block(block, buffer);
        if (err == SD_OK) {
            sd_wait_idle();                     /* let the programming finish */
            sd_us += time_us_64() - started;
            return SD_OK;
        }
        note_failure('W', block, (int)err);
        sleep_ms(STORAGE_PICO_RETRY_MS);
        if (tries == STORAGE_PICO_REINIT_TRY)
            (void)sd_init_paced();
    }
    sd_us += time_us_64() - started;
    return err;
}

static sd_error_t sd_read_retry(uint32_t block, uint8_t *buffer)
{
    sd_error_t err = SD_OK;
    unsigned tries;
    uint64_t started = time_us_64();

    sd_transfers++;
    for (tries = 0; tries < STORAGE_PICO_TRIES; tries++) {
        sd_wait_idle();
        err = sd_read_block(block, buffer);
        if (err == SD_OK) {
            sd_us += time_us_64() - started;
            return SD_OK;
        }
        note_failure('R', block, (int)err);
        sleep_ms(STORAGE_PICO_RETRY_MS);
        if (tries == STORAGE_PICO_REINIT_TRY)
            (void)sd_init_paced();
    }
    sd_us += time_us_64() - started;
    return err;
}

#define sd_write_block sd_write_retry
#define sd_read_block  sd_read_retry
#define sd_card_init   sd_init_paced
#include "fat32.c"      /* by Blair Leduc; see the note above */
#undef sd_write_block
#undef sd_read_block
#undef sd_card_init

struct hp_store {
    fat32_file_t file;
    uint32_t    *chain;                 /* cluster numbers of the file, in order */
    uint32_t     chain_clusters;        /* clusters the file's size needs */
    uint8_t      in_use;
    uint8_t      writable;
};

static struct hp_store handles[STORAGE_PICO_HANDLES];
static fat32_error_t last_error;

void storage_stats(uint32_t *milliseconds, uint32_t *transfers)
{
    *milliseconds = (uint32_t)(sd_us / 1000u);
    *transfers = sd_transfers;
}

unsigned storage_failures(char *text, unsigned size)
{
    snprintf(text, size, "%s", failure_text);
    return failures;
}

static uint8_t  io_buffer[FAT32_SECTOR_SIZE] __attribute__((aligned(4)));
static uint32_t io_sector = STORAGE_PICO_NO_SECTOR; /* sector held in io_buffer */
static uint8_t  fat_buffer[FAT32_SECTOR_SIZE] __attribute__((aligned(4)));

/* Writes wait in a one-sector buffer of their own (write-behind), because a tape
 * record is written as a marker, the data and a second marker and a disc sector is
 * half an SD sector: without it the same SD sector would be written two or three
 * times, and every SD write is a chance for the card to refuse. The read buffer
 * above is separate, so reading the disc while a tape record is written does not
 * push the waiting sector out. */
static uint8_t  wb_buffer[FAT32_SECTOR_SIZE] __attribute__((aligned(4)));
static uint32_t wb_sector = STORAGE_PICO_NO_SECTOR; /* sector held in wb_buffer */
static int      wb_dirty;                           /* ... and not yet on the card */

/* One sector of the volume. The retries are in sd_write_retry and sd_read_retry. */
static fat32_error_t retry_io(int writing, uint32_t sector, uint8_t *buffer)
{
    return writing ? write_sector(sector, buffer) : read_sector(sector, buffer);
}

/* Write the waiting sector to the card. Returns 0 on success. */
static int flush_wb(void)
{
    if (wb_sector != STORAGE_PICO_NO_SECTOR && wb_dirty) {
        if (retry_io(1, wb_sector, wb_buffer) != FAT32_OK)
            return 1;
        wb_dirty = 0;
    }
    return 0;
}

/* Everything on the card, and forget the read buffer, before the driver is used
 * directly (it reads and writes the same sectors behind our back). */
static int sync_all(void)
{
    int bad = flush_wb();

    io_sector = STORAGE_PICO_NO_SECTOR;
    if (!bad)
        wb_sector = STORAGE_PICO_NO_SECTOR;     /* the driver may change that sector now */
    return bad;
}

const char *storage_last_error(void)
{
    return fat32_error_string(last_error);
}

static struct hp_store *free_handle(void)
{
    unsigned i;

    for (i = 0; i < STORAGE_PICO_HANDLES; i++)
        if (!handles[i].in_use)
            return &handles[i];
    return NULL;
}

/* Read a file's cluster chain from the FAT, once. If anything looks wrong the
 * chain is left NULL and the file uses the driver's own (slow) read and write. */
static void build_chain(struct hp_store *s)
{
    uint32_t size = s->file.file_size;
    uint32_t n = (size + bytes_per_cluster - 1) / bytes_per_cluster;
    uint32_t cluster = s->file.start_cluster;
    uint32_t loaded = STORAGE_PICO_NO_SECTOR;
    uint32_t i;

    free(s->chain);
    s->chain = NULL;
    s->chain_clusters = n;
    if (n == 0)
        return;
    s->chain = malloc(n * sizeof *s->chain);
    if (s->chain == NULL)
        return;
    for (i = 0; i < n; i++) {
        s->chain[i] = cluster;
        if (i + 1 < n) {
            uint32_t offset = cluster * 4;
            uint32_t fat_sector = boot_sector.reserved_sectors +
                                  offset / FAT32_SECTOR_SIZE;
            uint32_t next;

            if (cluster < 2)
                break;
            if (fat_sector != loaded) {
                if (retry_io(0, fat_sector, fat_buffer) != FAT32_OK)
                    break;
                loaded = fat_sector;
            }
            next = *(uint32_t *)(fat_buffer + offset % FAT32_SECTOR_SIZE) &
                   STORAGE_PICO_FAT_MASK;
            if (next < 2 || next >= FAT32_FAT_ENTRY_EOC)
                break;
            cluster = next;
        }
    }
    if (i < n) {                                /* a broken chain: do not trust it */
        free(s->chain);
        s->chain = NULL;
    }
}

/* Get one SD sector into io_buffer, unless it is already there. */
static int load_sector(uint32_t sector)
{
    if (io_sector == sector)
        return 0;
    io_sector = STORAGE_PICO_NO_SECTOR;
    if (retry_io(0, sector, io_buffer) != FAT32_OK)
        return 1;
    io_sector = sector;
    return 0;
}

/* Read or write inside the existing size of a file whose chain is known. */
static int chain_io(struct hp_store *s, uint32_t offset, uint8_t *data,
                    uint32_t length, int writing)
{
    while (length > 0) {
        uint32_t in_cluster = offset % bytes_per_cluster;
        uint32_t sector = cluster_to_sector(s->chain[offset / bytes_per_cluster]) +
                          in_cluster / FAT32_SECTOR_SIZE;
        uint32_t at = in_cluster % FAT32_SECTOR_SIZE;
        uint32_t n = FAT32_SECTOR_SIZE - at;

        if (n > length)
            n = length;
        if (!writing) {
            if (wb_sector == sector) {          /* the newest copy is waiting to be written */
                memcpy(data, wb_buffer + at, n);
            } else {
                if (load_sector(sector) != 0)
                    return 1;
                memcpy(data, io_buffer + at, n);
            }
        } else {
            if (wb_sector != sector) {
                if (flush_wb() != 0)
                    return 1;
                wb_sector = STORAGE_PICO_NO_SECTOR;
                if (n < FAT32_SECTOR_SIZE &&
                    retry_io(0, sector, wb_buffer) != FAT32_OK)   /* keep the rest of it */
                    return 1;
                wb_sector = sector;
            }
            memcpy(wb_buffer + at, data, n);
            wb_dirty = 1;
            if (io_sector == sector)            /* the read buffer is now out of date */
                io_sector = STORAGE_PICO_NO_SECTOR;
        }
        offset += n;
        data += n;
        length -= n;
    }
    return 0;
}

/* Start the SD card driver and wait a few seconds for the card to be mounted. */
int storage_mount(void)
{
    unsigned waited;

    fat32_init();
    for (waited = 0; waited < STORAGE_PICO_MOUNT_WAIT_MS; waited += 50) {
        /* fat32_is_ready() mounts the card and keeps the driver's own status
         * in step; calling fat32_mount() directly leaves that status stale. */
        if (fat32_is_ready())
            return 0;
        last_error = fat32_get_status();
        sleep_ms(50);
    }
    return fat32_is_ready() ? 0 : 1;
}

hp_store_t *storage_open(const char *path, int writable)
{
    struct hp_store *s = free_handle();

    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof *s);
    last_error = fat32_open(&s->file, path);
    if (last_error != FAT32_OK)
        return NULL;
    s->in_use = 1;
    s->writable = (uint8_t)(writable != 0);
    (void)sync_all();
    build_chain(s);
    return s;
}

hp_store_t *storage_create(const char *path)
{
    struct hp_store *s = free_handle();

    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof *s);
    (void)fat32_delete(path);                   /* start empty; ignore "not found" */
    last_error = fat32_create(&s->file, path);
    if (last_error != FAT32_OK)
        return NULL;
    s->in_use = 1;
    s->writable = 1;
    (void)sync_all();
    return s;
}

/* Allocate all the clusters a file needs for the given size at once. The driver's own
 * write walks the whole cluster chain from the start (several times) for every call, so
 * growing a 24 MB file through it in small steps costs millions of SD reads; here the
 * end of the chain is known and each new cluster is linked to it directly. */
int storage_reserve(hp_store_t *store, uint32_t bytes)
{
    fat32_file_t *file;
    uint32_t have, need, last, i;

    if (store == NULL || !store->in_use || !store->writable)
        return 1;
    file = &store->file;
    have = fat32_size(file) == 0 ? 1u : (fat32_size(file) + bytes_per_cluster - 1) / bytes_per_cluster;
    need = (bytes + bytes_per_cluster - 1) / bytes_per_cluster;
    if (bytes <= fat32_size(file) || file->start_cluster < 2)
        return bytes <= fat32_size(file) ? 0 : 1;
    if (sync_all() != 0)
        return 1;
    if (find_last_cluster(file->start_cluster, have, &last) != FAT32_OK)
        return 1;
    for (i = have; i < need; i++) {
        uint32_t next = 0;

        if (allocate_and_link_cluster(last, &next) != FAT32_OK) {
            (void)sync_all();
            return 1;
        }
        last = next;
    }
    file->file_size = bytes;
    if (file->dir_entry_sector && file->dir_entry_offset < FAT32_SECTOR_SIZE &&
        read_sector(file->dir_entry_sector, sector_buffer) == FAT32_OK) {
        ((fat32_dir_entry_t *)(sector_buffer + file->dir_entry_offset))->file_size = bytes;
        (void)write_sector(file->dir_entry_sector, sector_buffer);
    }
    (void)sync_all();
    build_chain(store);
    return 0;
}

int storage_truncate(hp_store_t *store, uint32_t bytes)
{
    fat32_file_t *file;
    uint32_t have, keep;

    if (store == NULL || !store->in_use || !store->writable)
        return 1;
    file = &store->file;
    if (bytes > fat32_size(file))
        return 1;
    if (bytes == fat32_size(file))
        return 0;
    if (sync_all() != 0)
        return 1;
    keep = bytes == 0 ? 1u : (bytes + bytes_per_cluster - 1) / bytes_per_cluster;
    have = (fat32_size(file) + bytes_per_cluster - 1) / bytes_per_cluster;
    if (keep < have) {                          /* give back the clusters after the last one kept */
        uint32_t last_kept, first_freed;

        if (store->chain == NULL || store->chain_clusters < have)
            return 1;
        last_kept = store->chain[keep - 1];
        first_freed = store->chain[keep];
        if (write_cluster_fat_entry(last_kept, FAT32_FAT_ENTRY_EOC) != FAT32_OK ||
            release_cluster_chain(first_freed) != FAT32_OK)
            return 1;
    }
    file->file_size = bytes;
    if (file->dir_entry_sector && file->dir_entry_offset < FAT32_SECTOR_SIZE &&
        read_sector(file->dir_entry_sector, sector_buffer) == FAT32_OK) {
        ((fat32_dir_entry_t *)(sector_buffer + file->dir_entry_offset))->file_size = bytes;
        (void)write_sector(file->dir_entry_sector, sector_buffer);
    }
    (void)sync_all();
    build_chain(store);
    return 0;
}

static int name_ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name), k = strlen(suffix);

    if (k > n)
        return 0;
    name += n - k;
    while (*suffix != 0) {
        char a = *name++, b = *suffix++;

        if (a >= 'a' && a <= 'z')
            a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z')
            b = (char)(b - 'a' + 'A');
        if (a != b)
            return 0;
    }
    return 1;
}

unsigned storage_list(const char *folder, const char *const *suffixes, unsigned suffix_count,
                      char *names, unsigned name_size, unsigned max)
{
    fat32_file_t dir;
    fat32_entry_t entry;
    char path[96];
    unsigned count = 0, i;
    size_t n;

    snprintf(path, sizeof path, "%s", folder);
    n = strlen(path);
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\'))
        path[--n] = 0;
    (void)sync_all();
    if (fat32_open(&dir, path) != FAT32_OK)
        return 0;
    while (count < max && fat32_dir_read(&dir, &entry) == FAT32_OK && entry.filename[0]) {
        if (entry.attr & FAT32_ATTR_DIRECTORY)
            continue;
        for (i = 0; i < suffix_count; i++)
            if (name_ends_with(entry.filename, suffixes[i]) && strlen(entry.filename) < name_size) {
                strcpy(names + (size_t)count * name_size, entry.filename);
                count++;
                break;
            }
    }
    fat32_close(&dir);
    (void)sync_all();
    return count;
}

int storage_delete(const char *path)
{
    (void)sync_all();
    last_error = fat32_delete(path);
    (void)sync_all();                           /* the driver changed sectors behind our back */
    return last_error != FAT32_OK;
}

void storage_close(hp_store_t *store)
{
    if (store == NULL || !store->in_use)
        return;
    (void)sync_all();                           /* the last waiting sector goes to the card */
    fat32_close(&store->file);                  /* the driver writes as it goes */
    free(store->chain);
    store->chain = NULL;
    store->in_use = 0;
}

uint32_t storage_size(hp_store_t *store)
{
    return fat32_size(&store->file);
}

int storage_read(hp_store_t *store, uint32_t offset, void *buffer, uint32_t length)
{
    size_t got = 0;
    uint32_t size = fat32_size(&store->file);
    uint32_t wanted = length;

    if (offset >= size) {                       /* nothing there: zeros */
        memset(buffer, 0, length);
        return 0;
    }
    if (wanted > size - offset)
        wanted = size - offset;
    if (store->chain != NULL) {
        if (chain_io(store, offset, buffer, wanted, 0) != 0)
            return 1;
    } else {
        if (sync_all() != 0)
            return 1;
        if (fat32_seek(&store->file, offset) != FAT32_OK)
            return 1;
        if ((last_error = fat32_read(&store->file, buffer, wanted, &got)) != FAT32_OK ||
            got != wanted) {
            note_failure('r', 0, (int)last_error);
            return 1;
        }
    }
    if (wanted < length)
        memset((uint8_t *)buffer + wanted, 0, length - wanted);
    return 0;
}

/* Append zero bytes to bring the file to the given size, in large writes: each call
 * of the driver costs several SD transfers (the data sectors, the FAT, the directory
 * entry), so growing a tape four bytes at a time made a busy card refuse writes. */
static int extend_to(hp_store_t *store, uint32_t size)
{
    static const uint8_t zeros[STORAGE_PICO_ZERO_BLOCK];
    uint32_t have = fat32_size(&store->file);
    size_t put;

    while (have < size) {
        uint32_t n = size - have;

        if (n > sizeof zeros)
            n = sizeof zeros;
        if (fat32_seek(&store->file, have) != FAT32_OK ||
            fat32_write(&store->file, zeros, n, &put) != FAT32_OK || put != n)
            return 1;
        have += n;
    }
    return 0;
}

int storage_write(hp_store_t *store, uint32_t offset, const void *buffer, uint32_t length)
{
    size_t put = 0;
    uint32_t clusters;

    if (!store->writable)
        return 1;
    if (store->chain != NULL && offset + length <= fat32_size(&store->file))
        return chain_io(store, offset, (uint8_t *)(uintptr_t)buffer, length, 1);

    /* the file grows (or has no chain): let the driver allocate clusters. It is
     * extended past the end of this write by a good margin (zeros), so that the
     * many small writes that follow are in place and use the write-behind buffer. */
    if (sync_all() != 0)
        return 1;
    if (offset + length > fat32_size(&store->file)) {
        uint32_t target = (offset + length + STORAGE_PICO_GROW_BYTES - 1) /
                          STORAGE_PICO_GROW_BYTES * STORAGE_PICO_GROW_BYTES;

        if (extend_to(store, target) != 0)
            return 1;
        build_chain(store);
        if (store->chain != NULL)
            return chain_io(store, offset, (uint8_t *)(uintptr_t)buffer, length, 1);
    }
    if (fat32_seek(&store->file, offset) != FAT32_OK)
        return 1;
    if ((last_error = fat32_write(&store->file, buffer, length, &put)) != FAT32_OK ||
        put != length) {
        note_failure('w', 0, (int)last_error);
        return 1;
    }
    clusters = (fat32_size(&store->file) + bytes_per_cluster - 1) / bytes_per_cluster;
    if (clusters != store->chain_clusters)
        build_chain(store);
    return 0;
}
