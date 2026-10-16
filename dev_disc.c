/* dev_disc.c - 13210A disc drive interface with HP 7900 drives.
 *
 * Author: Thomas Dzubin.
 *
 * A port of the controller model in hp2100_dp.c of the SIMH HP 2100 simulator
 * (Robert M. Supnik 2001-2016, J. David Bryan 2016-2022, MIT licence), reduced
 * to the 13210A card pair and rearranged around the CPU's standard card
 * flip-flops. The model follows the 13210A and 7900A manuals as SIMH does: the
 * data channel moves one word per flag, the command channel runs the seek and
 * the transfer, and the status word is built when it is asked for.
 *
 * Differences from the standard card, handled in the hooks:
 *  - the data channel has no control flip-flop and never interrupts;
 *  - OTA/OTB to the command channel also clears its control flip-flop.
 */

#include <stdio.h>
#include "dev_disc.h"

static hp_word data_input(hp_card_t *card);
static void    data_output(hp_card_t *card, hp_word v);
static void    data_set_control(hp_card_t *card);
static void    data_clear_control(hp_card_t *card);
static void    data_reset(hp_card_t *card);
static hp_word command_input(hp_card_t *card);
static void    command_output(hp_card_t *card, hp_word v);
static void    command_set_control(hp_card_t *card);
static void    command_clear_control(hp_card_t *card);
static void    command_reset(hp_card_t *card);

static void    start_data_channel(dev_disc_t *d, unsigned function, unsigned drive,
                                  uint32_t time);
static void    start_controller(dev_disc_t *d, unsigned function, unsigned drive,
                                uint32_t time);
static void    data_service(void *arg);
static void    drive_service(void *arg);
static void    finish_command(dev_disc_t *d, unsigned drive);
static void    raise_data_flag(dev_disc_t *d);
static void    raise_command_flag(dev_disc_t *d);
static uint32_t disc_address(const dev_disc_t *d, unsigned cyl, unsigned head,
                             unsigned sector);

/* An event argument names its drive: the controller is found through a
 * back pointer kept next to the drive number. */
typedef struct {
    dev_disc_t *disc;
    unsigned    drive;
} drive_ref_t;

static drive_ref_t drive_ref[DISC_DRIVES];

void dev_disc_init(dev_disc_t *d, unsigned select_code)
{
    unsigned i;

    d->data_card.ctx = d;
    d->data_card.input = data_input;
    d->data_card.output = data_output;
    d->data_card.set_control = data_set_control;
    d->data_card.clear_control = data_clear_control;
    d->data_card.reset = data_reset;
    d->data_card.flag_set = NULL;
    d->data_card.flag_clear = NULL;
    d->data_card.edt = NULL;
    d->data_card.flag = 0;
    d->data_card.flag_buffer = 1;       /* power-on preset sets the flag buffer */
    d->data_card.control = 0;

    d->command_card.ctx = d;
    d->command_card.input = command_input;
    d->command_card.output = command_output;
    d->command_card.set_control = command_set_control;
    d->command_card.clear_control = command_clear_control;
    d->command_card.reset = command_reset;
    d->command_card.flag_set = NULL;
    d->command_card.flag_clear = NULL;
    d->command_card.edt = NULL;
    d->command_card.flag = 0;
    d->command_card.flag_buffer = 1;
    d->command_card.control = 0;

    d->data_event.active = 0;
    d->data_command = d->command_command = 0;
    d->data_function = d->data_drive = 0;
    d->transfer = d->write_valid = d->poll = d->end_of_cylinder = 0;
    d->busy = 0;
    d->rar_cyl = d->rar_head = d->rar_sector = 0;
    d->data_in = d->data_out = d->command_out = 0;
    d->check_count = 0;
    d->buffer_pos = 0;
    d->io_errors = 0;

    for (i = 0; i < DISC_DRIVES; i++) {
        d->drive[i].image = NULL;
        d->drive[i].write_protect = 0;
        d->drive[i].unloaded = 1;           /* no cartridge: heads unloaded */
        d->drive[i].function = 0;
        d->drive[i].cylinder = 0;
        d->drive[i].status = 0;
        d->drive[i].event.active = 0;
        drive_ref[i].disc = d;
        drive_ref[i].drive = i;
    }

    hp_card_install(&d->data_card, select_code);
    hp_card_install(&d->command_card, select_code + 1);
}

/* Mounting a cartridge loads the heads, which sets Attention and First Status. */
void dev_disc_attach(dev_disc_t *d, unsigned drive, hp_store_t *image,
                     int write_protect)
{
    disc_drive_t *dr = &d->drive[drive];

    dr->image = image;
    dr->write_protect = (uint8_t)(write_protect != 0);
    dr->unloaded = 0;
    dr->status |= DSTA_ATTENTION | DSTA_FIRST;
    if (d->poll)
        d->command_card.flag_buffer = 1;
}

/* ---- Helpers --------------------------------------------------------------- */

static void raise_data_flag(dev_disc_t *d)
{
    hp_card_set_flag(&d->data_card);
}

static void raise_command_flag(dev_disc_t *d)
{
    hp_card_set_flag(&d->command_card);
}

static uint32_t disc_address(const dev_disc_t *d, unsigned cyl, unsigned head,
                             unsigned sector)
{
    (void)d;
    return (((cyl * DISC_SURFACES) + head) * DISC_SECTORS + sector) *
           DISC_WORDS_PER_SECTOR;
}

/* An event is only scheduled if it is not already waiting (as in SIMH). */
static void activate(hp_event_t *ev, uint32_t time, void (*fn)(void *), void *arg)
{
    if (!hp_event_active(ev))
        hp_event_after(ev, time, fn, arg);
}

/* ---- Data channel (first card) -------------------------------------------- */

static hp_word data_input(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;

    return d->data_in;
}

static void data_output(hp_card_t *card, hp_word v)
{
    dev_disc_t *d = card->ctx;

    d->data_out = v;
    if (!d->busy || d->transfer)
        d->write_valid = 1;
}

/* STC sets the command flip-flop. The card has no control flip-flop of its
 * own, so the one the CPU sets is cleared again (no interrupts). */
static void data_set_control(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;

    card->control = 0;
    d->data_command = 1;
    if (d->busy && !d->transfer)
        d->drive[d->busy - 1].status |= DSTA_OVERRUN;
}

/* CLC ends the data transfer. */
static void data_clear_control(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;

    d->transfer = 0;
}

static void data_reset(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;

    d->data_command = 0;
    d->rar_cyl = 0;
    d->drive[(d->command_out >> 0) & DUNIT_MASK].cylinder = 0;
}

/* ---- Command channel (second card) ---------------------------------------- */

static hp_word command_input(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;
    hp_word v = 0;
    unsigned i;

    for (i = 0; i < DISC_DRIVES; i++)
        if (d->drive[i].status & DSTA_ATTENTION)
            v |= (hp_word)(1u << i);
    return v;
}

static void command_clear_control_logic(dev_disc_t *d)
{
    d->command_card.control = 0;
    if (d->busy)
        hp_event_cancel(&d->drive[d->busy - 1].event);
    hp_event_cancel(&d->data_event);
    d->transfer = 0;
    d->busy = 0;
    d->poll = 0;
}

/* OTA/OTB loads the command register and, on the 13210A, also does CLC. */
static void command_output(hp_card_t *card, hp_word v)
{
    dev_disc_t *d = card->ctx;

    d->command_out = v;
    command_clear_control_logic(d);
}

static void command_clear_control(hp_card_t *card)
{
    command_clear_control_logic(card->ctx);
}

static void command_reset(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;
    unsigned drv;

    card->control = 0;
    for (drv = 0; drv < DISC_DRIVES; drv++)
        d->drive[drv].status &= (hp_word)~DSTA_CLEARED_BY_RESET;
}

static void command_set_control(hp_card_t *card)
{
    dev_disc_t *d = card->ctx;
    unsigned drv = d->command_out & DUNIT_MASK;
    unsigned fnc = (d->command_out >> DCMD_SHIFT) & DCMD_MASK;

    if (fnc != DCMD_STATUS)
        d->drive[drv].status &= (hp_word)~DSTA_CLEARED_BY_COMMAND;

    switch (fnc) {
    case DCMD_SEEK:
        d->poll = 1;
        start_data_channel(d, fnc, drv, DISC_CHANNEL_TIME);
        break;
    case DCMD_STATUS:
        d->data_card.flag_buffer = 0;
        d->data_card.flag = 0;
        hp_irq_eval();
        start_data_channel(d, fnc, drv, DISC_CHANNEL_TIME);
        break;
    case DCMD_CHECK:
    case DCMD_ADDRESS:
        start_data_channel(d, fnc, drv, DISC_CHANNEL_TIME);
        break;
    case DCMD_READ:
    case DCMD_WRITE:
    case DCMD_REFINE:
    case DCMD_INIT:
        start_controller(d, fnc, drv, DISC_COMMAND_TIME);
        break;
    default:
        break;
    }
}

/* ---- Starting operations -------------------------------------------------- */

static void start_data_channel(dev_disc_t *d, unsigned function, unsigned drive,
                               uint32_t time)
{
    d->data_drive = (uint8_t)drive;
    d->data_function = (uint8_t)function;
    activate(&d->data_event, time, data_service, d);
}

static void start_controller(dev_disc_t *d, unsigned function, unsigned drive,
                             uint32_t time)
{
    disc_drive_t *dr = &d->drive[drive];
    uint32_t t = hp_event_remaining(&dr->event);

    if (t) {                                /* still seeking: wait for it */
        hp_event_cancel(&dr->event);
        dr->status &= (hp_word)~DSTA_BUSY;
        time += t;
    }
    d->buffer_pos = 0;
    d->end_of_cylinder = 0;
    d->busy = (uint8_t)(drive + 1);
    d->transfer = 1;
    dr->function = (uint8_t)function;
    dr->status &= (hp_word)~(DSTA_ATTENTION | DSTA_FIRST);
    activate(&dr->event, time, drive_service, &drive_ref[drive]);
}

/* ---- Data channel service ------------------------------------------------- */

static void data_service(void *arg)
{
    dev_disc_t *d = arg;
    unsigned drv = d->data_drive;
    disc_drive_t *dr = &d->drive[drv];
    unsigned i;
    uint32_t st;

    switch (d->data_function) {

    case DCMD_ADDRESS:                      /* need the cylinder */
    case DCMD_SEEK:
        if (d->data_command) {
            d->rar_cyl = (uint8_t)(d->data_out & DADDR_CYL_MASK);
            d->write_valid = 0;
            d->data_command = 0;
            raise_data_flag(d);
            d->data_function = (d->data_function == DCMD_ADDRESS)
                               ? DCMD_ADDRESS1 : DCMD_SEEK1;
        }
        activate(&d->data_event, DISC_WORD_TIME, data_service, d);
        break;

    case DCMD_ADDRESS1:                     /* need head and sector */
    case DCMD_SEEK1:
        if (d->data_command) {
            d->rar_head = (uint8_t)((d->data_out >> DADDR_HEAD_SHIFT) & DADDR_HEAD_MASK);
            d->rar_sector = (uint8_t)(d->data_out & DADDR_SECTOR_MASK);
            d->write_valid = 0;
            d->data_command = 0;
            raise_data_flag(d);

            if (d->data_function == DCMD_ADDRESS1) {
                d->command_command = 0;
                raise_command_flag(d);
                dr->status |= DSTA_ATTENTION;
                break;
            }
            if (hp_event_active(&dr->event)) {      /* busy: seek check */
                dr->status |= DSTA_SEEK_CHECK;
                break;
            }
            if (d->rar_cyl >= DISC_CYLINDERS || d->rar_sector >= DISC_SECTORS) {
                dr->status |= DSTA_SEEK_CHECK;
                hp_event_after(&dr->event, 1, drive_service, &drive_ref[drv]);
                dr->function = DCMD_SEEK3;
                break;
            }
            st = (uint32_t)(d->rar_cyl > dr->cylinder ? d->rar_cyl - dr->cylinder
                                                      : dr->cylinder - d->rar_cyl)
                 * DISC_SEEK_PER_CYL;
            if (st == 0)
                st = DISC_SEEK_PER_CYL;
            dr->cylinder = d->rar_cyl;
            hp_event_after(&dr->event, st, drive_service, &drive_ref[drv]);
            dr->status = (hp_word)((dr->status | DSTA_BUSY) &
                                   ~(DSTA_SEEK_CHECK | DSTA_SEEK_INCOMPLETE |
                                     DSTA_PROTECTED));
            dr->function = DCMD_SEEK2;
        } else {
            activate(&d->data_event, DISC_WORD_TIME, data_service, d);
        }
        break;

    case DCMD_STATUS:                       /* return the drive status */
        if (!dr->unloaded) {
            d->data_in = dr->status & (hp_word)~DSTA_ANY_ERROR;
            d->data_in = (hp_word)((d->data_in & ~(DSTA_ZERO_13210 | DSTA_PROTECTED)) |
                                   (dr->write_protect ? DSTA_PROTECTED : 0));
        } else {
            d->data_in = DSTA_UNLOADED;
        }
        if (d->data_in & DSTA_ERROR_SET)
            d->data_in |= DSTA_ANY_ERROR;

        d->command_command = 0;
        d->data_command = 0;
        raise_data_flag(d);

        dr->status &= (hp_word)~(DSTA_ATTENTION | DSTA_FIRST);
        d->poll = 1;
        for (i = 0; i < DISC_DRIVES; i++)
            if (d->drive[i].status & DSTA_ATTENTION) {
                raise_command_flag(d);
                break;
            }
        break;

    case DCMD_CHECK:                        /* need the sector count */
        if (d->data_command) {
            d->check_count = (int16_t)(d->data_out & DADDR_CHECK_MASK);
            d->write_valid = 0;
            start_controller(d, DCMD_CHECK1, drv, DISC_WORD_TIME);
        } else {
            activate(&d->data_event, DISC_WORD_TIME, data_service, d);
        }
        break;

    default:
        break;
    }
}

/* ---- Drive service -------------------------------------------------------- */

static void finish_command(dev_disc_t *d, unsigned drive)
{
    d->drive[drive].status |= DSTA_ATTENTION;
    d->command_command = 0;
    raise_command_flag(d);
    d->busy = 0;
    d->transfer = 0;
    d->write_valid = 0;
}

static void drive_service(void *arg)
{
    drive_ref_t *ref = arg;
    dev_disc_t *d = ref->disc;
    unsigned drv = ref->drive;
    disc_drive_t *dr = &d->drive[drv];
    uint32_t da;
    int err = 0;
    uint16_t raw[DISC_WORDS_PER_SECTOR];
    uint8_t bytes[DISC_WORDS_PER_SECTOR * 2];
    unsigned i;

    if (dr->unloaded || dr->image == NULL) {        /* drive down */
        d->command_command = 0;
        raise_command_flag(d);
        dr->status = 0;
        d->busy = 0;
        d->poll = 0;
        d->transfer = 0;
        d->write_valid = 0;
        return;
    }

    switch (dr->function) {

    case DCMD_SEEK2:                        /* positioning done */
        dr->status = (hp_word)((dr->status | DSTA_ATTENTION) & ~DSTA_BUSY);
        /* fall through */
    case DCMD_SEEK3:                        /* seek complete */
        if (d->poll) {
            d->command_command = 0;
            raise_command_flag(d);
        }
        return;

    case DCMD_REFINE:                       /* nothing to do */
        break;

    case DCMD_READ:
    case DCMD_CHECK1:
        if (d->buffer_pos == 0) {           /* a new sector */
            if (!d->data_command && dr->function != DCMD_CHECK1)
                break;
            if (d->rar_cyl != dr->cylinder)
                dr->status |= DSTA_ADDRESS_ERROR;
            if (d->rar_sector >= DISC_SECTORS) {
                dr->status |= DSTA_ADDRESS_ERROR;
                break;
            }
            if (d->end_of_cylinder) {
                dr->status |= DSTA_END_OF_CYL;
                break;
            }
            da = disc_address(d, d->rar_cyl, d->rar_head, d->rar_sector);
            d->rar_sector = (uint8_t)((d->rar_sector + 1) % DISC_SECTORS);
            if (d->rar_sector == 0) {
                d->rar_head ^= 1;
                d->end_of_cylinder = (d->rar_head & 1) == 0;
            }
            err = storage_read(dr->image, da * 2, bytes, sizeof bytes);
            if (err)
                break;
            for (i = 0; i < DISC_WORDS_PER_SECTOR; i++)
                d->buffer[i] = (uint16_t)(bytes[2 * i] | (bytes[2 * i + 1] << 8));
        }
        d->data_in = d->buffer[d->buffer_pos++];
        if (d->buffer_pos >= DISC_WORDS_PER_SECTOR) {
            if (dr->function == DCMD_CHECK1) {
                d->check_count = (int16_t)((d->check_count - 1) & DADDR_CHECK_MASK);
                if (d->check_count == 0)
                    break;
            }
            d->buffer_pos = 0;
        }
        if (d->data_command && d->transfer)
            raise_data_flag(d);
        d->data_command = 0;
        hp_event_after(&dr->event, DISC_WORD_TIME, drive_service, ref);
        return;

    case DCMD_INIT:
    case DCMD_WRITE:
        if (d->buffer_pos == 0) {           /* start of a sector */
            if (!d->data_command && !d->write_valid)
                break;
            if (dr->write_protect) {
                dr->status |= DSTA_FLAGGED;
                break;
            }
            if (d->rar_cyl != dr->cylinder || d->rar_sector >= DISC_SECTORS) {
                dr->status |= DSTA_ADDRESS_ERROR;
                break;
            }
            if (d->end_of_cylinder) {
                dr->status |= DSTA_END_OF_CYL;
                break;
            }
        }
        d->buffer[d->buffer_pos++] = d->write_valid ? d->data_out : 0;
        d->write_valid = 0;
        if (d->buffer_pos >= DISC_WORDS_PER_SECTOR) {   /* buffer full */
            da = disc_address(d, d->rar_cyl, d->rar_head, d->rar_sector);
            d->rar_sector = (uint8_t)((d->rar_sector + 1) % DISC_SECTORS);
            if (d->rar_sector == 0) {
                d->rar_head ^= 1;
                d->end_of_cylinder = (d->rar_head & 1) == 0;
            }
            for (i = 0; i < DISC_WORDS_PER_SECTOR; i++) {
                raw[i] = d->buffer[i];
                bytes[2 * i] = (uint8_t)(raw[i] & 0xFF);
                bytes[2 * i + 1] = (uint8_t)(raw[i] >> 8);
            }
            err = storage_write(dr->image, da * 2, bytes, sizeof bytes);
            if (err)
                break;
            d->buffer_pos = 0;
        }
        if (d->data_command && d->transfer)
            raise_data_flag(d);
        d->data_command = 0;
        hp_event_after(&dr->event, DISC_WORD_TIME, drive_service, ref);
        return;

    default:
        return;
    }

    if (err)
        d->io_errors++;
    finish_command(d, drv);
}
