/* dev_disc.h - 13210A disc drive interface with HP 7900 drives.
 *
 * Author: Thomas Dzubin.
 *
 * Two cards: the data channel (select code 11 in the 2000E) and the command
 * channel (select code 12, the next one up). Up to four drives, each backed by
 * an image file of 16-bit words, low byte first (the SIMH format).
 */

#ifndef DEV_DISC_H
#define DEV_DISC_H

#include "hp21xx.h"
#include "storage.h"
#include "dev_disc_const.h"

typedef struct {
    hp_store_t *image;                  /* NULL if no cartridge */
    uint8_t     write_protect;
    uint8_t     unloaded;               /* heads unloaded */
    uint8_t     function;               /* the drive's command in progress */
    uint8_t     cylinder;               /* where the drive's heads are */
    uint16_t    status;
    hp_event_t  event;
} disc_drive_t;

typedef struct {
    hp_card_t    data_card;             /* data channel */
    hp_card_t    command_card;          /* command channel */
    hp_event_t   data_event;
    disc_drive_t drive[DISC_DRIVES];

    uint8_t      data_command;          /* data channel command flip-flop */
    uint8_t      command_command;       /* command channel command flip-flop */
    uint8_t      data_function;         /* data channel function in progress */
    uint8_t      data_drive;
    uint8_t      transfer;              /* data transfer in progress */
    uint8_t      write_valid;           /* write data present */
    uint8_t      poll;                  /* seek-complete polling enabled */
    uint8_t      end_of_cylinder;
    uint8_t      busy;                  /* drive number + 1 while a command runs */
    uint8_t      rar_cyl, rar_head, rar_sector;
    uint16_t     data_in, data_out, command_out;
    int16_t      check_count;
    uint16_t     buffer_pos;
    uint16_t     buffer[DISC_WORDS_PER_SECTOR];
    uint32_t     io_errors;
} dev_disc_t;

/* Install the data channel at select_code and the command channel at the next. */
void dev_disc_init(dev_disc_t *d, unsigned select_code);
void dev_disc_attach(dev_disc_t *d, unsigned drive, hp_store_t *image,
                     int write_protect);

#endif
