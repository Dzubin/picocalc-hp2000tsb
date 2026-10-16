/*
 * hp2000e_const.h - every constant of HP 2000E Time-Shared BASIC: version, colours,
 * limits, file names, key assignments, screen positions and the scripts that
 * answer the system's questions. hp2000e.c holds only code, so a new constant or
 * a moved row goes here.
 *
 * Author: Thomas Dzubin
 */
#ifndef HP2000E_CONST_H
#define HP2000E_CONST_H

/* Shown on the splash screen; the firmware build reads it from this line, so
 * keep it in the form  #define VERSION "V0.01A"  on one line. */
#define VERSION "V0.01A"

/* ------------------------------------------------------------------ */
/*  Colours (full-strength only: dim greys are hard to read on the LCD) */
/* ------------------------------------------------------------------ */
#define COL_BG      RGB(  0,   0,   0)
#define COL_WHITE   RGB(235, 235, 235)
#define COL_RED     RGB(255,  90,  90)
#define COL_GREEN   RGB( 90, 235, 130)
#define COL_YELLOW  RGB(255, 225,  70)
#define COL_CYAN    RGB(120, 225, 255)

#define COL_OPERATOR  COL_GREEN         /* operator console text */
#define COL_USER      COL_WHITE         /* user terminal text */

/* ------------------------------------------------------------------ */
/*  Screen geometry: the LCD is a 40 x 32 grid of 8 x 10 pixel cells    */
/* ------------------------------------------------------------------ */
#define TCOLS         40
#define STATUS_ROW    31                /* below the 31 terminal rows */

/* ------------------------------------------------------------------ */
/*  Menu rows                                                           */
/* ------------------------------------------------------------------ */
#define MENU_ROW_TITLE     4
#define MENU_ROW_VERSION   6
#define MENU_ROW_AUTHOR    8
#define MENU_ROW_BOOT     16
#define MENU_ROW_RELOAD   18
#define MENU_ROW_EXIT     20
#define MENU_ROW_MESSAGE  24
#define MENU_ROW_MESSAGE2 25
#define MENU_ROW_MESSAGE3 26
#define MENU_ROW_PRESS_KEY 28

/* ------------------------------------------------------------------ */
/*  The machine: an HP 2100 with 16K words, as the 2000E kit configures it */
/* ------------------------------------------------------------------ */
#define MEMORY_WORDS     16384u
#define SC_TELEPRINTER   010            /* operator console */
#define SC_DISC          011            /* data channel; command channel is 012 */
#define SC_PAPER_TAPE    013
#define SC_MUX_LOWER     014            /* upper card 015 */
#define SC_MUX_CONTROL   016
#define SC_TIME_BASE     017
#define SC_TAPE          020            /* data channel; command channel is 021 */
#define TAPE_SELECT_CODE_TEXT "20"      /* typed to SLEEP and LOAD */

#define LOADER_RUN_LIMIT 10000000u      /* instructions allowed for a paper tape load */
#define PAPER_TAPE_MAX   (24u * 1024u)  /* biggest loader tape kept in memory */
#define RELOAD_START     02000          /* start address of the loader utility */
#define BOOT_START       2              /* start address of the disc bootstrap */

/* Terminals: 0 is the operator console, 1 the on-screen user terminal (multiplexer
 * channel 0), 2 the USB serial user terminal (channel 1). */
#define TERMINAL_COUNT   3
#define USER_LINES       2

/* ------------------------------------------------------------------ */
/*  Files in the data folder (platform.h: plat_data_dir)                */
/* ------------------------------------------------------------------ */
#define FILE_DISC        "SYSTEM-7900.disc"
#define FILE_TAPE        "SLEEP-7900.tape"
#define FILE_BOOT_TAPE   "24286-60001_Rev-1438.abin"
#define FILE_LOADER_TAPE "24285-60001_Rev-1534.abin"
/* longest path of a file; the desktop build sets a larger value, because there the
 * path starts with the program's own folder */
#ifndef PATH_MAX_LEN
#define PATH_MAX_LEN     96
#endif

/* ------------------------------------------------------------------ */
/*  Keys                                                                */
/* ------------------------------------------------------------------ */
#define KEY_OPERATOR_CONSOLE  KEY_F1    /* show and type on the operator console */
#define KEY_USER_CONSOLE      KEY_F2    /* show and type on the user terminal */
#define KEY_TAPE_ONLINE       KEY_F3    /* the tape drive's ON LINE button */
#define KEY_SHUTDOWN          KEY_F4    /* SLEEP the system to tape and stop */
#define KEY_BREAK_ALT         KEY_F5    /* BREAK, if the BRK key is not at hand */
#define CHAR_DELETE           0x5F      /* TSB's character delete: underscore */
#define CHAR_CR               0x0D
#define KEY_QUEUE_SIZE        64u

/* ------------------------------------------------------------------ */
/*  Timing (milliseconds of real time)                                  */
/* ------------------------------------------------------------------ */
#define RUN_CHUNK            4000u      /* instructions between checks of the clock */
#define KEY_POLL_MS            10
#define DRAW_MS                60
#define CALIBRATE_MS          250
#define POLL_MS                 3       /* pause between keyboard polls in menus */
#define REBOOT_MS             300       /* time the BOOTSEL notice stays visible */
#define NOTE_MS              1500       /* how long a status line message stays */

/* ------------------------------------------------------------------ */
/*  Scripts that answer the system's questions for the operator          */
/* ------------------------------------------------------------------ */
/* SLEEP the system to magnetic tape (typed after "SLEEP" itself). */
#define SHUTDOWN_FIRST   "SLEEP\r"
#define SHUTDOWN_STEPS \
    { { "MAG TAPE SLEEP?", "YES\r" }, \
      { "SELECT CODE?", TAPE_SELECT_CODE_TEXT "\r" }, \
      { "SLEEP COMPLETE", NULL }, \
      { NULL, NULL } }

/* Reload the system from the SLEEP tape with the 2000E loader utility. The
 * operator still answers the date and time questions that follow. */
#define RELOAD_STEPS \
    { { "*", "LOAD," TAPE_SELECT_CODE_TEXT "\r" }, \
      { "NUMBER OF PORTS? ", "16\r" }, \
      { "DATE?", NULL }, \
      { NULL, NULL } }

/* When this appears on the operator console the system is up and the user lines
 * can be connected. */
#define SYSTEM_UP_TEXT   "READY"

#endif /* HP2000E_CONST_H */
