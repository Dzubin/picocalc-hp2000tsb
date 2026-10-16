/* hp21xx_fp_const.h - constants for the 2100 floating point firmware. */

#ifndef HP21XX_FP_CONST_H
#define HP21XX_FP_CONST_H

#define FP_SIGN             0x80000000u     /* fraction sign bit */
#define FP_FRACTION         0xFFFFFF00u     /* the 24 fraction bits */
#define FP_EXP_SHIFT        1
#define FP_EXP_MASK         0177u           /* 7-bit exponent magnitude */
#define FP_EXP_SIGN         1u              /* bit 0 of the low word */
#define FP_NORM             0x40000000u     /* bit that shows normalised */
#define FP_ROUND_PLUS       0x80u           /* rounding added to a plus fraction */
#define FP_ROUND_MINUS      0x7Fu           /* ... and to a minus fraction */

#endif
