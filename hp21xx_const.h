/* hp21xx_const.h - constants for the HP 21xx CPU emulation (hp21xx.c).
 *
 * Author: Thomas Dzubin.
 *
 * The instruction field layout, select code limits and I/O instruction
 * numbering follow the HP 2100A computer, as described by the SIMH HP 2100
 * simulator (Robert M. Supnik 1993-2016, J. David Bryan 2017-2022, MIT licence).
 */

#ifndef HP21XX_CONST_H
#define HP21XX_CONST_H

/* ---- Words, memory ---------------------------------------------------- */

#define HP_WORD_MASK        0177777u    /* 16-bit word */
#define HP_SIGN             0100000u    /* sign bit of a word */
#define HP_ADDR_MASK        0077777u    /* 15-bit logical address */
#define HP_MEM_WORDS        32768u      /* most memory a 21xx can have */
#define HP_MEM_DEFAULT      16384u      /* the 2000E system uses 16K words */
#define HP_CPU_COUNT        2           /* CPUs in a two-processor system (2000F: SP and IOP) */

#define HP_CARD_SLOTS       64          /* select codes 00 to 77 octal */
#define HP_FIRST_IO_SC      010         /* first select code for plug-in cards */
#define HP_SC_MASK          077u        /* select code field of an I/O instruction */

#define HP_INDIRECT_LIMIT   16          /* longest indirect chain allowed */

#define HP_EVENT_SLOTS      40          /* scheduled device events */

/* ---- Instruction fields ----------------------------------------------- */

#define IR_INDIRECT         0100000u    /* memory reference: indirect */
#define IR_CURRENT_PAGE     0002000u    /* memory reference: current page */
#define IR_OFFSET           0001777u    /* memory reference: page offset */
#define IR_PAGE_BITS        0076000u    /* page number part of an address */
#define IR_MRG_TEST         0070000u    /* nonzero means memory reference */
#define IR_MRG_OP_SHIFT     11          /* bits 14-11 select the MRG opcode */
#define IR_MRG_OP_MASK      017u

#define IR_B_REGISTER       0004000u    /* register selector (A=0, B=1) */
#define IR_B_SHIFT          11
#define IR_ALTER_SKIP       0002000u    /* group 0: alter-skip (else shift-rotate) */

/* Memory reference opcodes, (ir >> 11) & 15 */
#define MRG_AND             2
#define MRG_JSB             3
#define MRG_XOR             4
#define MRG_JMP             5
#define MRG_IOR             6
#define MRG_ISZ             7
#define MRG_ADA             8
#define MRG_ADB             9
#define MRG_CPA             10
#define MRG_CPB             11
#define MRG_LDA             12
#define MRG_LDB             13
#define MRG_STA             14
#define MRG_STB             15

/* Alter-skip group micro-operations */
#define ASG_CM              0001000u    /* CMA, CMB */
#define ASG_CL              0000400u    /* CLA, CLB */
#define ASG_CME             0000200u
#define ASG_CLE             0000100u
#define ASG_SEZ             0000040u
#define ASG_SS              0000020u    /* SSA, SSB */
#define ASG_SL              0000010u    /* SLA, SLB */
#define ASG_IN              0000004u    /* INA, INB */
#define ASG_SZ              0000002u    /* SZA, SZB */
#define ASG_RSS             0000001u
#define ASG_SS_SL_RSS       (ASG_SS | ASG_SL | ASG_RSS)
#define ASG_ALL_SKIPS       (ASG_SEZ | ASG_SZ | ASG_SS_SL_RSS)

/* Shift-rotate group */
#define SRG_CLE             0000040u
#define SRG_SL              0000010u
#define SRG1_MASK           0001700u
#define SRG1_SHIFT          6
#define SRG2_MASK           0000027u
#define SRG_OP_ENABLE1      0000010u    /* after SRG1 shift: enable bit */
#define SRG_OP_ENABLE2      0000020u
#define SRG_ALS             0           /* ALS, BLS */
#define SRG_ARS             1
#define SRG_RAL             2
#define SRG_RAR             3
#define SRG_ALR             4
#define SRG_ERA             5
#define SRG_ELA             6
#define SRG_ALF             7

/* I/O group */
#define IOG_MASK            0001700u    /* micro-operation (with the C bit) */
#define IOG_SHIFT           6
#define IOG_TEST_MASK       0172000u    /* I/O group when this matches ... */
#define IOG_TEST_VALUE      0102000u    /* ... this */
#define IOG_CLC_MASK        0176700u
#define IOG_CLC_VALUE       0106700u
#define IOG_CLEAR_FLAG_BIT  0001000u

#define IOG_HLT             0
#define IOG_STF             1
#define IOG_SFC             2
#define IOG_SFS             3
#define IOG_MI              4
#define IOG_LI              5
#define IOG_OT              6
#define IOG_STC             7
#define IOG_CLC             16          /* made up: CLC and CLC,C */
#define IOG_WITH_C          8           /* added for the ,C form */

/* ---- Special select codes ---------------------------------------------- */

#define SC_INTERRUPT        0           /* STF/CLF turn interrupts on/off */
#define SC_OVERFLOW         1           /* switch register, display, overflow */
#define SC_MP_FENCE         5           /* OTA/OTB 5 loads the memory protect fence (the IOP stack pointer) */
#define SC_INTERRUPT_CODE   4           /* LIA/LIB 4 reads the central interrupt register */

/* ---- Stop reasons from hp_run() ---------------------------------------- */

#define HP_RUN_COUNT        0           /* ran the requested instructions */
#define HP_STOP_HALT        1           /* HLT instruction (see hp.t) */
#define HP_STOP_INDIRECT    2           /* indirect chain too long */
#define HP_STOP_UNIMPLEMENTED 3         /* instruction not implemented */
#define HP_STOP_BREAK       4           /* stopped by the program */

/* ---- Timing ------------------------------------------------------------ */

#define HP_TICK_NONE        0xFFFFFFFFu /* no event is due */

#endif
