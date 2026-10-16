/* spare.h - spare copies of the system files, and a note of how the last run ended.
 *
 * Author: Thomas Dzubin.
 *
 * The disc and the HIBERNATE tape change with every run, and a bad shutdown leaves
 * "SYSTEM NOT SLEPT". A spare is a copy of a file kept in the same folder under the
 * name SPARE1- or SPARE2- plus the live name. The two slots are used in turn, so one
 * always holds the newest spare and the other the one before it. Every slot has a
 * checksum file (SUMS.TXT with the prefix) holding a sequence number and each file's
 * size and CRC-32; it is written last, so a slot is valid only if its copy finished
 * and read back correctly. A save writes into the older slot and never touches the
 * newest. A restore checks the spare against its checksums before it touches the live
 * files. Nothing is renamed.
 *
 * Portable: it uses only storage.h and platform.h.
 */

#ifndef SPARE_H
#define SPARE_H

/* Called now and then while a file is copied or checked: a short text such as
 * "COPY name" and how far along it is (0 to 100). May be NULL. */
typedef void (*spare_progress_t)(const char *what, unsigned percent);

#define SPARE_MODE_SAVE         0   /* live files to the older slot (it becomes the newest) */
#define SPARE_MODE_RESTORE      1   /* newest spares to the live files */
#define SPARE_MODE_RESTORE_OLD  2   /* the spares before those, to the live files */

/* Do one of the above for the named live files. Nothing is touched unless every
 * source file exists, and a restore also needs the checksums to match. After a
 * restore, the file named by stale (the SLEEP tape, which no longer matches the
 * disc) is deleted if it exists; stale may be NULL. Returns NULL on success,
 * otherwise a short message for the menu, and puts the file name or a note in
 * detail (at most SPARE_DETAIL_MAX characters). Every call writes a line to the
 * run log. */
const char *spare_copy(const char *const *names, unsigned count, int mode,
                       const char *stale, spare_progress_t progress,
                       char *detail, unsigned size);

/* What is on the card (the card must be mounted). */
#define SPARE_NONE      0   /* no spare files at all */
#define SPARE_PARTIAL   1   /* there are spare files but no slot is complete */
#define SPARE_COMPLETE  2   /* at least one slot is complete (sizes match its checksum file) */
int spare_state(const char *const *names, unsigned count);

/* Whether both slots are complete, so there is an older spare (for the menu). */
int spare_old_present(const char *const *names, unsigned count);

/* A note of how the last run ended: SPARE_STATE_RUNNING (the machine was started
 * and never shut down cleanly), SPARE_STATE_CLEAN, or 0 if there is no note. */
int  spare_session_get(void);
void spare_session_set(int state);

#endif
