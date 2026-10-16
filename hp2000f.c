/*
 *  HP 2000F Time-Shared BASIC
 *  ===========================================
 *
 *  Two emulated HP 2100 computers running HP 2000F Time-Shared BASIC (TSB) from
 *  the 1970s, on the ClockworkPi PicoCalc and (as a desktop build) on Windows
 *  and Linux. The System Processor runs the BASIC interpreter and has the disc,
 *  the tape and the operator console; the I/O Processor runs the terminal
 *  multiplexer; a Processor Interconnect joins them. The disc image, the
 *  HIBERNATE tape and the two paper tape programs are files in one folder on the
 *  SD card (see hp2000f_const.h).
 *
 *  This is a separate program from the Access one (hp2000access.c); they share the
 *  CPU and device modules but each can be built on its own.
 *
 *  The program shows a menu, then runs the machine. Two terminals share the
 *  screen, one at a time: the operator console (the system's teleprinter) and a
 *  user terminal (a line of the terminal multiplexer). On the PicoCalc a second
 *  user line is carried over the USB serial link to a terminal program on another
 *  computer.
 *
 *  Controls while the machine runs
 *      F1 / F2 ........ show and type on the operator console / user terminal
 *      F3 ............. the tape drive's ON LINE button
 *      F4 ............. shut down: asks H (HIBERNATE to the tape), S (SLEEP onto the disc)
 *                       or D (SLEEP, and dump the changes to the SLEEP tape)
 *      BRK or F5 ...... BREAK
 *      F6 ............. import: choose a .TXT or .BAS file on the card and type it into
 *                       the user terminal, line by line (F6 again stops it)
 *      F7 ............. start or stop saving what the user terminal prints in EXPORT.TXT
 *      Backspace ...... TSB's character delete (underscore)
 *      CTRL+X ......... TSB's line delete
 *      ~ (SHIFT+`) .... reboot into BOOTSEL (USB drive) mode, on the menu only
 *                       (while the system runs it is an ordinary key, so a
 *                       slip cannot lose the session)
 *
 *  The system asks DATE? and TIME? on the operator console at every start; the
 *  operator gives them on the menu (day of the year and two-digit year, then hour
 *  and minute, for example 281/26 and 1200) and the program types them at the
 *  right moment.
 *
 *  Hardware used (all through the vendored picocalc-text-starter drivers by
 *  Blair Leduc, in picocalc-text-starter-main/): the LCD, the south bridge
 *  keyboard and the SD card. The keyboard is read straight from the south
 *  bridge's FIFO: each call to sb_read_keyboard() returns one 16-bit event,
 *  (state << 8) | code, or 0 when the FIFO is empty.
 *
 *  Layout of the code: this file is the program (menu, screen, keyboard and the
 *  run loop); hp21xx.c and the dev_*.c files are the machine; platform.h and
 *  storage.h are the only ways to the clock, USB and files.
 *
 *  Author: Thomas Dzubin
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lcd.h"          /* WIDTH, HEIGHT, GLYPH_HEIGHT, RGB(), lcd_*()      */
#include "southbridge.h"  /* sb_init(), sb_read_keyboard()                    */
#include "keyboard.h"     /* KEY_* codes, KEY_STATE_* values                  */

#include "platform.h"     /* clock, sleep, USB, BOOTSEL, loader exit          */
#include "storage.h"      /* image files (SD card or host files)              */
#include "console.h"      /* the terminals as the machine sees them           */
#include "hp21xx.h"
#include "hp_dma.h"
#include "hp_pair.h"      /* the two CPUs run side by side                    */
#include "hp_loaders.h"   /* the IOP binary loader and the disc bootstrap     */
#include "dev_ipl.h"
#include "dev_ptr.h"
#include "dev_tty.h"
#include "dev_tbg.h"
#include "dev_dq.h"
#include "dev_mux.h"
#include "dev_tape.h"
#include "terminal.h"
#include "termdraw.h"
#include "autotype.h"
#include "runlog.h"
#include "spare.h"
#include "spare_const.h"
#include "hostfile.h"
#include "runlog_const.h"
#include "hp2000f_const.h"   /* every constant and screen position */

/* ===================================================================== */
/*  Forward declarations (so main() can sit at the top)                   */
/* ===================================================================== */
static void clear_row(int row);
static void put_row(int row, const char *str, uint16_t fg);
static inline uint16_t key_event(void);
static inline uint8_t  ev_state(uint16_t e);
static inline uint8_t  ev_code(uint16_t e);
static inline bool     is_mod_key(uint8_t code);
static void go_bootsel(void);
static void go_loader(void);
static void drain_keys(void);
static uint8_t wait_any_key(void);

static void put_left(int row, const char *str, uint16_t fg);
static void title_screen(void);
static void show_message(const char *message, const char *message2,
                         const char *message3);
static void menu_screen(const char *message, const char *message2,
                        const char *message3);
static bool ask_date_time(void);
static const char *run_machine(bool reload, bool restore);
static const char *start_machine(bool reload, bool restore);
static void stop_machine(void);
static void show_halt_console(const char *halt_text);
static int  load_file(const char *name, uint8_t *buffer, uint32_t max, uint32_t *length);
static bool make_path(char *path, const char *name);
static const char *spare_menu(int mode, bool offer);
static const char *spare_startup(void);
static const char *startup_note;        /* second line for the first menu */
static void spare_progress(const char *what, unsigned percent);

static void run_loop(void);
static void poll_keyboard(void);
static void poll_usb(void);
static void handle_key(uint8_t code);
static void queue_put(unsigned terminal, unsigned key);
static int  queue_get(unsigned terminal);
static void watch_for_ready(uint8_t c);
static void draw(void);
static void draw_terminal(unsigned index, uint16_t colour);
static void draw_status(void);
static void show_note(const char *text);
static void toggle_import(void);
static void toggle_capture(void);
static void service_import(void);
static void end_import(const char *why);
static bool pick_import_file(char *out, unsigned size);

/* ===================================================================== */
/*  The machine and the terminals                                         */
/* ===================================================================== */
static dev_ptr_t  ptr;                  /* System Processor */
static dev_tty_t  tty;
static dev_tbg_t  tbg;
static dev_dq_t   disc;                 /* 12565A interface with 2883 drives */
static dev_tape_t magtape;
static hp_dma_t   dma;
static dev_tbg_t  tbg_iop;              /* I/O Processor */
static dev_mux_t  mux;
static hp_dma_t   dma_iop;
static dev_ipl_t  ipl_sp, ipl_iop;      /* the Processor Interconnect, both ends */
static ipl_bus_t  ipl_bus[IPL_CARDS];   /* ... and the cables between them */
static hp_store_t *disc_image, *tape_image;
static hp_store_t *sleep_image;         /* the SLEEP tape (changes since the HIBERNATE) */
static bool       restore_start;        /* a reload that reads the SLEEP tape first */
static unsigned   stop_cpu;             /* which CPU stopped the run, and its P and T */
static hp_word    stop_p, stop_t;
static uint8_t    paper_tape[PAPER_TAPE_MAX];

static char       fail_name[PATH_MAX_LEN];  /* file that failed, for the menu */
static char       fail_reason[TCOLS + 1];   /* and why */
static terminal_t screen[2];          /* the operator console and the user terminal */
static uint16_t   key_queue[TERMINAL_COUNT][KEY_QUEUE_SIZE];
static unsigned   queue_head[TERMINAL_COUNT], queue_tail[TERMINAL_COUNT];
static unsigned   active;               /* which terminal is on the screen */
static bool       unseen[2];            /* output on a terminal that is not shown */
static unsigned   last_cursor_row[2], last_cursor_col[2];
static bool       status_stale;         /* the status line must be drawn again */
static autotype_t operator_typist;
static char       report[TCOLS + 1];    /* run figures, shown under the menu message */
static bool       system_up;
static bool       shutting_down;
static bool       hibernated;           /* the HIBERNATE finished and the tape is written */
static bool       sleeping;             /* the shutdown under way is a SLEEP */
static bool       slept;                /* a SLEEP finished (the disc is up to date) */
static bool       slept_to_tape;        /* ... and the SLEEP tape was written too */
static bool       asking_shutdown;      /* F4 was pressed: waiting for H, S, D or ESC */
static char       up_tail[16];
static const char *note;                /* a short message for the status line */
static uint32_t   note_until;
static bool       ctrl_down;
static hostfile_import_t  importer;    /* a file being typed into the user terminal */
static hostfile_capture_t capturer;    /* the user terminal's output being saved */
static char       note_text[TCOLS + 1];

static const autotype_step_t boot_head_steps[] = BOOT_HEAD_STEPS;
static const autotype_step_t boot_tail_steps[] = BOOT_TAIL_STEPS;
static const autotype_step_t shutdown_steps[] = SHUTDOWN_STEPS;
static const autotype_step_t reload_steps[] = RELOAD_STEPS;
static const autotype_step_t sleep_plain_steps[] = SLEEP_PLAIN_STEPS;
static const autotype_step_t sleep_steps[] = SLEEP_STEPS;

/* The date and time the operator gave on the menu, and the script that types them. */
static char date_text[DATE_LENGTH + 1] = DEFAULT_DATE;
static char time_text[TIME_LENGTH + 1] = DEFAULT_TIME;
static char date_reply[DATE_LENGTH + 2], time_reply[TIME_LENGTH + 2];
static autotype_step_t run_steps[RUN_STEPS_MAX];

/* ===================================================================== */
/*  main: set up the hardware once, then the menu forever                 */
/* ===================================================================== */
int main(void)
{
    plat_init();

    sb_init();                /* keyboard / south-bridge I2C (no bg poll) */
    lcd_init();               /* ST7365P LCD */
    lcd_enable_cursor(false); /* no blinking text cursor */
    lcd_set_background(COL_BG);

    title_screen();           /* nothing touches the card until a key is pressed */

    {
        const char *first = spare_startup();

        if (first != NULL)
            show_message(first, fail_name[0] ? fail_name : startup_note,
                         fail_reason[0] ? fail_reason : NULL);
    }

    for (;;) {
        const char *message = NULL;
        uint8_t key;

        menu_screen(NULL, NULL, NULL);
        key = wait_any_key();
        if (key == KEY_ESC || key == 'q' || key == 'Q')
            go_loader();      /* does not return */
        if ((key == 'b' || key == 'B') && ask_date_time())
            message = run_machine(false, false);
        else if ((key == 'r' || key == 'R') && ask_date_time())
            message = run_machine(true, false);
        else if ((key == 's' || key == 'S') && ask_date_time())
            message = run_machine(true, true);
        else if (key == 'v' || key == 'V')
            message = spare_menu(SPARE_MODE_SAVE, false);
        else if (key == 'x' || key == 'X')
            message = spare_menu(SPARE_MODE_RESTORE, false);
        else if (key == 'w' || key == 'W')
            message = spare_menu(SPARE_MODE_RESTORE_OLD, false);
        else if (key == 't' || key == 'T') {
            put_row(MENU_ROW_MESSAGE, "TESTING THE SD CARD...", COL_YELLOW);
            if (storage_mount() == 0)
                sdtest_run(report, sizeof report);
            else
                snprintf(report, sizeof report, "NO SD CARD");
            message = "SD SPEED TEST DONE";
        }
        if (message != NULL) {
            show_message(message, fail_name[0] ? fail_name : NULL,
                         fail_reason[0] ? fail_reason : (report[0] ? report : NULL));
            report[0] = '\0';
        }
    }
}

/* ===================================================================== */
/*  Drawing helpers                                                       */
/* ===================================================================== */
static void clear_row(int row)
{
    lcd_solid_rectangle(COL_BG, 0, (uint16_t)(row * GLYPH_HEIGHT), WIDTH,
                        GLYPH_HEIGHT);
}

/* clear a row and print a string centred on it */
static void put_row(int row, const char *str, uint16_t fg)
{
    clear_row(row);
    int len = (int)strlen(str);
    int col = (SCREEN_COLS - len) / 2;
    if (col < 0)
        col = 0;
    lcd_set_foreground(fg);
    lcd_putstr((uint8_t)col, (uint8_t)row, str);
}

/* clear a row and print a string at the menu's left margin */
static void put_left(int row, const char *str, uint16_t fg)
{
    clear_row(row);
    lcd_set_foreground(fg);
    lcd_putstr(MENU_COL_LEFT, (uint8_t)row, str);
}

/* ===================================================================== */
/*  Keyboard helpers (raw south-bridge FIFO polling)                      */
/* ===================================================================== */
static inline uint16_t key_event(void)      { return sb_read_keyboard(); }
static inline uint8_t  ev_state(uint16_t e) { return e >> 8; }
static inline uint8_t  ev_code(uint16_t e)  { return e & 0xFF; }

/* true for a bare SHIFT / CTRL / ALT / SYM modifier key code */
static inline bool is_mod_key(uint8_t code)
{
    return code == KEY_MOD_SHL || code == KEY_MOD_SHR || code == KEY_MOD_CTRL ||
           code == KEY_MOD_ALT || code == KEY_MOD_SYM;
}

/* reboot into BOOTSEL (USB drive) mode; does not return */
static void go_bootsel(void)
{
    put_row(MENU_ROW_MESSAGE, "REBOOTING TO BOOTSEL...", COL_YELLOW);
    plat_sleep_ms(REBOOT_MS);
    plat_bootsel();
}

/* leave for the UF2 Loader menu (ESC or Q on the menu); does not return */
static void go_loader(void)
{
    put_row(MENU_ROW_MESSAGE, "BACK TO THE LOADER...", COL_YELLOW);
    plat_sleep_ms(REBOOT_MS);
    plat_exit_to_loader();
}

static void drain_keys(void)
{
    while (key_event() != 0)
        ;                     /* reading the FIFO empties it */
}

/* wait for a real key press and return its code ('~' still goes to BOOTSEL) */
static uint8_t wait_any_key(void)
{
    drain_keys();
    for (;;) {
        uint16_t e = key_event();
        if (e && ev_state(e) == KEY_STATE_PRESSED) {
            uint8_t code = ev_code(e);

            if (code == '~')
                go_bootsel();
            if (!is_mod_key(code)) {
                drain_keys();
                return code;
            }
        }
        plat_sleep_ms(POLL_MS);
    }
}

/* ===================================================================== */
/*  Menu                                                                  */
/* ===================================================================== */
/* Show a message on the menu screen and wait for a key. The count and text of failed SD
 * tries go on a row of their own, and a failure leaves a line in the run log, so that a
 * "not found" caused by an SD error can be told from a real one. */
static void show_message(const char *message, const char *message2, const char *message3)
{
    char sd_note[TCOLS + 1];
    char sd_last[24];
    unsigned sd_count = storage_failures(sd_last, sizeof sd_last);

    sd_note[0] = '\0';
    if (sd_count != 0)                                  /* SD tries that failed */
        snprintf(sd_note, sizeof sd_note, "SD FAILS %u LAST %s", sd_count, sd_last);
    if (fail_name[0] != '\0' || fail_reason[0] != '\0') {
        char line[RUNLOG_LINE_MAX];

        snprintf(line, sizeof line, "MENU %s: %s %s; %s", message, fail_name, fail_reason,
                 sd_note[0] ? sd_note : "no SD failures");
        runlog_append(line);
    }
    menu_screen(message, message2, message3);
    if (sd_note[0] != '\0')
        put_row(MENU_ROW_SD, sd_note, COL_WHITE);
    (void)wait_any_key();
}

/* The first screen: the name of the program, and a key press before anything else is done
 * (the first-start save of the spare copies, for one). ESC or Q leaves for the loader. The
 * screen does not mention it, but the tilde key reaches BOOTSEL from here, because
 * wait_any_key() handles it. */
static void title_screen(void)
{
    uint8_t key;

    lcd_clear_screen();
    put_row(MENU_ROW_TITLE, PROGRAM_TITLE, COL_CYAN);
    put_row(MENU_ROW_VERSION, VERSION, COL_CYAN);
    put_row(MENU_ROW_AUTHOR, "BY THOMAS DZUBIN", COL_WHITE);
    put_row(TITLE_ROW_START, "PRESS ANY KEY TO START", COL_GREEN);
    put_row(TITLE_ROW_EXIT, "ESC / Q   EXIT", COL_WHITE);
    key = wait_any_key();
    if (key == KEY_ESC || key == 'q' || key == 'Q')
        go_loader();                    /* does not return */
}

static void menu_screen(const char *message, const char *message2,
                        const char *message3)
{
    lcd_clear_screen();
    put_row(MENU_ROW_TITLE, PROGRAM_TITLE, COL_CYAN);
    put_row(MENU_ROW_VERSION, VERSION, COL_CYAN);
    put_row(MENU_ROW_AUTHOR, "BY THOMAS DZUBIN", COL_WHITE);
    put_left(MENU_ROW_BOOT, "B   START THE SYSTEM", COL_GREEN);
    put_left(MENU_ROW_RELOAD, "R   RELOAD FROM THE HIBERNATE TAPE", COL_GREEN);
    put_left(MENU_ROW_RESTORE, "S   RELOAD FROM THE SLEEP TAPE", COL_GREEN);
    put_left(MENU_ROW_SDTEST, "T   SD SPEED TEST", COL_GREEN);
    put_left(MENU_ROW_SPARE_SAVE, "V   SAVE SPARE COPIES", COL_GREEN);
    put_left(MENU_ROW_SPARE_BACK, "X   RESTORE FROM SPARE COPIES", COL_GREEN);
    put_left(MENU_ROW_SPARE_OLD, "W   RESTORE FROM OLDER SPARES", COL_GREEN);
    put_left(MENU_ROW_EXIT, "ESC / Q   EXIT", COL_WHITE);
    if (message != NULL)
        put_row(MENU_ROW_MESSAGE, message, COL_YELLOW);
    if (message2 != NULL)
        put_row(MENU_ROW_MESSAGE2, message2, COL_WHITE);
    if (message3 != NULL)
        put_row(MENU_ROW_MESSAGE3, message3, COL_WHITE);
    if (message != NULL)
        put_row(MENU_ROW_PRESS_KEY, "PRESS ANY KEY", COL_WHITE);
}

/* Ask for one value on a row: Enter takes it, ESC gives up, Backspace deletes. The
 * first key typed replaces the suggestion. Only digits and a slash are taken. */
static bool ask_value(int row, const char *label, char *value, unsigned length)
{
    char line[TCOLS + 1];
    bool fresh = true;
    unsigned n = (unsigned)strlen(value);

    for (;;) {
        uint8_t key;

        snprintf(line, sizeof line, "%s%s_", label, value);
        put_row(row, line, COL_WHITE);
        key = wait_any_key();
        if (key == KEY_ENTER || key == KEY_RETURN)
            return n == length;
        if (key == KEY_ESC)
            return false;
        if (key == KEY_BACKSPACE) {
            if (n > 0)
                value[--n] = 0;
            fresh = false;
        } else if ((key >= '0' && key <= '9') || key == '/') {
            if (fresh) {
                n = 0;
                value[0] = 0;
                fresh = false;
            }
            if (n < length) {
                value[n++] = (char)key;
                value[n] = 0;
            }
        }
    }
}

/* The system asks for the date and time at every start, and there is no clock to
 * read, so they are asked for here and typed for the operator later. */
static bool ask_date_time(void)
{
    char date_work[DATE_LENGTH + 1], time_work[TIME_LENGTH + 1];

    snprintf(date_work, sizeof date_work, "%s", date_text);
    snprintf(time_work, sizeof time_work, "%s", time_text);
    put_row(MENU_ROW_ASK, "ENTER ACCEPTS, ESC CANCELS", COL_WHITE);
    if (!ask_value(MENU_ROW_DATE, "DATE (DAY/YY)  ", date_work, DATE_LENGTH) ||
        !ask_value(MENU_ROW_TIME, "TIME (HHMM)    ", time_work, TIME_LENGTH))
        return false;
    snprintf(date_text, sizeof date_text, "%s", date_work);
    snprintf(time_text, sizeof time_text, "%s", time_work);
    snprintf(date_reply, sizeof date_reply, "%s\r", date_text);
    snprintf(time_reply, sizeof time_reply, "%s\r", time_text);
    clear_row(MENU_ROW_DATE);           /* the questions go at once, the system takes a while to load */
    clear_row(MENU_ROW_TIME);
    clear_row(MENU_ROW_ASK);
    return true;
}

/* Copy a script, add the date and time answers after it, then a second script that
 * follows them (the system asks DATE? and TIME? between the two parts). */
static const autotype_step_t *make_script(const autotype_step_t *head,
                                          const autotype_step_t *tail)
{
    unsigned n = 0, k;

    while (head[n].expect != NULL && n < RUN_STEPS_MAX - 4) {
        run_steps[n] = head[n];
        n++;
    }
    run_steps[n].expect = DATE_QUESTION;
    run_steps[n++].reply = date_reply;
    run_steps[n].expect = TIME_QUESTION;
    run_steps[n++].reply = time_reply;
    for (k = 0; tail != NULL && tail[k].expect != NULL && n < RUN_STEPS_MAX - 1; k++)
        run_steps[n++] = tail[k];
    run_steps[n].expect = NULL;
    run_steps[n].reply = NULL;
    return run_steps;
}

/* ===================================================================== */
/*  Starting and stopping the machine                                     */
/* ===================================================================== */
/* Spare copies of the disc and the HIBERNATE tape (V saves them, X restores them, W restores
 * the older ones). All ask first. A save keeps the previous spares as the older ones. */
static void spare_progress(const char *what, unsigned percent)
{
    char text[TCOLS + 1];

    snprintf(text, sizeof text, "%s %u%%", what, percent);
    put_row(MENU_ROW_MESSAGE3, text, COL_WHITE);
}

/* At the first start, before anything else. If the last run did not end cleanly, say so
 * (and make no spares from a disc that may be damaged). If there are no spares at all,
 * make them from the live files and show what is happening. Returns a message for the
 * menu, or NULL. */
static const char *spare_startup(void)
{
    static const char *const names[SPARE_FILE_COUNT] = SPARE_FILES;
    char detail[SPARE_DETAIL_MAX + 1];
    const char *error;
    int state;

    if (storage_mount() != 0)
        return NULL;                    /* the menu reports a missing card when it matters */
    state = spare_state(names, SPARE_FILE_COUNT);
    if (spare_session_get() == SPARE_STATE_RUNNING) {
        startup_note = state == SPARE_NONE ? "NO SPARES. FIX THE SYSTEM, THEN USE V."
                                           : "USE R OR S, OR X TO RESTORE SPARES";
        return "LAST RUN DID NOT END CLEANLY";
    }
    if (state == SPARE_COMPLETE)
        return NULL;
    if (state == SPARE_PARTIAL) {
        startup_note = "USE V TO SAVE NEW ONES";
        return "SPARE COPIES INCOMPLETE OR DAMAGED";
    }
    lcd_clear_screen();
    put_row(MENU_ROW_TITLE, PROGRAM_TITLE, COL_CYAN);
    put_row(MENU_ROW_VERSION, VERSION, COL_CYAN);
    put_row(MENU_ROW_MESSAGE, "NO SPARE COPIES FOUND", COL_YELLOW);
    put_row(MENU_ROW_MESSAGE2, "SAVING THE SYSTEM FILES AS SPARES", COL_WHITE);
    put_row(MENU_ROW_WAIT1, "PLEASE WAIT FOR THE SPARE SAVE", COL_YELLOW);
    put_row(MENU_ROW_WAIT2, "TO FINISH", COL_YELLOW);
    error = spare_copy(names, SPARE_FILE_COUNT, SPARE_MODE_SAVE, NULL, spare_progress, detail,
                       sizeof detail);
    if (error != NULL) {
        snprintf(fail_name, sizeof fail_name, "%s", detail);
        snprintf(fail_reason, sizeof fail_reason, "%s", storage_last_error());
        return error;
    }
    return "SPARE COPIES SAVED";
}

/* mode is a SPARE_MODE_ value; offer is true for the question after a HIBERNATE. Returns
 * a message for the menu, or NULL if the operator cancelled. */
static const char *spare_menu(int mode, bool offer)
{
    static const char *const names[SPARE_FILE_COUNT] = SPARE_FILES;
    char detail[SPARE_DETAIL_MAX + 1];
    const char *error;
    bool restore = mode != SPARE_MODE_SAVE;
    uint8_t key;

    fail_name[0] = fail_reason[0] = '\0';
    if (mode == SPARE_MODE_SAVE && !offer && spare_session_get() == SPARE_STATE_RUNNING) {
        snprintf(fail_name, sizeof fail_name, "RECOVER THE SYSTEM FIRST");
        return "LAST RUN NOT CLEAN. NOT SAVING.";
    }
    if (restore && storage_mount() == 0 && mode == SPARE_MODE_RESTORE_OLD &&
        !spare_old_present(names, SPARE_FILE_COUNT))
        return "NO OLDER SPARES";
    if (offer)
        put_row(MENU_ROW_MESSAGE, "SYSTEM HIBERNATED. SAVE SPARES?", COL_YELLOW);
    else if (!restore)
        put_row(MENU_ROW_MESSAGE, "SAVE NEW SPARE COPIES?", COL_YELLOW);
    else
        put_row(MENU_ROW_MESSAGE, mode == SPARE_MODE_RESTORE ? "REPLACE LIVE SYSTEM WITH SPARES?"
                                                            : "REPLACE LIVE SYSTEM WITH OLDER SPARES?",
                COL_YELLOW);
    put_row(MENU_ROW_MESSAGE2, restore ? "THE SLEEP TAPE IS DELETED TOO"
                                       : "THE LAST SPARES BECOME THE OLDER ONES", COL_WHITE);
    put_row(MENU_ROW_MESSAGE3, "Y = YES, ANY OTHER KEY CANCELS", COL_WHITE);
    key = wait_any_key();
    clear_row(MENU_ROW_MESSAGE);        /* the question goes at once, so the key press shows */
    clear_row(MENU_ROW_MESSAGE2);
    clear_row(MENU_ROW_MESSAGE3);
    if (key != 'y' && key != 'Y')
        return NULL;
    put_row(MENU_ROW_MESSAGE, restore ? "RESTORING, PLEASE WAIT..." : "SAVING, PLEASE WAIT...",
            COL_YELLOW);
    put_row(MENU_ROW_MESSAGE3, "CHECKING THE FILES...", COL_WHITE);
    error = spare_copy(names, SPARE_FILE_COUNT, mode, FILE_SLEEP_TAPE, spare_progress,
                       detail, sizeof detail);
    if (error != NULL) {
        snprintf(fail_name, sizeof fail_name, "%s", detail);
        snprintf(fail_reason, sizeof fail_reason, "%s", storage_last_error());
        return error;
    }
    if (restore) {
        spare_session_set(SPARE_STATE_CLEAN);
        return mode == SPARE_MODE_RESTORE ? "SYSTEM RESTORED FROM THE SPARES"
                                          : "SYSTEM RESTORED FROM OLDER SPARES";
    }
    return offer ? "HIBERNATED. SPARE COPIES SAVED." : "SPARE COPIES SAVED";
}

static bool make_path(char *path, const char *name)
{
    return (size_t)snprintf(path, PATH_MAX_LEN, "%s%s", plat_data_dir(), name)
           < PATH_MAX_LEN;
}

/* Read a whole (small) file into memory. Returns 0 on success. */
static int load_file(const char *name, uint8_t *buffer, uint32_t max, uint32_t *length)
{
    char path[PATH_MAX_LEN];
    hp_store_t *s;
    uint32_t size;

    if (!make_path(path, name) || (s = storage_open(path, 0)) == NULL)
        return 1;
    size = storage_size(s);
    if (size > max || storage_read(s, 0, buffer, size) != 0) {
        storage_close(s);
        return 2;
    }
    storage_close(s);
    *length = size;
    return 0;
}

/* Say which file failed and why, for the lines under the menu message. */
static void note_failure(const char *name)
{
    snprintf(fail_name, sizeof fail_name, "%s", name);
    snprintf(fail_reason, sizeof fail_reason, "%s", storage_last_error());
}

/* The device structures are static, so a second start (a boot after a reload, say)
 * would otherwise find the first run's leftover state. */
static void clear_devices(void)
{
    memset(&dma, 0, sizeof dma);
    memset(&tty, 0, sizeof tty);
    memset(&disc, 0, sizeof disc);
    memset(&ptr, 0, sizeof ptr);
    memset(&tbg, 0, sizeof tbg);
    memset(&magtape, 0, sizeof magtape);
    memset(&dma_iop, 0, sizeof dma_iop);
    memset(&mux, 0, sizeof mux);
    memset(&tbg_iop, 0, sizeof tbg_iop);
    memset(&ipl_sp, 0, sizeof ipl_sp);
    memset(&ipl_iop, 0, sizeof ipl_iop);
    memset(ipl_bus, 0, sizeof ipl_bus);
}

/* Build both computers, each with its own cards, and wire up the interconnect. */
static void build_machine(void)
{
    hp_select(HP_SP);
    hp_init(SP_MEMORY_WORDS, 1);
    hp_dma_init(&dma);
    dev_ipl_init(&ipl_sp, HP_SP, 0, SC_INTERCONNECT, ipl_bus);
    dev_tty_init(&tty, SC_TELEPRINTER);
    dev_ptr_init(&ptr, SC_PAPER_TAPE);
    dev_tbg_init(&tbg, SC_TIME_BASE_SP);
    dev_dq_init(&disc, SC_DISC);
    dev_tape_init(&magtape, SC_TAPE);
    hp_preset();
    hp.e = 1;

    hp_select(HP_IOP);
    hp_init(IOP_MEMORY_WORDS, 0);
    hp_dma_init(&dma_iop);
    dev_ipl_init(&ipl_iop, HP_IOP, 1, SC_INTERCONNECT, ipl_bus);
    dev_tbg_init(&tbg_iop, SC_TIME_BASE_IOP);
    dev_mux_init(&mux, SC_MUX_LOWER, SC_MUX_CONTROL);
    hp_preset();
    hp.e = 1;

    hp_select(HP_SP);
    hp_pair_init(&ipl_sp, &ipl_iop);
    dev_dq_attach(&disc, 0, disc_image, 0);
    dev_tape_mount(&magtape, 0, restore_start ? sleep_image : tape_image, 0);
}

/* Run both computers until all those in the mask have stopped (bit 0 the System
 * Processor, bit 1 the I/O Processor). Returns 0 if they did within the limit. */
static int run_until_stopped(unsigned mask, uint32_t limit)
{
    uint32_t done = 0;

    while (done < limit) {
        (void)hp_pair_run(PAIR_CHUNK);
        done += PAIR_CHUNK;
        if (((mask & 1u) == 0 || hp_pair_stopped(HP_SP)) &&
            ((mask & 2u) == 0 || hp_pair_stopped(HP_IOP)))
            return 0;
    }
    return 1;
}

/* Load a paper tape file, boot it on the System Processor's reader and run its
 * loader until it halts. Returns 0 if it halted with T showing a good load. */
static int paper_tape_boot(const char *name, uint32_t *length)
{
    int status;

    if (load_file(name, paper_tape, PAPER_TAPE_MAX, length) != 0) {
        note_failure(name);
        return 1;
    }
    hp_select(HP_SP);
    dev_ptr_load_tape(&ptr, paper_tape, *length);
    hp.s = 0;
    dev_ptr_boot(&ptr);
    status = hp_run(LOADER_RUN_LIMIT);
    return (status == HP_STOP_HALT && hp.t == HALT_DONE) ? 0 : 2;
}

/* Build the machine, start the I/O Processor and load the first program on the
 * System Processor. Returns an error message, or NULL when it is ready to run. */
static const char *start_machine(bool reload, bool restore)
{
    char path[PATH_MAX_LEN];
    uint32_t length;
    int code;

    fail_name[0] = fail_reason[0] = '\0';
    if (storage_mount() != 0) {
        snprintf(fail_reason, sizeof fail_reason, "%s", storage_last_error());
        return "NO SD CARD FOUND";
    }
    if (!make_path(path, FILE_DISC))
        return "DATA FOLDER PATH TOO LONG";
    disc_image = storage_open(path, 1);
    if (disc_image == NULL) {
        note_failure(FILE_DISC);
        return "DISC IMAGE NOT FOUND";
    }
    if (!make_path(path, FILE_TAPE)) {
        storage_close(disc_image);
        disc_image = NULL;
        return "DATA FOLDER PATH TOO LONG";
    }
    restore_start = restore;
    sleep_image = NULL;
    if (restore) {
        if (!make_path(path, FILE_SLEEP_TAPE) || (sleep_image = storage_open(path, 0)) == NULL) {
            note_failure(FILE_SLEEP_TAPE);
            storage_close(disc_image);
            disc_image = NULL;
            return "SLEEP TAPE NOT FOUND";
        }
        make_path(path, FILE_TAPE);
    }
    tape_image = storage_open(path, 1);
    if (tape_image == NULL && !reload)
        tape_image = storage_create(path);          /* the first HIBERNATE fills it */
    if (tape_image == NULL) {
        note_failure(FILE_TAPE);
        storage_close(disc_image);
        disc_image = NULL;
        return "HIBERNATE TAPE NOT FOUND";
    }

    clear_devices();
    build_machine();

    /* the cross-loader sends the I/O Processor its program across the interconnect
     * to the binary loader the I/O Processor is waiting in */
    code = paper_tape_boot(FILE_CROSS_TAPE, &length);
    if (code != 0) {
        stop_machine();
        return code == 1 ? "CROSS-LOADER TAPE NOT FOUND" : "PAPER TAPE LOAD FAILED";
    }
    hp.s = 0;
    hp.p = CROSS_LOADER_START;
    hp_pair_resume(HP_SP);
    hp_select(HP_IOP);
    hp_load_iop_loader(SC_INTERCONNECT);
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    if (run_until_stopped(3u, PAIR_RUN_LIMIT) != 0 || hp_cpus[HP_IOP].t != HALT_DONE ||
        hp_cpus[HP_SP].t != HALT_DONE) {
        stop_machine();
        return "I/O PROCESSOR LOAD FAILED";
    }

    /* start the I/O Processor's program; then the System Processor loads the system */
    hp_select(HP_IOP);
    hp.s = 0;
    hp.p = CROSS_LOADER_START;
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    hp.s = 0;
    if (reload) {
        code = paper_tape_boot(FILE_LOADER_TAPE, &length);
        if (code != 0) {
            stop_machine();
            if (code == 1)
                return "TSB LOADER TAPE NOT FOUND";
            return "PAPER TAPE LOAD FAILED";
        }
        hp.s = 0;
        hp.p = RELOAD_START;
        hp_pair_resume(HP_SP);
    } else {
        hp_load_dq_bootstrap(SC_DISC);
        hp_pair_resume(HP_SP);
        if (run_until_stopped(1u, PAIR_RUN_LIMIT) != 0 || hp.t != HALT_DONE) {
            stop_machine();
            return "SYSTEM LOAD FAILED";
        }
        hp.s = 0;
        hp_pair_resume(HP_SP);
    }

    terminal_init(&screen[0]);
    terminal_init(&screen[1]);
    memset(queue_head, 0, sizeof queue_head);
    memset(queue_tail, 0, sizeof queue_tail);
    unseen[0] = unseen[1] = false;
    last_cursor_row[0] = last_cursor_row[1] = 0;
    last_cursor_col[0] = last_cursor_col[1] = 0;
    status_stale = true;
    active = CONSOLE_OPERATOR;
    system_up = shutting_down = hibernated = false;
    sleeping = slept = slept_to_tape = asking_shutdown = false;
    up_tail[0] = 0;
    note = NULL;
    ctrl_down = false;
    autotype_start(&operator_typist,
                   reload ? make_script(reload_steps, NULL)
                          : make_script(boot_head_steps, boot_tail_steps), NULL);
    return NULL;
}

static void stop_machine(void)
{
    if (importer.file != NULL)
        hostfile_import_close(&importer);
    if (capturer.file != NULL)
        (void)hostfile_capture_close(&capturer);
    storage_close(disc_image);
    storage_close(tape_image);
    storage_close(sleep_image);
    disc_image = tape_image = sleep_image = NULL;
}


/* ===================================================================== */
/*  The run report (a long run writes its figures to RUNLOG.TXT)          */
/* ===================================================================== */
static uint64_t run_instr;              /* instructions run by the System Processor */
static uint32_t run_start_ms, sd_start_ms, sd_start_ops;
static uint64_t emu_us, keys_us, draw_us;     /* where the time goes, for the run log */
static uint32_t tape_seen_pos, tape_moved_ms;   /* for the tape progress on the status line */
static uint32_t progress_ms;            /* time and instruction count at the last progress line */
static uint64_t progress_instr;

static void start_report(void)
{
    run_instr = 0;
    emu_us = keys_us = draw_us = 0;
    run_start_ms = plat_now_ms();
    tape_seen_pos = 0;
    tape_moved_ms = run_start_ms;
    progress_ms = run_start_ms;
    progress_instr = 0;
    storage_stats(&sd_start_ms, &sd_start_ops);
    report[0] = '\0';
}

/* While a long run goes on, add a line to the log every few minutes: the speed over
 * that stretch. */
static void progress_report(uint32_t now)
{
    char line[RUNLOG_LINE_MAX];
    uint32_t ms = now - progress_ms;
    unsigned mips100;

    if (ms < RUNLOG_PROGRESS_MS)
        return;
    mips100 = (unsigned)((run_instr - progress_instr) / ((uint64_t)ms * 10u));
    snprintf(line, sizeof line, "  progress at %u s: %u.%02u MIPS over the last %u s; so far emulation %u s, keys %u s, screen %u s",
             (unsigned)((now - run_start_ms) / 1000u), mips100 / 100, mips100 % 100,
             (unsigned)(ms / 1000u), (unsigned)(emu_us / 1000000u),
             (unsigned)(keys_us / 1000000u), (unsigned)(draw_us / 1000000u));
    runlog_append(line);
    progress_ms = now;
    progress_instr = run_instr;
}

static void finish_report(const char *mode, const char *outcome)
{
    char line[RUNLOG_LINE_MAX], last[24];
    uint32_t secs = (plat_now_ms() - run_start_ms) / 1000u, sd_ms, sd_ops;
    unsigned mips100 = secs ? (unsigned)(run_instr / ((uint64_t)secs * 10000u)) : 0;

    storage_stats(&sd_ms, &sd_ops);
    snprintf(report, sizeof report, "%us %u.%02u MIPS SD %us", (unsigned)secs,
             mips100 / 100, mips100 % 100, (unsigned)((sd_ms - sd_start_ms) / 1000u));
    snprintf(line, sizeof line, "RUN %s %s: %u s, %u.%02u MIPS, SD %u s in %u transfers, SD fails %u; emulation %u s, keys %u s, screen %u s",
             mode, outcome, (unsigned)secs, mips100 / 100, mips100 % 100,
             (unsigned)((sd_ms - sd_start_ms) / 1000u), (unsigned)(sd_ops - sd_start_ops),
             storage_failures(last, sizeof last), (unsigned)(emu_us / 1000000u),
             (unsigned)(keys_us / 1000000u), (unsigned)(draw_us / 1000000u));
    runlog_append(line);
}

/* Run the machine until it halts or is shut down. Returns a message for the menu. */
static const char *run_machine(bool reload, bool restore)
{
    static char halted[TCOLS + 1];
    const char *error;

    int before = spare_session_get();

    put_row(MENU_ROW_MESSAGE, "STARTING THE SYSTEM...", COL_YELLOW);
    spare_session_set(SPARE_STATE_RUNNING);   /* stays until a clean HIBERNATE or SLEEP */
    error = start_machine(reload, restore);
    if (error != NULL) {
        spare_session_set(before != 0 ? before : SPARE_STATE_CLEAN);
        return error;
    }
    lcd_clear_screen();
    start_report();
    run_loop();
    stop_machine();
    finish_report(restore ? "RESTORE" : (reload ? "RELOAD" : "BOOT"),
                  hibernated ? "hibernated" : (slept ? "slept" : "halted"));
    if (hibernated || slept)
        spare_session_set(SPARE_STATE_CLEAN);
    if (hibernated) {
        const char *saved;

        menu_screen(NULL, NULL, NULL);
        saved = spare_menu(SPARE_MODE_SAVE, true);
        return saved != NULL ? saved : "SYSTEM HIBERNATED. TAPE WRITTEN.";
    }
    if (slept)
        return slept_to_tape ? "SYSTEM SLEPT. SLEEP TAPE WRITTEN." : "SYSTEM SLEPT. START WITH B.";
    snprintf(halted, sizeof halted, "HALT %s  P %05o  T %06o",
             stop_cpu == HP_SP ? "SP" : "IOP", stop_p, stop_t);
    show_halt_console(halted);
    return halted;
}

/* After a halt the menu would hide what the operator console said just before it, which
 * is the best clue to the cause. Show the console as it was, with the halt below it. */
static void show_halt_console(const char *halt_text)
{
    unsigned r;

    lcd_clear_screen();
    for (r = 0; r < TERM_ROWS; r++)
        termdraw_row(screen[0].text[r], r, COL_OPERATOR, -1);
    put_row(STATUS_ROW, halt_text, COL_YELLOW);
    (void)wait_any_key();
}

/* ===================================================================== */
/*  The run loop                                                          */
/* ===================================================================== */
static void run_loop(void)
{
    uint32_t last_keys = plat_now_ms(), last_draw = last_keys, last_cal = last_keys;
    uint32_t ticks_at_cal = hp_cpus[HP_SP].ticks;
    uint32_t iop_ticks_at_cal = hp_cpus[HP_IOP].ticks;

    screen[0].dirty = ~0ull;
    for (;;) {
        uint32_t now;
        uint32_t t_emu = plat_now_us();
        int stop = hp_pair_run(RUN_CHUNK);

        emu_us += (uint32_t)(plat_now_us() - t_emu);

        if (stop != 0) {
            unsigned cpu = (unsigned)stop - 1;

            if (cpu == HP_SP && shutting_down && hp_pair_status(cpu) == HP_STOP_HALT) {
                if (hp_cpus[cpu].t == HALT_TAPE_TO_AUTO) {  /* "CHANGE MAG TAPE TO AUTO" */
                    dev_tape_set_online(&magtape, 0);
                    hp_pair_resume(HP_SP);
                    continue;
                }
                if (hp_cpus[cpu].t == HALT_DONE) {          /* the shutdown finished */
                    if (sleeping)
                        slept = true;
                    else
                        hibernated = true;
                }
            }
            stop_cpu = cpu;
            stop_p = hp_cpus[cpu].p;
            stop_t = hp_cpus[cpu].t;
            return;                                 /* a halt or a fault */
        }
        now = plat_now_ms();
        service_import();
        if (now - last_keys >= KEY_POLL_MS) {
            last_keys = now;
            t_emu = plat_now_us();
            poll_keyboard();
            poll_usb();
            keys_us += (uint32_t)(plat_now_us() - t_emu);
        }
        if (now - last_cal >= CALIBRATE_MS) {
            uint32_t d = hp_cpus[HP_SP].ticks - ticks_at_cal;

            run_instr += d;
            progress_report(now);

            dev_tbg_calibrate(&tbg, d, now - last_cal);
            dev_tbg_calibrate(&tbg_iop, hp_cpus[HP_IOP].ticks - iop_ticks_at_cal, now - last_cal);
            ticks_at_cal = hp_cpus[HP_SP].ticks;
            iop_ticks_at_cal = hp_cpus[HP_IOP].ticks;
            last_cal = now;
        }
        if (now - last_draw >= DRAW_MS) {
            last_draw = now;
            t_emu = plat_now_us();
            draw();
            draw_us += (uint32_t)(plat_now_us() - t_emu);
        }
    }
}

/* ===================================================================== */
/*  The terminals as the machine sees them (console.h)                    */
/* ===================================================================== */
static void queue_put(unsigned terminal, unsigned key)
{
    unsigned next = (queue_head[terminal] + 1) % KEY_QUEUE_SIZE;

    if (next != queue_tail[terminal]) {         /* drop keys if it is full */
        key_queue[terminal][queue_head[terminal]] = (uint16_t)key;
        queue_head[terminal] = next;
    }
}

static int queue_get(unsigned terminal)
{
    int key;

    if (queue_head[terminal] == queue_tail[terminal])
        return -1;
    key = key_queue[terminal][queue_tail[terminal]];
    queue_tail[terminal] = (queue_tail[terminal] + 1) % KEY_QUEUE_SIZE;
    return key;
}

/* The system says READY when it is up: only then do the terminals "dial in". */
static void watch_for_ready(uint8_t c)
{
    size_t n = strlen(up_tail);

    if (system_up)
        return;
    if (n >= sizeof up_tail - 1) {
        memmove(up_tail, up_tail + 1, n);
        n--;
    }
    up_tail[n] = (char)c;
    up_tail[n + 1] = 0;
    if (strstr(up_tail, SYSTEM_UP_TEXT) != NULL) {
        unsigned line;

        system_up = true;
        if (restore_start) {            /* the SLEEP tape was read: put the HIBERNATE tape back */
            dev_tape_unmount(&magtape, 0);
            dev_tape_mount(&magtape, 0, tape_image, 0);
        }
        for (line = 0; line < USER_LINES; line++)
            dev_mux_connect(&mux, line, 1);
    }
}

void console_write(unsigned terminal, uint8_t character)
{
    if (terminal == CONSOLE_OPERATOR) {
        terminal_put(&screen[0], character);
        autotype_see(&operator_typist, character);
        watch_for_ready(character);
        if (active != 0)
            unseen[0] = true;
    } else if (terminal == CONSOLE_USER) {
        terminal_put(&screen[1], character);
        hostfile_import_see(&importer, character);
        hostfile_capture_put(&capturer, character);
        if (active != 1)
            unseen[1] = true;
    } else {
        plat_usb_write(character);
    }
}

int console_read(unsigned terminal)
{
    if (terminal == CONSOLE_OPERATOR) {
        int c = autotype_next(&operator_typist);

        return c >= 0 ? c : queue_get(0);
    }
    return queue_get(terminal == CONSOLE_USER ? 1 : 2);
}

/* ===================================================================== */
/*  Keyboard and USB input                                                */
/* ===================================================================== */
static void poll_keyboard(void)
{
    uint16_t e;

    while ((e = key_event()) != 0) {
        uint8_t code = ev_code(e), st = ev_state(e);

        if (code == KEY_MOD_CTRL) {
            ctrl_down = (st == KEY_STATE_PRESSED || st == KEY_STATE_HOLD);
            continue;
        }
        if (is_mod_key(code) || st != KEY_STATE_PRESSED)
            continue;
        handle_key(code);                       /* '~' is an ordinary key here */
    }
}

/* Start the shutdown the operator chose: HIBERNATE (the whole system to the
 * HIBERNATE tape), SLEEP (the system back onto the disc only), or SLEEP that also
 * dumps the entries changed since the last HIBERNATE to the SLEEP tape. */
static void begin_shutdown(bool sleep, bool dump)
{
    if (dump) {
        char path[PATH_MAX_LEN];

        dev_tape_unmount(&magtape, 0);
        if (sleep_image != NULL) {      /* still open from a restore: close it first */
            storage_close(sleep_image);
            sleep_image = NULL;
        }
        if (!make_path(path, FILE_SLEEP_TAPE) ||
            ((sleep_image = storage_open(path, 1)) == NULL &&
             (sleep_image = storage_create(path)) == NULL)) {
            dev_tape_mount(&magtape, 0, tape_image, 0);
            show_note("NO SLEEP TAPE FILE");
            return;
        }
        dev_tape_mount(&magtape, 0, sleep_image, 0);
    }
    shutting_down = true;
    sleeping = sleep;
    slept_to_tape = dump;
    dev_tape_set_online(&magtape, 0);
    if (!sleep)
        autotype_start(&operator_typist, shutdown_steps, SHUTDOWN_FIRST);
    else if (dump)
        autotype_start(&operator_typist, sleep_steps, SLEEP_FIRST);
    else
        autotype_start(&operator_typist, sleep_plain_steps, SLEEP_PLAIN_FIRST);
    active = 0;
    screen[0].dirty = ~0ull;
    show_note(sleep ? "SLEEPING" : "SHUTTING DOWN");
}

static void handle_key(uint8_t code)
{
    unsigned terminal = active == 0 ? 0 : 1;

    if (asking_shutdown) {                      /* the answer to F4: H, S, D or ESC */
        asking_shutdown = false;
        status_stale = true;
        switch (code) {
        case 'h': case 'H':
            begin_shutdown(false, false);
            break;
        case 's': case 'S':
            begin_shutdown(true, false);
            break;
        case 'd': case 'D':
            begin_shutdown(true, true);
            break;
        default:
            break;                              /* ESC or any other key cancels */
        }
        return;
    }
    if (importer.active && terminal == 1 &&
        (code == KEY_BACKSPACE || code == KEY_ENTER || code == KEY_RETURN || (code >= 0x20 && code < 0x7F)))
        return;                                 /* typing now would mix with the file being typed */
    switch (code) {
    case KEY_IMPORT_FILE:
        toggle_import();
        return;
    case KEY_CAPTURE_OUTPUT:
        toggle_capture();
        return;
    case KEY_OPERATOR_CONSOLE:
        active = 0;
        unseen[0] = false;
        screen[0].dirty = ~0ull;
        return;
    case KEY_USER_CONSOLE:
        active = 1;
        unseen[1] = false;
        screen[1].dirty = ~0ull;
        return;
    case KEY_TAPE_ONLINE:
        dev_tape_set_online(&magtape, 0);
        show_note("TAPE ON LINE");
        return;
    case KEY_SHUTDOWN:
        if (!shutting_down)
            asking_shutdown = true;             /* the status line asks which kind */
        return;
    case KEY_BREAK:
    case KEY_BREAK_ALT:
        if (importer.active)
            end_import("IMPORT STOPPED");
        queue_put(terminal, CONSOLE_BREAK);
        return;
    case KEY_BACKSPACE:
        queue_put(terminal, CHAR_DELETE);
        return;
    case KEY_ENTER:
    case KEY_RETURN:
        queue_put(terminal, CHAR_CR);
        return;
    default:
        break;
    }
    if (code >= 0x20 && code < 0x7F) {
        if (ctrl_down && code >= '@')
            code &= 0x1F;                       /* CTRL+letter */
        queue_put(terminal, code);
    }
}

/* Keys from a terminal program on the USB link go to the second user line. */
static void poll_usb(void)
{
    int c;

    while ((c = plat_usb_read()) >= 0) {
        if (c == 0x03)
            queue_put(2, CONSOLE_BREAK);        /* CTRL+C */
        else if (c == 0x08 || c == 0x7F)
            queue_put(2, CHAR_DELETE);
        else if (c != '\n')
            queue_put(2, (unsigned)c);          /* Enter sends CR (or CR LF) */
    }
}

/* ===================================================================== */
/*  Drawing the terminals                                                 */
/* ===================================================================== */
static void show_note(const char *text)
{
    note = text;
    note_until = plat_now_ms() + NOTE_MS;
}

/* ===================================================================== */
/*  Importing a file into the user terminal, and capturing its output     */
/* ===================================================================== */
static unsigned queue_pending(unsigned terminal)
{
    return (queue_head[terminal] + KEY_QUEUE_SIZE - queue_tail[terminal]) % KEY_QUEUE_SIZE;
}

/* A key press, without the BOOTSEL shortcut: the machine is running and a slip must not stop it. */
static uint8_t wait_plain_key(void)
{
    drain_keys();
    for (;;) {
        uint16_t e = key_event();

        if (e && ev_state(e) == KEY_STATE_PRESSED && !is_mod_key(ev_code(e))) {
            drain_keys();
            return ev_code(e);
        }
        plat_sleep_ms(POLL_MS);
    }
}

/* Choose a file from the card to type into the user terminal. Returns false if there is
 * none or the operator gave up. The machine waits while the list is on the screen. */
static bool pick_import_file(char *out, unsigned size)
{
    static char names[IMPORT_FILES_MAX][IMPORT_NAME_MAX];
    static const char *const suffixes[] = IMPORT_SUFFIXES;
    static const char *const skip[] = IMPORT_SKIP_NAMES;
    unsigned found, n = 0, i, k, sel = 0, first = 0;
    char line[TCOLS + 1];

    found = storage_list(plat_data_dir(), suffixes, IMPORT_SUFFIX_COUNT, &names[0][0],
                         IMPORT_NAME_MAX, IMPORT_FILES_MAX);
    for (i = 0; i < found; i++) {
        bool keep = true;

        for (k = 0; k < IMPORT_SKIP_COUNT; k++)
            if (strcmp(names[i], skip[k]) == 0)
                keep = false;
        if (keep) {
            if (n != i)
                memcpy(names[n], names[i], IMPORT_NAME_MAX);
            n++;
        }
    }
    if (n == 0) {
        show_note("NO .TXT OR .BAS FILES ON THE CARD");
        return false;
    }
    for (;;) {
        uint8_t key;

        lcd_clear_screen();
        put_row(PICKER_ROW_TITLE, "TYPE A FILE INTO THE USER TERMINAL", COL_CYAN);
        for (i = 0; i < PICKER_ROWS && first + i < n; i++) {
            snprintf(line, sizeof line, "%c %s", first + i == sel ? '>' : ' ', names[first + i]);
            put_left(PICKER_ROW_FIRST + (int)i, line, first + i == sel ? COL_GREEN : COL_WHITE);
        }
        put_row(PICKER_ROW_HELP, "UP DOWN   ENTER TYPES   ESC CANCELS", COL_YELLOW);
        key = wait_plain_key();
        if (key == KEY_ESC)
            return false;
        if (key == KEY_ENTER || key == KEY_RETURN) {
            snprintf(out, size, "%s", names[sel]);
            return true;
        }
        if (key == KEY_UP && sel > 0)
            sel--;
        else if (key == KEY_DOWN && sel + 1 < n)
            sel++;
        if (sel < first)
            first = sel;
        else if (sel >= first + PICKER_ROWS)
            first = sel + 1 - PICKER_ROWS;
    }
}

/* The import is over (finished, or stopped by the operator): say how it went. */
static void end_import(const char *why)
{
    if (why != NULL)
        snprintf(note_text, sizeof note_text, "%s", why);
    else if (importer.errors != 0)
        snprintf(note_text, sizeof note_text, "IMPORTED %u LINES, %u REFUSED", importer.sent, importer.errors);
    else
        snprintf(note_text, sizeof note_text, "IMPORTED %u LINES", importer.sent);
    hostfile_import_close(&importer);
    show_note(note_text);
}

/* F6: stop an import that is under way, or choose a file and start typing it. */
static void toggle_import(void)
{
    char name[IMPORT_NAME_MAX];

    if (importer.active) {
        end_import("IMPORT STOPPED");
        return;
    }
    if (!system_up) {
        show_note("THE SYSTEM IS NOT UP YET");
        return;
    }
    if (active != 1) {
        show_note("SWITCH TO THE USER TERMINAL (F2)");
        return;
    }
    if (!pick_import_file(name, sizeof name)) {
        screen[1].dirty = ~0ull;
        status_stale = true;
        return;
    }
    screen[1].dirty = ~0ull;
    status_stale = true;
    switch (hostfile_import_open(&importer, name, hp_cpus[HP_SP].ticks)) {
    case 0:
        snprintf(note_text, sizeof note_text, "IMPORTING %.28s", name);
        show_note(note_text);
        break;
    case 1:
        show_note("CANNOT OPEN THE FILE");
        break;
    default:
        show_note("THE FILE HAS NO LINES");
        break;
    }
}

/* F7: start copying what the user terminal prints into EXPORT.TXT, or stop. */
static void toggle_capture(void)
{
    if (capturer.active) {
        uint32_t size = hostfile_capture_close(&capturer);

        if (size != 0)
            snprintf(note_text, sizeof note_text, "SAVED %u BYTES IN %s", (unsigned)size, EXPORT_FILE);
        else
            snprintf(note_text, sizeof note_text, "THE CAPTURE FAILED");
        show_note(note_text);
        return;
    }
    if (hostfile_capture_open(&capturer, EXPORT_FILE) != 0) {
        show_note("CANNOT OPEN " EXPORT_FILE);
        return;
    }
    show_note("CAPTURING TO " EXPORT_FILE);
}

/* Called as often as the machine is run: type the next characters of the file. */
static void service_import(void)
{
    int key;

    if (!importer.active)
        return;
    while (queue_pending(1) < IMPORT_QUEUE_LIMIT &&
           (key = hostfile_import_next(&importer, hp_cpus[HP_SP].ticks)) >= 0)
        queue_put(1, (unsigned)key);
    if (!importer.active)
        end_import(NULL);                       /* the whole file is typed */
}

static void draw_terminal(unsigned index, uint16_t colour)
{
    terminal_t *t = &screen[index];
    unsigned r, cc = t->col < TERM_COLS ? t->col : TERM_COLS - 1;
    uint64_t rows = t->dirty;

    /* Redraw only what changed: a cursor that stays put is left alone, so it
     * does not flicker and the LCD bus is not kept busy. */
    if (t->row != last_cursor_row[index] || cc != last_cursor_col[index])
        rows |= (1ull << t->row) | (1ull << last_cursor_row[index]);
    if (rows == 0)
        return;
    t->dirty = 0;
    last_cursor_row[index] = t->row;
    last_cursor_col[index] = cc;
    for (r = 0; r < TERM_ROWS; r++)
        if (rows & (1ull << r))
            termdraw_row(t->text[r], r, colour, r == t->row ? (int)cc : -1);
}

/* The status line while the machine runs: a turning character shows it is alive, and
 * while the tape is moving the line shows how far along it is and the time so far. */
static void legend_line(char *line, size_t size, uint32_t now)
{
    uint32_t pos, tape_size = 0, secs = (now - run_start_ms) / 1000u;
    char spin = SPINNER_CHARS[(now / SPINNER_MS) % 4u];
    char legend[TCOLS + 1];

    pos = dev_tape_position(&magtape, 0, &tape_size);
    if (pos != tape_seen_pos) {
        tape_seen_pos = pos;
        tape_moved_ms = now;
    }
    if (tape_size != 0 && now - tape_moved_ms < TAPE_BUSY_MS) {
        unsigned pct = (unsigned)((uint64_t)pos * 100u / tape_size);

        if (pct > 99)
            pct = 99;
        snprintf(line, size, "TAPE %2u%% %uK OF %uK  %02u:%02u %c", pct, (unsigned)(pos / 1024u),
                 (unsigned)(tape_size / 1024u), (unsigned)(secs / 60u % 100u),
                 (unsigned)(secs % 60u), spin);
        return;
    }
    if (importer.active) {
        char text[TCOLS + 1];

        snprintf(text, sizeof text, "IMPORT %u OF %u   F6 STOPS", importer.sent, importer.total);
        snprintf(line, size, "%-*s%c", TCOLS - 1, text, spin);
        return;
    }
    if (capturer.active) {
        snprintf(line, size, "%-*s%c", TCOLS - 1, "CAPTURING TO " EXPORT_FILE "  F7 STOPS", spin);
        return;
    }
    snprintf(legend, sizeof legend, "F1 OPERATOR%c F2 USER%c F3 TAPE F4 STOP",
             active == 0 ? '<' : (unseen[0] ? '!' : ' '),
             active == 1 ? '<' : (unseen[1] ? '!' : ' '));
    snprintf(line, size, "%-*s%c", TCOLS - 1, legend, spin);
}

static void draw_status(void)
{
    static char shown[TCOLS + 1];
    char line[TCOLS + 1];

    if (asking_shutdown) {
        snprintf(line, sizeof line, "%-*s", TCOLS, SHUTDOWN_ASK_TEXT);
    } else if (note != NULL && plat_now_ms() < note_until) {
        snprintf(line, sizeof line, "%-*s", TCOLS, note);
    } else {
        note = NULL;
        legend_line(line, sizeof line, plat_now_ms());
    }
    if (strcmp(line, shown) == 0 && !status_stale)
        return;                                 /* nothing new to show */
    status_stale = false;
    memcpy(shown, line, sizeof shown);
    lcd_set_foreground(COL_CYAN);
    lcd_set_reverse(true);
    lcd_putstr(0, STATUS_ROW, line);
    lcd_set_reverse(false);
}

static void draw(void)
{
    static unsigned seen_failures;
    char text[32];
    unsigned n = storage_failures(text, sizeof text);

    if (n != seen_failures) {               /* tell the operator about an SD error */
        char line[64];
        const char *p;

        seen_failures = n;
        snprintf(line, sizeof line, "\r\n[SD ERROR %u: %s]\r\n", n, text);
        for (p = line; *p != 0; p++)
            terminal_put(&screen[0], (uint8_t)*p);
    }
    draw_terminal(active == 0 ? 0 : 1, active == 0 ? COL_OPERATOR : COL_USER);
    draw_status();
}
