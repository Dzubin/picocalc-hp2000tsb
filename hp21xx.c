/* hp21xx.c - HP 2100A CPU emulation.
 *
 * Author: Thomas Dzubin.
 *
 * Registers, memory, the base instruction set (memory reference, alter-skip,
 * shift-rotate, I/O), the extended arithmetic unit (EAU), the interrupt system
 * and a small event scheduler for devices. Floating point firmware is in
 * hp21xx_fp.c.
 *
 * Instruction behaviour is taken from the HP 2100A and from the SIMH HP 2100
 * simulator (Robert M. Supnik 1993-2016, J. David Bryan 2017-2022, MIT licence):
 * the interrupt deferral rules, the EAU operand and overflow rules and the
 * ordering of the I/O signals follow hp2100_cpu.c and hp2100_cpu1.c.
 *
 * There is no memory protect, DMA, dual map or power fail here: the 2000E
 * Time-Shared BASIC system uses none of them.
 */

#include <string.h>
#include "hp21xx.h"

hp_cpu_t hp_cpus[HP_CPU_COUNT];
hp_cpu_t *hp_cur = &hp_cpus[0];

void hp_select(unsigned cpu)
{
    hp_cur = &hp_cpus[cpu < HP_CPU_COUNT ? cpu : 0];
}

unsigned hp_selected(void)
{
    return (unsigned)(hp_cur - hp_cpus);
}

/* Internal status passed back through the instruction executors. */
#define NOTE_INTERRUPT      (-1)        /* interrupt seen while resolving addresses */

static int  execute(void);
static int  mrg_address(void);
static int  do_iog(void);
static int  do_eau(void);
static int  operand_k(hp_word *value);
static int  operand_d(uint32_t *value);
static int  operand_a(hp_word *address);
static int  resolve_indirects(int interruptible);
static int  reenable_interrupts(void);
static hp_word shift_micro_op(hp_word value, unsigned op);
static void run_events(void);
static void recompute_next_due(void);
static void crs_all_cards(void);
static void irq_acknowledge(uint16_t sc);

/* ---- Memory -------------------------------------------------------------- */

/* Memory locations 0 and 1 are the A and B registers. */
hp_word hp_read(uint32_t address)
{
    address &= HP_ADDR_MASK;
    if (address == 0)
        return hp.a;
    if (address == 1)
        return hp.b;
    return address < hp.mem_size ? hp.mem[address] : 0;
}

void hp_write(uint32_t address, hp_word value)
{
    address &= HP_ADDR_MASK;
    if (address == 0)
        hp.a = value;
    else if (address == 1)
        hp.b = value;
    else if (address < hp.mem_size)
        hp.mem[address] = value;
}

/* The core memory itself, as DMA sees it: locations 0 and 1 are real words. */
hp_word hp_read_core(uint32_t address)
{
    address &= HP_ADDR_MASK;
    return address < hp.mem_size ? hp.mem[address] : 0;
}

void hp_write_core(uint32_t address, hp_word value)
{
    address &= HP_ADDR_MASK;
    if (address < hp.mem_size)
        hp.mem[address] = value;
}

/* A memory read or write by the CPU also loads the M and T registers. */
static inline hp_word read_word(uint32_t address)
{
    hp_word value;

    address &= HP_ADDR_MASK;
    hp.m = (hp_word)address;
    if (address > 1 && address < hp.mem_size)           /* locations 0 and 1 are A and B */
        value = hp.mem[address];
    else
        value = hp_read(address);
    hp.t = value;
    return value;
}

static void write_word(uint32_t address, hp_word value)
{
    hp.m = (hp_word)(address & HP_ADDR_MASK);
    hp.t = value;
    hp_write(address, value);
}

/* ---- Start-up ------------------------------------------------------------- */

void hp_enable_iop(void)
{
    hp.have_fp = 0;
    hp.have_iop = 1;
}

void hp_init(uint32_t mem_words, int with_fp)
{
    memset(&hp, 0, sizeof hp);
    hp.mem_size = mem_words > HP_MEM_WORDS ? HP_MEM_WORDS : mem_words;
    hp.have_fp = (uint8_t)(with_fp != 0);
    hp.have_eau = 1;
    hp.int_ok = 1;
}

/* The power-on preset, as the I/O backplane signals it: control reset, then the
 * power-on preset to I/O, which sets every flag buffer, then enable flag, which
 * sets every flag whose buffer is set. So all cards start with flag and flag
 * buffer set (and a DMA channel with its flag set is inactive). */
void hp_preset(void)
{
    unsigned sc;
    hp_card_t *c;

    hp.int_system = 0;
    hp.int_ok = 1;
    hp.irq_sc = 0;
    hp.halted = 0;
    hp.e = 0;
    hp.o = 0;
    crs_all_cards();
    for (sc = 6; sc < HP_CARD_SLOTS; sc++) {
        c = hp.card[sc];
        if (c == NULL)
            continue;
        c->flag_buffer = 1;
        c->flag = 1;
        if (c->flag_set != NULL)
            c->flag_set(c);
    }
    hp_irq_eval();
}

/* ---- Cards ---------------------------------------------------------------- */

/* The interrupt scan used to walk all 64 select codes at every I/O instruction; it
 * now walks the few that have a card. */
static void list_irq_slots(void)
{
    unsigned sc, n = 0;

    for (sc = 4; sc < HP_CARD_SLOTS; sc++)
        if (hp.card[sc] != NULL)
            hp.irq_slot[n++] = (uint8_t)sc;
    hp.irq_slots = (uint8_t)n;
}

int hp_card_install(hp_card_t *card, unsigned select_code)
{
    if (select_code >= HP_CARD_SLOTS)
        return -1;
    card->select_code = (uint8_t)select_code;
    hp.card[select_code] = card;
    list_irq_slots();
    return 0;
}

void hp_card_remove(unsigned select_code)
{
    if (select_code < HP_CARD_SLOTS)
        hp.card[select_code] = NULL;
    list_irq_slots();
}

void hp_card_set_flag(hp_card_t *card)
{
    card->flag_buffer = 1;
    card->flag = 1;
    if (card->flag_set != NULL)
        card->flag_set(card);
    hp_irq_eval();
}

void hp_card_clear_flag(hp_card_t *card)
{
    card->flag_buffer = 0;
    card->flag = 0;
    if (card->flag_clear != NULL)
        card->flag_clear(card);
    hp_irq_eval();
}

/* Work out which card, if any, is interrupting. A card whose control and flag
 * are both set holds off every card of lower priority (higher select code);
 * if its flag buffer is set too, it is the one interrupting. */
void hp_irq_eval(void)
{
    unsigned slot, sc;
    hp_card_t *c;

    if (hp.dma_eval != NULL)
        hp.dma_eval();
    hp.irq_sc = 0;
    if (!hp.int_system)
        return;
    for (slot = 0; slot < hp.irq_slots; slot++) {
        sc = hp.irq_slot[slot];
        c = hp.card[sc];
        if (c->control && c->flag) {
            if (c->flag_buffer)
                hp.irq_sc = (uint16_t)sc;
            return;
        }
    }
}

static void crs_all_cards(void)
{
    unsigned sc;
    hp_card_t *c;

    for (sc = 6; sc < HP_CARD_SLOTS; sc++) {
        c = hp.card[sc];
        if (c == NULL)
            continue;
        c->control = 0;
        if (c->reset != NULL)
            c->reset(c);
    }
}

static void irq_acknowledge(uint16_t sc)
{
    hp_card_t *c = hp.card[sc];

    if (c != NULL)
        c->flag_buffer = 0;
    hp_irq_eval();
}

/* ---- Events --------------------------------------------------------------- */

/* The CPU keeps pointers to the events devices have scheduled; the event
 * structures themselves belong to the devices. */

static void register_event(hp_event_t *ev)
{
    unsigned i;

    for (i = 0; i < HP_EVENT_SLOTS; i++)
        if (hp.event[i] == ev)
            return;
    for (i = 0; i < HP_EVENT_SLOTS; i++)
        if (hp.event[i] == NULL) {
            hp.event[i] = ev;
            return;
        }
}

static void recompute_next_due(void)
{
    unsigned i;
    int32_t best = 0x40000000;
    int32_t d;

    for (i = 0; i < HP_EVENT_SLOTS; i++) {
        if (hp.event[i] == NULL || !hp.event[i]->active)
            continue;
        d = (int32_t)(hp.event[i]->due - hp.ticks);
        if (d < best)
            best = d;
    }
    hp.next_due = hp.ticks + (uint32_t)best;
}

void hp_event_after(hp_event_t *ev, uint32_t instructions,
                    void (*fn)(void *arg), void *arg)
{
    if (instructions == 0)
        instructions = 1;
    ev->due = hp.ticks + instructions;
    ev->fn = fn;
    ev->arg = arg;
    ev->active = 1;
    register_event(ev);
    recompute_next_due();
}

void hp_event_cancel(hp_event_t *ev)
{
    ev->active = 0;
}

int hp_event_active(const hp_event_t *ev)
{
    return ev->active;
}

uint32_t hp_event_remaining(const hp_event_t *ev)
{
    int32_t d;

    if (!ev->active)
        return 0;
    d = (int32_t)(ev->due - hp.ticks);
    return d > 0 ? (uint32_t)d : 1;
}

static void run_events(void)
{
    unsigned i;
    hp_event_t *ev;

    for (i = 0; i < HP_EVENT_SLOTS; i++) {
        ev = hp.event[i];
        if (ev == NULL || !ev->active)
            continue;
        if ((int32_t)(hp.ticks - ev->due) >= 0) {
            ev->active = 0;
            ev->fn(ev->arg);
        }
    }
    recompute_next_due();
}

/* ---- Run loop ------------------------------------------------------------- */

int hp_run(uint32_t max_instructions)
{
    int status = HP_RUN_COUNT;
    uint16_t sc;
    hp_word save_p;

    hp.halted = 0;
    while (max_instructions != 0) {
        max_instructions--;

        if ((int32_t)(hp.ticks - hp.next_due) >= 0)
            run_events();

        if (hp.dma_request) {               /* a DMA cycle steals the memory cycle */
            hp.dma_cycle();
            if (hp.dma_request)
                continue;
        }

        save_p = hp.p;

        hp.last_irq = 0;
        if (hp.irq_sc != 0 && (hp.int_ok || reenable_interrupts())) {
            sc = hp.irq_sc;
            hp.last_irq = sc;
            hp.cir = sc;
            hp.int_ok = 0;                  /* no interruption of the trap cell */
            irq_acknowledge(sc);
            hp.ir = read_word(sc);          /* P is not advanced */
        } else {
            hp.ir = read_word(hp.p);
            hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
            hp.int_ok = 1;
        }

        status = execute();
        hp.ticks++;

        if (status == NOTE_INTERRUPT) {     /* back out and take the interrupt */
            hp.p = save_p;
            status = HP_RUN_COUNT;
        }
        if (status != HP_RUN_COUNT) {
            if (status != HP_STOP_HALT)
                hp.p = save_p;              /* an error stop leaves P at the cause */
            hp.halted = 1;
            return status;
        }
    }
    return HP_RUN_COUNT;
}

/* After JMP, JSB,I, STF, CLF, STC and CLC the following instruction cannot be
 * interrupted. On the 2100 that deferral is lifted if the following
 * instruction is a memory reference other than JMP and JSB,I. */
static int reenable_interrupts(void)
{
    hp_word next = hp_read(hp.p);

    if ((next & IR_MRG_TEST) != 0) {
        unsigned op = (next >> IR_MRG_OP_SHIFT) & IR_MRG_OP_MASK;

        if (op == MRG_JMP)
            return 0;
        if (op == MRG_JSB && (next & IR_INDIRECT))
            return 0;
        return 1;
    }
    return 0;
}

/* ---- Memory reference addressing ------------------------------------------ */

/* Follow an indirect chain. On entry M holds a word with the indirect bit
 * possibly set; on exit M is the direct address. */
static int resolve_indirects(int interruptible)
{
    unsigned level;

    if (!(hp.m & IR_INDIRECT))
        return 0;
    hp.m = read_word(hp.m & HP_ADDR_MASK);
    if (hp.m & IR_INDIRECT) {
        if (interruptible && hp.irq_sc != 0 && hp.int_ok)
            return NOTE_INTERRUPT;
        for (level = 2; hp.m & IR_INDIRECT; level++) {
            if (level > HP_INDIRECT_LIMIT)
                return HP_STOP_INDIRECT;
            hp.m = read_word(hp.m & HP_ADDR_MASK);
        }
    }
    return 0;
}

/* The effective address of a memory reference instruction goes into M. */
static int mrg_address(void)
{
    if (hp.ir & IR_CURRENT_PAGE)
        hp.m = (hp_word)((hp.ir & (IR_INDIRECT | IR_OFFSET)) |
                         (hp.m & IR_PAGE_BITS));
    else
        hp.m = (hp_word)(hp.ir & (IR_INDIRECT | IR_OFFSET));
    return resolve_indirects(1);
}

/* ---- Instruction execution ------------------------------------------------ */

static hp_word shift_micro_op(hp_word value, unsigned op)
{
    unsigned ext;

    switch (op) {
    case SRG_OP_ENABLE1 | SRG_ALS:
    case SRG_OP_ENABLE2 | SRG_ALS:
        return (hp_word)((value & HP_SIGN) | ((value << 1) & 0077777u));
    case SRG_OP_ENABLE1 | SRG_ARS:
    case SRG_OP_ENABLE2 | SRG_ARS:
        return (hp_word)((value & HP_SIGN) | (value >> 1));
    case SRG_OP_ENABLE1 | SRG_RAL:
    case SRG_OP_ENABLE2 | SRG_RAL:
        return (hp_word)(((value << 1) | (value >> 15)) & HP_WORD_MASK);
    case SRG_OP_ENABLE1 | SRG_RAR:
    case SRG_OP_ENABLE2 | SRG_RAR:
        return (hp_word)(((value >> 1) | (value << 15)) & HP_WORD_MASK);
    case SRG_OP_ENABLE1 | SRG_ALR:
    case SRG_OP_ENABLE2 | SRG_ALR:
        return (hp_word)((value << 1) & 0077777u);
    case SRG_ERA:                           /* disabled ERA/ERB still shifts E */
        hp.e = value & 1;
        return value;
    case SRG_OP_ENABLE1 | SRG_ERA:
    case SRG_OP_ENABLE2 | SRG_ERA:
        ext = hp.e;
        hp.e = value & 1;
        return (hp_word)((value >> 1) | (ext << 15));
    case SRG_ELA:                           /* disabled ELA/ELB still shifts E */
        hp.e = (value >> 15) & 1;
        return value;
    case SRG_OP_ENABLE1 | SRG_ELA:
    case SRG_OP_ENABLE2 | SRG_ELA:
        ext = hp.e;
        hp.e = (value >> 15) & 1;
        return (hp_word)(((value << 1) | ext) & HP_WORD_MASK);
    case SRG_OP_ENABLE1 | SRG_ALF:
    case SRG_OP_ENABLE2 | SRG_ALF:
        return (hp_word)(((value << 4) | (value >> 12)) & HP_WORD_MASK);
    default:
        return value;
    }
}

static int execute(void)
{
    unsigned op;
    int status;
    hp_word data;
    uint32_t result;
    hp_word *reg;

    if (hp.ir & IR_MRG_TEST) {
        /* ---- memory reference group ---- */
        op = (hp.ir >> IR_MRG_OP_SHIFT) & IR_MRG_OP_MASK;

        if (op == MRG_JMP || (op == MRG_JSB && (hp.ir & IR_INDIRECT)))
            hp.int_ok = 0;                  /* defer interrupts */

        status = mrg_address();
        if (status != 0)
            return status;

        switch (op) {
        case MRG_AND:
            hp.a &= read_word(hp.m);
            break;
        case MRG_JSB:
            write_word(hp.m, hp.p);
            hp.p = (hp_word)((hp.m + 1) & HP_ADDR_MASK);
            break;
        case MRG_XOR:
            hp.a ^= read_word(hp.m);
            break;
        case MRG_JMP:
            hp.p = hp.m;
            break;
        case MRG_IOR:
            hp.a |= read_word(hp.m);
            break;
        case MRG_ISZ:
            data = (hp_word)((read_word(hp.m) + 1) & HP_WORD_MASK);
            write_word(hp.m, data);
            if (data == 0)
                hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
            break;
        case MRG_ADA:
        case MRG_ADB:
            reg = op == MRG_ADA ? &hp.a : &hp.b;
            data = read_word(hp.m);
            result = (uint32_t)*reg + data;
            if (result > HP_WORD_MASK)
                hp.e = 1;
            if (~(*reg ^ data) & (*reg ^ result) & HP_SIGN)
                hp.o = 1;
            *reg = (hp_word)(result & HP_WORD_MASK);
            break;
        case MRG_CPA:
            if (hp.a != read_word(hp.m))
                hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
            break;
        case MRG_CPB:
            if (hp.b != read_word(hp.m))
                hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
            break;
        case MRG_LDA:
            hp.a = read_word(hp.m);
            break;
        case MRG_LDB:
            hp.b = read_word(hp.m);
            break;
        case MRG_STA:
            write_word(hp.m, hp.a);
            break;
        case MRG_STB:
            write_word(hp.m, hp.b);
            break;
        }
        return 0;
    }

    /* IR bits 14-12 are zero from here: the other groups */

    if ((hp.ir & IOG_TEST_MASK) == IOG_TEST_VALUE)
        return do_iog();

    if (hp.ir & 0100000u) {                 /* EAU and user instruction groups */
        switch ((hp.ir >> 8) & 0377) {
        case 0200: case 0201: case 0202: case 0210: case 0211:
            return do_eau();
        case 0212:                          /* user instruction group 0 */
            if (hp.have_fp)
                return hp_fp_execute();
            if (hp.have_iop)
                return hp_iop_execute();
            return 0;
        default:
            return 0;                       /* not installed: no operation */
        }
    }

    reg = (hp.ir & IR_B_REGISTER) ? &hp.b : &hp.a;
    data = *reg;

    if (hp.ir & IR_ALTER_SKIP) {
        /* ---- alter-skip group (bit 10 set) ---- */
        unsigned skip = 0;
        unsigned rss = (hp.ir & ASG_RSS) != 0;

        if (hp.ir & ASG_CL)
            data = 0;
        if (hp.ir & ASG_CM)
            data ^= HP_WORD_MASK;
        if ((hp.ir & ASG_SEZ) && ((hp.e == 0) ^ rss))
            skip = 1;
        if (hp.ir & ASG_CLE)
            hp.e = 0;
        if (hp.ir & ASG_CME)
            hp.e ^= 1;
        if ((hp.ir & ASG_SS_SL_RSS) == ASG_SS_SL_RSS) {
            if ((data & (HP_SIGN | 1)) == (HP_SIGN | 1))
                skip = 1;
        } else {
            if ((hp.ir & ASG_SS) && (!(data & HP_SIGN) ^ rss))
                skip = 1;
            if ((hp.ir & ASG_SL) && (!(data & 1) ^ rss))
                skip = 1;
        }
        if (hp.ir & ASG_IN) {
            data = (hp_word)((data + 1) & HP_WORD_MASK);
            if (data == 0)
                hp.e = 1;
            else if (data == HP_SIGN)
                hp.o = 1;
        }
        if ((hp.ir & ASG_SZ) && ((data == 0) ^ rss))
            skip = 1;
        if ((hp.ir & ASG_ALL_SKIPS) == ASG_RSS)
            skip = 1;
        *reg = data;
        hp.p = (hp_word)((hp.p + skip) & HP_ADDR_MASK);
        return 0;
    }

    /* ---- shift-rotate group ---- */
    data = shift_micro_op(data, (hp.ir & SRG1_MASK) >> SRG1_SHIFT);
    if (hp.ir & SRG_CLE)
        hp.e = 0;
    if ((hp.ir & SRG_SL) && !(data & 1))
        hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
    *reg = shift_micro_op(data, hp.ir & SRG2_MASK);
    return 0;
}

/* ---- Extended arithmetic unit --------------------------------------------- */

/* The operand of MPY and DIV: the next word is the address (DEF) of the value. */
static int operand_a(hp_word *address)
{
    int status;

    hp.m = read_word(hp.p);
    status = resolve_indirects(1);
    if (status != 0)
        return status;
    *address = hp.m;
    hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
    return 0;
}

static int operand_k(hp_word *value)
{
    hp_word address;
    int status = operand_a(&address);

    if (status == 0)
        *value = read_word(address);
    return status;
}

static int operand_d(uint32_t *value)
{
    hp_word address;
    int status = operand_a(&address);
    hp_word high, low;

    if (status == 0) {
        high = read_word(address);
        low = read_word((uint32_t)address + 1);
        *value = ((uint32_t)high << 16) | low;
    }
    return status;
}

static uint32_t to_dword(hp_word high, hp_word low)
{
    return ((uint32_t)high << 16) | low;
}

static int do_eau(void)
{
    unsigned shift = hp.ir & 017u;
    uint32_t operand, fill, mask;
    int32_t sop1, sop2;
    uint32_t v1, v2, rs, qs;
    uint32_t dword;
    hp_word value, address;
    int status;

    if (!hp.have_eau)
        return 0;
    if (shift == 0)
        shift = 16;

    switch ((hp.ir >> 8) & 0377) {

    case 0200:                              /* EAU group 0 */
        switch ((hp.ir >> 4) & 017) {
        case 000:                           /* DIAG: no operation */
            break;
        case 001:                           /* ASL 100020-100037 */
            operand = to_dword(hp.b, hp.a);
            mask = 0xFFFFFFFFu << (31 - shift);
            if (operand & 0x80000000u)
                hp.o = (~operand & mask) != 0;
            else
                hp.o = (operand & mask) != 0;
            operand = ((operand << shift) & 0x7FFFFFFFu) |
                      (operand & 0x80000000u);
            hp.b = (hp_word)(operand >> 16);
            hp.a = (hp_word)operand;
            break;
        case 002:                           /* LSL 100040-100057 */
        case 003:                           /* TIMER 100060 acts as LSL on a 2100 */
            operand = to_dword(hp.b, hp.a) << shift;
            hp.b = (hp_word)(operand >> 16);
            hp.a = (hp_word)operand;
            break;
        case 004:                           /* RRL 100100-100117 */
            operand = to_dword(hp.b, hp.a);
            fill = operand;
            operand = (operand << shift) | (fill >> (32 - shift));
            hp.b = (hp_word)(operand >> 16);
            hp.a = (hp_word)operand;
            break;
        case 010:                           /* MPY 100200 */
            status = operand_k(&value);
            if (status != 0)
                return status;
            sop1 = (int16_t)hp.a;
            sop2 = (int16_t)value;
            sop1 = sop1 * sop2;
            hp.b = (hp_word)((uint32_t)sop1 >> 16);
            hp.a = (hp_word)(uint32_t)sop1;
            hp.o = 0;
            break;
        default:                            /* undefined: no operation */
            break;
        }
        break;

    case 0201:                              /* DIV 100400 */
        status = operand_k(&value);
        if (status != 0)
            return status;
        rs = qs = hp.b & HP_SIGN;
        if (rs) {                           /* make the dividend positive */
            hp.a = (hp_word)((~hp.a + 1) & HP_WORD_MASK);
            hp.b = (hp_word)((~hp.b + (hp.a == 0)) & HP_WORD_MASK);
        }
        v2 = value;
        if (v2 & HP_SIGN) {
            v2 = (~v2 + 1) & HP_WORD_MASK;
            qs ^= HP_SIGN;
        }
        if (hp.b >= v2) {
            hp.o = 1;
        } else {
            hp.o = 0;
            v1 = ((uint32_t)hp.b << 16) | hp.a;
            hp.a = (hp_word)((v1 / v2) & HP_WORD_MASK);
            hp.b = (hp_word)((v1 % v2) & HP_WORD_MASK);
            if (hp.a) {
                if (qs)
                    hp.a = (hp_word)((~hp.a + 1) & HP_WORD_MASK);
                if ((hp.a ^ qs) & HP_SIGN)
                    hp.o = 1;
            }
            if (rs)
                hp.b = (hp_word)((~hp.b + 1) & HP_WORD_MASK);
        }
        break;

    case 0202:                              /* EAU group 2 */
        switch ((hp.ir >> 4) & 017) {
        case 001:                           /* ASR 101020-101037 */
            hp.o = 0;
            operand = to_dword(hp.b, hp.a);
            fill = (operand & 0x80000000u) ? 0xFFFFFFFFu : 0;
            operand = (operand >> shift) | (fill << (32 - shift));
            hp.b = (hp_word)(operand >> 16);
            hp.a = (hp_word)operand;
            break;
        case 002:                           /* LSR 101040-101057 */
            operand = to_dword(hp.b, hp.a) >> shift;
            hp.b = (hp_word)(operand >> 16);
            hp.a = (hp_word)operand;
            break;
        case 004:                           /* RRR 101100-101117 */
            operand = to_dword(hp.b, hp.a);
            fill = operand;
            operand = (operand >> shift) | (fill << (32 - shift));
            hp.b = (hp_word)(operand >> 16);
            hp.a = (hp_word)operand;
            break;
        default:
            break;
        }
        break;

    case 0210:                              /* DLD 104200 */
        status = operand_d(&dword);
        if (status != 0)
            return status;
        hp.a = (hp_word)(dword >> 16);
        hp.b = (hp_word)dword;
        break;

    case 0211:                              /* DST 104400 */
        status = operand_a(&address);
        if (status != 0)
            return status;
        write_word(address, hp.a);
        write_word((uint32_t)address + 1, hp.b);
        break;
    }
    return 0;
}

/* Run one I/O instruction taken from memory instead of the instruction register
 * (PRFIO and PRFEI). */
int hp_iog_run(hp_word instruction)
{
    hp_word saved = hp.ir;
    int status;

    hp.ir = instruction;
    status = do_iog();
    hp.ir = saved;
    return status;
}

/* ---- Input/output group --------------------------------------------------- */

/* The micro-operation is bits 8-6 of the instruction (HLT, STF, SFC, SFS, MI,
 * LI, OT, STC) with bit 9 meaning ",C": clear the flag afterwards. STF with
 * the C bit is the CLF instruction, which only clears the flag. CLC is the STC
 * pattern with the A/B bit set. */
static int do_iog(void)
{
    unsigned sc = hp.ir & HP_SC_MASK;
    hp_word *reg = (hp.ir & IR_B_REGISTER) ? &hp.b : &hp.a;
    unsigned op, with_c;
    int skip = 0;
    int clf_only = 0;
    hp_card_t *c = hp.card[sc];

    if ((hp.ir & IOG_CLC_MASK) == IOG_CLC_VALUE) {
        op = IOG_CLC;
        with_c = (hp.ir & IOG_CLEAR_FLAG_BIT) != 0;
    } else {
        op = (hp.ir & IOG_MASK) >> IOG_SHIFT;
        with_c = (op & IOG_WITH_C) != 0;
        op &= 7;
        if (op == IOG_STF && with_c) {      /* CLF */
            clf_only = 1;
            with_c = 0;
        }
    }

    /* HLT, STF, CLF, STC and CLC hold off interrupts for the next instruction */
    hp.int_ok = !(op == IOG_HLT || op == IOG_STF || op == IOG_STC ||
                  op == IOG_CLC);

    if (hp.io_trace != NULL)
        hp.io_trace(sc, hp.ir, *reg);

    if (op == IOG_LI)
        *reg = 0;

    if (sc == SC_INTERRUPT) {
        if (clf_only) {
            hp.int_system = 0;
        } else {
            switch (op) {
            case IOG_STF:
                hp.int_system = 1;
                break;
            case IOG_SFC:
                skip = !hp.int_system;
                break;
            case IOG_SFS:
                skip = hp.int_system;
                break;
            case IOG_CLC:
                crs_all_cards();
                break;
            default:
                break;
            }
            if (with_c)
                hp.int_system = 0;
        }
        hp_irq_eval();
    } else if (sc == SC_INTERRUPT_CODE) {
        /* the interrupt code register: reading it gives the select code of the
         * device that interrupted last; nothing else is connected here */
        if (op == IOG_LI)
            *reg = hp.cir;
        else if (op == IOG_MI)
            *reg |= hp.cir;
    } else if (sc == SC_OVERFLOW) {
        if (clf_only) {
            hp.o = 0;
        } else {
            switch (op) {
            case IOG_STF:
                hp.o = 1;
                break;
            case IOG_SFC:
                skip = !hp.o;
                break;
            case IOG_SFS:
                skip = hp.o;
                break;
            case IOG_MI:
                *reg |= hp.s;
                break;
            case IOG_LI:
                *reg = hp.s;
                break;
            case IOG_OT:
                hp.display = *reg;
                break;
            default:
                break;
            }
            if (with_c)
                hp.o = 0;
        }
    } else if (sc == SC_MP_FENCE && hp.have_iop) {
        /* on the IOP the memory protect fence register is the stack pointer */
        if (op == IOG_OT)
            hp.spr = *reg;
    } else if (c != NULL) {
        if (clf_only) {
            c->flag_buffer = 0;
            c->flag = 0;
            if (c->flag_clear != NULL)
                c->flag_clear(c);
        } else {
            switch (op) {
            case IOG_STF:
                c->flag_buffer = 1;
                c->flag = 1;
                if (c->flag_set != NULL)
                    c->flag_set(c);
                break;
            case IOG_SFC:
                skip = !c->flag;
                break;
            case IOG_SFS:
                skip = c->flag;
                break;
            case IOG_MI:
                if (c->input != NULL)
                    *reg |= c->input(c);
                break;
            case IOG_LI:
                *reg = c->input != NULL ? c->input(c) : 0;
                break;
            case IOG_OT:
                if (c->output != NULL)
                    c->output(c, *reg);
                break;
            case IOG_STC:
                c->control = 1;
                hp.iog_clf = (uint8_t)with_c;
                if (c->set_control != NULL)
                    c->set_control(c);
                with_c = hp.iog_clf;            /* a card may cancel the CLF */
                hp.iog_clf = 0;
                break;
            case IOG_CLC:
                c->control = 0;
                if (c->clear_control != NULL)
                    c->clear_control(c);
                break;
            default:
                break;
            }
            if (with_c) {
                c->flag_buffer = 0;
                c->flag = 0;
                if (c->flag_clear != NULL)
                    c->flag_clear(c);
            }
        }
        hp_irq_eval();
    }
    /* an empty slot reads zero and never skips */

    if (skip)
        hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
    if (op == IOG_HLT)
        return HP_STOP_HALT;
    return 0;
}
