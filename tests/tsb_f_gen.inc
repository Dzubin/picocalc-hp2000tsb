/* tsb_f_gen.inc - generate a new 2000F System Processor program for tsb_session.c.
 *
 * Author: Thomas Dzubin.
 *
 * Script command:  genf DISC.disc HIBERNATE.tape
 *
 * It follows the SIMH kit's tsb-spgen.sim for the 2000F rev 1410: the cross-loader
 * sends the I/O Processor its program, the TSB loader for the disc (env
 * DISC_MODEL=2883 gives the 2883A loader, otherwise the 7900A one) reads the three
 * system paper tapes and answers the configuration questions, the four example
 * accounts are made, the system sleeps, is booted again to load the example
 * programs from examples.DUMP.tape, and is hibernated onto a new tape.
 */

/* Put a paper tape from the kit folder in the System Processor's reader. */
static int f_attach_tape(const char *file)
{
    char path[512];
    uint32_t length;
    uint8_t *data;

    snprintf(path, sizeof path, "%s/%s", kit_dir, file);
    data = read_file(path, &length);
    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    hp_select(HP_SP);
    dev_ptr_load_tape(&ptr, data, length);
    return 0;
}

/* A paper tape has been read: the system halts with "END OF TAPE". Change the
 * tape and carry on. */
static int f_next_tape(const char *file)
{
    if (gen_until_halt(2000) != 0)
        return 1;
    if (file != NULL && f_attach_tape(file) != 0)
        return 1;
    hp.s = 0;
    hp_pair_resume(HP_SP);
    return 0;
}

static int gen_f_system(const char *disc_path, const char *hib_path)
{
    char path[512];
    unsigned k;
    static const char *const accounts[] = {
        "NEW-A000,OZZIE,65535,65535\r",
        "NEW-A001,HARIET,65535,65535\r",
        "NEW-B000,DAVID,65535,65535\r",
        "NEW-B001,RICK,65535,65535\r",
        NULL
    };

    open_disc(0, disc_path, 1);
    if (boot_f(2) != 0)                 /* cross-load the IOP, load the TSB loader, start it */
        return 1;
    fprintf(stderr, "[TSB loader running]\n");
    if (gen_ask("IS THE SYSTEM CONSOLE AN HP 2762A?  ", "NO\r") ||
        gen_ask("LIBRARY?  ", "NO\r") ||
        gen_ask("SYSTEM IDENTIFICATION? ", "SIMH 2000F\r") ||
        gen_ask("CONFIGURATION OPTIONS? ", "YES\r") ||
        gen_ask("MODIFICATIONS?  ", "NO\r") ||
        gen_ask("DO YOU WANT IT LABELED? ", "YES\r") ||
        gen_ask("OR MUNLOCK COMMANDS? ", "NO\r") ||
        gen_ask("NUMBER OF PORTS? ", "16\r") ||
        gen_ask("NUMBER OF DIRECTORY TRACKS PER DISC? ", "1\r") ||
        gen_ask("NUMBER OF ID TRACKS? ", "1\r"))
        return 1;
    if (f_next_tape("24337-60002_Rev-1410.abin") != 0 ||
        f_next_tape("24337-60003_Rev-1410.abin") != 0)
        return 1;
    if (gen_ask("DATE?  ", "281/26\r") || gen_ask("TIME?  ", "1200\r"))
        return 1;
    for (k = 0; accounts[k] != NULL; k++)
        if (gen_wait("\r\n", 800) || gen_reply(accounts[k]))
            return 1;
    if (gen_wait("\r\n", 800) || gen_reply("SLEEP\r") || gen_until_halt(2000) != 0)
        return 1;
    fprintf(stderr, "[system generated; booting it to load the example programs]\n");

    /* boot the new system from the disc */
    hp_select(HP_SP);
    hp_preset();
    hp.s = 0;
    if (use_dq)
        hp_load_dq_bootstrap(F_SC_DISC);
    else
        hp_load_disc_bootstrap(F_SC_DISC);
    hp_pair_resume(HP_SP);
    if (gen_until_halt(4000) != 0)
        return 1;
    snprintf(path, sizeof path, "%s/examples.DUMP.tape", kit_dir);
    if (mount_access_tape(path, 0, 1) != 0)
        return 1;
    hp.s = 0;
    hp_pair_resume(HP_SP);
    if (gen_ask("CONFIGURATION OPTIONS? ", "YES\r") ||
        gen_ask("NUMBER OF PORTS? ", "\r") ||
        gen_ask("LOAD OR DUMP COMMANDS? ", "LOAD\r") ||
        gen_wait("\r\n", 800) || gen_reply("B001\r") ||
        gen_wait("\r\n", 800) || gen_reply("END\r") ||
        gen_ask("MAG TAPE SELECT CODE? ", "21\r") ||
        gen_ask("LOAD OR DUMP COMMANDS? ", "NO\r") ||
        gen_ask("DATE?  ", "281/26\r") || gen_ask("TIME?  ", "1200\r"))
        return 1;
    if (mount_access_tape(hib_path, 1, 0) != 0)
        return 1;
    if (gen_wait("READY", 800) || gen_wait("\r\n", 800) || gen_reply("HIBERNATE\r") ||
        gen_ask("VERIFY? ", "YES\r") || gen_until_halt(2000) != 0)
        return 1;
    fprintf(stderr, "[2000F system generated on %s, hibernated to %s]\n", disc_path, hib_path);
    return 0;
}
