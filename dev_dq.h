/* dev_dq.h - 12565A disc interface with HP 2883 drives.
 *
 * Author: Thomas Dzubin.
 *
 * Two cards: the data channel (select code 14 in the Access and 2000F systems)
 * and the command channel (the next select code). Two drives of about 24 MB, each
 * backed by an image file of 16-bit words, low byte first (the SIMH format).
 */

#ifndef DEV_DQ_H
#define DEV_DQ_H

#include "hp21xx.h"
#include "storage.h"
#include "dev_dq_const.h"

typedef struct {
    hp_store_t *image;                  /* NULL if no cartridge */
    uint8_t     write_protect;
    uint8_t     unloaded;               /* heads unloaded */
    uint8_t     function;               /* the drive's command in progress */
    uint8_t     cylinder;               /* where the drive's heads are */
    uint8_t     head;
    uint16_t    status;
    hp_event_t  event;
} dq_drive_t;

typedef struct {
    hp_card_t    data_card;             /* data channel */
    hp_card_t    command_card;          /* command channel */
    hp_event_t   data_event;
    dq_drive_t   drive[DQ_DRIVES];

    uint8_t      data_command;          /* data channel command flip-flop */
    uint8_t      command_command;       /* command channel command flip-flop */
    uint8_t      data_function;         /* data channel function in progress */
    uint8_t      data_drive;
    uint8_t      busy;                  /* drive number + 1 while a command runs */
    uint8_t      transfer;              /* data transfer in progress */
    uint8_t      write_valid;           /* write data present */
    uint8_t      rar_cyl, rar_head, rar_sector;
    uint16_t     data_in, data_out, command_out;
    int16_t      check_count;
    uint16_t     buffer_pos;
    uint16_t     buffer[DQ_WORDS_PER_SECTOR];
} dev_dq_t;

/* Install the data channel at select_code and the command channel at the next. */
void dev_dq_init(dev_dq_t *d, unsigned select_code);
void dev_dq_attach(dev_dq_t *d, unsigned drive, hp_store_t *image,
                   int write_protect);

#endif
