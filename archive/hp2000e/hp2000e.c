/*
 *  HP 2000E Time-Shared BASIC
 *  ===========================================
 *
 *  An emulated HP 2100 computer running HP 2000E Time-Shared BASIC (TSB) from
 *  the 1970s, on the ClockworkPi PicoCalc and (as a desktop build) on Windows
 *  and Linux. The disc image, the SLEEP tape and the two paper tape loaders are
 *  files in one folder on the SD card (see hp2000e_const.h).
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
 *      F4 ............. shut down: SLEEP the system to the tape, then stop
 *      BRK or F5 ...... BREAK
 *      Backspace ...... TSB's character delete (underscore)
 *      CTRL+X ......... TSB's line delete
 *      ~ (SHIFT+`) .... reboot into BOOTSEL (USB drive) mode, on the menu only
 *                       (while the system runs it is an ordinary key, so a
 *                       slip cannot lose the session)
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
#include "dev_ptr.h"
#include "dev_tty.h"
#include "dev_tbg.h"
#include "dev_disc.h"
#include "dev_mux.h"
#include "dev_tape.h"
#include "terminal.h"
#include "autotype.h"
#include "hp2000e_const.h"   /* every constant and screen position */

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

static void menu_screen(const char *message, const char *message2,
                        const char *message3);
static const char *run_machine(bool reload);
static const char *start_machine(bool reload);
static void stop_machine(void);
static int  load_file(const char *name, uint8_t *buffer, uint32_t max, uint32_t *length);
static bool make_path(char *path, const char *name);

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

/* ===================================================================== */
/*  The machine and the terminals                                         */
/* ===================================================================== */
static dev_ptr_t  ptr;
static dev_tty_t  tty;
static dev_tbg_t  tbg;
static dev_disc_t disc;
static dev_mux_t  mux;
static dev_tape_t magtape;
static hp_dma_t   dma;
static hp_store_t *disc_image, *tape_image;
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
static bool       system_up;
static bool       shutting_down;
static char       up_tail[16];
static const char *note;                /* a short message for the status line */
static uint32_t   note_until;
static bool       ctrl_down;

static const autotype_step_t shutdown_steps[] = SHUTDOWN_STEPS;
static const autotype_step_t reload_steps[] = RELOAD_STEPS;

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

    for (;;) {
        const char *message = NULL;
        uint8_t key;

        menu_screen(NULL, NULL, NULL);
        key = wait_any_key();
        if (key == KEY_ESC || key == 'q' || key == 'Q')
            go_loader();      /* does not return */
        if (key == 'b' || key == 'B')
            message = run_machine(false);
        else if (key == 'r' || key == 'R')
            message = run_machine(true);
        if (message != NULL) {
            menu_screen(message, fail_name[0] ? fail_name : NULL,
                        fail_reason[0] ? fail_reason : NULL);
            wait_any_key();
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
    int col = (TCOLS - len) / 2;
    if (col < 0)
        col = 0;
    lcd_set_foreground(fg);
    lcd_putstr((uint8_t)col, (uint8_t)row, str);
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
static void menu_screen(const char *message, const char *message2,
                        const char *message3)
{
    lcd_clear_screen();
    put_row(MENU_ROW_TITLE, "HP 2000E TIME-SHARED BASIC", COL_CYAN);
    put_row(MENU_ROW_VERSION, VERSION, COL_CYAN);
    put_row(MENU_ROW_AUTHOR, "BY THOMAS DZUBIN", COL_WHITE);
    put_row(MENU_ROW_BOOT, "B   START THE SYSTEM", COL_GREEN);
    put_row(MENU_ROW_RELOAD, "R   RELOAD FROM THE SLEEP TAPE", COL_GREEN);
    put_row(MENU_ROW_EXIT, "ESC / Q   EXIT", COL_WHITE);
    if (message != NULL)
        put_row(MENU_ROW_MESSAGE, message, COL_YELLOW);
    if (message2 != NULL)
        put_row(MENU_ROW_MESSAGE2, message2, COL_WHITE);
    if (message3 != NULL)
        put_row(MENU_ROW_MESSAGE3, message3, COL_WHITE);
    if (message != NULL)
        put_row(MENU_ROW_PRESS_KEY, "PRESS ANY KEY", COL_WHITE);
}

/* ===================================================================== */
/*  Starting and stopping the machine                                     */
/* ===================================================================== */
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

/* Build the machine and load the first program. Returns an error message, or NULL
 * when the machine is ready to run. */
static const char *start_machine(bool reload)
{
    char path[PATH_MAX_LEN];
    const char *tape_name = reload ? FILE_LOADER_TAPE : FILE_BOOT_TAPE;
    uint32_t length;
    int status;

    fail_name[0] = fail_reason[0] = '\0';
    if (storage_mount() != 0) {
        snprintf(fail_reason, sizeof fail_reason, "%s", storage_last_error());
        return "NO SD CARD FOUND";
    }
    if (load_file(tape_name, paper_tape, PAPER_TAPE_MAX, &length) != 0) {
        note_failure(tape_name);
        return reload ? "LOADER TAPE NOT FOUND" : "BOOT TAPE NOT FOUND";
    }

    make_path(path, FILE_DISC);
    disc_image = storage_open(path, 1);
    if (disc_image == NULL) {
        note_failure(FILE_DISC);
        return "DISC IMAGE NOT FOUND";
    }
    make_path(path, FILE_TAPE);
    tape_image = storage_open(path, 1);
    if (tape_image == NULL && !reload)
        tape_image = storage_create(path);          /* the first SLEEP fills it */
    if (tape_image == NULL) {
        note_failure(FILE_TAPE);
        storage_close(disc_image);
        return "SLEEP TAPE NOT FOUND";
    }

    /* The device structures are static, so a second start (a boot after a
     * reload, say) would otherwise find the first run's leftover state. A
     * program run once from the shell never sees that; here it must be cleared. */
    memset(&dma, 0, sizeof dma);
    memset(&tty, 0, sizeof tty);
    memset(&disc, 0, sizeof disc);
    memset(&ptr, 0, sizeof ptr);
    memset(&mux, 0, sizeof mux);
    memset(&tbg, 0, sizeof tbg);
    memset(&magtape, 0, sizeof magtape);

    hp_init(MEMORY_WORDS, 1);
    hp_dma_init(&dma);
    dev_tty_init(&tty, SC_TELEPRINTER);
    dev_disc_init(&disc, SC_DISC);
    dev_ptr_init(&ptr, SC_PAPER_TAPE);
    dev_mux_init(&mux, SC_MUX_LOWER, SC_MUX_CONTROL);
    dev_tbg_init(&tbg, SC_TIME_BASE);
    dev_tape_init(&magtape, SC_TAPE);
    hp_preset();
    dev_disc_attach(&disc, 0, disc_image, 0);
    dev_tape_mount(&magtape, 0, tape_image, 0);

    /* the paper tape loader reads the first program and halts */
    dev_ptr_load_tape(&ptr, paper_tape, length);
    hp.s = 0;
    dev_ptr_boot(&ptr);
    status = hp_run(LOADER_RUN_LIMIT);
    if (status != HP_STOP_HALT || hp.t != 0102077) {
        stop_machine();
        return "PAPER TAPE LOAD FAILED";
    }
    hp.s = 0;
    hp.p = reload ? RELOAD_START : BOOT_START;

    terminal_init(&screen[0]);
    terminal_init(&screen[1]);
    memset(queue_head, 0, sizeof queue_head);
    memset(queue_tail, 0, sizeof queue_tail);
    unseen[0] = unseen[1] = false;
    last_cursor_row[0] = last_cursor_row[1] = 0;
    last_cursor_col[0] = last_cursor_col[1] = 0;
    status_stale = true;
    active = CONSOLE_OPERATOR;
    system_up = shutting_down = false;
    up_tail[0] = 0;
    note = NULL;
    ctrl_down = false;
    autotype_start(&operator_typist, reload ? reload_steps : NULL, NULL);
    return NULL;
}

static void stop_machine(void)
{
    storage_close(disc_image);
    storage_close(tape_image);
    disc_image = tape_image = NULL;
}

/* Run the machine until it halts or is shut down. Returns a message for the menu. */
static const char *run_machine(bool reload)
{
    static char halted[TCOLS + 1];
    const char *error = start_machine(reload);

    if (error != NULL)
        return error;
    lcd_clear_screen();
    run_loop();
    stop_machine();
    if (shutting_down && autotype_finished(&operator_typist))
        return "SYSTEM SLEPT. TAPE WRITTEN.";
    snprintf(halted, sizeof halted, "HALT  P %05o  T %06o", hp.p, hp.t);
    return halted;
}

/* ===================================================================== */
/*  The run loop                                                          */
/* ===================================================================== */
static void run_loop(void)
{
    uint32_t last_keys = plat_now_ms(), last_draw = last_keys, last_cal = last_keys;
    uint32_t ticks_at_cal = hp.ticks;

    screen[0].dirty = ~0u;
    for (;;) {
        uint32_t now;
        int status = hp_run(RUN_CHUNK);

        if (status != HP_RUN_COUNT)
            return;                             /* a halt or a fault */
        if (shutting_down && autotype_finished(&operator_typist))
            return;
        now = plat_now_ms();
        if (now - last_keys >= KEY_POLL_MS) {
            last_keys = now;
            poll_keyboard();
            poll_usb();
        }
        if (now - last_cal >= CALIBRATE_MS) {
            dev_tbg_calibrate(&tbg, hp.ticks - ticks_at_cal, now - last_cal);
            ticks_at_cal = hp.ticks;
            last_cal = now;
        }
        if (now - last_draw >= DRAW_MS) {
            last_draw = now;
            draw();
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

static void handle_key(uint8_t code)
{
    unsigned terminal = active == 0 ? 0 : 1;

    switch (code) {
    case KEY_OPERATOR_CONSOLE:
        active = 0;
        unseen[0] = false;
        screen[0].dirty = ~0u;
        return;
    case KEY_USER_CONSOLE:
        active = 1;
        unseen[1] = false;
        screen[1].dirty = ~0u;
        return;
    case KEY_TAPE_ONLINE:
        dev_tape_set_online(&magtape, 0);
        show_note("TAPE ON LINE");
        return;
    case KEY_SHUTDOWN:
        if (!shutting_down) {
            shutting_down = true;
            dev_tape_set_online(&magtape, 0);
            autotype_start(&operator_typist, shutdown_steps, SHUTDOWN_FIRST);
            active = 0;
            screen[0].dirty = ~0u;
            show_note("SHUTTING DOWN");
        }
        return;
    case KEY_BREAK:
    case KEY_BREAK_ALT:
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

static void draw_terminal(unsigned index, uint16_t colour)
{
    terminal_t *t = &screen[index];
    unsigned r, cc = t->col < TERM_COLS ? t->col : TERM_COLS - 1;
    uint32_t rows = t->dirty;

    /* Redraw only what changed: a cursor that stays put is left alone, so it
     * does not flicker and the LCD bus is not kept busy. */
    if (t->row != last_cursor_row[index] || cc != last_cursor_col[index])
        rows |= (1u << t->row) | (1u << last_cursor_row[index]);
    if (rows == 0)
        return;
    t->dirty = 0;
    last_cursor_row[index] = t->row;
    last_cursor_col[index] = cc;
    lcd_set_foreground(colour);
    for (r = 0; r < TERM_ROWS; r++)
        if (rows & (1u << r))
            lcd_putstr(0, (uint8_t)r, t->text[r]);
    lcd_set_reverse(true);                      /* the cursor cell */
    lcd_putc((uint8_t)cc, (uint8_t)t->row, (uint8_t)t->text[t->row][cc]);
    lcd_set_reverse(false);
}

static void draw_status(void)
{
    static char shown[TCOLS + 1];
    char line[TCOLS + 1];

    if (note != NULL && plat_now_ms() < note_until) {
        snprintf(line, sizeof line, "%-*s", TCOLS, note);
    } else {
        note = NULL;
        snprintf(line, sizeof line, "F1 OPERATOR%c F2 USER%c F3 TAPE F4 STOP",
                 active == 0 ? '<' : (unseen[0] ? '!' : ' '),
                 active == 1 ? '<' : (unseen[1] ? '!' : ' '));
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
    draw_terminal(active == 0 ? 0 : 1, active == 0 ? COL_OPERATOR : COL_USER);
    draw_status();
}
