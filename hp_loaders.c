/* hp_loaders.c - the I/O Processor binary loader and the 7900 disc bootstrap.
 *
 * Author: Thomas Dzubin.
 *
 * The loader words are in ipl_loader_const.h (from the SIMH HP 2100 simulator,
 * MIT licence). They are listed for a 32K memory; for another size the words that
 * hold addresses are moved down and the select codes are patched.
 */

#include "hp_loaders.h"
#include "ipl_loader_const.h"
#include "dev_dq_const.h"

static const hp_word ipl_bbl[LOADER_WORDS] = IPL_BBL_INIT;
static const hp_word bmdl[LOADER_WORDS] = BMDL_7900_INIT;
static const hp_word bmtl[LOADER_WORDS] = BMTL_INIT;
static const hp_word bmdl_dq[DQ_LOADER_WORDS] = DQ_LOADER_INIT;

void hp_load_iop_loader(unsigned interconnect_sc)
{
    uint32_t base = hp.mem_size - LOADER_WORDS;
    unsigned i;

    for (i = 0; i < LOADER_WORDS; i++) {
        hp_word w = ipl_bbl[i];

        if (i == IPL_BBL_FWA_INDEX)
            w = (hp_word)((0u - base) & HP_WORD_MASK);
        else if (i == IPL_BBL_POINTER_1 || i == IPL_BBL_POINTER_2)
            w = (hp_word)(w - IPL_BBL_LISTING_BASE + base);
        else if (i == IPL_BBL_SC_INDEX_1 || i == IPL_BBL_SC_INDEX_2)
            w = (hp_word)interconnect_sc;
        hp_write(base + i, w);
    }
    hp.p = (hp_word)(base + IPL_BBL_START_INDEX);
}

void hp_load_disc_bootstrap(unsigned disc_sc)
{
    uint32_t base = hp.mem_size - LOADER_WORDS;
    unsigned i;

    for (i = 0; i < LOADER_WORDS; i++) {
        hp_word w = bmdl[i];

        if (i == BMDL_DMA_INDEX) {
            w = (hp_word)(0120000u | disc_sc);
        } else if ((w & IOG_TEST_MASK) == IOG_TEST_VALUE && (w & 0172700u) != 0102000u) {
            unsigned sc = w & HP_SC_MASK;

            if (sc == BMDL_LISTING_SC || sc == BMDL_LISTING_SC + 1)
                w = (hp_word)((w & ~(hp_word)HP_SC_MASK) | (sc - BMDL_LISTING_SC + disc_sc));
        }
        hp_write(base + i, w);
    }
    hp.p = (hp_word)(base + BMDL_START_INDEX);
}

void hp_load_dq_bootstrap(unsigned disc_sc)
{
    uint32_t base = hp.mem_size - DQ_LOADER_WORDS;
    unsigned i;

    for (i = 0; i < DQ_LOADER_WORDS; i++) {
        hp_word w = bmdl_dq[i];

        if (i == DQ_LOADER_DMA_INDEX) {
            w = (hp_word)(0120000u | disc_sc);
        } else if (i >= DQ_LOADER_START_INDEX && (w & IOG_TEST_MASK) == IOG_TEST_VALUE &&
                   (w & 0172700u) != 0102000u) {
            unsigned sc = w & HP_SC_MASK;

            /* the disc half of the listing uses select codes 10 and 11 */
            if (sc == DQ_LOADER_LISTING_SC || sc == DQ_LOADER_LISTING_SC + 1)
                w = (hp_word)((w & ~(hp_word)HP_SC_MASK) | (sc - DQ_LOADER_LISTING_SC + disc_sc));
        }
        hp_write(base + i, w);
    }
    hp.p = (hp_word)(base + DQ_LOADER_START_INDEX);
}

void hp_load_tape_loader(unsigned tape_sc)
{
    uint32_t base = hp.mem_size - LOADER_WORDS;
    unsigned i;

    for (i = 0; i < LOADER_WORDS; i++) {
        hp_word w = bmtl[i];

        /* I/O instructions on the listing's select codes 10 and 11 (but not the
         * halts, whose field is a halt code) move to the tape interface */
        if ((w & IOG_TEST_MASK) == IOG_TEST_VALUE && (w & 0172700u) != 0102000u) {
            unsigned sc = w & HP_SC_MASK;

            if (sc == BMTL_LISTING_SC || sc == BMTL_LISTING_SC + 1)
                w = (hp_word)((w & ~(hp_word)HP_SC_MASK) | (sc - BMTL_LISTING_SC + tape_sc));
        }
        hp_write(base + i, w);
    }
    hp.p = (hp_word)(base + BMTL_START_INDEX);
}
