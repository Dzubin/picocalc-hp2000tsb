/* ptr_loader_const.h - the 21xx basic binary loader (paper tape bootstrap).
 *
 * The 64-word loader that the 2100's front panel puts at the top of memory
 * (octal 77700 for 32K, 37700 for 16K) and runs to read an absolute binary
 * tape from the paper tape reader. Taken from the HP 21xx Basic Binary Loader
 * as listed in the SIMH HP 2100 simulator (hp2100_pt.c, MIT licence).
 *
 * The listing shows the loader for 32K memory. Loading it into memory moves it
 * to the top of the installed memory (base = last 64 words); the reader's
 * select code is patched in by hp_load_ptr_loader().
 */

#ifndef PTR_LOADER_CONST_H
#define PTR_LOADER_CONST_H

#define PTR_LOADER_WORDS        64
#define PTR_LOADER_FWA_INDEX    072         /* negative start address goes here */
#define PTR_LOADER_START_INDEX  0           /* run from the first word */
#define PTR_LOADER_LISTING_SC   010         /* select code in the listing */

#define PTR_LOADER_INIT \
    { 0107700u, 0063770u, 0106501u, 0004010u, 0002400u, 0006020u, 0063771u, \
      0073736u, 0006401u, 0067773u, 0006006u, 0027717u, 0107700u, 0102077u, \
      0027700u, 0017762u, 0002003u, 0027712u, 0003104u, 0073774u, 0017762u, \
      0017753u, 0070001u, 0073775u, 0063775u, 0043772u, 0002040u, 0027751u, \
      0017753u, 0044000u, 0000000u, 0002101u, 0102000u, 0037775u, 0037774u, \
      0027730u, 0017753u, 0054000u, 0027711u, 0102011u, 0027700u, 0102055u, \
      0027700u, 0000000u, 0017762u, 0001727u, 0073776u, 0017762u, 0033776u, \
      0127753u, 0000000u, 0103710u, 0102310u, 0027764u, 0102510u, 0127762u, \
      0173775u, 0153775u, 0100100u, 0177765u, 0000000u, 0000000u, 0000000u, \
      0000000u }

#endif
