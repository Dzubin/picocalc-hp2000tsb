/* storage_pico_const.h - constants for the SD card storage (storage_pico.c). */

#ifndef STORAGE_PICO_CONST_H
#define STORAGE_PICO_CONST_H

#define STORAGE_PICO_HANDLES    8u      /* files open at once (disc, tape, loader tapes) */

#define STORAGE_PICO_MOUNT_WAIT_MS  4000u  /* how long to wait for the card */

#define STORAGE_PICO_SPI_HZ  6000000u   /* SPI clock for the card (the driver asks for 25 MHz); 12 MHz gave hundreds of failed tries */

#define STORAGE_PICO_BUSY_US  500000u   /* longest wait for a busy card (the SD limit for a write is 250 ms) */

#define STORAGE_PICO_TRIES      5u      /* tries for one SD sector transfer */
#define STORAGE_PICO_RETRY_MS   5u      /* pause after a failed try */
#define STORAGE_PICO_REINIT_TRY 2u      /* restart the card after this failed try */

#define STORAGE_PICO_GROW_BYTES 65536u  /* a file that must grow is extended by this much at once */
#define STORAGE_PICO_ZERO_BLOCK 8192u   /* ... in writes of this size (zero filled) */

#define STORAGE_PICO_NO_SECTOR  0xFFFFFFFFu /* "no sector held in the buffer" */
#define STORAGE_PICO_FAT_MASK   0x0FFFFFFFu /* a FAT32 entry uses 28 bits */

#endif
