/* test_boot.c - host test: boot Time-Shared BASIC 2000E from the kit and use it.
 *
 * Loads the disc bootstrap tape through the paper tape loader, starts the system
 * from the disc image and answers the start-up questions at the operator
 * console, like the kit's tsb-auto command file. Then it logs in on the user
 * terminal (channel 0 of the multiplexer), loads and runs the kit's example
 * program, and logs off. Console output goes to stdout, marked <O> for the
 * operator and <U> for the user terminal.
 *
 * Build from the project folder:
 *   gcc -O2 -std=c99 -Wall -Wextra -I. hp21xx.c hp21xx_fp.c hp_dma.c dev_ptr.c \
 *       dev_tty.c dev_tbg.c dev_disc.c dev_mux.c storage_stdio.c \
 *       tests/test_boot.c -o test_boot
 * Run:  test_boot kitdir diskcopy [max_million_instructions] [trace]
 * The disc image is changed by the run, so give it a copy.
 *
 * Debug aids: set the environment variable BREAK_P (octal) to stop when P reaches
 * that address (BREAK_COUNT for the nth time); the registers, the words at the
 * octal addresses in DUMP (space separated) and, if DUMPFILE is set, all of
 * memory are written.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hp21xx.h"
#include "dev_ptr.h"
#include "dev_tty.h"
#include "dev_tbg.h"
#include "dev_disc.h"
#include "dev_mux.h"
#include "dev_tape.h"
#include "hp_dma.h"
#include "console.h"

/* A script step: when the terminal's output ends with 'expect', type 'reply'. */
typedef struct { const char *expect; const char *reply; } step_t;

static const step_t operator_script[] = {
    { "IS SYSTEM ON SUBCHANNEL 0?", "YES\r" },
    { "DATE?  ", "142/18\r" },
    { "TIME?  ", "1740\r" },
    { "MAG TAPE SLEEP?", "YES\r" },
    { "SELECT CODE?", "20\r" },
    { "SLEEP COMPLETE", NULL },
    { NULL, NULL }
};

static const step_t reload_script[] = {
    { "*", "LOAD,20\r" },
    { "NUMBER OF PORTS? ", "16\r" },
    { "DATE?  ", "142/18\r" },
    { "TIME?  ", "1740\r" },
    { "READY", "SLEEP\r" },
    { "MAG TAPE SLEEP?", "YES\r" },
    { "SELECT CODE?", "20\r" },
    { "SLEEP COMPLETE", NULL },
    { NULL, NULL }
};

/* SLOAD=1: boot, take the user disc (subchannel 1) down, SLEEP to the tape on
 * the drive (TAPE), then mount the tape named by LIBTAPE and selectively load
 * subchannel 1 from it with the loader utility, as the kit's system generation
 * does. */
static const step_t sload_script[] = {
    { "IS SYSTEM ON SUBCHANNEL 0?", "YES\r" },
    { "DATE?  ", "142/18\r" },
    { "TIME?  ", "1740\r" },
    { "READY", "DISC-DN,1\r" },
    { "\n", "SLEEP\r" },
    { "MAG TAPE SLEEP?", "YES\r" },
    { "SELECT CODE?", "20\r" },
    { "*", "SLOAD,1,20,0\r" },
    { "*", NULL },
    { NULL, NULL }
};

static const step_t user_script[] = {
    { "READY", "GET-TEN\r" },              /* after the logon */
    { "GET-TEN", "RUN\r" },
    { "DONE", "BYE\r" },
    { "TERMINAL TIME", NULL },
    { NULL, NULL }
};

typedef struct {
    const step_t *script;
    unsigned      step;
    const char   *reply;
    char          tail[128];
    unsigned      length;
    int           finished;
} terminal_t;

static terminal_t terminal[2] = {
    { operator_script, 0, NULL, "", 0, 0 },
    { user_script, 0, NULL, "", 0, 0 }
};
int disc_trace_on;                      /* used by an instrumented dev_disc.c */
static int operator_ready;
static int greeted;
static unsigned idle_polls;
static unsigned key_polls;
static dev_mux_t mux;
static int sleep_sent;
static dev_tape_t magtape;
static int reload_mode, sload_mode, tape_swapped;
static hp_store_t *tape_image;

static void remember(terminal_t *t, uint8_t c)
{
    const step_t *s;
    size_t n;

    if (t->length >= sizeof t->tail - 1) {
        memmove(t->tail, t->tail + 64, t->length - 64);
        t->length -= 64;
    }
    t->tail[t->length++] = (char)c;
    t->tail[t->length] = 0;
    s = &t->script[t->step];
    if (s->expect == NULL || (t->reply != NULL && *t->reply != 0))
        return;                                 /* still typing the last line */
    n = strlen(s->expect);
    if (t->length >= n && strstr(t->tail, s->expect) != NULL) {
        t->reply = s->reply;
        if (s->reply == NULL)
            t->finished = 1;
        t->step++;
        t->length = 0;
        t->tail[0] = 0;
    }
}

/* Each terminal's output is shown a line at a time, marked <O> or <U>. */
static char line_buffer[2][256];
static unsigned line_length[2];

static void flush_line(unsigned terminal_number)
{
    if (line_length[terminal_number] != 0) {
        line_buffer[terminal_number][line_length[terminal_number]] = 0;
        printf("<%c> %s\n", terminal_number ? 'U' : 'O', line_buffer[terminal_number]);
        fflush(stdout);
        line_length[terminal_number] = 0;
    }
}

void console_write(unsigned terminal_number, uint8_t c)
{
    terminal_t *t = &terminal[terminal_number & 1];
    unsigned k = terminal_number & 1;

    if (c == '\n') {
        flush_line(k);
    } else if (c != '\r' && line_length[k] < sizeof line_buffer[k] - 1) {
        line_buffer[k][line_length[k]++] = (char)c;
    }
    if (c == '\n' && (k == 0 ? 0 : 0))
        return;
    remember(t, c);
    if (terminal_number == CONSOLE_OPERATOR && !operator_ready &&
        t->step >= 3)
        operator_ready = 1;                     /* all three questions answered */
    if (greeted && getenv("DISCTRACE") != NULL)
        disc_trace_on = 1;
    if (sload_mode && terminal_number == CONSOLE_OPERATOR && t->step == 8 && !tape_swapped &&
        getenv("LIBTAPE") != NULL) {
        tape_swapped = 1;                       /* the operator mounts the library tape */
        dev_tape_unmount(&magtape, 0);
        storage_close(tape_image);
        tape_image = storage_open(getenv("LIBTAPE"), 1);
        dev_tape_mount(&magtape, 0, tape_image, 0);
    }
    if (reload_mode && terminal_number == CONSOLE_OPERATOR && t->step >= 4)
        dev_tape_set_online(&magtape, 0);       /* the operator presses ON LINE */
    if (operator_ready && terminal_number == CONSOLE_OPERATOR && !mux.connected[0])
        dev_mux_connect(&mux, 0, getenv("LOCAL") ? 0 : 1);  /* the terminal comes on the line */
}

int console_read(unsigned terminal_number)
{
    terminal_t *t = &terminal[terminal_number & 1];

    if (terminal_number == CONSOLE_OPERATOR && terminal[1].finished && !sleep_sent) {
        sleep_sent = 1;
        t->reply = "SLEEP\r";
    }
    if (terminal_number == CONSOLE_USER) {
        if (!operator_ready)
            return -1;
        if (!greeted && ++idle_polls < 4000)    /* give the system time to settle */
            return -1;
        if (!greeted) {                         /* log on */
            greeted = 1;
            t->reply = "HELLO-B001,RICK\r";
        }
    }
    if (terminal_number == CONSOLE_USER && (++key_polls % 8) != 0)
        return -1;                              /* type slowly: one key per 8 polls */
    if (t->reply != NULL && *t->reply != 0) {
        if (getenv("IOTRACE") != NULL && terminal_number == CONSOLE_USER)
            fprintf(stderr, "[user key %02x]\n", (unsigned char)*t->reply);
        return (unsigned char)*t->reply++;
    }
    t->reply = NULL;
    return -1;
}

/* IOTRACE=1 prints the I/O instructions to the multiplexer cards. */
static void trace_io(unsigned sc, hp_word ir, hp_word reg)
{
    if (sc >= 011 && sc <= 012 && greeted)
        fprintf(stderr, "[io %06o sc %02o reg %06o P %05o]\n", ir, sc, reg, hp.p - 1);
}

/* RING=n keeps the last n instructions and prints them at the end. */
static struct { hp_word p, ir, a, b; } *ring;
static unsigned ring_size, ring_pos;

static void ring_dump(void)
{
    unsigned i;

    for (i = 0; ring != NULL && i < ring_size; i++) {
        unsigned k = (ring_pos + i) % ring_size;

        fprintf(stderr, "  %05o %06o A %06o B %06o\n", ring[k].p, ring[k].ir,
                ring[k].a, ring[k].b);
    }
}

static hp_dma_t dma;
static int follow;
static long log_p = -1;
static long break_p = -1;
static long break_count = 1;

static void show_break(void)
{
    const char *dump = getenv("DUMP");
    char *end;

    fprintf(stderr, "\n[break at P %05o: A %06o B %06o E %d O %d]\n",
            hp.p, hp.a, hp.b, hp.e, hp.o);
    {
        unsigned sc;

        for (sc = 0; sc < HP_CARD_SLOTS; sc++)
            if (hp.card[sc] != NULL)
                fprintf(stderr, "  card %02o: control %d flag %d fb %d\n", sc,
                        hp.card[sc]->control, hp.card[sc]->flag,
                        hp.card[sc]->flag_buffer);
        fprintf(stderr, "  dma_request %d, dma1 cw1 %06o cw2 %06o cw3 %06o xfer %06o\n",
                hp.dma_request, dma.channel[0].cw1, dma.channel[0].cw2,
                dma.channel[0].cw3, dma.channel[0].xfer_sc);
    }
    while (dump != NULL && *dump) {
        long a = strtol(dump, &end, 8);

        if (end == dump)
            break;
        fprintf(stderr, "  [%05lo] = %06o\n", a, hp_read((uint32_t)a));
        dump = end;
    }
    if (getenv("DUMPFILE") != NULL) {           /* all of memory, "address: word" */
        FILE *f = fopen(getenv("DUMPFILE"), "w");
        uint32_t i;

        for (i = 0; f != NULL && i < hp.mem_size; i++)
            fprintf(f, "%05o: %06o\n", i, hp_read(i));
        if (f != NULL)
            fclose(f);
    }
}

static uint8_t *read_file(const char *name, uint32_t *length)
{
    FILE *f = fopen(name, "rb");
    uint8_t *buf;
    long n;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n)
        return NULL;
    fclose(f);
    *length = (uint32_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    static dev_ptr_t ptr;
    static dev_tty_t tty;
    static dev_tbg_t tbg;
    static dev_disc_t disc;
    char path[512];
    uint32_t length;
    uint8_t *tape;
    hp_store_t *image;
    unsigned long max_million = argc > 3 ? strtoul(argv[3], NULL, 10) : 50;
    FILE *trace = argc > 4 ? fopen(argv[4], "w") : NULL;
    unsigned long n, limit;
    int status = HP_RUN_COUNT;

    if (argc < 3) {
        fprintf(stderr, "usage: %s kitdir diskcopy [million_instr] [trace]\n", argv[0]);
        return 2;
    }
    sload_mode = getenv("SLOAD") != NULL;
    reload_mode = getenv("RELOAD") != NULL;
    snprintf(path, sizeof path, "%s/%s", argv[1],
             reload_mode ? "24285-60001_Rev-1534.abin" : "24286-60001_Rev-1438.abin");
    if (reload_mode)
        terminal[0].script = reload_script;
    if (sload_mode)
        terminal[0].script = sload_script;
    tape = read_file(path, &length);
    image = storage_open(argv[2], 1);
    if (tape == NULL || image == NULL) {
        fprintf(stderr, "cannot open the tape or the disc image\n");
        return 2;
    }
    if (getenv("RING") != NULL) {
        ring_size = (unsigned)strtoul(getenv("RING"), NULL, 10);
        ring = calloc(ring_size, sizeof *ring);
    }
    if (getenv("LOG_P") != NULL)
        log_p = strtol(getenv("LOG_P"), NULL, 8);
    if (getenv("BREAK_P") != NULL)
        break_p = strtol(getenv("BREAK_P"), NULL, 8);
    if (getenv("BREAK_COUNT") != NULL)
        break_count = strtol(getenv("BREAK_COUNT"), NULL, 10);

    hp_init(16384, 1);
    if (getenv("IOTRACE") != NULL)
        hp.io_trace = trace_io;
    hp_dma_init(&dma);
    dev_tty_init(&tty, 010);
    dev_disc_init(&disc, 011);
    dev_ptr_init(&ptr, 013);
    dev_mux_init(&mux, 014, 016);
    dev_tbg_init(&tbg, 017);
    dev_tape_init(&magtape, 020);
    hp_preset();
    dev_disc_attach(&disc, 0, image, 0);
    if (getenv("TAPE") != NULL) {
        tape_image = storage_open(getenv("TAPE"), 1);
        if (tape_image == NULL) {
            fprintf(stderr, "cannot open the tape image\n");
            return 2;
        }
        dev_tape_mount(&magtape, 0, tape_image, 0);
    }
    dev_ptr_load_tape(&ptr, tape, length);
    hp.e = 1;

    /* load the disc bootstrap from paper tape */
    hp.s = 0;
    dev_ptr_boot(&ptr);
    status = hp_run(10000000);
    if (status != HP_STOP_HALT || hp.t != 0102077) {
        fprintf(stderr, "paper tape load failed\n");
        return 1;
    }
    status = HP_RUN_COUNT;
    fprintf(stderr, "[bootstrap loaded]\n");

    /* start the system: S = 0, P = 2 */
    hp.s = 0;
    hp.p = reload_mode ? 02000 : 2;
    limit = max_million * 1000000ul;
    for (n = 0; n < limit && status == HP_RUN_COUNT && !terminal[0].finished &&
         !(getenv("NOSLEEP") != NULL && terminal[1].finished); ) {
        if (break_p >= 0 && hp.p == (hp_word)break_p && --break_count <= 0) {
            show_break();
            break;
        }
        if (trace != NULL) {
            hp_word p = hp.p, ir = hp_read(hp.p), a = hp.a, b = hp.b;
            int e = hp.e, o = hp.o;

            status = hp_run(1);
            n++;
            if (hp.last_irq) {                  /* logged as the reference does */
                fprintf(trace, "%05o %06o %06o %06o %d %d\n", p, hp.last_irq, a, b, e, o);
                fprintf(trace, "%05o %06o %06o %06o %d %d\n", hp.last_irq,
                        hp_read(hp.last_irq), a, b, e, o);
            } else {
                fprintf(trace, "%05o %06o %06o %06o %d %d\n", p, ir, a, b, e, o);
            }
        } else if (break_p >= 0 || ring != NULL || log_p >= 0) {
            if (log_p >= 0 && hp.p == (hp_word)log_p)
                fprintf(stderr, "[log %05lo: A %06o B %06o]\n", (unsigned long)log_p, hp.a, hp.b);
            if (follow > 0) {
                fprintf(stderr, "  %05o %06o A %06o B %06o\n", hp.p, hp_read(hp.p), hp.a, hp.b);
                follow--;
            }
            if (ring != NULL) {
                ring[ring_pos].p = hp.p;
                ring[ring_pos].ir = hp_read(hp.p);
                ring[ring_pos].a = hp.a;
                ring[ring_pos].b = hp.b;
                ring_pos = (ring_pos + 1) % ring_size;
            }
            status = hp_run(1);
            n++;
            if (hp.last_irq == 012 && getenv("WATCH_IRQ") != NULL)
                follow = 80;
            if (hp.last_irq && getenv("WATCH_IRQ") != NULL)
                fprintf(stderr, "[irq sc %02o at tick %u, P was %05o]\n", hp.last_irq, hp.ticks, hp.m);
        } else {
            status = hp_run(100000);
            n += 100000;
        }
    }
    fprintf(stderr, "\n[stopped after %lu instructions: status %d, P %05o, A %06o, "
            "B %06o, T %06o, disc errors %u]\n",
            n, status, hp.p, hp.a, hp.b, hp.t, disc.io_errors);
    fprintf(stderr, "[mux: lower flag %d fb %d ctl %d, control flag %d fb %d ctl %d, "
            "int_system %d irq_sc %d, recv_param0 %06o pending %d status %06o connected %d]\n",
            mux.lower.flag, mux.lower.flag_buffer, mux.lower.control,
            mux.control.flag, mux.control.flag_buffer, mux.control.control,
            hp.int_system, hp.irq_sc, mux.recv_param[0], mux.recv_pending[0],
            mux.status[0], mux.connected[0]);
    {
        unsigned sc;

        for (sc = 0; sc < HP_CARD_SLOTS; sc++)
            if (hp.card[sc] != NULL && (hp.card[sc]->flag || hp.card[sc]->control))
                fprintf(stderr, "[card %02o: control %d flag %d fb %d]\n", sc,
                        hp.card[sc]->control, hp.card[sc]->flag,
                        hp.card[sc]->flag_buffer);
    }
    flush_line(0);
    flush_line(1);
    ring_dump();
    if (trace != NULL)
        fclose(trace);
    storage_close(image);
    return 0;
}
