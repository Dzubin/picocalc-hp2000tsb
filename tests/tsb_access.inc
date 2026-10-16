/* tsb_access.inc - the Access system for tsb_session.c: System Processor plus
 * I/O Processor, both 32K, with the select codes of the HP 2000 Access Operator's
 * Manual. Included by tsb_session.c after its terminal and disc helpers.
 *
 * Author: Thomas Dzubin.
 *
 * Script commands it adds:
 *   system access                          build the Access machine
 *   genaccess iop OUT.tape                 make the I/O Processor program tape
 *   genaccess sp IOP.tape DISC HIB.tape    generate the System Processor on a new
 *                                          7900 disc and write the HIBERNATE tape
 *   bootaccess IOP.tape                    start the system from the disc
 */

#define A_SP_WORDS       32768u
#define A_IOP_WORDS      32768u
#define A_SC_IPL_SP      010
#define A_SC_TTY         012
#define A_SC_TBG_SP      013
#define A_SC_DISC        014
#define A_SC_TAPE        016
#define A_SC_TBG_IOP     010
#define A_SC_MUX_LOWER   011
#define A_SC_MUX_CONTROL 013
#define A_SC_IPL_IOP     014
#define A_MCP_2100       "22687-11002_Rev-1812.tape"
#define A_SETTLE         600000u        /* instructions TSB gets after a reply */

/* The last I/O instructions of the System Processor, shown when a wait times out. */
#define TRACE_RING 48u
#define IPL_RING 120u
static struct { hp_word ir, reg; unsigned long long at; } ipl_ring[IPL_RING];
static unsigned ipl_next;
static struct { unsigned sc; hp_word ir, reg; unsigned long long at; } trace_ring[TRACE_RING];
static unsigned trace_next;
static int trace_trigger;               /* set when the S register is read */

static void sp_trace(unsigned sc, hp_word ir, hp_word reg)
{
    if (trace_next != 0 && trace_ring[(trace_next - 1) % TRACE_RING].ir == ir &&
        trace_ring[(trace_next - 1) % TRACE_RING].reg == reg)
        return;                         /* a wait loop: keep only the first */
    if (ir == 0102501)
        trace_trigger = 1;
    if ((sc == 010 || sc == 011) && ipl_next < IPL_RING) {
        ipl_ring[ipl_next % IPL_RING].ir = ir;
        ipl_ring[ipl_next % IPL_RING].reg = reg;
        ipl_ring[ipl_next % IPL_RING].at = hp.ticks;
        ipl_next++;
    }
    trace_ring[trace_next % TRACE_RING].sc = sc;
    trace_ring[trace_next % TRACE_RING].ir = ir;
    trace_ring[trace_next % TRACE_RING].reg = reg;
    trace_ring[trace_next % TRACE_RING].at = total_instructions;
    trace_next++;
}

static void dump_ipl_trace(void)
{
    unsigned n, first = 0;

    fprintf(stderr, "[SP interconnect I/O (%u so far), first ones:", ipl_next);
    for (n = first; n < ipl_next && n < IPL_RING; n++)
        fprintf(stderr, " %06o/%o@%llu", ipl_ring[n % IPL_RING].ir,
                (unsigned)ipl_ring[n % IPL_RING].reg, ipl_ring[n % IPL_RING].at);
    fprintf(stderr, "]\n");
}

static void dump_io_trace(void)
{
    unsigned n, first = trace_next > TRACE_RING ? trace_next - TRACE_RING : 0;

    dump_ipl_trace();
    fprintf(stderr, "[I/O instructions:");
    for (n = first; n < trace_next; n++)
        fprintf(stderr, " %06o/%o", trace_ring[n % TRACE_RING].ir,
                (unsigned)trace_ring[n % TRACE_RING].reg);
    fprintf(stderr, "]\n");
}

/* The same for the I/O Processor. */
static struct { hp_word ir, reg; } iop_ring[TRACE_RING];
static unsigned iop_next;

static struct { hp_word ir, reg; unsigned long long at; } iopl_ring[120];
static unsigned iopl_next;

static void iop_trace(unsigned sc, hp_word ir, hp_word reg)
{
    if ((sc == 014 || sc == 015) && iopl_next < 120) {
        iopl_ring[iopl_next].ir = ir;
        iopl_ring[iopl_next].reg = reg;
        iopl_ring[iopl_next].at = hp.ticks;
        iopl_next++;
    }
    if (iop_next != 0 && iop_ring[(iop_next - 1) % TRACE_RING].ir == ir &&
        iop_ring[(iop_next - 1) % TRACE_RING].reg == reg)
        return;
    iop_ring[iop_next % TRACE_RING].ir = ir;
    iop_ring[iop_next % TRACE_RING].reg = reg;
    iop_next++;
}

static void dump_iop_trace(void)
{
    unsigned w, previous = hp_selected();

    fprintf(stderr, "[IOP interconnect I/O, first %u:", iopl_next);
    for (w = 0; w < iopl_next; w++)
        fprintf(stderr, " %06o/%o@%llu", iopl_ring[w].ir, (unsigned)iopl_ring[w].reg, iopl_ring[w].at);
    fprintf(stderr, "]\n");
    hp_select(HP_IOP);
    fprintf(stderr, "[IOP memory at P-12:");
    for (w = 0; w < 28; w++)
        fprintf(stderr, " %06o", hp.mem[(hp.p - 12 + w) & HP_ADDR_MASK]);
    fprintf(stderr, "]\n[IOP ints %d irq_sc %o spr %06o cards:", hp.int_system, hp.irq_sc, hp.spr);
    for (w = 010; w < 022; w++)
        if (hp.card[w] != NULL)
            fprintf(stderr, " %02o(f%d fb%d c%d)", w, hp.card[w]->flag, hp.card[w]->flag_buffer,
                    hp.card[w]->control);
    fprintf(stderr, "]\n");
    hp_select(previous);
    unsigned n, first = iop_next > TRACE_RING ? iop_next - TRACE_RING : 0;

    fprintf(stderr, "[IOP I/O instructions:");
    for (n = first; n < iop_next; n++)
        fprintf(stderr, " %06o/%o", iop_ring[n % TRACE_RING].ir,
                (unsigned)iop_ring[n % TRACE_RING].reg);
    fprintf(stderr, "]\n");
}

static void build_access_system(void)
{
    hp_select(HP_SP);
    hp_init(A_SP_WORDS, 1);
    hp.io_trace = sp_trace;
    hp_dma_init(&dma);
    dev_ipl_init(&ipl_sp, HP_SP, 0, A_SC_IPL_SP, ipl_bus);
    dev_tty_init(&tty, A_SC_TTY);
    dev_tbg_init(&tbg, A_SC_TBG_SP);
    use_dq = getenv("DISC_MODEL") != NULL && strcmp(getenv("DISC_MODEL"), "2883") == 0;
    if (use_dq)
        dev_dq_init(&dq, A_SC_DISC);
    else
        dev_disc_init(&disc, A_SC_DISC);
    dev_tape_init(&magtape, A_SC_TAPE);
    hp_preset();
    hp.e = 1;

    hp_select(HP_IOP);
    hp_init(A_IOP_WORDS, 0);
    hp_enable_iop();
    hp.io_trace = iop_trace;
    hp_dma_init(&dma_iop);
    dev_ipl_init(&ipl_iop, HP_IOP, 1, A_SC_IPL_IOP, ipl_bus);
    dev_tbg_init(&tbg_iop, A_SC_TBG_IOP);
    dev_mux_init(&mux, A_SC_MUX_LOWER, A_SC_MUX_CONTROL);
    hp_preset();
    hp.e = 1;

    hp_select(HP_SP);
    hp_pair_init(&ipl_sp, &ipl_iop);
    hp_pair_hold(HP_IOP);               /* no program yet */
}

/* Mount a tape image on drive 0 of the System Processor's tape unit and put it
 * on line. create makes a new empty tape; read_only protects it. */
static int mount_access_tape(const char *path, int create, int read_only)
{
    if (tape_image != NULL) {
        dev_tape_unmount(&magtape, 0);
        storage_close(tape_image);
        tape_image = NULL;
    }
    tape_image = create ? storage_create(path) : storage_open(path, !read_only);
    if (tape_image == NULL) {
        fprintf(stderr, "cannot open the tape image %s\n", path);
        return 1;
    }
    dev_tape_mount(&magtape, 0, tape_image, read_only);
    dev_tape_set_online(&magtape, 0);
    return 0;
}

static int pump(unsigned n)
{
    total_instructions += n;
    return hp_pair_run(n);
}

/* Run both processors until the System Processor's console has printed the text
 * (after the last match). Returns 0, or 1 if the processor stopped or time ran out. */
static int gen_wait(const char *text, unsigned long long limit_m)
{
    term_t *t = &term[0];
    unsigned long long guard = total_instructions + limit_m * 1000000ull;

    for (;;) {
        if (t->out != NULL) {
            char *hit = strstr(t->out + t->scan, text);

            if (hit != NULL) {
                t->scan = (size_t)(hit - t->out) + strlen(text);
                return 0;
            }
        }
        pump(CHUNK);
        if (hp_pair_stopped(HP_SP)) {
            flush_line(0);
            hp_select(HP_SP);
            fprintf(stderr, "[the System Processor stopped (P %05o, T %06o) waiting for \"%s\"]\n",
                    hp.p, hp.t, text);
            return 1;
        }
        if (total_instructions > guard) {
            unsigned cpu;
            size_t n;

            fprintf(stderr, "[console text so far, last 80 chars:");
            for (n = t->len > 80 ? t->len - 80 : 0; n < t->len; n++)
                fprintf(stderr, (unsigned char)t->out[n] < 32 ? "<%o>" : "%c",
                        (unsigned char)t->out[n] < 32 ? (unsigned)t->out[n]
                                                       : (unsigned)(unsigned char)t->out[n]);
            fprintf(stderr, "]\n");
            flush_line(0);
            fprintf(stderr, "[timeout waiting for \"%s\"]\n", text);
            for (cpu = 0; cpu < HP_CPU_COUNT; cpu++) {
                hp_select(cpu);
                fprintf(stderr, "[%s: P %05o A %06o B %06o IR %06o stopped %d int %d]\n",
                        cpu ? "IOP" : "SP", hp.p, hp.a, hp.b, hp.ir, hp_pair_stopped(cpu),
                        hp.int_system);
                if (cpu == HP_SP) {
                    unsigned w;

                    fprintf(stderr, "[SP memory at P-8:");
                    for (w = 0; w < 16; w++)
                        fprintf(stderr, " %06o", hp.mem[(hp.p - 8 + w) & HP_ADDR_MASK]);
                    fprintf(stderr, "]\n");
                    {
                        unsigned n, first = trace_next > TRACE_RING ? trace_next - TRACE_RING : 0;

                        fprintf(stderr, "[last I/O instructions:");
                        for (n = first; n < trace_next; n++)
                            fprintf(stderr, " %06o/%o", trace_ring[n % TRACE_RING].ir,
                                    (unsigned)trace_ring[n % TRACE_RING].reg);
                        fprintf(stderr, "]\n");
                    }
                    fprintf(stderr, "[tty: flag %d fb %d ctl %d mode %06o data %03o print_active %d key_active %d ticks %u next_due %u]\n",
                            tty.card.flag, tty.card.flag_buffer, tty.card.control, tty.mode,
                            tty.data, hp_event_active(&tty.print_event),
                            hp_event_active(&tty.key_event), hp.ticks, hp.next_due);
                }
            }
            hp_select(HP_SP);
            return 1;
        }
    }
}

/* Type a reply on the System Processor's console and give TSB time to take it. */
static int gen_reply(const char *text)
{
    unsigned long long guard = total_instructions + 200000000ull;

    type_text(0, text);
    while (!typing_done(0)) {
        pump(CHUNK);
        if (total_instructions > guard || hp_pair_stopped(HP_SP)) {
            fprintf(stderr, "[the reply \"%s\" was not taken]\n", text);
            return 1;
        }
    }
    pump(A_SETTLE);
    return 0;
}

static int gen_ask(const char *prompt, const char *reply)
{
    return gen_wait(prompt, 800) || gen_reply(reply);
}

/* Run until the System Processor halts; returns 0 if it was HLT 77. */
static int gen_until_halt(unsigned long long limit_m)
{
    unsigned long long guard = total_instructions + limit_m * 1000000ull;

    while (!hp_pair_stopped(HP_SP)) {
        pump(CHUNK);
        if (total_instructions > guard) {
            flush_line(0);
            fprintf(stderr, "[the System Processor did not stop]\n");
            return 1;
        }
    }
    hp_select(HP_SP);
    flush_line(0);
    if (hp.t != 0102077) {
        unsigned n, first = trace_next > TRACE_RING ? trace_next - TRACE_RING : 0;

        fprintf(stderr, "[memory 02000:");
        for (n = 0; n < 40; n++)
            fprintf(stderr, " %06o", hp.mem[02000 + n]);
        fprintf(stderr, "]\n[memory 77700 up, 64 words:");
        for (n = 0; n < 64; n++)
            fprintf(stderr, " %06o", hp.mem[077700 + n]);
        fprintf(stderr, "]\n");
        fprintf(stderr, "[last I/O instructions:");
        for (n = first; n < trace_next; n++)
            fprintf(stderr, " %06o/%o", trace_ring[n % TRACE_RING].ir,
                    (unsigned)trace_ring[n % TRACE_RING].reg);
        fprintf(stderr, "]\n");
    }
    fprintf(stderr, "[System Processor halted: status %d, P %05o, T %06o, A %06o, B %06o, IR %06o]\n",
            hp_pair_status(HP_SP), hp.p, hp.t, hp.a, hp.b, hp.ir);
    return hp.t != 0102077;
}

/* Load the first file of the mounted tape into the System Processor with the BMTL
 * loader, leaving it at the program's start with S set as the Access programs
 * expect (the tape select code in bits 11-6). */
static int load_from_tape(void)
{
    hp_select(HP_SP);
    hp_load_tape_loader(A_SC_TAPE);
    hp.s = 0;
    hp_pair_resume(HP_SP);
    if (gen_until_halt(400) != 0) {
        fprintf(stderr, "the tape loader did not finish with HLT 77\n");
        return 1;
    }
    hp.s = 001600;
    hp.p = 002000;
    return 0;
}

/* Send the I/O Processor its program through its binary loader; the System
 * Processor has just printed START IOP PROTECTED LOADER. */
static int start_iop_loader(void)
{
    hp_select(HP_IOP);
    hp_load_iop_loader(A_SC_IPL_IOP);
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    return gen_reply("\r");
}

/* Generate the I/O Processor program tape with the Master Control Program. */
static int gen_access_iop(const char *out_tape)
{
    static const char *const answers[][2] = {
        { "2754? ", "YES\r" },
        { "LOAD WHICH MODULE?  ", "I/O\r" },
        { "RELOAD? ", "NO\r" },
        { "DATE? ", "10/08/2026 12:00\r" },
        { "MEMORY SIZE?  ", "32\r" },
        { "NUMBER OF PORTS?  ", "16\r" },
        { "BUFFER LENGTH OPTION? ", "NO\r" },
        { "TIME BASE GENERATOR SELECT CODE?  ", "10\r" },
        { "INTERCONNECT KIT SELECT CODE?  ", "14\r" },
        { "MULTIPLEXER SELECT CODE?  ", "11\r" },
        { "2741 TYPE TERMINALS?  ", "NO\r" },
        { "INCLUDE RJE FUNCTION? ", "NO\r" },
        { "NON-SHAREABLE DEVICES?  ", "YES\r" },
        { "NUMBER OF MT DEVICES? ", "1\r" },
        { "MT0 DEFAULT RECORD SIZE?  (MAXIMUM IS 1024) ", "1024\r" },
        { "NUMBER OF PR DEVICES? ", "1\r" },
        { "PR0 SELECT CODE?  ", "16\r" },
        { "PR0 DEFAULT RECORD SIZE?  (MAXIMUM IS 0064) ", "64\r" },
        { "NUMBER OF PP DEVICES? ", "1\r" },
        { "PP0 SELECT CODE?  ", "17\r" },
        { "PP0 DEFAULT RECORD SIZE?  (MAXIMUM IS 0064) ", "64\r" },
        { "NUMBER OF LP DEVICES? ", "1\r" },
        { "LP0 SELECT CODE?  ", "20\r" },
        { "LP0 SUBTYPE?  ", "1\r" },
        { "LP0 DEFAULT RECORD SIZE?  (MAXIMUM IS 0068) ", "68\r" },
        { "NUMBER OF CR DEVICES? ", "0\r" },
        { "NUMBER OF RP DEVICES? ", "0\r" },
        { "NUMBER OF SL DEVICES? ", "0\r" },
        { "NUMBER OF LT DEVICES? ", "0\r" },
        { "MAGNETIC TAPE COPY? ", "YES\r" },
        { "SEPARATE TAPE?  ", "YES\r" },
        { "PRINT CONFIGURATION REPORT? ", "YES\r" },
        { NULL, NULL }
    };
    char path[512];
    unsigned k;

    snprintf(path, sizeof path, "%s/%s", kit_dir, A_MCP_2100);
    if (mount_access_tape(path, 0, 1) != 0 || load_from_tape() != 0)
        return 1;
    fprintf(stderr, "[Master Control Program loaded]\n");
    hp_pair_resume(HP_SP);
    for (k = 0; answers[k][0] != NULL; k++) {
        const char *reply = answers[k][1];

        if (getenv("IOP_BUFFER") != NULL && strcmp(answers[k][0], "BUFFER LENGTH OPTION? ") == 0) {
            if (gen_ask(answers[k][0], "YES\r") != 0)     /* probe: what does it ask next? */
                return 1;
            if (strcmp(getenv("IOP_BUFFER"), "probe") == 0) {
                unsigned n;

                for (n = 0; n < 50; n++)
                    pump(CHUNK);
                flush_line(0);
                return 1;
            }
            /* IOP_BUFFER is a list such as 0,256;1,256: port number and buffer length */
            if (gen_wait("TERMINATE WITH 'END'", 800) != 0)
                return 1;
            {
                char list[200], line[40], *tok;

                snprintf(list, sizeof list, "%s", getenv("IOP_BUFFER"));
                for (tok = strtok(list, ";"); tok != NULL; tok = strtok(NULL, ";")) {
                    snprintf(line, sizeof line, "%s\r", tok);
                    if (gen_wait("\r\n", 800) || gen_reply(line))
                        return 1;
                }
                if (gen_wait("\r\n", 800) || gen_reply("END\r"))
                    return 1;
            }
            continue;
        }
        if (gen_ask(answers[k][0], reply) != 0)
            return 1;
    }
    if (gen_wait("START IOP PROTECTED LOADER.  PRESS RETURN ", 800) || start_iop_loader())
        return 1;
    if (gen_wait("MOUNT IOP COPY TAPE.  PRESS RETURN", 3000))
        return 1;
    hp_select(HP_IOP);
    if (!hp_pair_stopped(HP_IOP) || hp.t != 0102077) {
        hp_select(HP_SP);
        fprintf(stderr, "[the I/O Processor did not finish its load]\n");
        return 1;
    }
    hp_select(HP_SP);
    fprintf(stderr, "[I/O Processor program received]\n");
    if (mount_access_tape(out_tape, 1, 0) != 0 || gen_reply("\r"))
        return 1;
    if (gen_wait("START IOP AT LOCATION 2002", 800))
        return 1;
    hp_select(HP_IOP);
    hp.s = 0;
    hp.p = 002002;
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    if (gen_until_halt(3000) != 0)
        return 1;
    fprintf(stderr, "[I/O Processor program tape written: %s]\n", out_tape);
    return 0;
}

/* Load the I/O Processor from its program tape: the System Processor reads the
 * cross-loader from the first file, which sends the IOP its program. The IOP is
 * left halted at the end of its load, ready to be started at 2. */
static int cross_load_iop(const char *iop_tape)
{
    if (mount_access_tape(iop_tape, 0, 1) != 0 || load_from_tape() != 0)
        return 1;
    hp_pair_resume(HP_SP);
    if (getenv("EARLY_IOP") != NULL) {  /* start the IOP loader with the cross-loader, as the program does */
        hp_select(HP_IOP);
        hp_load_iop_loader(A_SC_IPL_IOP);
        hp_pair_resume(HP_IOP);
        hp_select(HP_SP);
        if (gen_wait("START IOP PROTECTED LOADER.  PRESS RETURN ", 800) || gen_reply("\r"))
            return 1;
    } else if (gen_wait("START IOP PROTECTED LOADER.  PRESS RETURN ", 800) || start_iop_loader())
        return 1;
    if (gen_until_halt(3000) != 0)
        return 1;
    hp_select(HP_IOP);
    if (!hp_pair_stopped(HP_IOP) || hp.t != 0102077) {
        hp_select(HP_SP);
        fprintf(stderr, "[the I/O Processor did not finish its load]\n");
        return 1;
    }
    hp.s = 0;
    hp.p = 2;
    hp_select(HP_SP);
    fprintf(stderr, "[I/O Processor program loaded]\n");
    return 0;
}

static int gen_date_time(void)
{
    return gen_ask("DATE?  ", "281/26\r") || gen_ask("TIME?  ", "1200\r");
}

/* After the time is given, the system may ask "ARE YOU SURE THAT'S TODAY'S DATE?"
 * (the date is earlier than the one saved on the tape); answer YES. Returns when
 * the banner appears. */
static int gen_sure_date(void)
{
    term_t *t = &term[0];
    unsigned long long guard = total_instructions + 800ull * 1000000ull;

    for (;;) {
        char *ask;

        if (t->out != NULL && strstr(t->out + t->scan, "HP22687A") != NULL)
            return 0;
        ask = t->out != NULL ? strstr(t->out + t->scan, "TODAY'S DATE? ") : NULL;
        if (ask != NULL) {
            t->scan = (size_t)(ask - t->out) + 14;
            return gen_reply("YES\r");
        }
        pump(CHUNK);
        if (hp_pair_stopped(HP_SP) || total_instructions > guard) {
            flush_line(0);
            fprintf(stderr, "[stopped waiting for the banner]\n");
            return 1;
        }
    }
}

/* The disc module the Master Control Program is asked to load. */
static const char *module_reply(void)
{
    return use_dq ? "2883\r" : "7900\r";
}

/* The Master Control Program may ask "7900 OPTION?", "2883 OPTION?" and so on
 * (answer NO) before it asks the generation question. */
static int gen_skip_options(void)
{
    for (;;) {
        term_t *t = &term[0];
        unsigned long long guard = total_instructions + 800ull * 1000000ull;
        char *opt;

        for (;;) {
            opt = t->out ? strstr(t->out + t->scan, " OPTION?  ") : NULL;
            if (opt != NULL ||
                (t->out && strstr(t->out + t->scan, "SYSTEM GENERATION? ") != NULL))
                break;
            pump(CHUNK);
            if (hp_pair_stopped(HP_SP) || total_instructions > guard) {
                flush_line(0);
                fprintf(stderr, "[stopped waiting for the generation question]\n");
                return 1;
            }
        }
        if (opt == NULL)
            return 0;
        t->scan = (size_t)(opt - t->out) + 10;
        if (gen_reply("NO\r"))
            return 1;
    }
}

/* Format the other subchannels: two for each 7900 drive, one for each 2883 drive;
 * 0 is done already. */
static int gen_format_more(unsigned drives)
{
    unsigned sub;
    char reply[32];

    for (sub = 1; sub < (use_dq ? drives : 2 * drives); sub++) {
        snprintf(reply, sizeof reply, "FORMAT-%u\r", sub);
        if (gen_ask("DISC OR FORMAT COMMANDS? ", reply))
            return 1;
    }
    return 0;
}

/* Generate the System Processor program onto a new 7900 disc image and make the
 * HIBERNATE tape of the new system. */
static int gen_access_sp(const char *iop_tape, const char *disc_path, const char *hib_path)
{
    static const char *const accounts[] = {
        "NEW-A000,OZZIE,65535,65535,LP,PR,PP,MT\r",
        "NEW-A001,HARIET,65535,65535,LP,PR\r",
        "NEW-B000,DAVID,65535,65535,LP\r",
        "NEW-B001,RICK,65535,65535,LP\r",
        NULL
    };
    char path[512];
    unsigned k, drives = getenv("GEN_DRIVES") ? (unsigned)atoi(getenv("GEN_DRIVES")) : 1u;

    if (drives < 1 || drives > (use_dq ? DQ_DRIVES : DISC_DRIVES))
        drives = 1;
    if (cross_load_iop(iop_tape) != 0)
        return 1;
    open_disc(0, disc_path, 1);
    for (k = 1; k < drives; k++) {      /* more drives: disc_path with .d1, .d2 ... added */
        snprintf(path, sizeof path, "%s.d%u", disc_path, k);
        open_disc(k, path, 1);
    }
    snprintf(path, sizeof path, "%s/%s", kit_dir, A_MCP_2100);
    if (mount_access_tape(path, 0, 1) != 0 || load_from_tape() != 0)
        return 1;
    fprintf(stderr, "[Master Control Program loaded]\n");
    hp_select(HP_IOP);                  /* the IOP program runs from here on */
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    hp_pair_resume(HP_SP);

    if (gen_ask("2754? ", "YES\r") || gen_ask("LOAD WHICH MODULE?  ", module_reply()) ||
        gen_skip_options())
        return 1;
    if (gen_ask("SYSTEM GENERATION? ", "YES\r") || gen_ask("UPDATE? ", "NO\r") ||
        gen_ask("SYSTEM IDENTIFICATION? ", "SIMH 2000G\r") ||
        gen_ask("CONFIGURATION OPTIONS? ", "YES\r") ||
        gen_ask("DISC OR FORMAT COMMANDS? ", "FORMAT-0\r") ||
        gen_format_more(drives) ||
        gen_ask("DISC OR FORMAT COMMANDS? ", "NO\r") ||
        gen_ask("DO YOU WANT IT LABELED? ", "YES\r") ||
        gen_ask("MLOCK OR MUNLOCK COMMANDS? ", "NO\r") ||
        gen_ask("NUMBER OF DIRECTORY TRACKS PER DISC? ", "\r") ||
        gen_ask("NUMBER OF ID TRACKS? ", "1\r") || gen_date_time())
        return 1;
    for (k = 0; accounts[k] != NULL; k++)
        if (gen_wait("\r\n", 800) || gen_reply(accounts[k]))
            return 1;
    if (gen_wait("\r\n", 800) || gen_reply("SLEEP\r") ||
        gen_ask("SYSTEM DUMP OR RELOAD? ", "RELOAD\r") ||
        gen_ask("LOAD OR DUMP COMMANDS? ", "LOAD\r"))
        return 1;
    snprintf(path, sizeof path, "%s/examples.DUMP.tape", kit_dir);
    if (mount_access_tape(path, 0, 1) != 0)
        return 1;
    if (gen_wait("\r\n", 800) || gen_reply("ALL\r") ||
        gen_ask("MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r") ||
        gen_ask("LOAD OR DUMP COMMANDS? ", "NO\r") || gen_date_time())
        return 1;
    if (mount_access_tape(hib_path, 1, 0) != 0)
        return 1;
    if (gen_wait("\r\n", 800) || gen_reply("HIBERNATE\r") ||
        gen_ask("MOUNT REEL NUMBER  1.  PRESS RETURN ", "\r") ||
        gen_ask("VERIFY? ", "YES\r") || gen_wait("SYSTEM DUMP OR RELOAD? ", 800))
        return 1;
    type_text(0, "NO\r");               /* the system halts right after this */
    if (gen_until_halt(800) != 0)
        return 1;
    fprintf(stderr, "[Access system generated on %s]\n", disc_path);
    return 0;
}

/* Start a generated Access system: the IOP from its tape, then the disc. */
static int boot_access(const char *iop_tape)
{
    if (cross_load_iop(iop_tape) != 0)
        return 1;
    hp_select(HP_IOP);
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    {                                   /* let the IOP finish its start-up first */
        unsigned long long left = getenv("HEADSTART") ? strtoull(getenv("HEADSTART"), NULL, 10)
                                                      : 3000000ull;

        hp_pair_hold(HP_SP);
        while (left > 0) {
            unsigned step = left > 100000 ? 100000u : (unsigned)left;

            pump(step);
            left -= step;
        }
        fprintf(stderr, "[IOP head start done]\n");
    }
    hp_preset();                        /* the PRESET key: clears the DMA select flip-flop */
    ipl_next = iopl_next = 0;           /* trace only what the booted system does */
    hp.s = 0;
    if (use_dq)
        hp_load_dq_bootstrap(A_SC_DISC);
    else
        hp_load_disc_bootstrap(A_SC_DISC);
    if (getenv("BOOT_FIXED") != NULL)   /* read the fixed platter (head 2) */
        hp_write(hp.mem_size - 64 + 050, 031000);
    hp_pair_resume(HP_SP);
    if (getenv("TRACE_INS") != NULL) {
        unsigned n;

        int seen_wait = 0;

        hp_select(HP_SP);
        for (;;) {
            static hp_word prev_p;

            if (hp.p == 077774)
                seen_wait = 1;
            else if (seen_wait && hp.p != 077775) {
                fprintf(stderr, "[left the wait loop: from P %05o to P %05o; words 77774-77777: %06o %06o %06o %06o; 77717: %06o; 2055: %06o; DMA cw2 %06o cw3 %06o]\n",
                        prev_p, hp.p, hp.mem[077774], hp.mem[077775], hp.mem[077776],
                        hp.mem[077777], hp.mem[077717], hp.mem[02055],
                        ((hp_dma_t *)hp.dma_ctx)->channel[0].cw2,
                        ((hp_dma_t *)hp.dma_ctx)->channel[0].cw3);
                break;
            }
            prev_p = hp.p;
            hp_run(1);
        }
        for (n = 0; n < 140; n++) {
            hp_word p = hp.p;

            hp_run(1);
            fprintf(stderr, "P %05o IR %06o -> A %06o B %06o E%d O%d\n", p, hp.ir, hp.a, hp.b,
                    hp.e, hp.o);
        }
        return 1;
    }
    if (gen_until_halt(4000) != 0)
        return 1;
    hp.s = 0;
    hp_pair_resume(HP_SP);
    return 0;
}

