/* hp21xx_iop.c - the HP 2000 I/O Processor firmware of the 2100 (option
 * 13206A): queue, stack, translate, move and I/O helper instructions that the
 * 2000F and Access I/O Processors use.
 *
 * Author: Thomas Dzubin.
 *
 * The behaviour follows the HP 2100 notes and the SIMH HP 2100 simulator
 * (MIT licence): the instruction codes are those of the 2100; the stack pointer
 * is the memory protect fence register, which OTA/OTB 5 loads. The byte and
 * word moves take a signed count, and a negative count does nothing. Interrupts
 * are only taken between instructions, so a long move runs to the end.
 */

#include "hp21xx.h"
#include "hp21xx_iop_const.h"

static hp_word next_word(void)
{
    hp_word w = hp_read(hp.p);

    hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
    return w;
}

/* The inline word is a DEF: follow the indirect chain to the address. */
static int resolve_def(hp_word *address)
{
    hp_word a = next_word();
    unsigned level = 0;

    while (a & IR_INDIRECT) {
        if (++level > HP_INDIRECT_LIMIT)
            return HP_STOP_INDIRECT;
        a = hp_read(a & HP_ADDR_MASK);
    }
    *address = a;
    return 0;
}

static uint8_t read_byte(hp_word byte_address)
{
    hp_word w = hp_read(byte_address >> 1);

    return (uint8_t)((byte_address & 1) ? (w & 0377) : (w >> 8));
}

static void write_byte(hp_word byte_address, uint8_t v)
{
    hp_word addr = (hp_word)(byte_address >> 1);
    hp_word w = hp_read(addr);

    if (byte_address & 1)
        w = (hp_word)((w & 0177400u) | v);
    else
        w = (hp_word)((w & 0377u) | ((hp_word)v << 8));
    hp_write(addr, w);
}

static hp_word inc16(hp_word v)
{
    return (hp_word)((v + 1) & HP_WORD_MASK);
}

/* MBYTE and MWORD: DEF to the initial count, then the address of the working
 * count. The working count in the second word lets an interrupted move go on. */
static int move(int bytes)
{
    hp_word count_addr, work_addr, wc;
    int status;

    status = resolve_def(&count_addr);
    if (status != 0)
        return status;
    work_addr = hp.p;                       /* the inline variable itself */
    hp.p = inc16(hp.p);
    wc = hp_read(work_addr);
    if (wc == 0)
        wc = hp_read(count_addr);
    if (wc & HP_SIGN)
        return 0;                           /* negative count: no operation */
    while (wc != 0) {
        if (bytes) {
            write_byte(hp.b, read_byte(hp.a));
        } else {
            hp_write(hp.b & HP_ADDR_MASK, hp_read(hp.a & HP_ADDR_MASK));
        }
        hp.a = inc16(hp.a);
        hp.b = inc16(hp.b);
        wc = (hp_word)((wc - 1) & HP_WORD_MASK);
    }
    hp_write(work_addr, wc);
    return 0;
}

int hp_iop_execute(void)
{
    hp_word ir = hp.ir;
    hp_word t, head, tail, address, count, varaddr, iw;
    unsigned i;
    int status = 0;

    /* LAI and SAI: A loaded or stored indexed by B, offset -16 to +15 */
    if (ir >= IOP_LAI_FIRST && ir <= IOP_LAI_LAST) {
        address = (hp_word)((hp.b + (int)(ir - IOP_LAI_FIRST) - IOP_INDEX_BIAS)
                            & HP_ADDR_MASK);
        hp.a = hp_read(address);
        return 0;
    }
    if (ir >= IOP_SAI_FIRST && ir <= IOP_SAI_LAST) {
        address = (hp_word)((hp.b + (int)(ir - IOP_SAI_FIRST) - IOP_INDEX_BIAS)
                            & HP_ADDR_MASK);
        hp_write(address, hp.a);
        return 0;
    }

    switch (ir) {
    case IOP_ILIST:                         /* store A, A+1, ... at a list */
        status = resolve_def(&address);
        if (status != 0)
            return status;
        count = next_word();
        do {
            hp_write(address, hp.a);
            hp.a = inc16(hp.a);
            address = (hp_word)((address + 1) & HP_ADDR_MASK);
            count = (hp_word)((count - 1) & HP_WORD_MASK);
        } while (count != 0);
        return 0;

    case IOP_MBYTE:
        return move(1);

    case IOP_MWORD:
        return move(0);

    case IOP_SBYTE:
        write_byte(hp.b, (uint8_t)(hp.a & 0377));
        hp.b = inc16(hp.b);
        return 0;

    case IOP_LBYTE:
        hp.a = read_byte(hp.b);
        hp.b = inc16(hp.b);
        return 0;

    case IOP_CRC:                           /* one character into a CRC word */
        varaddr = hp.p;
        hp.p = inc16(hp.p);
        t = (hp_word)(hp_read(varaddr) ^ (hp.a & 0377));
        for (i = 0; i < 8; i++) {
            t = (hp_word)(((t >> 1) | ((t & 1) << 15)) & HP_WORD_MASK);
            if (t & HP_SIGN)
                t ^= 020001;
        }
        hp_write(varaddr, t);
        return 0;

    case IOP_TRSLT:                         /* translate B-pointed bytes by table A */
        varaddr = hp.p;
        hp.p = inc16(hp.p);
        count = hp_read(varaddr);
        if (count & HP_SIGN)
            return 0;
        while (count != 0) {
            address = (hp_word)((hp.a + hp.a + read_byte(hp.b)) & HP_ADDR_MASK);
            write_byte(hp.b, read_byte(address));
            hp.b = inc16(hp.b);
            count = (hp_word)((count - 1) & HP_WORD_MASK);
        }
        return 0;                           /* the inline count is left as it was */

    case IOP_READF:
        hp.a = hp.spr;
        return 0;

    case IOP_ENQ:
        head = hp_read(hp.a);
        tail = hp_read(hp.a + 1);
        hp_write(hp.b - 1, 0);
        hp_write(tail - 1, hp.b);
        hp_write(hp.a + 1, hp.b);
        if (head != 0)
            hp.p = inc16(hp.p);
        return 0;

    case IOP_PENQ:
        head = hp_read(hp.a);
        hp_write(hp.b - 1, head);
        hp_write(hp.a, hp.b);
        if (head == 0)
            hp_write(hp.a + 1, hp.b);
        else
            hp.p = inc16(hp.p);
        return 0;

    case IOP_DEQ:
        hp.b = hp_read(hp.a);
        if (hp.b != 0) {
            head = hp_read(hp.b - 1);
            hp_write(hp.a, head);
            if (head == 0)
                hp_write(hp.a + 1, (hp_word)(hp.a + 1));
            hp.p = inc16(hp.p);
        }
        return 0;

    case IOP_SAVE:                          /* push A, B and E/O on the stack */
        hp_write(hp.spr, hp.a);
        hp.spr = inc16(hp.spr);
        hp_write(hp.spr, hp.b);
        hp.spr = inc16(hp.spr);
        hp_write(hp.spr, (hp_word)((((hp.o ^ 1) & 1) << 1) | hp.e));
        hp.spr = inc16(hp.spr);
        return 0;

    case IOP_RESTORE:
        hp.spr = (hp_word)((hp.spr - 1) & HP_WORD_MASK);
        t = hp_read(hp.spr);
        hp.o = (uint8_t)(((t >> 1) ^ 1) & 1);
        hp.e = (uint8_t)(t & 1);
        hp.spr = (hp_word)((hp.spr - 1) & HP_WORD_MASK);
        hp.b = hp_read(hp.spr);
        hp.spr = (hp_word)((hp.spr - 1) & HP_WORD_MASK);
        hp.a = hp_read(hp.spr);
        return 0;

    case IOP_PRFIO:                         /* constant, flag word: do the I/O */
    case IOP_PRFEI:                         /* ... then exit through a DEF */
        iw = next_word();
        varaddr = hp.p;
        hp.p = inc16(hp.p);
        hp_write(varaddr, 1);
        status = hp_iog_run(iw);
        if (ir == IOP_PRFIO)
            return status;
        status = resolve_def(&address);
        if (status != 0)
            return status;
        /* fall into the exit */
        hp.p = (hp_word)(hp_read(address) & HP_ADDR_MASK);
        hp_write(address, 0);
        return 0;

    case IOP_PRFEX:                         /* exit through a DEF */
        status = resolve_def(&address);
        if (status != 0)
            return status;
        hp.p = (hp_word)(hp_read(address) & HP_ADDR_MASK);
        hp_write(address, 0);
        return 0;

    default:
        return HP_STOP_UNIMPLEMENTED;
    }
}
