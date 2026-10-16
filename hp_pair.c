/* hp_pair.c - run two CPUs side by side. See hp_pair.h.
 *
 * Author: Thomas Dzubin.
 */

#include "hp_pair.h"

static dev_ipl_t *link[HP_CPU_COUNT];
static int        stopped[HP_CPU_COUNT];
static int        status[HP_CPU_COUNT];

void hp_pair_init(dev_ipl_t *sp_ipl, dev_ipl_t *iop_ipl)
{
    link[HP_SP] = sp_ipl;
    link[HP_IOP] = iop_ipl;
    stopped[HP_SP] = stopped[HP_IOP] = 0;
}

void hp_pair_resume(unsigned cpu)
{
    stopped[cpu] = 0;
}

void hp_pair_hold(unsigned cpu)
{
    stopped[cpu] = 1;
}

int hp_pair_status(unsigned cpu)
{
    return status[cpu];
}

int hp_pair_stopped(unsigned cpu)
{
    return stopped[cpu];
}

int hp_pair_run(uint32_t instructions)
{
    static unsigned round;
    uint32_t done = 0;
    unsigned cpu;
    int result = 0;

    while (done < instructions) {
        round++;
        for (cpu = 0; cpu < HP_CPU_COUNT; cpu++) {
            if (stopped[cpu])
                continue;
            if (cpu == HP_IOP && round % IPL_IOP_EVERY != 0)
                continue;               /* the idle I/O Processor gets fewer slices */
            hp_select(cpu);
            status[cpu] = hp_run(IPL_SLICE_INSTRUCTIONS);
            if (status[cpu] != HP_RUN_COUNT) {
                stopped[cpu] = 1;
                if (result == 0)
                    result = 1 + (int)cpu;
            }
        }
        for (cpu = 0; cpu < HP_CPU_COUNT; cpu++)
            if (link[cpu] != NULL)
                dev_ipl_poll(link[cpu]);
        done += IPL_SLICE_INSTRUCTIONS;
        if (result != 0)
            break;
    }
    hp_select(HP_SP);
    return result;
}
