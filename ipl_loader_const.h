/* ipl_loader_const.h - the two bootstrap loaders a two-CPU system needs.
 *
 * Author: Thomas Dzubin.
 *
 * Taken from the SIMH HP 2100 simulator (hp2100_ipl.c and hp2100_dp.c, Robert M.
 * Supnik and J. David Bryan, MIT licence); the listings there show them for a
 * 32K memory (octal 77700 up).
 *
 * IPL_BBL: the 2000/Access special basic binary loader. Starting at its first
 * word it reads an absolute tape from the Processor Interconnect; the I/O
 * Processor runs it to receive its program from the System Processor.
 *
 * BMDL_7900: the HP 21xx basic moving-head disc loader. The second half (octal
 * 50 and up) reads the disc bootstrap from cylinder 0, head 0, sector 0 of a 7900
 * into memory at 2011 and jumps to 2055.
 */

#ifndef IPL_LOADER_CONST_H
#define IPL_LOADER_CONST_H

#define LOADER_WORDS            64u

#define IPL_BBL_FWA_INDEX       073u    /* ABS -PI: minus the loader's own address */
#define IPL_BBL_POINTER_1       074u    /* DEF *+2, an address inside the loader */
#define IPL_BBL_POINTER_2       075u
#define IPL_BBL_SC_INDEX_1      076u    /* the interconnect select code */
#define IPL_BBL_SC_INDEX_2      077u    /* the paper tape reader select code */
#define IPL_BBL_START_INDEX     0u      /* start here to read from the interconnect */
#define IPL_BBL_LISTING_BASE    077700u /* where the listing puts it */

#define BMDL_START_INDEX        050u    /* start here to read the disc */
#define BMDL_DMA_INDEX          077u    /* the DMA control word: 120000 + disc select code */
#define BMDL_LISTING_SC         010u    /* select codes in the listing: 10 and 11 */

#define IPL_BBL_INIT \
    { 0163774U, 0027751U, 0107700U, 0002702U, 0063772U, 0002307U, \
      0027760U, 0017736U, 0007307U, 0027705U, 0077770U, 0017736U, \
      0017736U, 0074000U, 0077771U, 0067771U, 0047773U, 0002040U, \
      0102055U, 0017736U, 0040001U, 0177771U, 0037771U, 0000040U, \
      0037770U, 0027717U, 0017736U, 0054000U, 0027704U, 0102011U, \
      0000000U, 0006600U, 0103700U, 0102300U, 0027741U, 0106400U, \
      0002041U, 0127736U, 0005767U, 0027740U, 0163775U, 0043765U, \
      0073741U, 0043766U, 0073740U, 0043767U, 0073743U, 0027702U, \
      0063777U, 0067776U, 0102077U, 0027702U, 0000000U, 0102300U, \
      0001400U, 0002500U, 0000000U, 0000000U, 0177746U, 0100100U, \
      0077776U, 0077777U, 0000010U, 0000010U }

#define BMDL_7900_INIT \
    { 0002401U, 0063721U, 0107700U, 0002307U, 0102077U, 0017735U, \
      0007307U, 0027702U, 0077733U, 0017735U, 0017735U, 0074000U, \
      0077747U, 0047734U, 0002140U, 0102055U, 0017735U, 0177747U, \
      0040001U, 0067747U, 0006104U, 0037733U, 0027714U, 0017735U, \
      0054000U, 0027701U, 0102011U, 0000000U, 0100100U, 0000000U, \
      0006400U, 0103710U, 0102310U, 0027740U, 0107410U, 0002240U, \
      0127735U, 0005727U, 0027737U, 0000000U, 0030000U, 0067741U, \
      0106611U, 0103711U, 0063750U, 0102610U, 0103710U, 0102611U, \
      0103711U, 0063777U, 0102606U, 0063732U, 0102602U, 0103710U, \
      0102702U, 0102602U, 0106611U, 0103710U, 0103706U, 0103711U, \
      0102311U, 0027774U, 0117717U, 0120010U }


/* BMTL: the HP 21xx basic magnetic tape loader. Started at its first word with the
 * file number in S (0 = the file at the present tape position), it reads an
 * absolute binary file from a 13183B tape and stops with HLT 77. Its listing
 * uses select codes 10 (data) and 11 (command). */
#define BMTL_START_INDEX        0u
#define BMTL_LISTING_SC         010u

#define BMTL_INIT \
    { 0102501U, 0013775U, 0003007U, 0027714U, 0073777U, 0067771U, \
      0017761U, 0102311U, 0027707U, 0067773U, 0037777U, 0027706U, \
      0067772U, 0017761U, 0103710U, 0017740U, 0005727U, 0007004U, \
      0077777U, 0017740U, 0074000U, 0077776U, 0017740U, 0177776U, \
      0040001U, 0037776U, 0037777U, 0027726U, 0017740U, 0054000U, \
      0017740U, 0102011U, 0000000U, 0102310U, 0027745U, 0107510U, \
      0127740U, 0102311U, 0027741U, 0102511U, 0013774U, 0067777U, \
      0001727U, 0002020U, 0102077U, 0002003U, 0006002U, 0102000U, \
      0027714U, 0000000U, 0106611U, 0102511U, 0001323U, 0001310U, \
      0027762U, 0103711U, 0127761U, 0001501U, 0001423U, 0000203U, \
      0016263U, 0000077U, 0000000U, 0000000U }

#endif
