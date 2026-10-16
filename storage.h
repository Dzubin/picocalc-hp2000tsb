/* storage.h - random access to the image files (disc, tape, paper tape).
 *
 * Author: Thomas Dzubin.
 *
 * The emulated devices never touch files directly. They use this small set of
 * functions, so the same device code runs on the desktop (storage_stdio.c) and
 * on the PicoCalc's SD card (storage_pico.c). Offsets and lengths are in bytes.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

typedef struct hp_store hp_store_t;

/* Get the storage ready (mount the SD card). Returns 0 when files can be opened. */
int storage_mount(void);

/* Open an existing image. writable is nonzero to allow storage_write().
 * Returns NULL if it cannot be opened. */
hp_store_t *storage_open(const char *path, int writable);

/* Create (or empty) an image file and open it for reading and writing. */
hp_store_t *storage_create(const char *path);

/* Delete a file (its space is freed). No file may be open. Returns 0 on success.
 * There is deliberately no rename: the FAT32 driver's rename leaves a damaged
 * directory entry (its short name starts with the deleted-entry marker). */
int storage_delete(const char *path);

/* Make a file at least this long in one go, for a file that is about to be written
 * from start to end (a copy). The new bytes are NOT zeroed: whatever was on the card
 * is there until it is written. On the PicoCalc a file that grows a little at a time
 * costs more and more SD transfers as it gets longer, so a big copy reserves first.
 * Returns 0 on success; a nonzero result means nothing changed and plain writes still
 * work, only slower. */
int storage_reserve(hp_store_t *store, uint32_t bytes);

/* Cut a file back to an exact length (bytes must not be more than its size). The
 * space after it is freed. A file that grew is padded with zeros up to a block, so
 * a text file written piece by piece is cut to what was written. Returns 0 on success. */
int storage_truncate(hp_store_t *store, uint32_t bytes);

/* List the files in a folder (the data folder, as plat_data_dir() gives it) whose names
 * end in one of the suffixes (case ignored). The names go one after another in names,
 * name_size bytes each, at most max of them, in the order the card gives them. Returns
 * how many were found. */
unsigned storage_list(const char *folder, const char *const *suffixes, unsigned suffix_count,
                      char *names, unsigned name_size, unsigned max);

/* Short text for why the last storage_mount, storage_open or storage_create
 * failed (for the menu). */
const char *storage_last_error(void);

void     storage_close(hp_store_t *store);
uint32_t storage_size(hp_store_t *store);

/* Return 0 on success, nonzero on error or a short transfer. Reading beyond
 * the end of the image gives zeros for the missing part and returns 0. */
int storage_read(hp_store_t *store, uint32_t offset, void *buffer, uint32_t length);
int storage_write(hp_store_t *store, uint32_t offset, const void *buffer, uint32_t length);

/* How many reads or writes have failed so far, and a short text about the last
 * one (for the operator console). The desktop version never fails. */
unsigned storage_failures(char *text, unsigned size);

/* Time spent in SD transfers (milliseconds) and their number, since the program
 * started, for the run report. The desktop version reports zero. */
void storage_stats(uint32_t *milliseconds, uint32_t *transfers);

#endif
