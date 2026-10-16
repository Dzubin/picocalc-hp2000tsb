/* dev_tape.h - 13183B magnetic tape unit interface (HP 7970 drives).
 *
 * Author: Thomas Dzubin.
 *
 * Two cards: the data channel (select code 20 in the 2000E) and the command
 * channel (21). Up to four drives, each with a tape image mounted (tape_image.h).
 * TSB writes its SLEEP copy of the system to tape and reloads from it.
 */

#ifndef DEV_TAPE_H
#define DEV_TAPE_H

#include "hp21xx.h"
#include "storage.h"
#include "tape_image.h"
#include "dev_tape_const.h"

typedef struct {
    tape_image_t tape;
    hp_event_t   event;
    uint16_t     function;              /* command in progress */
    uint16_t     status;                /* drive status */
    uint8_t      offline;
} tape_drive_t;

typedef struct {
    hp_card_t    data_card, command_card;
    tape_drive_t drive[TAPE_DRIVES];
    uint16_t     status;                /* controller status */
    uint16_t     command_buffer;
    uint16_t     data_buffer;
    uint8_t      selected;              /* drive number */
    uint8_t      first_service;
    uint8_t      crc_ready;
    uint32_t     pointer, length;       /* into the record buffer */
    uint8_t      record[TAPE_RECORD_MAX];
} dev_tape_t;

void dev_tape_init(dev_tape_t *t, unsigned select_code);

/* Mount an image on a drive (the drive goes online) or take it off. */
void dev_tape_mount(dev_tape_t *t, unsigned drive, hp_store_t *image, int write_protect);
void dev_tape_unmount(dev_tape_t *t, unsigned drive);

/* The ON LINE button: after a rewind and offline command the drive stays offline
 * (TSB then asks to "CHANGE MAG TAPE TO AUTO") until the operator puts it back. */
int  dev_tape_set_online(dev_tape_t *t, unsigned drive);   /* 0 if a tape is mounted */
int  dev_tape_is_online(const dev_tape_t *t, unsigned drive);

/* Where the tape is (byte offset in the image) and how big the image file is, for
 * the progress display. Both are zero if no tape is mounted. */
uint32_t dev_tape_position(const dev_tape_t *t, unsigned drive, uint32_t *size);

#endif
