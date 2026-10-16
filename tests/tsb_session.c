/* tsb_session.c - host tool: run an emulated 2000E, 2000F or Access from a
 * script file.
 *
 * Author: Thomas Dzubin.
 *
 * Where test_boot.c has one fixed session built in, this program reads the
 * session from a text file, so a system can be prepared on the PC: generate a
 * 2000F or Access system on a 2883 disc (genf, genaccess), reload or boot it,
 * load a library of BASIC programs through a user terminal and SAVE them, then
 * HIBERNATE it. The finished disc and tape images are copied to the SD card.
 *
 * Build from the project folder (the 2000E-only files are not needed):
 *   gcc -O2 -std=gnu99 -Wall -Wextra -I. hp21xx.c hp21xx_fp.c hp21xx_iop.c \
 *       hp_dma.c dev_ptr.c dev_tty.c dev_tbg.c dev_disc.c dev_dq.c dev_mux.c \
 *       dev_tape.c tape_image.c dev_ipl.c hp_pair.c hp_loaders.c \
 *       storage_stdio.c tests/tsb_session.c -o tsb_session
 * Run:  tsb_session kitdir script.txt
 * Environment knobs: DISC_MODEL=2883, KEY_POLLS=1 (fast typing), LIB_DRYRUN=1
 * (only find out what TSB accepts), GEN_DRIVES, IOP_BUFFER, HEADSTART,
 * MUX_DEBUG, TRACE_F, BOTH_LINES.
 *
 * Script commands (one per line, # starts a comment, strings in double quotes
 * with the escapes \r \n \\ \"). Besides those listed here: system f|access,
 * genaccess, genf, reloadaccess and bootaccess (see tests/tsb_access.inc,
 * tests/tsb_access_reload.inc and tests/tsb_f_gen.inc).
 *   disc UNIT PATH [new]    attach a disc image to a drive (new: create it if missing)
 *   tape PATH               mount a tape image on the tape drive (replaces any)
 *   online                  press the tape drive's ON LINE button
 *   boot disc | boot loader load the bootstrap or the loader utility and start it
 *   connect                 put the first user terminal on the line
 *   expect O|U "text" [M]   wait for text on the operator or user terminal
 *                           (at most M million instructions, default 400)
 *   send O|U "text"         type the text
 *   sendfile U PATH         type a text file, one line at a time, and report any
 *                           line TSB answers with ???
 *   run M                   run M million instructions
 *   gap K                   after each line sent, give TSB K thousand instructions
 *                           to answer (default 1500)
 *   quiet on|off            do not print the user terminal's output
 *   library PATH            load programs through the user terminal and save them
 *                           as A000's system library (see load_library); PATH is a
 *                           manifest, one program per line: NAME SUBCHANNEL FILE
 *   quit                    stop (the images are closed)
 * The operator and user terminals both print to stdout, marked <O> and <U>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hp21xx.h"
#include "dev_ptr.h"
#include "dev_tty.h"
#include "dev_tbg.h"
#include "dev_disc.h"
#include "dev_disc_const.h"
#include "dev_dq.h"
#include "dev_mux.h"
#include "dev_tape.h"
#include "console.h"
#include "storage.h"
#include "hp_dma.h"
#include "dev_ipl.h"
#include "hp_pair.h"
#include "hp_loaders.h"

#define CHUNK            20000u     /* instructions per step of the interpreter */
#define DEFAULT_LIMIT_M  400u
#define USER_KEY_POLLS_DEFAULT 8u  /* the user line takes one key per this many polls */
#define LINE_GAP_K       1500u      /* thousand instructions TSB gets to finish a sent line */
#define MAX_ACTIONS      512u
#define LOADER_RUN_LIMIT 10000000u

typedef enum { A_DISC, A_TAPE, A_ONLINE, A_BOOT, A_CONNECT, A_EXPECT, A_SEND,
               A_SENDFILE, A_RUN, A_QUIT, A_GAP, A_QUIET, A_LIBRARY, A_SYSTEM, A_GEN,
               A_BOOT_ACCESS } action_type_t;

typedef struct {
    action_type_t type;
    int           term;             /* 0 operator, 1 user */
    char         *text;             /* string, or path */
    char         *text2, *text3;    /* more paths (genaccess) */
    unsigned      number;           /* unit, or million instructions */
    int           flag;
} action_t;

typedef struct {
    char    *out;                   /* everything the terminal printed */
    size_t   len, cap, scan;
    char    *in;                    /* characters waiting to be typed */
    size_t   in_len, in_pos;
    unsigned polls;
    char     line[256];
    unsigned line_len;
} term_t;

static term_t term[2];
static dev_ptr_t  ptr;
static dev_tty_t  tty;
static dev_tbg_t  tbg;
static dev_disc_t disc;
static dev_dq_t   dq;                   /* the 2883 disc, when env DISC_MODEL=2883 (Access) */
static int        use_dq;
static dev_mux_t  mux;
static dev_tape_t magtape;
static hp_dma_t   dma;
static int        system_f;             /* a 2000F: System Processor and I/O Processor */
static int        system_access;        /* an Access system: two bigger CPUs, other select codes */
static void dump_io_trace(void);        /* in tsb_access.inc */
static void dump_iop_trace(void);
static dev_tbg_t  tbg_iop;
static hp_dma_t   dma_iop;
static dev_ipl_t  ipl_sp, ipl_iop;
static ipl_bus_t  ipl_bus[IPL_CARDS];
static hp_store_t *disc_image[DISC_DRIVES], *tape_image;
static const char *kit_dir;
static unsigned long long total_instructions;
static unsigned line_gap = LINE_GAP_K * 1000u;
static int quiet_user;

/* ---- the terminals ---------------------------------------------------------- */

static void flush_line(int k)
{
    if (term[k].line_len != 0) {
        term[k].line[term[k].line_len] = 0;
        printf("<%c> %s\n", k ? 'U' : 'O', term[k].line);
        fflush(stdout);
        term[k].line_len = 0;
    }
}

void console_write(unsigned terminal_number, uint8_t c)
{
    term_t *t = &term[terminal_number & 1];
    int k = (int)(terminal_number & 1);

    if (c == '\n') {
        flush_line(k);
    } else if (c != '\r' && t->line_len < sizeof t->line - 1) {
        t->line[t->line_len++] = (char)c;
    }
    if (t->len + 2 > t->cap) {
        t->cap = t->cap ? t->cap * 2 : 65536;
        t->out = realloc(t->out, t->cap);
        if (t->out == NULL) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    t->out[t->len++] = (char)c;
    t->out[t->len] = 0;
}

/* Polls between keys on the user line; env KEY_POLLS speeds up library loading. */
static unsigned key_polls(void)
{
    static unsigned n;

    if (n == 0) {
        const char *e = getenv("KEY_POLLS");

        n = e ? (unsigned)atoi(e) : USER_KEY_POLLS_DEFAULT;
        if (n == 0)
            n = 1;
    }
    return n;
}

int console_read(unsigned terminal_number)
{
    term_t *t = &term[terminal_number & 1];

    if (t->in_pos >= t->in_len)
        return -1;
    if (terminal_number == CONSOLE_USER && (++t->polls % key_polls()) != 0)
        return -1;
    if (terminal_number == CONSOLE_USER && getenv("MUX_DEBUG") != NULL)
        fprintf(stderr, "[IOP ticks %u user key %d read; mux conn %d recv_param %06o status %06o pending %d "
                "lower f%d fb%d c%d control f%d fb%d c%d; IOP ints %d irq %o]\n",
                (unsigned)hp_cpus[HP_IOP].ticks, (unsigned char)t->in[t->in_pos], mux.connected[0], mux.recv_param[0], mux.status[0],
                mux.recv_pending[0], mux.lower.flag, mux.lower.flag_buffer, mux.lower.control,
                mux.control.flag, mux.control.flag_buffer, mux.control.control,
                hp_cpus[HP_IOP].int_system, hp_cpus[HP_IOP].irq_sc);
    return (unsigned char)t->in[t->in_pos++];
}

static void type_text(int k, const char *s)
{
    term_t *t = &term[k];
    size_t n = strlen(s);

    t->in = realloc(t->in, t->in_len + n + 1);
    memcpy(t->in + t->in_len, s, n);
    t->in_len += n;
}

static int typing_done(int k)
{
    return term[k].in_pos >= term[k].in_len;
}

/* ---- helpers ------------------------------------------------------------------ */

static uint8_t *read_file(const char *path, uint32_t *length)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long size;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    if (data != NULL && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *length = (uint32_t)size;
    return data;
}

/* Run the machine for some instructions. Returns 0, or 1 if the CPU stopped. */
static int run_chunk(unsigned n)
{
    int status;

    total_instructions += n;
    if (system_f) {
        int stop = hp_pair_run(n);

        if (stop != 0) {
            unsigned cpu = (unsigned)stop - 1;

            hp_select(cpu);
            flush_line(0);
            flush_line(1);
            printf("[%s stopped: status %d, P %05o, T %06o]\n", cpu ? "IOP" : "SP",
                   hp_pair_status(cpu), hp.p, hp.t);
            for (cpu = 0; cpu < HP_CPU_COUNT; cpu++) {
                hp_select(cpu);
                printf("[%s now: P %05o A %06o B %06o IR %06o stopped %d]\n", cpu ? "IOP" : "SP",
                       hp.p, hp.a, hp.b, hp.ir, hp_pair_stopped(cpu));
            }
            hp_select(HP_SP);
            if (system_access) {
                dump_io_trace();
                dump_iop_trace();
            }
            return 1;
        }
        return 0;
    }
    status = hp_run(n);
    if (status != HP_RUN_COUNT) {
        flush_line(0);
        flush_line(1);
        printf("[CPU stopped: status %d, P %05o, T %06o]\n", status, hp.p, hp.t);
        return 1;
    }
    return 0;
}

static int boot_from_tape(const char *file, unsigned start)
{
    char path[512];
    uint32_t length;
    uint8_t *data;
    int status;

    snprintf(path, sizeof path, "%s/%s", kit_dir, file);
    data = read_file(path, &length);
    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    dev_ptr_load_tape(&ptr, data, length);
    hp.s = 0;
    dev_ptr_boot(&ptr);
    status = hp_run(LOADER_RUN_LIMIT);
    if (status != HP_STOP_HALT || hp.t != 0102077) {
        fprintf(stderr, "paper tape load of %s failed\n", file);
        return 1;
    }
    hp.s = 0;
    hp.p = (hp_word)start;
    return 0;
}

/* ---- the 2000F: System Processor plus I/O Processor -------------------------- */

#define F_SP_WORDS       32768u
#define F_IOP_WORDS      8192u
#define F_SC_IPL         010
#define F_SC_TTY         012
#define F_SC_PTR         013
#define F_SC_TBG_SP      014
#define F_SC_DISC        015
#define F_SC_TAPE        021
#define F_SC_TBG_IOP     012
#define F_SC_MUX_LOWER   013
#define F_SC_MUX_CONTROL 015

static void build_f_system(void)
{
    hp_select(HP_SP);
    hp_init(F_SP_WORDS, 1);
    hp_dma_init(&dma);
    dev_ipl_init(&ipl_sp, HP_SP, 0, F_SC_IPL, ipl_bus);
    dev_tty_init(&tty, F_SC_TTY);
    dev_ptr_init(&ptr, F_SC_PTR);
    dev_tbg_init(&tbg, F_SC_TBG_SP);
    use_dq = getenv("DISC_MODEL") != NULL && strcmp(getenv("DISC_MODEL"), "2883") == 0;
    if (use_dq)
        dev_dq_init(&dq, F_SC_DISC);
    else
        dev_disc_init(&disc, F_SC_DISC);
    dev_tape_init(&magtape, F_SC_TAPE);
    hp_preset();
    hp.e = 1;

    hp_select(HP_IOP);
    hp_init(F_IOP_WORDS, 0);
    hp_dma_init(&dma_iop);
    dev_ipl_init(&ipl_iop, HP_IOP, 1, F_SC_IPL, ipl_bus);
    dev_tbg_init(&tbg_iop, F_SC_TBG_IOP);
    dev_mux_init(&mux, F_SC_MUX_LOWER, F_SC_MUX_CONTROL);
    hp_preset();
    hp.e = 1;

    hp_select(HP_SP);
    hp_pair_init(&ipl_sp, &ipl_iop);
}

/* Start a 2000F: the System Processor reads the cross-loader from paper tape and
 * runs it; it sends the I/O Processor's program across the interconnect to the
 * I/O Processor's binary loader. Then both are started and the System Processor
 * boots the disc. */
static int boot_f(int mode)             /* 0 boot the disc, 1 load the TSB loader, 2 start a generation */
{
    int reload = mode != 0;
    char path[512];
    uint32_t length;
    uint8_t *data;
    int status;

    snprintf(path, sizeof path, "%s/24343-60001_Rev-1710.abin", kit_dir);
    data = read_file(path, &length);
    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    hp_select(HP_SP);
    dev_ptr_load_tape(&ptr, data, length);
    hp.s = 0;
    dev_ptr_boot(&ptr);
    status = hp_run(LOADER_RUN_LIMIT);
    if (status != HP_STOP_HALT || hp.t != 0102077) {
        fprintf(stderr, "paper tape load of the cross-loader failed (status %d, T %06o)\n",
                status, hp.t);
        return 1;
    }
    fprintf(stderr, "[cross-loader loaded]\n");
    hp.s = 0;
    hp.p = 2;                           /* the cross-loader starts at 2 */
    hp_pair_resume(HP_SP);

    hp_select(HP_IOP);
    hp_load_iop_loader(F_SC_IPL);
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);

    /* run both until each has stopped at the end of the transfer */
    while (!(hp_pair_stopped(HP_SP) && hp_pair_stopped(HP_IOP))) {
        if (hp_pair_run(1000) != 0)
            continue;
        total_instructions += 1000;
        if (total_instructions > 2000000000ull) {
            fprintf(stderr, "the I/O Processor program transfer did not finish\n");
            return 1;
        }
    }
    hp_select(HP_SP);
    fprintf(stderr, "[I/O Processor program transferred: SP T %06o, IOP T ", hp.t);
    hp_select(HP_IOP);
    fprintf(stderr, "%06o]\n", hp.t);
    if (hp.t != 0102077) {
        hp_select(HP_SP);
        return 1;
    }

    /* start the I/O Processor program, then boot the disc on the System Processor */
    hp.s = 0;
    hp.p = 2;
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    hp.s = 0;
    if (reload) {                       /* the TSB loader reads the HIBERNATE tape */
        snprintf(path, sizeof path, "%s/%s", kit_dir,
                 use_dq ? "24339-60001_Rev-1710.abin" : "24338-60001_Rev-1710.abin");
        data = read_file(path, &length);
        if (data == NULL) {
            fprintf(stderr, "cannot read %s\n", path);
            return 1;
        }
        dev_ptr_load_tape(&ptr, data, length);
        dev_ptr_boot(&ptr);
        status = hp_run(LOADER_RUN_LIMIT);
        if (status != HP_STOP_HALT || hp.t != 0102077) {
            fprintf(stderr, "paper tape load of the TSB loader failed\n");
            return 1;
        }
        if (mode == 2) {                /* the first system tape is read by the loader */
            snprintf(path, sizeof path, "%s/24337-60001_Rev-1410.abin", kit_dir);
            data = read_file(path, &length);
            if (data == NULL) {
                fprintf(stderr, "cannot read %s\n", path);
                return 1;
            }
            dev_ptr_load_tape(&ptr, data, length);
        }
        hp.s = 0;
        hp.p = 02000;
        hp_pair_resume(HP_SP);
        return 0;
    }
    if (use_dq)
        hp_load_dq_bootstrap(F_SC_DISC);
    else
        hp_load_disc_bootstrap(F_SC_DISC);
    hp_pair_resume(HP_SP);

    /* the disc bootstrap loads the system and halts; then the operator presses RUN */
    while (!hp_pair_stopped(HP_SP)) {
        int stop = hp_pair_run(1000);

        total_instructions += 1000;
        if (stop == 1 + (int)HP_IOP) {
            fprintf(stderr, "the I/O Processor stopped during the system load\n");
            return 1;
        }
        if (total_instructions > 4000000000ull) {
            fprintf(stderr, "the system did not load\n");
            return 1;
        }
    }
    if (hp.t != 0102077) {
        fprintf(stderr, "the system load stopped with T %06o\n", hp.t);
        return 1;
    }
    hp.s = 0;
    hp_pair_resume(HP_SP);
    return 0;
}

static void open_disc(unsigned unit, const char *path, int create)
{
    hp_store_t *image = storage_open(path, 1);

    if (image == NULL && create) {
        static const uint8_t zero = 0;

        image = storage_create(path);
        if (image != NULL)
            storage_write(image, (use_dq ? DQ_IMAGE_BYTES : DISC_IMAGE_BYTES) - 1, &zero, 1);   /* sets the size */
    }
    if (image == NULL) {
        fprintf(stderr, "cannot open the disc image %s\n", path);
        exit(2);
    }
    if (unit >= (use_dq ? DQ_DRIVES : DISC_DRIVES)) {
        fprintf(stderr, "bad drive %u\n", unit);
        exit(2);
    }
    disc_image[unit] = image;
    if (use_dq)
        dev_dq_attach(&dq, unit, image, 0);
    else
        dev_disc_attach(&disc, unit, image, 0);
}

static void mount_tape(const char *path)
{
    if (tape_image != NULL) {
        dev_tape_unmount(&magtape, 0);
        storage_close(tape_image);
    }
    tape_image = storage_open(path, 1);
    if (tape_image == NULL) {
        fprintf(stderr, "cannot open the tape image %s\n", path);
        exit(2);
    }
    dev_tape_mount(&magtape, 0, tape_image, 0);
}

#include "tsb_access.inc"
#include "tsb_access_reload.inc"
#include "tsb_f_gen.inc"

/* ---- the script --------------------------------------------------------------- */

static action_t actions[MAX_ACTIONS];
static unsigned action_count;

/* Decode a quoted string argument (the escapes \r \n \\ \"). Returns the end. */
static char *parse_quoted(char *p, char **result)
{
    char *out;

    while (*p == ' ')
        p++;
    if (*p != '"') {
        fprintf(stderr, "a quoted string was expected\n");
        exit(2);
    }
    p++;
    *result = out = p;
    while (*p != 0 && *p != '"') {
        if (*p == '\\' && p[1] != 0) {
            p++;
            *out++ = *p == 'r' ? '\r' : *p == 'n' ? '\n' : *p;
            p++;
        } else {
            *out++ = *p++;
        }
    }
    if (*p == '"')
        p++;
    *out = 0;
    return p;
}

static char *word(char **p)
{
    char *start;

    while (**p == ' ')
        (*p)++;
    start = *p;
    while (**p != 0 && **p != ' ')
        (*p)++;
    if (**p != 0)
        *(*p)++ = 0;
    return start;
}

static void read_script(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1024];

    if (f == NULL) {
        fprintf(stderr, "cannot open the script %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line, *cmd;
        action_t *a;
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        while (*p == ' ')
            p++;
        if (*p == 0 || *p == '#')
            continue;
        if (action_count >= MAX_ACTIONS) {
            fprintf(stderr, "too many script lines\n");
            exit(2);
        }
        a = &actions[action_count++];
        memset(a, 0, sizeof *a);
        cmd = word(&p);
        if (strcmp(cmd, "disc") == 0) {
            char *unit = word(&p), *file = word(&p), *opt = word(&p);

            a->type = A_DISC;
            a->number = (unsigned)atoi(unit);
            a->text = strdup(file);
            a->flag = strcmp(opt, "new") == 0;
        } else if (strcmp(cmd, "tape") == 0) {
            a->type = A_TAPE;
            a->text = strdup(word(&p));
        } else if (strcmp(cmd, "online") == 0) {
            a->type = A_ONLINE;
        } else if (strcmp(cmd, "boot") == 0) {
            char *which = word(&p);

            a->type = A_BOOT;
            a->flag = strcmp(which, "loader") == 0;
            a->number = strcmp(which, "f") == 0 ? 1 : strcmp(which, "freload") == 0 ? 2 : 0;
        } else if (strcmp(cmd, "system") == 0) {
            a->type = A_SYSTEM;
            char *which = word(&p);

            if (strcmp(which, "f") == 0)
                system_f = 1;
            else if (strcmp(which, "access") == 0)
                system_f = system_access = 1;
        } else if (strcmp(cmd, "genaccess") == 0) {
            char *which = word(&p);

            a->type = A_GEN;
            a->flag = strcmp(which, "sp") == 0;
            a->text = strdup(word(&p));
            a->text2 = strdup(word(&p));
            a->text3 = strdup(word(&p));
        } else if (strcmp(cmd, "genf") == 0) {
            a->type = A_GEN;
            a->flag = 3;
            a->text = strdup(word(&p));
            a->text2 = strdup(word(&p));
        } else if (strcmp(cmd, "reloadaccess") == 0) {
            a->type = A_GEN;
            a->flag = 2;
            a->text = strdup(word(&p));
            a->text2 = strdup(word(&p));
            a->text3 = strdup("");
        } else if (strcmp(cmd, "bootaccess") == 0) {
            a->type = A_BOOT_ACCESS;
            a->text = strdup(word(&p));
        } else if (strcmp(cmd, "connect") == 0) {
            a->type = A_CONNECT;
        } else if (strcmp(cmd, "expect") == 0 || strcmp(cmd, "send") == 0) {
            char *who = word(&p);

            a->type = cmd[0] == 'e' ? A_EXPECT : A_SEND;
            a->term = who[0] == 'U';
            p = parse_quoted(p, &a->text);
            a->text = strdup(a->text);
            a->number = (unsigned)atoi(p) ? (unsigned)atoi(p) : DEFAULT_LIMIT_M;
        } else if (strcmp(cmd, "sendfile") == 0) {
            a->type = A_SENDFILE;
            a->term = word(&p)[0] == 'U';
            a->text = strdup(word(&p));
        } else if (strcmp(cmd, "run") == 0) {
            a->type = A_RUN;
            a->number = (unsigned)atoi(word(&p));
        } else if (strcmp(cmd, "gap") == 0) {
            a->type = A_GAP;
            a->number = (unsigned)atoi(word(&p));
        } else if (strcmp(cmd, "quiet") == 0) {
            a->type = A_QUIET;
            a->flag = strcmp(word(&p), "on") == 0;
        } else if (strcmp(cmd, "library") == 0) {
            a->type = A_LIBRARY;
            a->text = strdup(word(&p));
        } else if (strcmp(cmd, "quit") == 0) {
            a->type = A_QUIT;
        } else {
            fprintf(stderr, "unknown script command: %s\n", cmd);
            exit(2);
        }
    }
    fclose(f);
}

/* ---- running the script ------------------------------------------------------- */

typedef struct {
    char   **lines;                 /* sendfile: the lines of the file */
    unsigned count, next;
    unsigned errors;
    int      waiting;               /* a line was typed; wait for TSB */
    size_t   mark;
    unsigned long long wait_until;
} sendfile_t;

static sendfile_t sf;

static int load_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    char buffer[1024];
    unsigned cap = 0;

    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    sf.lines = NULL;
    sf.count = sf.next = sf.errors = 0;
    while (fgets(buffer, sizeof buffer, f) != NULL) {
        size_t n = strlen(buffer);

        while (n > 0 && (buffer[n - 1] == '\n' || buffer[n - 1] == '\r'))
            buffer[--n] = 0;
        if (sf.count == cap) {
            cap = cap ? cap * 2 : 256;
            sf.lines = realloc(sf.lines, cap * sizeof *sf.lines);
        }
        sf.lines[sf.count++] = strdup(buffer);
    }
    fclose(f);
    sf.waiting = 0;
    return 0;
}

/* ---- loading a library of programs --------------------------------------------- */

/* Run n instructions. Returns 1 if the CPU stopped. */
static int run_n(unsigned long long n)
{
    unsigned long long end = total_instructions + n;

    while (total_instructions < end)
        if (run_chunk(CHUNK))
            return 1;
    return 0;
}

/* Type text on a terminal and run until it has all been taken. */
static int type_and_wait(int k, const char *text)
{
    unsigned long long guard = total_instructions + 400000000ull;

    type_text(k, text);
    while (!typing_done(k)) {
        if (run_chunk(CHUNK))
            return 1;
        if (total_instructions > guard) {
            unsigned sc;

            fprintf(stderr, "[typing stalled at %llu million instructions: %zu of %zu characters taken]\n",
                    total_instructions / 1000000ull, term[k].in_pos, term[k].in_len);
            fprintf(stderr, "[mux: connected %d, recv_pending %d, status %06o, recv_param %06o, "
                    "lower flag %d fb %d ctl %d, control flag %d fb %d ctl %d, int_system %d irq_sc %d]\n",
                    mux.connected[0], mux.recv_pending[0], mux.status[0], mux.recv_param[0],
                    mux.lower.flag, mux.lower.flag_buffer, mux.lower.control,
                    mux.control.flag, mux.control.flag_buffer, mux.control.control,
                    hp.int_system, hp.irq_sc);
            for (sc = 0; sc < HP_CARD_SLOTS; sc++)
                if (hp.card[sc] != NULL && (hp.card[sc]->flag || hp.card[sc]->control))
                    fprintf(stderr, "[card %02o: control %d flag %d fb %d]\n", sc,
                            hp.card[sc]->control, hp.card[sc]->flag, hp.card[sc]->flag_buffer);
            fprintf(stderr, "[P %05o A %06o B %06o]\n", hp.p, hp.a, hp.b);
            return 1;
        }
    }
    return 0;
}

/* Type a line, wait for the line feed of its echo, then give TSB time to answer. */
static int send_line(int k, const char *text)
{
    term_t *t = &term[k];
    size_t mark = t->len;
    unsigned long long guard = total_instructions + 400000000ull;

    if (type_and_wait(k, text) || type_and_wait(k, "\r"))
        return 1;
    while (memchr(t->out + mark, '\n', t->len - mark) == NULL) {
        if (run_chunk(CHUNK))
            return 1;
        if (total_instructions > guard) {
            fprintf(stderr, "[no echo for \"%s\"]\n", text);
            return 1;
        }
    }
    return run_n(line_gap);
}

/* Count the lines printed since 'from' that are complaints: an output line that
 * starts with ERROR or ???, or that mentions OVERLOAD. The first one is copied. */
static unsigned count_problems(const term_t *t, size_t from, char *first, size_t first_size)
{
    size_t i = from;
    unsigned n = 0;

    first[0] = 0;
    while (i < t->len) {
        const char *l = t->out + i;
        const char *end = memchr(l, '\n', t->len - i);
        size_t len = end ? (size_t)(end - l) : t->len - i;
        int bad = 0;

        if (len >= 5 && strncmp(l, "ERROR", 5) == 0)
            bad = 1;
        else if (len >= 3 && strncmp(l, "???", 3) == 0)
            bad = 1;
        else {
            size_t k;

            for (k = 0; k + 8 <= len; k++)
                if (strncmp(l + k, "OVERLOAD", 8) == 0)
                    bad = 1;
        }
        if (bad) {
            if (n == 0) {
                size_t c = len < first_size - 1 ? len : first_size - 1;

                memcpy(first, l, c);
                first[c] = 0;
            }
            n++;
        }
        i += len + 1;
    }
    return n;
}

/* Load every program in the manifest as A000's library. The user terminal must be
 * logged on as A000. For each program: SCR, NAME-name, the lines, and SAVE; if TSB
 * complained about any line the program is scratched instead of saved. A program
 * for a subchannel other than 0 is then moved there from the operator console.
 * Results are appended to library_result.txt. */
static int load_library(const char *manifest)
{
    FILE *list = fopen(manifest, "r");
    FILE *result = fopen("library_result.txt", "a");
    char entry[1024];
    unsigned saved = 0, rejected = 0, failed = 0;

    if (list == NULL || result == NULL) {
        fprintf(stderr, "cannot open the manifest or the result file\n");
        return 1;
    }
    while (fgets(entry, sizeof entry, list) != NULL) {
        char name[16], path[900], first[200], command[64];
        unsigned sub;
        FILE *program;
        char text[1024];
        size_t mark;
        unsigned lines = 0, problems;

        if (sscanf(entry, "%15s %u %899[^\r\n]", name, &sub, path) != 3)
            continue;
        program = fopen(path, "r");
        if (program == NULL) {
            fprintf(result, "MISSING %s %s\n", name, path);
            continue;
        }
        mark = term[1].len;
        snprintf(command, sizeof command, "NAME-%s", name);
        if (send_line(1, "SCR") || send_line(1, command))
            return 1;
        while (fgets(text, sizeof text, program) != NULL) {
            size_t n = strlen(text);

            while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r'))
                text[--n] = 0;
            if (n == 0)
                continue;
            if (send_line(1, text))
                return 1;
            lines++;
        }
        fclose(program);
        if (send_line(1, "") || run_n(line_gap * 2))
            return 1;
        problems = count_problems(&term[1], mark, first, sizeof first);
        if (problems != 0) {
            if (send_line(1, "SCR"))
                return 1;
            fprintf(result, "REJECT %s %u problems, first: %s\n", name, problems, first);
            fprintf(stderr, "[reject %s: %s]\n", name, first);
            rejected++;
            continue;
        }
        mark = term[1].len;
        if (getenv("LIB_DRYRUN") != NULL) {         /* only find out what TSB accepts */
            if (send_line(1, "SCR"))
                return 1;
            fprintf(result, "OK %s %u %u\n", name, sub, lines);
            fflush(result);
            saved++;
            continue;
        }
        if (send_line(1, "SAVE") || run_n(line_gap * 2))
            return 1;
        problems = count_problems(&term[1], mark, first, sizeof first);
        if (problems != 0) {
            fprintf(result, "SAVEFAIL %s %s\n", name, first);
            fprintf(stderr, "[save failed %s: %s]\n", name, first);
            failed++;
            continue;
        }
        if (sub != 0) {
            mark = term[0].len;
            snprintf(command, sizeof command, "MOVE-A000,%s,%u\r", name, sub);
            if (type_and_wait(0, command) || run_n(line_gap * 3))
                return 1;
            problems = count_problems(&term[0], mark, first, sizeof first);
            if (problems != 0 || strstr(term[0].out + mark, "NO SUCH") != NULL) {
                fprintf(result, "MOVEFAIL %s %u %s\n", name, sub, first);
                fprintf(stderr, "[move failed %s to %u: %s]\n", name, sub, first);
                failed++;
                continue;
            }
        }
        fprintf(result, "OK %s %u %u\n", name, sub, lines);
        fflush(result);
        saved++;
        if (saved % 10 == 0)
            fprintf(stderr, "[%u saved, %u rejected, %u failed; %llu million instructions]\n",
                    saved, rejected, failed, total_instructions / 1000000ull);
    }
    fprintf(stderr, "[library: %u saved, %u rejected, %u failed]\n", saved, rejected, failed);
    fclose(list);
    fclose(result);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned i = 0, started = 0;
    unsigned long long deadline = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s kitdir script.txt\n", argv[0]);
        return 2;
    }
    kit_dir = argv[1];
    read_script(argv[2]);

    if (system_access) {
        build_access_system();
    } else if (system_f) {
        build_f_system();
        hp_select(HP_SP);
        hp.io_trace = sp_trace;
    } else {
        hp_init(16384, 1);
        hp_dma_init(&dma);
        dev_tty_init(&tty, 010);
        dev_disc_init(&disc, 011);
        dev_ptr_init(&ptr, 013);
        dev_mux_init(&mux, 014, 016);
        dev_tbg_init(&tbg, 017);
        dev_tape_init(&magtape, 020);
        hp_preset();
        hp.e = 1;
    }

    while (i < action_count) {
        action_t *a = &actions[i];
        term_t *t = &term[a->term];

        switch (a->type) {
        case A_DISC:
            open_disc(a->number, a->text, a->flag);
            i++;
            break;
        case A_TAPE:
            mount_tape(a->text);
            i++;
            break;
        case A_ONLINE:
            dev_tape_set_online(&magtape, 0);
            i++;
            break;
        case A_SYSTEM:
            i++;
            break;
        case A_BOOT:
            if (a->number) {
                if (boot_f(a->number == 2) != 0)
                    return 1;
                if (getenv("TRACE_F") != NULL)
                    dump_io_trace();
                fprintf(stderr, "[2000F started]\n");
                i++;
                break;
            }
            if (boot_from_tape(a->flag ? "24285-60001_Rev-1534.abin"
                                       : "24286-60001_Rev-1438.abin",
                               a->flag ? 02000u : 2u) != 0)
                return 1;
            fprintf(stderr, "[%s started]\n", a->flag ? "loader utility" : "system");
            i++;
            break;
        case A_GEN:
            if ((a->flag == 3 ? gen_f_system(a->text, a->text2)
                 : a->flag == 2 ? reload_access(a->text, a->text2)
                              : a->flag ? gen_access_sp(a->text, a->text2, a->text3)
                                        : gen_access_iop(a->text)) != 0)
                return 1;
            i++;
            break;
        case A_BOOT_ACCESS:
            if (boot_access(a->text) != 0)
                return 1;
            fprintf(stderr, "[Access system started]\n");
            i++;
            break;
        case A_CONNECT:
            if (system_f)
                hp_select(HP_IOP);              /* the multiplexer is in the I/O Processor */
            dev_mux_connect(&mux, 0, 1);
            if (getenv("MUX_DEBUG") != NULL)
                fprintf(stderr, "[IOP ticks %u: connecting the user line]\n",
                        (unsigned)hp_cpus[HP_IOP].ticks);
            if (getenv("BOTH_LINES") != NULL)
                dev_mux_connect(&mux, 1, 1);
            hp_select(HP_SP);
            i++;
            break;
        case A_SEND:
            type_text(a->term, a->text);
            started = 0;
            i++;
            break;
        case A_EXPECT:
            if (!started) {
                started = 1;
                deadline = total_instructions + (unsigned long long)a->number * 1000000ull;
            }
            if (t->len > t->scan && strstr(t->out + t->scan, a->text) != NULL) {
                char *hit = strstr(t->out + t->scan, a->text);

                t->scan = (size_t)(hit - t->out) + strlen(a->text);
                started = 0;
                i++;
                break;
            }
            if (run_chunk(CHUNK))
                return 1;
            if (total_instructions > deadline) {
                flush_line(0);
                flush_line(1);
                fprintf(stderr, "[timeout waiting for \"%s\"]\n", a->text);
                return 1;
            }
            break;
        case A_SENDFILE:
            if (!started) {
                started = 1;
                if (load_lines(a->text) != 0)
                    return 1;
            }
            if (!typing_done(a->term)) {
                if (run_chunk(CHUNK))
                    return 1;
                break;
            }
            if (sf.waiting) {
                /* the line is typed: wait for TSB to echo it and settle */
                if (t->len > sf.mark && memchr(t->out + sf.mark, '\n', t->len - sf.mark) != NULL
                    && total_instructions >= sf.wait_until) {
                    if (strstr(t->out + sf.mark, "???") != NULL) {
                        sf.errors++;
                        flush_line(a->term);
                        fprintf(stderr, "[line %u not accepted: %s]\n", sf.next, sf.lines[sf.next - 1]);
                    }
                    sf.waiting = 0;
                    t->scan = t->len;
                } else {
                    if (run_chunk(CHUNK))
                        return 1;
                    if (total_instructions > sf.wait_until + 200000000ull) {
                        fprintf(stderr, "[no answer to line %u]\n", sf.next);
                        return 1;
                    }
                }
                break;
            }
            if (sf.next < sf.count) {
                type_text(a->term, sf.lines[sf.next]);
                type_text(a->term, "\r");
                sf.next++;
                sf.waiting = 1;
                sf.mark = t->len;
                sf.wait_until = total_instructions + line_gap;
            } else {
                fprintf(stderr, "[sent %u lines, %u not accepted]\n", sf.count, sf.errors);
                started = 0;
                i++;
            }
            break;
        case A_RUN:
            if (!started) {
                started = 1;
                deadline = total_instructions + (unsigned long long)a->number * 1000000ull;
            }
            if (total_instructions >= deadline) {
                started = 0;
                i++;
                break;
            }
            if (run_chunk(CHUNK))
                return 1;
            break;
        case A_GAP:
            line_gap = a->number * 1000u;
            i++;
            break;
        case A_QUIET:
            quiet_user = a->flag;
            i++;
            break;
        case A_LIBRARY:
            if (load_library(a->text) != 0)
                return 1;
            i++;
            break;
        case A_QUIT:
            i = action_count;
            break;
        }
    }
    flush_line(0);
    flush_line(1);
    fprintf(stderr, "[done after %llu million instructions]\n", total_instructions / 1000000ull);
    {
        unsigned d;

        for (d = 0; d < DISC_DRIVES; d++)
            if (disc_image[d] != NULL)
                storage_close(disc_image[d]);
    }
    if (tape_image != NULL)
        storage_close(tape_image);
    return 0;
}
