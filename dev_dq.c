/* dev_dq.c - 12565A disc interface with HP 2883 drives.
 *
 * Author: Thomas Dzubin.
 *
 * A port of hp2100_dq.c of the SIMH HP 2100 simulator (Robert M. Supnik
 * 2001-2016, J. David Bryan 2016-2022, MIT licence; the 2883 changes by William
 * Oliver), rearranged around the CPU's standard card flip-flops. The model follows
 * the 12565A manual as SIMH does: the data channel moves one word per flag, the
 * command channel starts the drive, and the drive service runs the seek and the
 * sector transfers. The interface differs from the 13210A (dev_disc.c) in that
 * both cards have their own control flip-flop, the command channel is not cleared
 * by an output, and a transfer stops on an address miscompare.
 *
 * The record address register (RAR) is shared by the two drives; each drive keeps
 * its own cylinder and head. Sector operations start at the RAR sector.
 */

#include "dev_dq.h"

static hp_word data_input(hp_card_t *card);
static void    data_output(hp_card_t *card, hp_word v);
static void    data_set_control(hp_card_t *card);
static void    data_clear_control(hp_card_t *card);
static void    data_reset(hp_card_t *card);
static void    command_output(hp_card_t *card, hp_word v);
static void    command_set_control(hp_card_t *card);
static void    command_clear_control(hp_card_t *card);
static void    command_reset(hp_card_t *card);

static void    start_data_channel(dev_dq_t *d, unsigned function, unsigned drive,
                                  uint32_t time);
static void    start_controller(dev_dq_t *d, unsigned function, unsigned drive,
                                uint32_t time);
static void    data_service(void *arg);
static void    drive_service(void *arg);

/* An event argument names its drive: the controller is found through a back
 * pointer kept next to the drive number. */
typedef struct {
    dev_dq_t *dq;
    unsigned  drive;
} drive_ref_t;

static drive_ref_t drive_ref[DQ_DRIVES];

void dev_dq_init(dev_dq_t *d, unsigned select_code)
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
    d->command_card.input = NULL;       /* reads as zero */
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
    d->busy = d->transfer = d->write_valid = 0;
    d->rar_cyl = d->rar_head = d->rar_sector = 0;
    d->data_in = d->data_out = d->command_out = 0;
    d->check_count = 0;
    d->buffer_pos = 0;

    for (i = 0; i < DQ_DRIVES; i++) {
        d->drive[i].image = NULL;
        d->drive[i].write_protect = 0;
        d->drive[i].unloaded = 1;       /* no cartridge: heads unloaded */
        d->drive[i].function = 0;
        d->drive[i].cylinder = 0;
        d->drive[i].head = 0;
        d->drive[i].status = 0;
        d->drive[i].event.active = 0;
        drive_ref[i].dq = d;
        drive_ref[i].drive = i;
    }

    hp_card_install(&d->data_card, select_code);
    hp_card_install(&d->command_card, select_code + 1);
}

void dev_dq_attach(dev_dq_t *d, unsigned drive, hp_store_t *image,
                   int write_protect)
{
    dq_drive_t *dr = &d->drive[drive];

    dr->image = image;
    dr->write_protect = (uint8_t)(write_protect != 0);
    dr->unloaded = 0;                   /* loading the heads */
}

/* ---- Helpers --------------------------------------------------------------- */

/* An event is only scheduled if it is not already waiting (as in SIMH). */
static void activate(hp_event_t *ev, uint32_t time, void (*fn)(void *), void *arg)
{
    if (!hp_event_active(ev))
        hp_event_after(ev, time, fn, arg);
}

static void raise_data_flag(dev_dq_t *d)
{
    hp_card_set_flag(&d->data_card);
}

static void raise_command_flag(dev_dq_t *d)
{
    hp_card_set_flag(&d->command_card);
}

/* The word offset of a sector. */
static uint32_t disc_address(unsigned cyl, unsigned head, unsigned sector)
{
    return (((cyl * DQ_SURFACES) + head) * DQ_SECTORS + sector) *
           DQ_WORDS_PER_SECTOR;
}

/* ---- Data channel (first card) -------------------------------------------- */

static hp_word data_input(hp_card_t *card)
{
    dev_dq_t *d = card->ctx;

    return d->data_in;
}

static void data_output(hp_card_t *card, hp_word v)
{
    dev_dq_t *d = card->ctx;

    d->data_out = v;
    if (!d->busy || d->transfer)
        d->write_valid = 1;             /* not an overrun: the data is valid */
}

/* STC sets the command flip-flop; a word that arrives while the drive is busy
 * and no transfer is wanted is an overrun. */
static void data_set_control(hp_card_t *card)
{
    dev_dq_t *d = card->ctx;

    d->data_command = 1;
    if (d->busy && !d->transfer)
        d->drive[d->busy - 1].status |= DQ_STA_DATA_ERROR;
}

static void data_clear_control(hp_card_t *card)
{
    dev_dq_t *d = card->ctx;

    d->transfer = 0;
}

static void data_reset(hp_card_t *card)
{
    dev_dq_t *d = card->ctx;

    card->control = 0;
    d->data_command = 0;
    d->transfer = 0;
}

/* ---- Command channel (second card) ---------------------------------------- */

static void command_output(hp_card_t *card, hp_word v)
{
    dev_dq_t *d = card->ctx;

    d->command_out = v;
}

/* CLC and a control reset stop everything the drive and the data channel are doing. */
static void command_stop(dev_dq_t *d)
{
    d->command_card.control = 0;
    d->command_command = 0;
    if (d->busy)
        hp_event_cancel(&d->drive[d->busy - 1].event);
    hp_event_cancel(&d->data_event);
    d->transfer = 0;
    d->busy = 0;
}

static void command_clear_control(hp_card_t *card)
{
    command_stop(card->ctx);
}

static void command_reset(hp_card_t *card)
{
    command_stop(card->ctx);
}

static void command_set_control(hp_card_t *card)
{
    dev_dq_t *d = card->ctx;
    unsigned drv, fnc;

    if (d->command_command)
        return;
    d->command_command = 1;
    drv = d->command_out & DQ_DRIVE_MASK;
    fnc = (d->command_out >> DQ_CMD_SHIFT) & DQ_CMD_MASK;

    switch (fnc) {
    case DQ_FNC_SEEK:
    case DQ_FNC_RECALIBRATE:
    case DQ_FNC_CHECK:
        d->drive[drv].status = 0;       /* clear status */
        start_data_channel(d, fnc, drv, DQ_CHANNEL_TIME);
        break;
    case DQ_FNC_STATUS:
    case DQ_FNC_LOAD_ADDRESS:
        start_data_channel(d, fnc, drv, DQ_CHANNEL_TIME);
        break;
    case DQ_FNC_READ:
    case DQ_FNC_WRITE:
    case DQ_FNC_READ_ADDRESS:
    case DQ_FNC_WRITE_ADDRESS:
    case DQ_FNC_ADDRESS_SKIP:
        start_controller(d, fnc, drv, DQ_COMMAND_TIME);
        break;
    default:
        break;
    }
}

/* ---- Starting operations -------------------------------------------------- */

static void start_data_channel(dev_dq_t *d, unsigned function, unsigned drive,
                               uint32_t time)
{
    d->data_drive = (uint8_t)drive;
    d->data_function = (uint8_t)function;
    activate(&d->data_event, time, data_service, d);
}

static void start_controller(dev_dq_t *d, unsigned function, unsigned drive,
                             uint32_t time)
{
    dq_drive_t *dr = &d->drive[drive];
    uint32_t t = hp_event_remaining(&dr->event);

    if (t) {                            /* still seeking: wait for it */
        hp_event_cancel(&dr->event);
        time += t;
    }
    dr->status = 0;
    d->buffer_pos = 0;
    d->busy = (uint8_t)(drive + 1);
    d->transfer = 1;
    dr->function = (uint8_t)function;
    activate(&dr->event, time, drive_service, &drive_ref[drive]);
}

/* ---- Data channel service ------------------------------------------------- */

/* The data channel moves the address words and the status word, and a transfer
 * that is blocked by a seek in progress waits here. */
static void data_service(void *arg)
{
    dev_dq_t *d = arg;
    unsigned drv = d->data_drive;
    dq_drive_t *dr = &d->drive[drv];
    uint32_t st;

    switch (d->data_function) {

    case DQ_FNC_LOAD_ADDRESS:           /* need the cylinder */
    case DQ_FNC_SEEK:
        if (d->data_command) {
            d->rar_cyl = (uint8_t)(d->data_out & DQ_CYL_MASK);
            d->write_valid = 0;
            d->data_command = 0;
            raise_data_flag(d);
            d->data_function = (d->data_function == DQ_FNC_LOAD_ADDRESS)
                ? DQ_FNC_LOAD_ADDRESS1 : DQ_FNC_SEEK1;
        }
        activate(&d->data_event, DQ_WORD_TIME, data_service, d);
        break;

    case DQ_FNC_LOAD_ADDRESS1:          /* need head and sector */
    case DQ_FNC_SEEK1:
        if (d->data_command) {
            d->rar_head = (uint8_t)((d->data_out >> DQ_HEAD_SHIFT) & DQ_HEAD_MASK);
            d->rar_sector = (uint8_t)(d->data_out & DQ_SECTOR_MASK);
            d->write_valid = 0;
            d->data_command = 0;
            raise_data_flag(d);
            if (d->data_function == DQ_FNC_LOAD_ADDRESS1) {
                d->command_command = 0;
                raise_command_flag(d);
                break;                  /* done if Load Address */
            }
            if (hp_event_active(&dr->event))
                break;                  /* busy: seek check */
            st = (uint32_t)(d->rar_cyl > dr->cylinder ? d->rar_cyl - dr->cylinder
                                                      : dr->cylinder - d->rar_cyl)
                 * DQ_SEEK_PER_CYL;
            if (st == 0)
                st = DQ_WORD_TIME;      /* on the cylinder: minimum time */
            else
                dr->status |= DQ_STA_BUSY;
            dr->cylinder = d->rar_cyl;  /* transfer the RAR */
            dr->head = d->rar_head;
            activate(&dr->event, st, drive_service, &drive_ref[drv]);
            dr->function = DQ_FNC_SEEK2;
        } else {
            activate(&d->data_event, DQ_WORD_TIME, data_service, d);
        }
        break;

    case DQ_FNC_RECALIBRATE:
        d->rar_cyl = d->rar_head = d->rar_sector = 0;
        if (hp_event_active(&dr->event))
            break;                      /* ignored if busy */
        st = (uint32_t)dr->cylinder * DQ_SEEK_PER_CYL;
        if (st == 0)
            st = DQ_WORD_TIME;
        else
            dr->status |= DQ_STA_BUSY;
        activate(&dr->event, st, drive_service, &drive_ref[drv]);
        dr->cylinder = dr->head = 0;
        dr->function = DQ_FNC_SEEK2;
        break;

    case DQ_FNC_STATUS:                 /* read status */
        if (d->data_command) {
            if (!dr->unloaded)
                d->data_in = (hp_word)(dr->status & ~DQ_STA_DRIVE_ID);
            else
                d->data_in = DQ_STA_NOT_READY;
            if (d->data_in & DQ_STA_ANY_ERROR)
                d->data_in |= DQ_STA_ERROR;
            if (drv)
                d->data_in |= DQ_STA_DRIVE_ID;
            d->command_command = 0;
            d->data_command = 0;
            raise_data_flag(d);
            dr->status &= (hp_word)~DQ_STA_ANY_ERROR;
        } else {
            activate(&d->data_event, DQ_WORD_TIME, data_service, d);
        }
        break;

    case DQ_FNC_CHECK:                  /* check, need the count */
        if (d->data_command) {
            d->check_count = (int16_t)(d->data_out & DQ_CHECK_MASK);
            d->write_valid = 0;
            start_controller(d, DQ_FNC_CHECK1, drv, DQ_COMMAND_TIME);
        } else {
            activate(&d->data_event, DQ_WORD_TIME, data_service, d);
        }
        break;

    default:
        break;
    }
}

/* ---- Drive service -------------------------------------------------------- */

/* The drive moves the sector data a word at a time and finishes seeks. */
static void drive_service(void *arg)
{
    drive_ref_t *ref = arg;
    dev_dq_t *d = ref->dq;
    unsigned drv = ref->drive;
    dq_drive_t *dr = &d->drive[drv];
    uint8_t bytes[DQ_WORDS_PER_SECTOR * 2];
    uint32_t da;
    unsigned i;
    int err = 0;

    if (dr->unloaded) {                 /* drive down */
        d->command_command = 0;
        raise_command_flag(d);
        dr->status = 0;
        d->busy = 0;
        d->transfer = d->write_valid = 0;
        return;
    }

    switch (dr->function) {

    case DQ_FNC_SEEK2:                  /* seek done */
        if (dr->cylinder >= DQ_CYLINDERS) {
            dr->status |= DQ_STA_BUSY | DQ_STA_ERROR;   /* seek check */
            dr->cylinder = 0;
        } else {
            dr->status &= (hp_word)~DQ_STA_BUSY;
        }
        goto controller_wait;           /* then the same as the next state */
    case DQ_FNC_SEEK3:
    controller_wait:
        if (d->busy || d->command_card.flag) {          /* controller busy: wait */
            dr->function = DQ_FNC_SEEK3;
            activate(&dr->event, DQ_WORD_TIME, drive_service, ref);
        } else {
            d->command_command = 0;
            raise_command_flag(d);
        }
        return;

    case DQ_FNC_READ_ADDRESS:
        if (!d->data_command)
            break;                      /* data channel clear: done */
        if (d->buffer_pos == 0) {
            d->data_in = dr->cylinder;
        } else if (d->buffer_pos == 1) {
            d->data_in = (hp_word)((dr->head << DQ_HEAD_SHIFT) | d->rar_sector);
            d->rar_sector = (uint8_t)((d->rar_sector + 1) % DQ_SECTORS);
        } else {
            break;
        }
        d->buffer_pos++;
        d->data_command = 0;
        raise_data_flag(d);
        activate(&dr->event, DQ_WORD_TIME, drive_service, ref);
        return;

    case DQ_FNC_ADDRESS_SKIP:
    case DQ_FNC_READ:
    case DQ_FNC_CHECK1:
        if (d->buffer_pos == 0) {       /* new sector */
            if (!d->data_command && dr->function != DQ_FNC_CHECK1)
                break;
            if (d->rar_cyl != dr->cylinder || d->rar_head != dr->head ||
                d->rar_sector >= DQ_SECTORS) {
                dr->status |= DQ_STA_ADDRESS_ERROR;     /* no record found */
                break;
            }
            if (d->rar_head >= DQ_SURFACES) {
                dr->status |= DQ_STA_END_OF_CYL;
                break;
            }
            da = disc_address(d->rar_cyl, d->rar_head, d->rar_sector);
            d->rar_sector = (uint8_t)((d->rar_sector + 1) % DQ_SECTORS);
            if (d->rar_sector == 0)     /* wrap: next head */
                dr->head = d->rar_head = (uint8_t)(d->rar_head + 1);
            if (storage_read(dr->image, da * 2, bytes, sizeof bytes) != 0) {
                err = 1;
                break;
            }
            for (i = 0; i < DQ_WORDS_PER_SECTOR; i++)
                d->buffer[i] = (uint16_t)(bytes[2 * i] | (bytes[2 * i + 1] << 8));
        }
        d->data_in = d->buffer[d->buffer_pos++];
        if (d->buffer_pos >= DQ_WORDS_PER_SECTOR) {     /* end of sector */
            if (dr->function == DQ_FNC_CHECK1) {
                d->check_count = (int16_t)((d->check_count - 1) & DQ_CHECK_MASK);
                if (d->check_count == 0)
                    break;              /* done */
            }
            d->buffer_pos = 0;
        }
        if (d->data_command && d->transfer)
            raise_data_flag(d);
        d->data_command = 0;
        activate(&dr->event, DQ_WORD_TIME, drive_service, ref);
        return;

    case DQ_FNC_WRITE_ADDRESS:
    case DQ_FNC_WRITE:
        if (d->buffer_pos == 0) {       /* sector start */
            if (!d->data_command && !d->write_valid)
                break;                  /* transfer done */
            if (dr->write_protect) {
                dr->status |= DQ_STA_FLAGGED;
                break;
            }
            if (d->rar_cyl != dr->cylinder || d->rar_head != dr->head ||
                d->rar_sector >= DQ_SECTORS) {
                dr->status |= DQ_STA_ADDRESS_ERROR;
                break;
            }
            if (d->rar_head >= DQ_SURFACES) {
                dr->status |= DQ_STA_END_OF_CYL;
                break;
            }
        }
        d->buffer[d->buffer_pos++] = d->write_valid ? d->data_out : 0;
        d->write_valid = 0;
        if (d->buffer_pos >= DQ_WORDS_PER_SECTOR) {     /* buffer full */
            da = disc_address(d->rar_cyl, d->rar_head, d->rar_sector);
            d->rar_sector = (uint8_t)((d->rar_sector + 1) % DQ_SECTORS);
            if (d->rar_sector == 0)
                dr->head = d->rar_head = (uint8_t)(d->rar_head + 1);
            for (i = 0; i < DQ_WORDS_PER_SECTOR; i++) {
                bytes[2 * i] = (uint8_t)d->buffer[i];
                bytes[2 * i + 1] = (uint8_t)(d->buffer[i] >> 8);
            }
            if (storage_write(dr->image, da * 2, bytes, sizeof bytes) != 0) {
                err = 1;
                break;
            }
            d->buffer_pos = 0;
        }
        if (d->data_command && d->transfer)
            raise_data_flag(d);
        d->data_command = 0;
        activate(&dr->event, DQ_WORD_TIME, drive_service, ref);
        return;

    default:
        return;
    }

    /* the command is finished */
    d->command_command = 0;
    raise_command_flag(d);
    d->busy = 0;
    d->transfer = d->write_valid = 0;
    if (err)
        d->drive[drv].status |= DQ_STA_DATA_ERROR;  /* a host file error */
}
