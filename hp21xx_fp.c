/* hp21xx_fp.c - HP 2100 floating point firmware (option 12901A).
 *
 * Author: Thomas Dzubin.
 *
 * FAD, FSB, FMP, FDV, FIX and FLT, in the 2100's own two-word floating point
 * format. The algorithms deliberately reproduce the 2100 microcode, including
 * its quirks (the multiply loses the low bit of the product, the divide can be
 * one bit out), because Time-Shared BASIC output depends on them. They are
 * adapted from hp2100_cpu_fp.c of the SIMH HP 2100 simulator, itself a
 * reconstruction of the microcode by Robert M. Supnik (2002) with corrections
 * by J. David Bryan (MIT licence).
 *
 * Format: word 1 = sign and 15 high fraction bits; word 2 = 8 low fraction
 * bits, a 7-bit exponent magnitude and the exponent sign in bit 0.
 *
 *    15 14                                         0
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |S |               fraction high                | : A
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |     fraction low      |      exponent      |XS| : A + 1
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 */

#include "hp21xx.h"
#include "hp21xx_fp_const.h"

typedef struct {
    int32_t  exp;                           /* exponent, two's complement */
    uint32_t fr;                            /* fraction, left justified */
} ufp_t;

static uint32_t fp_ab(void);
static uint32_t unpack_fp(ufp_t *fop, uint32_t operand);
static void     norm_fp(ufp_t *fop);
static uint32_t pack_fp(const ufp_t *fop);
static uint32_t store_fp(ufp_t *fop);
static uint32_t fr_ars(uint32_t v, unsigned s);
static uint32_t fr_neg(uint32_t v);
static uint32_t divide_step(uint32_t ba, uint32_t dvr, uint32_t *rem);

static uint32_t f_fix(void);
static uint32_t f_flt(void);
static uint32_t f_as(uint32_t operand, int subtract);
static uint32_t f_mul(uint32_t operand);
static uint32_t f_div(uint32_t operand);

static int32_t sext16(uint32_t v)
{
    return (int32_t)(int16_t)(v & HP_WORD_MASK);
}

static uint32_t fp_ab(void)
{
    return ((uint32_t)hp.a << 16) | hp.b;
}

/* Arithmetic right shift of a 32-bit fraction; 0 < s < 32 */
static uint32_t fr_ars(uint32_t v, unsigned s)
{
    uint32_t r = v >> s;

    if (v & FP_SIGN)
        r |= 0xFFFFFFFFu << (32 - s);
    return r;
}

static uint32_t fr_neg(uint32_t v)
{
    return ~v + 1;
}

/* ---- Instruction dispatch ------------------------------------------------ */

/* The operand of FAD, FSB, FMP and FDV is the address (DEF) of a two-word
 * number in the word after the instruction. */
int hp_fp_execute(void)
{
    unsigned entry = (hp.ir >> 4) & 017;
    uint32_t operand = 0;
    hp_word address, high, low;
    int status;

    if (entry > 5)
        return 0;                           /* not a floating point opcode */

    if (entry <= 3) {
        hp.m = hp_read(hp.p);
        /* resolve the indirect chain of the DEF */
        {
            unsigned level = 0;

            while (hp.m & IR_INDIRECT) {
                if (++level > HP_INDIRECT_LIMIT)
                    return HP_STOP_INDIRECT;
                hp.m = hp_read(hp.m & HP_ADDR_MASK);
            }
        }
        address = hp.m;
        high = hp_read(address);
        low = hp_read((uint32_t)address + 1);
        operand = ((uint32_t)high << 16) | low;
        hp.p = (hp_word)((hp.p + 1) & HP_ADDR_MASK);
    }

    switch (entry) {
    case 0:
        hp.o = (uint8_t)f_as(operand, 0);
        break;
    case 1:
        hp.o = (uint8_t)f_as(operand, 1);
        break;
    case 2:
        hp.o = (uint8_t)f_mul(operand);
        break;
    case 3:
        hp.o = (uint8_t)f_div(operand);
        break;
    case 4:
        hp.o = (uint8_t)f_fix();
        break;
    case 5:
        hp.o = (uint8_t)f_flt();
        break;
    }
    status = 0;
    return status;
}

/* ---- The arithmetic ------------------------------------------------------- */

/* Floating to integer conversion */
static uint32_t f_fix(void)
{
    ufp_t fop;
    uint32_t res = 0;

    unpack_fp(&fop, fp_ab());
    if (fop.exp < 0) {                      /* less than one: result is zero */
        hp.a = 0;
        return 0;                           /* B unchanged */
    }
    if (fop.exp > 15) {                     /* too big */
        hp.b = hp.a;                        /* B has the high bits */
        hp.a = 077777;
        return 1;                           /* overflow */
    }
    if (fop.exp < 15) {                     /* shift into place */
        res = fr_ars(fop.fr, 15 - fop.exp);
        hp.a = (hp_word)(res >> 16);
    }
    hp.b = hp.a;
    if ((hp.a & HP_SIGN) && ((fop.fr | res) & HP_WORD_MASK))
        hp.a = (hp_word)((hp.a + 1) & HP_WORD_MASK);    /* any low bits lost */
    return 0;
}

/* Integer to floating conversion */
static uint32_t f_flt(void)
{
    ufp_t res;

    res.exp = 15;
    res.fr = (uint32_t)hp.a << 16;
    store_fp(&res);
    return 0;
}

/* Floating point add and subtract */
static uint32_t f_as(uint32_t operand, int subtract)
{
    ufp_t fop1, fop2, t;
    int32_t ediff;

    unpack_fp(&fop1, fp_ab());
    unpack_fp(&fop2, operand);
    if (subtract) {
        fop2.fr = fr_neg(fop2.fr);
        if (fop2.fr == FP_SIGN) {           /* -1/2 */
            fop2.fr >>= 1;
            fop2.exp += 1;
        }
    }
    if (fop1.fr == 0) {
        fop1 = fop2;
    } else if (fop2.fr != 0) {
        if (fop1.exp < fop2.exp) {          /* swap so fop1 is the larger */
            t = fop2;
            fop2 = fop1;
            fop1 = t;
        }
        ediff = fop1.exp - fop2.exp;
        if (ediff <= 24) {
            if (ediff)
                fop2.fr = fr_ars(fop2.fr, (unsigned)ediff);
            if ((fop1.fr ^ fop2.fr) & FP_SIGN) {
                fop1.fr = fop1.fr + fop2.fr;    /* effective subtract */
            } else {
                fop1.fr = fop1.fr + fop2.fr;    /* effective add */
                if (fop2.fr & FP_SIGN) {        /* both negative */
                    if ((fop1.fr & FP_SIGN) == 0) {
                        fop1.fr = FP_SIGN | (fop1.fr >> 1);
                        fop1.exp += 1;
                    }
                } else if (fop1.fr & FP_SIGN) { /* both positive, carry out */
                    fop1.fr >>= 1;
                    fop1.exp += 1;
                }
            }
        }
    }
    return store_fp(&fop1);
}

/* Floating point multiply: 16 by 16 bit multiplies giving a 31 bit result */
static uint32_t f_mul(uint32_t operand)
{
    ufp_t fop1, fop2, res;
    int32_t shi1, shi2, t1, t2, t3, t4, t5;

    res.exp = 0;
    res.fr = 0;
    unpack_fp(&fop1, fp_ab());
    unpack_fp(&fop2, operand);
    if (fop1.fr && fop2.fr) {
        res.exp = fop1.exp + fop2.exp + 1;
        shi1 = sext16(fop1.fr >> 16);
        shi2 = sext16(fop2.fr >> 16);
        t1 = shi2 * (int32_t)((fop1.fr >> 1) & 077600);
        t2 = shi1 * (int32_t)((fop2.fr >> 1) & 077600);
        t3 = t1 + t2;
        t4 = (shi1 * shi2) & ~1;
        t5 = sext16((uint32_t)t3 >> 16) << 1;
        res.fr = (uint32_t)(t4 + t5);       /* bit 0 is lost */
    }
    return store_fp(&res);
}

/* One 32 by 16 bit signed divide done with unsigned arithmetic */
static uint32_t divide_step(uint32_t ba, uint32_t dvr, uint32_t *rem)
{
    int sdvd = 0, sdvr = 0;
    uint32_t q, r;

    if (ba & FP_SIGN)
        sdvd = 1;
    if (dvr & HP_SIGN)
        sdvr = 1;
    if (sdvd)
        ba = fr_neg(ba);
    if (sdvr)
        dvr = (~dvr + 1) & HP_WORD_MASK;
    q = ba / dvr;
    r = ba % dvr;
    if (sdvd ^ sdvr)
        q = (~q + 1) & HP_WORD_MASK;
    if (sdvd)
        r = (~r + 1) & HP_WORD_MASK;
    if (rem)
        *rem = r;
    return q;
}

/* Floating point divide, an approximation that can be one bit out */
static uint32_t f_div(uint32_t operand)
{
    ufp_t fop1, fop2, quo;
    uint32_t ba, q0, q1, q2, dvrh;

    quo.exp = 0;
    quo.fr = 0;
    unpack_fp(&fop1, fp_ab());
    unpack_fp(&fop2, operand);
    dvrh = fop2.fr >> 16;
    if (dvrh == 0) {                        /* divide by zero */
        hp.a = 0077777;                     /* most positive number */
        hp.b = 0177776;
        return 1;
    }
    if (fop1.fr) {
        quo.exp = fop1.exp - fop2.exp + 1;
        ba = fr_ars(fop1.fr, 2);            /* prevent overflow */
        q0 = divide_step(ba, dvrh, &ba);
        ba = (ba & ~1u) << 16;              /* the remainder */
        ba = fr_ars(ba, 1);
        q1 = divide_step(ba, dvrh, NULL);
        ba = (fop2.fr & 0xFF00u) << 13;     /* low divisor / 8 */
        q2 = divide_step(ba, dvrh, NULL);
        ba = (uint32_t)(-sext16(q2 & HP_WORD_MASK) * sext16(q0 & HP_WORD_MASK));
        ba = (ba >> 16) & 0xFFFFu;          /* keep the high half */
        if (q1 & HP_SIGN)
            quo.fr -= 0x00010000u;
        if (ba & HP_SIGN)
            quo.fr -= 0x00010000u;
        quo.fr += ((ba << 2) & 0xFFFFu) + q1;
        quo.fr <<= 1;
        quo.fr += q0 << 16;
    }
    return store_fp(&quo);
}

/* ---- Utilities ------------------------------------------------------------ */

static uint32_t unpack_fp(ufp_t *fop, uint32_t operand)
{
    fop->fr = operand & FP_FRACTION;
    fop->exp = (int32_t)((operand >> FP_EXP_SHIFT) & FP_EXP_MASK);
    if (operand & FP_EXP_SIGN)
        fop->exp |= ~(int32_t)FP_EXP_MASK;
    return operand >> 31;
}

static void norm_fp(ufp_t *fop)
{
    uint32_t test;

    if (fop->fr) {
        test = (fop->fr >> 1) & FP_NORM;
        while ((fop->fr & FP_NORM) == test) {
            fop->exp -= 1;
            fop->fr <<= 1;
        }
    } else {
        fop->exp = 0;                       /* a clean zero */
    }
}

static uint32_t pack_fp(const ufp_t *fop)
{
    return (fop->fr & FP_FRACTION) |
           (((uint32_t)fop->exp & FP_EXP_MASK) << FP_EXP_SHIFT) |
           (fop->exp < 0 ? FP_EXP_SIGN : 0);
}

/* Normalise, round and store in A and B; the return value is the overflow. */
static uint32_t store_fp(ufp_t *fop)
{
    uint32_t sign, svfr, hi, ov = 0;

    norm_fp(fop);
    svfr = fop->fr;
    sign = fop->fr >> 31;
    fop->fr = (fop->fr + (sign ? FP_ROUND_MINUS : FP_ROUND_PLUS)) & FP_FRACTION;
    if ((fop->fr ^ svfr) & FP_SIGN) {       /* the sign changed: renormalise */
        fop->fr >>= 1;
        fop->exp += 1;
    } else {
        norm_fp(fop);
    }
    if (fop->fr == 0) {
        hi = 0;
    } else if (fop->exp < -(int32_t)(FP_EXP_MASK + 1)) {    /* underflow */
        hi = 0;
        ov = 1;
    } else if (fop->exp > (int32_t)FP_EXP_MASK) {           /* overflow */
        hi = 0x7FFFFFFEu;
        ov = 1;
    } else {
        hi = pack_fp(fop);
    }
    hp.a = (hp_word)(hi >> 16);
    hp.b = (hp_word)(hi & HP_WORD_MASK);
    return ov;
}
