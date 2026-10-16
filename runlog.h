/* runlog.h - a log file on the card and a quick SD speed test.
 *
 * Author: Thomas Dzubin.
 *
 * Portable: it uses only storage.h and platform.h. Long runs (a HIBERNATE takes
 * half an hour on the PicoCalc) write their figures to RUNLOG.TXT in the system's
 * folder, so nobody has to watch the screen; the speed test measures the card in
 * a few seconds.
 */

#ifndef RUNLOG_H
#define RUNLOG_H

#include <stdint.h>

/* Add one line of text to the log (the file is created on first use). */
void runlog_append(const char *line);

/* Read 1 MB from the system disc image and write 256 KB to a scratch file, timing
 * both. Fills text with a one-line result (at most 40 characters) and logs it. */
void sdtest_run(char *text, unsigned size);

#endif
