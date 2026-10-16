/* spare.c - spare copies of the system files (see spare.h).
 *
 * Author: Thomas Dzubin.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "storage.h"
#include "runlog.h"
#include "spare.h"
#include "spare_const.h"

static uint32_t crc_table[256];
static uint8_t  buffer[SPARE_CHUNK];    /* shared by the copy and the check */

static void crc_init(void)
{
    unsigned i, k;

    if (crc_table[1] != 0)
        return;
    for (i = 0; i < 256; i++) {
        uint32_t c = i;

        for (k = 0; k < 8; k++)
            c = (c & 1u) ? (c >> 1) ^ SPARE_CRC_POLY : c >> 1;
        crc_table[i] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    while (length--)
        crc = crc_table[(crc ^ *data++) & 0xFFu] ^ (crc >> 8);
    return crc;
}

/* Build the path of a file in the data folder, with a prefix in front of the name. */
static int make_path(char *path, const char *prefix, const char *name)
{
    return (unsigned)snprintf(path, SPARE_PATH_MAX, "%s%s%s", plat_data_dir(), prefix,
                              name) < SPARE_PATH_MAX;
}

/* The name of file number n of a slot, or of the slot's checksum file when n is
 * SPARE_FILES_MAX. */
static void slot_name(char *out, unsigned slot, unsigned n)
{
    if (n >= SPARE_FILES_MAX)
        snprintf(out, SPARE_NAME_MAX, SPARE_SUMS_FORMAT, slot + 1u);
    else
        snprintf(out, SPARE_NAME_MAX, SPARE_FILE_FORMAT, slot + 1u, n);
}

static int file_exists(const char *prefix, const char *name)
{
    char path[SPARE_PATH_MAX];
    hp_store_t *probe;

    if (!make_path(path, prefix, name) || (probe = storage_open(path, 0)) == NULL)
        return 0;
    storage_close(probe);
    return 1;
}

static void delete_file(const char *prefix, const char *name)
{
    char path[SPARE_PATH_MAX];

    if (file_exists(prefix, name) && make_path(path, prefix, name))
        (void)storage_delete(path);
}

static void report(spare_progress_t progress, const char *verb, const char *name,
                   unsigned percent, unsigned *shown)
{
    char label[SPARE_LABEL_MAX];

    if (progress == NULL || percent == *shown)
        return;
    *shown = percent;
    snprintf(label, sizeof label, "%s %s", verb, name);
    progress(label, percent);
}

/* Copy from to a new file (an old file of that name is replaced), working out the size
 * and CRC of what was read. */
static int copy_file(const char *from, const char *to, const char *name,
                     spare_progress_t progress, uint32_t *size_out, uint32_t *crc_out)
{
    hp_store_t *in, *out;
    uint32_t size, done = 0, crc = 0xFFFFFFFFu;
    unsigned shown = 101;
    int bad = 0;

    in = storage_open(from, 0);
    if (in == NULL)
        return 1;
    out = storage_create(to);
    if (out == NULL) {
        storage_close(in);
        return 1;
    }
    size = storage_size(in);
    /* One pass for the clusters. Without it the file grows in steps, ends up longer than
     * the original and the copy takes over an hour, so a failure here stops the copy. */
    if (storage_reserve(out, size) != 0) {
        storage_close(in);
        storage_close(out);
        return 1;
    }
    while (done < size && !bad) {
        uint32_t n = size - done < SPARE_CHUNK ? size - done : SPARE_CHUNK;

        bad = storage_read(in, done, buffer, n) || storage_write(out, done, buffer, n);
        crc = crc_update(crc, buffer, n);
        done += n;
        report(progress, "COPY", name, size ? (unsigned)((uint64_t)done * 100u / size) : 100u,
               &shown);
    }
    storage_close(in);
    storage_close(out);
    *size_out = size;
    *crc_out = ~crc;
    return bad;
}

/* Read a file again and check its size and CRC. Returns 0 if they match. */
static int check_file(const char *path, const char *name, uint32_t size, uint32_t want,
                      spare_progress_t progress)
{
    hp_store_t *in = storage_open(path, 0);
    uint32_t done = 0, crc = 0xFFFFFFFFu;
    unsigned shown = 101;

    if (in == NULL)
        return 1;
    if (storage_size(in) != size) {
        storage_close(in);
        return 1;
    }
    while (done < size) {
        uint32_t n = size - done < SPARE_CHUNK ? size - done : SPARE_CHUNK;

        if (storage_read(in, done, buffer, n) != 0) {
            storage_close(in);
            return 1;
        }
        crc = crc_update(crc, buffer, n);
        done += n;
        report(progress, "CHECK", name, size ? (unsigned)((uint64_t)done * 100u / size) : 100u,
               &shown);
    }
    storage_close(in);
    return ~crc != want;
}

/* The checksum file: "SEQ n", then one line per file, "name size crc" (crc in hex). */
static int sums_write(unsigned slot, uint32_t seq, const char *const *names,
                      unsigned count, const uint32_t *sizes, const uint32_t *crcs)
{
    char path[SPARE_PATH_MAX], text[SPARE_SUMS_MAX], file[SPARE_NAME_MAX];
    hp_store_t *out;
    unsigned i, length;
    int n, bad;

    n = snprintf(text, sizeof text, "%s %lu\n", SPARE_SEQ_WORD, (unsigned long)seq);
    if (n < 0 || (unsigned)n >= sizeof text)
        return 1;
    length = (unsigned)n;
    for (i = 0; i < count; i++) {
        n = snprintf(text + length, sizeof text - length, "%s %lu %08lX\n", names[i],
                     (unsigned long)sizes[i], (unsigned long)crcs[i]);
        if (n < 0 || (unsigned)n >= sizeof text - length)
            return 1;
        length += (unsigned)n;
    }
    slot_name(file, slot, SPARE_FILES_MAX);
    if (!make_path(path, "", file) || (out = storage_create(path)) == NULL)
        return 1;
    bad = storage_write(out, 0, text, length);
    storage_close(out);
    return bad;
}

/* Read a run of digits (decimal or hex) at *p; skips leading spaces. Returns 0 if none. */
static int read_number(const char **p, unsigned base, uint32_t *value)
{
    const char *q = *p;
    uint32_t v = 0;
    unsigned digits = 0;

    while (*q == ' ')
        q++;
    for (;; q++, digits++) {
        unsigned d;

        if (*q >= '0' && *q <= '9')
            d = (unsigned)(*q - '0');
        else if (base == 16 && *q >= 'A' && *q <= 'F')
            d = (unsigned)(*q - 'A') + 10u;
        else if (base == 16 && *q >= 'a' && *q <= 'f')
            d = (unsigned)(*q - 'a') + 10u;
        else
            break;
        v = v * base + d;
    }
    *p = q;
    *value = v;
    return digits != 0;
}

static void skip_line_ends(const char **line)
{
    while (**line == 10 || **line == 13)        /* line feed, carriage return */
        (*line)++;
}

static int sums_read(unsigned slot, uint32_t *seq, const char *const *names,
                     unsigned count, uint32_t *sizes, uint32_t *crcs)
{
    char path[SPARE_PATH_MAX], text[SPARE_SUMS_MAX + 1], file[SPARE_NAME_MAX];
    hp_store_t *in;
    uint32_t length;
    unsigned i;
    const char *line;
    size_t n;

    slot_name(file, slot, SPARE_FILES_MAX);
    if (!make_path(path, "", file) || (in = storage_open(path, 0)) == NULL)
        return 1;
    length = storage_size(in);
    if (length > SPARE_SUMS_MAX)            /* a small file is padded with zeros on the card */
        length = SPARE_SUMS_MAX;
    if (storage_read(in, 0, text, length) != 0) {
        storage_close(in);
        return 1;
    }
    storage_close(in);
    text[length] = 0;                       /* the text ends at the first zero byte */
    line = text;
    n = strlen(SPARE_SEQ_WORD);
    if (strncmp(line, SPARE_SEQ_WORD, n) != 0 || line[n] != ' ')
        return 1;
    line += n;
    if (!read_number(&line, 10, seq))
        return 1;
    skip_line_ends(&line);
    for (i = 0; i < count; i++) {
        n = strlen(names[i]);
        if (strncmp(line, names[i], n) != 0 || line[n] != ' ')
            return 1;
        line += n;
        if (!read_number(&line, 10, &sizes[i]) || !read_number(&line, 16, &crcs[i]))
            return 1;
        skip_line_ends(&line);
    }
    return 0;
}

/* What one slot holds: 1 if it is complete (checksum file read, every file there with
 * the size the checksum file says), else 0. Fills in the sequence, sizes and CRCs. */
static int slot_valid(unsigned slot, const char *const *names, unsigned count,
                      uint32_t *seq, uint32_t *sizes, uint32_t *crcs)
{
    unsigned i;

    if (sums_read(slot, seq, names, count, sizes, crcs) != 0)
        return 0;
    for (i = 0; i < count; i++) {
        char path[SPARE_PATH_MAX], file[SPARE_NAME_MAX];
        hp_store_t *probe;
        uint32_t have;

        slot_name(file, slot, i);
        if (!make_path(path, "", file) || (probe = storage_open(path, 0)) == NULL)
            return 0;
        have = storage_size(probe);
        storage_close(probe);
        if (have != sizes[i])
            return 0;
    }
    return 1;
}

int spare_state(const char *const *names, unsigned count)
{
    uint32_t seq, sizes[SPARE_FILES_MAX], crcs[SPARE_FILES_MAX];
    unsigned slot, i;
    char file[SPARE_NAME_MAX];
    int any = 0;

    if (count > SPARE_FILES_MAX)
        return SPARE_NONE;
    for (slot = 0; slot < SPARE_SLOTS; slot++) {
        if (slot_valid(slot, names, count, &seq, sizes, crcs))
            return SPARE_COMPLETE;
        slot_name(file, slot, SPARE_FILES_MAX);
        any |= file_exists("", file);
        for (i = 0; i < count; i++) {
            slot_name(file, slot, i);
            any |= file_exists("", file);
        }
    }
    return any ? SPARE_PARTIAL : SPARE_NONE;
}

int spare_old_present(const char *const *names, unsigned count)
{
    uint32_t seq, sizes[SPARE_FILES_MAX], crcs[SPARE_FILES_MAX];

    if (count > SPARE_FILES_MAX)
        return 0;
    return slot_valid(0, names, count, &seq, sizes, crcs) &&
           slot_valid(1, names, count, &seq, sizes, crcs);
}

int spare_session_get(void)
{
    char path[SPARE_PATH_MAX];
    hp_store_t *in;
    char c = 0;

    if (!make_path(path, "", SPARE_STATE_FILE) || (in = storage_open(path, 0)) == NULL)
        return 0;
    if (storage_size(in) < 1 || storage_read(in, 0, &c, 1) != 0)
        c = 0;
    storage_close(in);
    return c == SPARE_STATE_RUNNING || c == SPARE_STATE_CLEAN ? c : 0;
}

void spare_session_set(int state)
{
    char path[SPARE_PATH_MAX];
    hp_store_t *out;
    char c = (char)state;

    if (!make_path(path, "", SPARE_STATE_FILE) || (out = storage_create(path)) == NULL)
        return;
    (void)storage_write(out, 0, &c, 1);
    storage_close(out);
}

/* Remove everything a slot holds (its checksum file first, so it is not valid any more). */
static void clear_slot(unsigned slot, unsigned count)
{
    char file[SPARE_NAME_MAX];
    unsigned i;

    slot_name(file, slot, SPARE_FILES_MAX);
    delete_file("", file);
    for (i = 0; i < count; i++) {
        slot_name(file, slot, i);
        delete_file("", file);
    }
}

static const char *do_save(const char *const *names, unsigned count,
                           spare_progress_t progress, char *detail, unsigned size)
{
    uint32_t seq[SPARE_SLOTS] = { 0, 0 }, sizes[SPARE_SLOTS][SPARE_FILES_MAX];
    uint32_t crcs[SPARE_SLOTS][SPARE_FILES_MAX], new_sizes[SPARE_FILES_MAX];
    uint32_t new_crcs[SPARE_FILES_MAX];
    char from[SPARE_PATH_MAX], to[SPARE_PATH_MAX], file[SPARE_NAME_MAX];
    unsigned slot, target, i;
    const char *why = NULL;

    for (slot = 0; slot < SPARE_SLOTS; slot++)
        if (!slot_valid(slot, names, count, &seq[slot], sizes[slot], crcs[slot]))
            seq[slot] = 0;
    target = seq[0] <= seq[1] ? 0 : 1;          /* the empty or older slot */

    clear_slot(target, count);
    for (i = 0; i < count && why == NULL; i++) {
        slot_name(file, target, i);
        if (!make_path(from, "", names[i]) || !make_path(to, "", file) ||
            copy_file(from, to, names[i], progress, &new_sizes[i], &new_crcs[i]))
            why = "COPY FAILED";
        else if (check_file(to, names[i], new_sizes[i], new_crcs[i], progress))
            why = "COPY DID NOT READ BACK";
        if (why != NULL)
            snprintf(detail, size, "%s", names[i]);
    }
    if (why == NULL &&
        sums_write(target, (seq[0] > seq[1] ? seq[0] : seq[1]) + 1u, names, count,
                   new_sizes, new_crcs))
        why = "CANNOT WRITE CHECKSUMS";
    if (why != NULL) {
        clear_slot(target, count);
        return why;
    }
    return NULL;
}

static const char *do_restore(const char *const *names, unsigned count, int older,
                              const char *stale, spare_progress_t progress, char *detail,
                              unsigned size)
{
    uint32_t seq[SPARE_SLOTS] = { 0, 0 }, sizes[SPARE_SLOTS][SPARE_FILES_MAX];
    uint32_t crcs[SPARE_SLOTS][SPARE_FILES_MAX];
    char from[SPARE_PATH_MAX], to[SPARE_PATH_MAX], file[SPARE_NAME_MAX];
    unsigned slot, source, i;
    uint32_t got_size, got_crc;

    for (slot = 0; slot < SPARE_SLOTS; slot++)
        if (!slot_valid(slot, names, count, &seq[slot], sizes[slot], crcs[slot]))
            seq[slot] = 0;
    if (seq[0] == 0 && seq[1] == 0)
        return "NO SPARE COPY TO RESTORE";
    if (older) {
        if (seq[0] == 0 || seq[1] == 0)
            return "NO OLDER SPARES";
        source = seq[0] < seq[1] ? 0 : 1;
    } else {
        source = seq[0] > seq[1] ? 0 : 1;
    }

    /* the spare must read back correctly BEFORE the live files are touched */
    for (i = 0; i < count; i++) {
        slot_name(file, source, i);
        if (!make_path(from, "", file) ||
            check_file(from, names[i], sizes[source][i], crcs[source][i], progress)) {
            snprintf(detail, size, "%s", names[i]);
            return "SPARE IS DAMAGED. LIVE FILES KEPT.";
        }
    }
    for (i = 0; i < count; i++) {
        slot_name(file, source, i);
        if (!make_path(from, "", file) || !make_path(to, "", names[i]) ||
            copy_file(from, to, names[i], progress, &got_size, &got_crc) ||
            got_size != sizes[source][i] || got_crc != crcs[source][i] ||
            check_file(to, names[i], got_size, got_crc, progress)) {
            snprintf(detail, size, "%s", names[i]);
            return "RESTORE FAILED. SPARE IS SAFE. TRY AGAIN.";
        }
    }
    if (stale != NULL)
        delete_file("", stale);
    return NULL;
}

const char *spare_copy(const char *const *names, unsigned count, int mode,
                       const char *stale, spare_progress_t progress,
                       char *detail, unsigned size)
{
    const char *what = mode == SPARE_MODE_SAVE ? "SAVE" : "RESTORE";
    char line[96];
    const char *why;
    uint32_t t0;
    unsigned i;

    detail[0] = 0;
    if (count > SPARE_FILES_MAX)
        return "TOO MANY FILES";
    if (storage_mount() != 0)
        return "NO SD CARD";
    crc_init();
    if (mode == SPARE_MODE_SAVE)                /* every live file must be there first */
        for (i = 0; i < count; i++)
            if (!file_exists("", names[i])) {
                snprintf(detail, size, "%s", names[i]);
                return "FILE NOT FOUND";
            }

    t0 = plat_now_ms();
    if (mode == SPARE_MODE_SAVE)
        why = do_save(names, count, progress, detail, size);
    else
        why = do_restore(names, count, mode == SPARE_MODE_RESTORE_OLD, stale, progress,
                         detail, size);
    if (why != NULL) {
        snprintf(line, sizeof line, "SPARE %s FAILED: %s (%s)", what, why, detail);
        runlog_append(line);
        return why;
    }
    snprintf(line, sizeof line, "SPARE %s%s done, %u files, %u s", what,
             mode == SPARE_MODE_RESTORE_OLD ? " (older)" : "", count,
             (unsigned)((plat_now_ms() - t0) / 1000u));
    runlog_append(line);
    return NULL;
}
