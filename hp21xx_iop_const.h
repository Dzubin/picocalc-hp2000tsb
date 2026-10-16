/*
 * hp21xx_iop_const.h - instruction codes of the HP 2000 I/O Processor firmware
 * (option 13206A for the 2100).
 *
 * Author: Thomas Dzubin.
 */
#ifndef HP21XX_IOP_CONST_H
#define HP21XX_IOP_CONST_H

#define IOP_LAI_FIRST   0105020u        /* LAI: A = (B + offset) */
#define IOP_LAI_LAST    0105057u
#define IOP_SAI_FIRST   0105060u        /* SAI: (B + offset) = A */
#define IOP_SAI_LAST    0105117u
#define IOP_INDEX_BIAS  16              /* offset = code - first - bias */

#define IOP_ILIST       0105000u
#define IOP_MBYTE       0105120u
#define IOP_CRC         0105150u
#define IOP_TRSLT       0105160u
#define IOP_MWORD       0105200u
#define IOP_READF       0105220u
#define IOP_PRFIO       0105221u
#define IOP_PRFEI       0105222u
#define IOP_PRFEX       0105223u
#define IOP_ENQ         0105240u
#define IOP_PENQ        0105257u
#define IOP_DEQ         0105260u
#define IOP_SBYTE       0105300u
#define IOP_LBYTE       0105320u
#define IOP_RESTORE     0105340u
#define IOP_SAVE        0105362u

#endif /* HP21XX_IOP_CONST_H */
