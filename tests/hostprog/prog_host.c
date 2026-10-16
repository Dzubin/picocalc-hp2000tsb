/* prog_host.c - host test for the real program (hp2000access.c or hp2000f.c):
 * stands in for the LCD, the keyboard and the platform, types a
 * script of keys at the right moments and prints what the screen shows at the end.
 *
 * Author: Thomas Dzubin.
 *
 * Build (from the project folder, MSYS2 UCRT64), for the Access program:
 *   gcc -O2 -std=gnu99 -Itests/hostprog -I. -Ipicocalc-text-starter-main/drivers \
 *       hp2000access.c tests/hostprog/prog_host.c dev_ipl.c hp_pair.c hp_loaders.c \
 *       hp21xx_iop.c hp21xx.c hp21xx_fp.c hp_dma.c dev_tty.c dev_tbg.c dev_disc.c \
 *       dev_mux.c dev_tape.c tape_image.c terminal.c autotype.c storage_stdio.c \
 *       -o prog_access
 * Run it in a folder that holds the data folder (hp2000access/) with copies of the
 * files. The screen is the 40 x 32 text grid the program draws with lcd_putstr().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "lcd.h"
#include "southbridge.h"
#include "keyboard.h"
#include "platform.h"
#include "hp21xx.h"

#define GRID_ROWS 32
#define GRID_COLS 40

static char grid[GRID_ROWS][GRID_COLS + 1];
static uint32_t started_ms;

typedef struct {
    const char *wait_text;              /* wait until the screen shows this (or NULL) */
    unsigned    delay_ms;               /* then wait this long */
    const char *keys;                   /* \n = Enter, \x01 = F1, \x02 = F2 */
} step_t;

static const step_t script[] = {
    { NULL,        1000, "r" },
    { "DATE (DAY",  300, "\n" },
    { "TIME (HHMM", 300, "\n" },
    { "HP22687A",  100, "\x02" },
    { NULL,        100, "\n" },
    { NULL,        500, "\n" },
    { NULL,        3000, "HELLO-B001,RICK\n" },
    { NULL,        8000, "" },
};
#define STEPS (sizeof script / sizeof script[0])

static unsigned step, key_pos;
static uint32_t step_ready_at;
static int step_armed;

uint32_t plat_now_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount() - started_ms;
#else
    return 0;
#endif
}

static int screen_has(const char *text)
{
    unsigned r;

    for (r = 0; r < GRID_ROWS; r++)
        if (strstr(grid[r], text) != NULL)
            return 1;
    return 0;
}

static void dump_screen(void)
{
    unsigned r;

    printf("[SP P %05o ir %06o halted %d | IOP P %05o ir %06o halted %d]\n", hp_cpus[0].p, hp_cpus[0].ir, hp_cpus[0].halted, hp_cpus[1].p, hp_cpus[1].ir, hp_cpus[1].halted);
    printf("---- screen ----\n");
    for (r = 0; r < GRID_ROWS; r++)
        printf("|%s|\n", grid[r]);
}

uint16_t sb_read_keyboard(void)
{
    const step_t *s;
    uint32_t now = plat_now_ms();

    if (step >= STEPS) {
        dump_screen();
        exit(0);
    }
    s = &script[step];
    if (!step_armed) {
        if (s->wait_text != NULL && !screen_has(s->wait_text))
            return 0;
        step_armed = 1;
        step_ready_at = now + s->delay_ms;
        key_pos = 0;
    }
    if (now < step_ready_at)
        return 0;
    if (s->keys[key_pos] == 0) {
        step++;
        step_armed = 0;
        return 0;
    }
    {
        char c = s->keys[key_pos++];
        if (c == 2) { printf("[before F2: operator screen]\n"); dump_screen(); }
        uint8_t code = c == '\n' ? KEY_ENTER : c == 1 ? KEY_F1 : c == 2 ? KEY_F2 : (uint8_t)c;

        step_ready_at = now + 40;       /* a key every 40 ms */
        return (uint16_t)((KEY_STATE_PRESSED << 8) | code);
    }
}

void sb_init(void) {}
void lcd_init(void)
{
    memset(grid, ' ', sizeof grid);
}
void lcd_enable_cursor(bool on) { (void)on; }
void lcd_set_background(uint16_t c) { (void)c; }
void lcd_set_foreground(uint16_t c) { (void)c; }
void lcd_set_reverse(bool on) { (void)on; }

void lcd_clear_screen(void)
{
    unsigned r;

    for (r = 0; r < GRID_ROWS; r++) {
        memset(grid[r], ' ', GRID_COLS);
        grid[r][GRID_COLS] = 0;
    }
}

void lcd_solid_rectangle(uint16_t colour, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    unsigned row = y / GLYPH_HEIGHT;

    (void)colour; (void)x; (void)w; (void)h;
    if (row < GRID_ROWS)
        memset(grid[row], ' ', GRID_COLS);
}

void lcd_putc(uint8_t column, uint8_t row, uint8_t c)
{
    if (row < GRID_ROWS && column < GRID_COLS)
        grid[row][column] = (char)c;
}

void lcd_putstr(uint8_t column, uint8_t row, const char *str)
{
    while (*str != 0 && column < GRID_COLS)
        lcd_putc(column++, row, (uint8_t)*str++);
}

static DWORD WINAPI watchdog(LPVOID arg)
{
    unsigned n;

    (void)arg;
    Sleep(35000);
    for (n = 0; n < 4; n++) {
        printf("[watchdog: SP P %05o ir %06o ticks %u | IOP P %05o ir %06o ticks %u]\n",
               hp_cpus[0].p, hp_cpus[0].ir, hp_cpus[0].ticks, hp_cpus[1].p, hp_cpus[1].ir,
               hp_cpus[1].ticks);
        fflush(stdout);
        Sleep(1000);
    }
    dump_screen();
    fflush(stdout);
    ExitProcess(9);
    return 0;
}

void plat_init(void)
{
#ifdef _WIN32
    started_ms = (uint32_t)GetTickCount();
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
#endif
    lcd_clear_screen();
}

void plat_sleep_ms(uint32_t ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    (void)ms;
#endif
}

_Noreturn void plat_bootsel(void)
{
    exit(3);
}

_Noreturn void plat_exit_to_loader(void)
{
    dump_screen();
    exit(0);
}

const char *plat_data_dir(void)
{
    return "hp2000access/";
}

int plat_usb_read(void)
{
    return -1;
}

void plat_usb_write(uint8_t c)
{
    (void)c;
}

uint32_t plat_now_us(void)
{
    return plat_now_ms() * 1000u;
}
