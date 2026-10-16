/*
 * hp2000access_const.h - every constant of HP 2000 Access Time-Shared BASIC:
 * version, colours, limits, file names, key assignments, screen positions and the
 * scripts that answer the system's questions. hp2000access.c holds only code, so
 * a new constant or a moved row goes here.
 *
 * Author: Thomas Dzubin
 */
#ifndef HP2000ACCESS_CONST_H
#define HP2000ACCESS_CONST_H

/* Shown on the splash screen; the firmware build reads it from this line, so
 * keep it in the form  #define VERSION "V1.00A"  on one line. */
#define VERSION "V1.00A"

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
#define SCREEN_COLS   (WIDTH / 8)       /* text columns on the LCD: 40 on the PicoCalc, 80 on the desktop */
#define TCOLS         40
#define STATUS_ROW    31                /* below the 31 terminal rows */

/* ------------------------------------------------------------------ */
/*  Menu rows                                                           */
/* ------------------------------------------------------------------ */
#define PROGRAM_TITLE      "HP 2000 ACCESS TIME-SHARED BASIC"   /* heading of the title screen, the menu and the first-start screen */
#define TITLE_ROW_START   14   /* title screen: press any key */
#define TITLE_ROW_EXIT    16   /* title screen: how to leave */
#define MENU_ROW_TITLE     4
#define MENU_ROW_VERSION   6
#define MENU_ROW_AUTHOR    8
#define MENU_COL_LEFT      3   /* the option lines start at this column */
#define MENU_ROW_BOOT     14
#define MENU_ROW_RELOAD   15
#define MENU_ROW_RESTORE  16
#define MENU_ROW_SDTEST   17
#define MENU_ROW_SPARE_SAVE 18
#define MENU_ROW_SPARE_BACK 19
#define MENU_ROW_SPARE_OLD  20
#define MENU_ROW_EXIT     21
#define MENU_ROW_MESSAGE  24
#define MENU_ROW_MESSAGE2 25
#define MENU_ROW_MESSAGE3 26
#define MENU_ROW_SD       27   /* count and text of failed SD tries, when there were some */
#define MENU_ROW_PRESS_KEY 28
#define MENU_ROW_WAIT1    13   /* the first-start spare save: do not touch anything */
#define MENU_ROW_WAIT2    14

/* ------------------------------------------------------------------ */
/*  The machine: two HP 2100 computers with 32K words each, as the      */
/*  Access kit configures them. The System Processor (SP) runs the BASIC */
/*  interpreter (it has the floating point firmware) and owns the disc,  */
/*  the tape and the operator console; the I/O Processor (IOP, with the  */
/*  2000 IOP instruction set) runs the terminal multiplexer. The 12875A  */
/*  Processor Interconnect joins them. Select codes are those of the     */
/*  HP 2000 Access Operator's Manual.                                    */
/* ------------------------------------------------------------------ */
#define SP_MEMORY_WORDS   32768u
#define IOP_MEMORY_WORDS  32768u

#define SC_INTERCONNECT_SP  010         /* SP: inbound card; the outbound card is 011 */
#define SC_TELEPRINTER      012         /* SP: operator console */
#define SC_TIME_BASE_SP     013
#define SC_DISC             014         /* SP: data channel; command channel is 015 */
#define SC_TAPE             016         /* SP: data channel; command channel is 017 */
#define SC_TIME_BASE_IOP    010
#define SC_MUX_LOWER        011         /* IOP: lower data card; upper is 012 */
#define SC_MUX_CONTROL      013         /* IOP */
#define SC_INTERCONNECT_IOP 014         /* IOP: inbound card; the outbound card is 015 */

#define PAIR_RUN_LIMIT   2000000000u    /* instructions allowed for each load step */
#define PAIR_CHUNK            1000u
#define IOP_HEAD_START   3000000u       /* the IOP must finish starting up before the SP
                                         * sends its first command */
#define MCP_START        02000          /* start address of the tape-loaded programs */
#define MCP_S_REGISTER   (SC_TAPE << 6) /* they want the tape select code in S bits 11-6 */
#define IOP_START        2              /* start address of the IOP program */

#define HALT_TAPE_TO_AUTO  0102044      /* T register: "CHANGE MAG TAPE TO AUTO" */
#define HALT_DONE          0102077      /* T register: loaded or hibernated */

/* Terminals: 0 is the operator console, 1 the on-screen user terminal (multiplexer
 * channel 0), 2 the USB serial user terminal (channel 1). */
#define TERMINAL_COUNT   3
#define USER_LINES       2

/* ------------------------------------------------------------------ */
/*  Files in the data folder (platform.h: plat_data_dir)                */
/* ------------------------------------------------------------------ */
#define FILE_DISC        "SYSTEM-2883.disc"
#define FILE_TAPE        "HIBERNATE-2883.tape"
#define FILE_SLEEP_TAPE  "SLEEP-2883.tape"             /* changes since the last HIBERNATE */
#define FILE_IOP_TAPE    "IOP-2100.tape"                /* cross-loader and IOP program */
#define FILE_MCP_TAPE    "22687-11002_Rev-1812.tape"    /* Master Control Program for the 2100 */
/* Files the spare copies cover (spare.c): the disc and the HIBERNATE tape, a matched
 * pair. Each is kept as SPARE- plus its name. Restoring empties the SLEEP tape,
 * which no longer matches the restored disc. */
#define SPARE_FILES      { FILE_DISC, FILE_TAPE }
#define SPARE_FILE_COUNT 2
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
#define KEY_SHUTDOWN          KEY_F4    /* shut down: asks HIBERNATE, SLEEP or SLEEP + tape */
#define KEY_BREAK_ALT         KEY_F5    /* BREAK, if the BRK key is not at hand */

/* ------------------------------------------------------------------ */
/*  Import and capture (hostfile.c)                                      */
/* ------------------------------------------------------------------ */
#define KEY_IMPORT_FILE       KEY_F6    /* type a file from the card into the user terminal */
#define KEY_CAPTURE_OUTPUT    KEY_F7    /* start or stop copying the user terminal's output to a file */
#define IMPORT_SUFFIXES       { ".TXT", ".BAS" }
#define IMPORT_SUFFIX_COUNT   2
#define IMPORT_SKIP_NAMES     { "RUNLOG.TXT", "SP1SUMS.TXT", "SP2SUMS.TXT" }  /* files of the program itself */
#define IMPORT_SKIP_COUNT     3
#define IMPORT_FILES_MAX      40        /* names the list can hold */
#define IMPORT_NAME_MAX       40
#define IMPORT_QUEUE_LIMIT    4         /* characters waiting in the terminal's queue at most */
#define EXPORT_FILE           "EXPORT.TXT"
#define PICKER_ROW_TITLE      1
#define PICKER_ROW_FIRST      4
#define PICKER_ROWS           22        /* names shown at once */
#define PICKER_ROW_HELP       28
#define CHAR_DELETE           0x5F      /* TSB's character delete: underscore */
#define CHAR_CR               0x0D
#define KEY_QUEUE_SIZE        64u

/* ------------------------------------------------------------------ */
/*  Timing (milliseconds of real time)                                  */
/* ------------------------------------------------------------------ */
#define RUN_CHUNK            4000u      /* instructions per CPU between checks of the clock */
#define KEY_POLL_MS            40       /* the I2C keyboard read costs about 3 ms, so not too often */
#define DRAW_MS                60
#define CALIBRATE_MS          250
#define POLL_MS                 3       /* pause between keyboard polls in menus */
#define REBOOT_MS             300       /* time the BOOTSEL notice stays visible */
#define SPINNER_CHARS        "|/-+"     /* turns while the machine runs: it is alive */
#define SPINNER_MS            250
#define TAPE_BUSY_MS         5000       /* the tape counts as busy this long after it moved */
#define NOTE_MS              1500       /* how long a status line message stays */

/* ------------------------------------------------------------------ */
/*  Scripts that answer the system's questions for the operator          */
/* ------------------------------------------------------------------ */
/* The tape cross-loader asks the operator to start the IOP's loader; the program
 * has already done that, so only RETURN is needed. */
#define IOP_LOAD_STEPS \
    { { "START IOP PROTECTED LOADER.  PRESS RETURN ", "\r" }, \
      { NULL, NULL } }

/* After a start from the disc. The system then asks DATE? and TIME?. There is no
 * clock to read, so the operator types the date and time on the menu first and the
 * program supplies them at once: a start that waits for a person at those two
 * questions makes the I/O Processor give up on the System Processor, and the user
 * lines then never answer. */
#define BOOT_STEPS \
    { { "LOAD OR DUMP COMMANDS? ", "NO\r" }, \
      { NULL, NULL } }
#define DATE_QUESTION    "DATE?  "
#define TIME_QUESTION    "TIME?  "
#define SURE_QUESTION    "ARE YOU SURE THAT'S TODAY'S DATE? "     /* asked if the date jumped */
#define SURE_ANSWER      "YES\r"
#define RUN_STEPS_MAX    16             /* steps in the script, with the end marker */

/* The date (day of the year / two-digit year) and time (hour and minute) asked for
 * on the menu; these are the first suggestions. */
#define DEFAULT_DATE     "281/26"
#define DEFAULT_TIME     "1200"
#define DATE_LENGTH      6
#define TIME_LENGTH      4
#define MENU_ROW_DATE    22
#define MENU_ROW_TIME    23
#define MENU_ROW_ASK     25

/* HIBERNATE the system to magnetic tape (the command itself is typed first). The
 * tape is mounted and on line already. The last halt (HLT 77) ends it. */
#define SHUTDOWN_FIRST   "HIBERNATE\r"
#define SHUTDOWN_STEPS \
    { { "MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r" }, \
      { "VERIFY? ", "YES\r" }, \
      { "SYSTEM DUMP OR RELOAD? ", "NO\r" }, \
      { NULL, NULL } }

/* F4 asks which shutdown, on the status line. */
#define SHUTDOWN_ASK_TEXT  "H HIBERNATE  S SLEEP  D SLEEP+TAPE ESC"

/* SLEEP the system onto the disc (the command itself is typed first). The disc is
 * then up to date, so the next start is a plain B. The tape question is answered NO. */
#define SLEEP_FIRST      "SLEEP\r"
#define SLEEP_STEPS \
    { { "SYSTEM DUMP OR RELOAD? ", "NO\r" }, \
      { NULL, NULL } }

/* SLEEP and also dump the library entries changed since the last HIBERNATE to the
 * SLEEP tape (a second tape file, mounted when the reel is asked for). */
#define SLEEP_DUMP_STEPS \
    { { "SYSTEM DUMP OR RELOAD? ", "DUMP\r" }, \
      { "MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r" }, \
      { "VERIFY? ", "YES\r" }, \
      { "SYSTEM DUMP OR RELOAD? ", "NO\r" }, \
      { NULL, NULL } }

/* Reload the system from the HIBERNATE tape with the Master Control Program. When
 * it asks for the reel the program puts the HIBERNATE tape on the drive. The
 * operator still answers the date and time questions that follow. */
#define RELOAD_STEPS \
    { { "2754? ", "YES\r" }, \
      { "LOAD WHICH MODULE?  ", "2883\r" }, \
      { "SYSTEM GENERATION? ", "NO\r" }, \
      { "MAG TAPE RELOAD? ", "YES\r" }, \
      { "MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r" }, \
      { "CONFIGURATION OPTIONS? ", "NO\r" }, \
      { "LOAD OR DUMP COMMANDS? ", "NO\r" }, \
      { NULL, NULL } }

/* The same reload, then RESTORE of everything on the SLEEP tape: the entries it holds
 * replace the HIBERNATE tape's. The second reel asked for is the SLEEP tape. */
#define RELOAD_RESTORE_STEPS \
    { { "2754? ", "YES\r" }, \
      { "LOAD WHICH MODULE?  ", "2883\r" }, \
      { "SYSTEM GENERATION? ", "NO\r" }, \
      { "MAG TAPE RELOAD? ", "YES\r" }, \
      { "MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r" }, \
      { "CONFIGURATION OPTIONS? ", "NO\r" }, \
      { "LOAD OR DUMP COMMANDS? ", "RESTORE\r" }, \
      { "WITH 'END'", "ALL\r" }, \
      { "MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r" }, \
      { "DO YOU TAKE RESPONSIBILITY?", "YES\r" }, \
      { "LOAD OR DUMP COMMANDS? ", "NO\r" }, \
      { NULL, NULL } }
#define MOUNT_REEL_TEXT  "MOUNT REEL NUMBER"

/* When this appears on the operator console the system is up and the user lines
 * can be connected. */
#define SYSTEM_UP_TEXT   "HP22687A"

#endif /* HP2000ACCESS_CONST_H */
