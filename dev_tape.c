/* dev_tape.c - 13183B magnetic tape unit interface with HP 7970 drives.
 *
 * Author: Thomas Dzubin.
 *
 * A port of hp2100_ms.c of the SIMH HP 2100 simulator (Robert M. Supnik
 * 2002-2016, J. David Bryan 2005-2022, MIT licence), reduced to the 13183B
 * controller at its fast timing, and rearranged around the CPU's standard card
 * flip-flops. The controller takes a command word on the command channel,
 * moves data one 16-bit word per data channel flag, and reports status; the
 * drive does the motion.
 */

#include "dev_tape.h"

static hp_word data_input(hp_card_t *card);
static void    data_output(hp_card_t *card, hp_word v);
static void    data_set_control(hp_card_t *card);
static void    data_edt(hp_card_t *card);
static void    data_reset(hp_card_t *card);
static hp_word command_input(hp_card_t *card);
static void    command_output(hp_card_t *card, hp_word v);
static void    command_set_control(hp_card_t *card);
static void    command_reset(hp_card_t *card);

static void    drive_service(void *arg);
static void    controller_clear(dev_tape_t *t);
static void    map_error(dev_tape_t *t, tape_status_t st);
static int     recoverable(tape_status_t st);
static uint32_t crc_lrc(const uint8_t *buffer, uint32_t length);
static void    finish_command(dev_tape_t *t);

typedef struct {
    dev_tape_t *tape;
    unsigned    drive;
} drive_ref_t;

static drive_ref_t drive_ref[TAPE_DRIVES];

static const uint8_t select_map[16] = { 0, 0, 1, 1, 2, 2, 2, 2,
                                        3, 3, 3, 3, 3, 3, 3, 3 };

void dev_tape_init(dev_tape_t *t, unsigned select_code)
{
    unsigned i;

    t->data_card.ctx = t;
    t->data_card.input = data_input;
    t->data_card.output = data_output;
    t->data_card.set_control = data_set_control;
    t->data_card.clear_control = NULL;
    t->data_card.reset = data_reset;
    t->data_card.flag_set = NULL;
    t->data_card.flag_clear = NULL;
    t->data_card.edt = data_edt;
    t->data_card.flag = 0;
    t->data_card.flag_buffer = 1;
    t->data_card.control = 0;

    t->command_card.ctx = t;
    t->command_card.input = command_input;
    t->command_card.output = command_output;
    t->command_card.set_control = command_set_control;
    t->command_card.clear_control = NULL;
    t->command_card.reset = command_reset;
    t->command_card.flag_set = NULL;
    t->command_card.flag_clear = NULL;
    t->command_card.edt = NULL;
    t->command_card.flag = 0;
    t->command_card.flag_buffer = 1;
    t->command_card.control = 0;

    t->status = 0;
    t->command_buffer = t->data_buffer = 0;
    t->selected = 0;
    t->first_service = 0;
    t->crc_ready = 0;
    t->pointer = t->length = 0;
    for (i = 0; i < TAPE_DRIVES; i++) {
        t->drive[i].tape.store = NULL;
        t->drive[i].tape.position = 0;
        t->drive[i].tape.write_protect = 0;
        t->drive[i].tape.position_not_updated = 0;
        t->drive[i].event.active = 0;
        t->drive[i].function = 0;
        t->drive[i].status = 0;
        t->drive[i].offline = 1;
        drive_ref[i].tape = t;
        drive_ref[i].drive = i;
    }
    hp_card_install(&t->data_card, select_code);
    hp_card_install(&t->command_card, select_code + 1);
}

void dev_tape_mount(dev_tape_t *t, unsigned drive, hp_store_t *image, int write_protect)
{
    tape_mount(&t->drive[drive].tape, image, write_protect);
    t->drive[drive].offline = 0;
    t->drive[drive].status = 0;
}

void dev_tape_unmount(dev_tape_t *t, unsigned drive)
{
    tape_unmount(&t->drive[drive].tape);
    t->drive[drive].offline = 1;
    t->drive[drive].status = 0;
}

int dev_tape_set_online(dev_tape_t *t, unsigned drive)
{
    if (drive >= TAPE_DRIVES || t->drive[drive].tape.store == NULL)
        return -1;
    t->drive[drive].offline = 0;
    return 0;
}

int dev_tape_is_online(const dev_tape_t *t, unsigned drive)
{
    return drive < TAPE_DRIVES && !t->drive[drive].offline;
}

uint32_t dev_tape_position(const dev_tape_t *t, unsigned drive, uint32_t *size)
{
    *size = 0;
    if (drive >= TAPE_DRIVES || t->drive[drive].tape.store == NULL)
        return 0;
    *size = storage_size(t->drive[drive].tape.store);
    return t->drive[drive].tape.position;
}

/* ---- Data channel --------------------------------------------------------- */

static uint32_t parity9(unsigned c)
{
    c &= 0377;
    c ^= c >> 4;
    c ^= c >> 2;
    c ^= c >> 1;
    return (c & 1) ? 0 : TAPE_PARITY_EVEN;
}

/* The CRC and LRC characters, as the interface generates them after a record. */
static uint32_t crc_lrc(const uint8_t *buffer, uint32_t length)
{
    uint32_t i, byte, crc = 0, lrc = 0;

    for (i = 0; i < length; i++) {
        byte = parity9(buffer[i]) | buffer[i];
        crc ^= byte;
        lrc ^= byte;
        if (crc & 1)
            crc = (crc >> 1) ^ TAPE_CRC_POLY;
        else
            crc >>= 1;
    }
    crc ^= TAPE_CRC_FINAL;
    lrc ^= crc;
    return (crc << 16) | lrc;
}

static hp_word data_input(hp_card_t *card)
{
    dev_tape_t *t = card->ctx;

    if (t->crc_ready) {
        uint32_t check = crc_lrc(t->record, t->length);

        t->data_buffer = (hp_word)(((check >> 16) & 0xFF) << 8 | (check & 0xFF));
    }
    return t->data_buffer;
}

static void data_output(hp_card_t *card, hp_word v)
{
    ((dev_tape_t *)card->ctx)->data_buffer = v;
}

static void data_set_control(hp_card_t *card)
{
    ((dev_tape_t *)card->ctx)->crc_ready = 0;
}

static void data_reset(hp_card_t *card)
{
    card->flag_buffer = 1;
}

/* End of a DMA data transfer clears the flag. */
static void data_edt(hp_card_t *card)
{
    card->flag_buffer = 0;
    card->flag = 0;
}

/* ---- Command channel ------------------------------------------------------ */

static hp_word command_input(hp_card_t *card)
{
    dev_tape_t *t = card->ctx;
    tape_drive_t *d = &t->drive[t->selected];
    hp_word v = (hp_word)(t->status & ~TSTA_DYNAMIC);

    if (!d->offline) {
        v |= d->status;
        if (tape_at_bot(&d->tape))
            v |= TSTA_BOT;
        if (hp_event_active(&d->event) &&
            !((d->function & TFLAG_REWIND) && tape_at_bot(&d->tape)))
            v |= TSTA_TRANSPORT_BUSY;
        if (d->tape.write_protect)
            v |= TSTA_WRITE_LOCKED;
    } else {
        v |= TSTA_TRANSPORT_BUSY | TSTA_LOCAL;
    }
    v |= TSTA_PE | (hp_word)(t->selected << TSTA_SELECT_SHIFT);
    return v;
}

/* The command word is checked when it is written; a bad one is rejected. */
static void command_output(hp_card_t *card, hp_word v)
{
    dev_tape_t *t = card->ctx;
    tape_drive_t *d = &t->drive[t->selected];

    t->command_buffer = v;
    t->status &= (uint16_t)~TSTA_REJECT;
    if ((v & 0377) == TCMD_CLEAR)
        return;                                 /* clear is always accepted */
    if (t->status & TSTA_BUSY) {
        t->status |= TSTA_REJECT;               /* busy: reject, no select change */
        return;
    }
    if (v & TFLAG_CHANGE_SELECT) {
        t->selected = select_map[(v >> TCMD_SELECT_SHIFT) & TCMD_SELECT_MASK];
        d = &t->drive[t->selected];
    }
    if (((v & TFLAG_MOTION) && hp_event_active(&d->event)) ||
        ((v & TFLAG_REVERSE) && tape_at_bot(&d->tape)) ||
        ((v & TFLAG_WRITE) && d->tape.write_protect))
        t->status |= TSTA_REJECT;
}

static void command_reset(hp_card_t *card)
{
    card->flag_buffer = 1;
}

/* STC starts the command, unless it was rejected. */
static void command_set_control(hp_card_t *card)
{
    dev_tape_t *t = card->ctx;
    tape_drive_t *d = &t->drive[t->selected];
    uint32_t sched;

    if (t->status & TSTA_REJECT)
        return;
    if ((t->command_buffer & 0377) == TCMD_CLEAR) {
        controller_clear(t);
        card->control = 1;
        hp.iog_clf = 0;                         /* the clear completes at once */
        hp_card_set_flag(card);
        return;
    }
    d->function = t->command_buffer & 0377;
    if (d->function & TFLAG_REWIND) {
        if (!tape_at_bot(&d->tape))
            d->status = TSTA_REWINDING;
        sched = TTIME_REWIND_START;
    } else {
        if (tape_at_bot(&d->tape))
            sched = TTIME_BOT_START;
        else if (d->function == TCMD_GAP || d->function == TCMD_GAP_MARK)
            sched = TTIME_GAP;
        else
            sched = 0;
        if (d->function != TCMD_GAP)
            sched += TTIME_COMMAND;
    }
    if (t->command_buffer & ~TCMD_SELECT_BITS)
        hp_event_after(&d->event, sched, drive_service, &drive_ref[t->selected]);
    t->status = TSTA_BUSY;
    t->first_service = 1;
    card->control = 1;
}

/* A controller clear stops everything but a rewind in progress. A write in
 * progress has its partial record written, marked as in error. */
static void controller_clear(dev_tape_t *t)
{
    unsigned i;

    for (i = 0; i < TAPE_DRIVES; i++) {
        tape_drive_t *d = &t->drive[i];

        if (hp_event_active(&d->event) && d->function == TCMD_WRITE && t->pointer > 0) {
            tape_write_bad_record(&d->tape, t->record, (uint32_t)t->pointer);
            t->pointer = 0;
        }
        if (!(d->status & TSTA_REWINDING))
            hp_event_cancel(&d->event);
    }
    t->status = 0;
    t->first_service = 0;
}

/* ---- The drive ------------------------------------------------------------ */

/* Turn a tape library result into controller status. */
static void map_error(dev_tape_t *t, tape_status_t st)
{
    switch (st) {
    case TAPE_EOM:
    case TAPE_MARK:
        t->status |= TSTA_EOF;
        break;
    case TAPE_TOO_LONG:
    case TAPE_IO_ERROR:
    case TAPE_RECORD_ERROR:
        t->status |= TSTA_PARITY;
        break;
    case TAPE_WRITE_PROTECTED:
    case TAPE_NOT_MOUNTED:
        t->status |= TSTA_REJECT;
        break;
    default:
        break;
    }
}

/* End of medium and tape marks leave the command to complete normally. */
static int recoverable(tape_status_t st)
{
    return st == TAPE_EOM || st == TAPE_MARK || st == TAPE_RECORD_ERROR ||
           st == TAPE_BOT;
}

static void finish_command(dev_tape_t *t)
{
    t->status &= (uint16_t)~TSTA_BUSY;
    hp_card_set_flag(&t->command_card);
}

static void drive_service(void *arg)
{
    drive_ref_t *ref = arg;
    dev_tape_t *t = ref->tape;
    tape_drive_t *d = &t->drive[ref->drive];
    tape_status_t st;
    uint32_t length;

    if (d->function != TCMD_REWIND_OFFLINE && d->offline) {
        t->status = (uint16_t)((t->status | TSTA_REJECT) & ~TSTA_BUSY);
        hp_card_set_flag(&t->command_card);
        return;
    }

    switch (d->function) {

    case TCMD_REWIND_OFFLINE:
        tape_rewind(&d->tape);
        d->offline = 1;
        d->status = 0;
        break;

    case TCMD_REWIND:
        if (d->status & TSTA_REWINDING) {
            d->function |= TCMD_COMPLETION;
            hp_event_after(&d->event, TTIME_COMMAND, drive_service, ref);
        }
        break;

    case TCMD_REWIND | TCMD_COMPLETION:
        tape_rewind(&d->tape);
        d->status = 0;
        return;                                 /* the drive is free */

    case TCMD_GAP_MARK:                         /* fast timing: just the mark */
    case TCMD_WRITE_MARK:
        st = tape_write_mark(&d->tape);
        if (st != TAPE_OK)
            map_error(t, st);
        t->status = TSTA_EOF;
        break;

    case TCMD_GAP:
        st = tape_write_gap(&d->tape, TAPE_GAP_BYTES);
        if (st != TAPE_OK)
            map_error(t, st);
        break;

    case TCMD_FORWARD_RECORD:
        st = tape_space_forward(&d->tape, &length);
        if (st != TAPE_OK)
            map_error(t, st);
        if (length & 1)
            t->status |= TSTA_ODD;
        else
            t->status &= (uint16_t)~TSTA_ODD;
        break;

    case TCMD_BACKWARD_RECORD:
        st = tape_space_reverse(&d->tape, &length);
        if (st != TAPE_OK)
            map_error(t, st);
        if (length & 1)
            t->status |= TSTA_ODD;
        else
            t->status &= (uint16_t)~TSTA_ODD;
        break;

    case TCMD_FORWARD_FILE:
        while ((st = tape_space_forward(&d->tape, &length)) == TAPE_OK)
            ;
        map_error(t, st);
        break;

    case TCMD_BACKWARD_FILE:
        while ((st = tape_space_reverse(&d->tape, &length)) == TAPE_OK)
            ;
        map_error(t, st);
        break;

    case TCMD_READ_FILE_FWD:                    /* diagnostic read */
    case TCMD_READ:
        if (t->first_service) {
            t->first_service = 0;
            t->pointer = t->length = 0;
            st = tape_read_forward(&d->tape, t->record, &length, TAPE_RECORD_MAX);
            t->length = length;
            if (st == TAPE_RECORD_ERROR) {
                t->status |= TSTA_PARITY;
            } else if (st != TAPE_OK) {
                map_error(t, st);
                if (recoverable(st)) {          /* wait out the gap, then finish */
                    hp_event_after(&d->event, TTIME_INTERRECORD, drive_service, ref);
                    d->function |= TCMD_COMPLETION;
                    return;
                }
                break;
            }
            t->status |= TSTA_ODD;              /* the 13183 sets this on a read */
        }
        if (t->data_card.control && t->pointer < t->length) {
            if (t->data_card.flag)
                t->status |= TSTA_TIMING | TSTA_PARITY;     /* data overrun */
            t->data_buffer = (hp_word)((t->record[t->pointer] << 8) |
                             (t->pointer + 1 == t->length ? 0 : t->record[t->pointer + 1]));
            t->pointer += 2;
            hp_card_set_flag(&t->data_card);
            hp_event_after(&d->event, TTIME_WORD, drive_service, ref);
            return;
        }
        if (t->length & 1)
            t->status |= TSTA_ODD;
        else
            t->status &= (uint16_t)~TSTA_ODD;
        hp_event_after(&d->event, TTIME_INTERRECORD, drive_service, ref);
        if (d->function == TCMD_READ_FILE_FWD) {
            t->first_service = 1;               /* diagnostic: restart */
        } else {
            d->function |= TCMD_COMPLETION;
            t->crc_ready = 1;
        }
        return;

    case TCMD_READ_FILE_FWD | TCMD_COMPLETION:
    case TCMD_READ | TCMD_COMPLETION:
        break;

    case TCMD_WRITE:
        if (t->first_service) {                 /* no data moves on the first service */
            t->first_service = 0;
            t->pointer = 0;
        } else if (t->pointer + 2 <= TAPE_RECORD_MAX) {
            t->record[t->pointer] = (uint8_t)(t->data_buffer >> 8);
            t->record[t->pointer + 1] = (uint8_t)(t->data_buffer & 0377);
            t->pointer += 2;
        } else {
            t->status |= TSTA_PARITY;
        }
        if (t->data_card.control) {             /* ask for the next word */
            hp_card_set_flag(&t->data_card);
            hp_event_after(&d->event, TTIME_WORD, drive_service, ref);
            return;
        }
        if (t->pointer) {
            st = tape_write_record(&d->tape, t->record, (uint32_t)t->pointer);
            if (st != TAPE_OK) {
                map_error(t, st);
                break;
            }
        }
        hp_event_after(&d->event, TTIME_INTERRECORD, drive_service, ref);
        d->function |= TCMD_COMPLETION;
        t->length = t->pointer;
        t->crc_ready = 1;
        return;

    case TCMD_WRITE | TCMD_COMPLETION:
        break;

    default:                                    /* unknown or unsupported: no operation */
        break;
    }
    finish_command(t);
}
