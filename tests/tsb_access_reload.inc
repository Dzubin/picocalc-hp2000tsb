/* tsb_access_reload.inc - the Access reload from a HIBERNATE tape for
 * tsb_session.c, done the way hp2000access.c does it: the I/O Processor first,
 * then the Master Control Program, which asks for the reel.
 *
 * Script command:  reloadaccess IOP.tape HIBERNATE.tape
 *
 * Author: Thomas Dzubin.
 */

static int reload_access(const char *iop_tape, const char *hib_tape)
{
    char path[512];
    unsigned long long left;

    if (cross_load_iop(iop_tape) != 0)
        return 1;
    hp_select(HP_IOP);
    hp_pair_resume(HP_IOP);
    hp_select(HP_SP);
    for (left = 3000000ull; left > 0; left -= 100000ull)
        pump(100000);
    snprintf(path, sizeof path, "%s/%s", kit_dir, A_MCP_2100);
    if (mount_access_tape(path, 0, 1) != 0 || load_from_tape() != 0)
        return 1;
    hp_pair_resume(HP_SP);
    if (gen_ask("2754? ", "YES\r") || gen_ask("LOAD WHICH MODULE?  ", module_reply()) ||
        gen_skip_options() || gen_ask("SYSTEM GENERATION? ", "NO\r") || gen_ask("MAG TAPE RELOAD? ", "YES\r") ||
        gen_wait("MOUNT REEL NUMBER  1.  PRESS RETURN ", 800))
        return 1;
    if (mount_access_tape(hib_tape, 0, 0) != 0 || gen_reply("\r"))
        return 1;
    if (gen_ask("CONFIGURATION OPTIONS? ", "NO\r") || gen_ask("LOAD OR DUMP COMMANDS? ", "NO\r") ||
        gen_date_time() || gen_sure_date())
        return 1;
    fprintf(stderr, "[Access system reloaded]\n");
    return 0;
}
