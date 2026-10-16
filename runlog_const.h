/* runlog_const.h - constants for the run log and the SD speed test (runlog.c). */

#ifndef RUNLOG_CONST_H
#define RUNLOG_CONST_H

#define RUNLOG_FILE          "RUNLOG.TXT"   /* in the system's data folder */
#define RUNLOG_MAX_BYTES     16384u         /* when it is full the log starts again */
#define RUNLOG_LINE_MAX      256u

#define RUNLOG_PROGRESS_MS   120000u        /* a progress line this often during a run */

#define SDTEST_FILE          "SDTEST.TMP"   /* scratch file the write test fills */
#define SDTEST_READ_FILE     "SYSTEM-2883.disc"
#define SDTEST_CHUNK         4096u          /* bytes per transfer */
#define SDTEST_READ_BYTES    (1024u * 1024u)
#define SDTEST_WRITE_BYTES   (256u * 1024u)

#endif
