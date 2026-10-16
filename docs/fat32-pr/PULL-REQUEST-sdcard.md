# sdcard: wait for a busy card by time, and before every command (a card that is still programming makes the next command fail)

To: Blair Leduc, `picocalc-text-starter`, `drivers/sdcard.c`
From: Thomas Dzubin

A second report, separate from the `fat32.c` one. It is about how `sdcard.c` waits for
the card to finish programming a block.

## What I saw on a real PicoCalc

Copying a 24 MB file to a freshly formatted 32 GB card (16 KB clusters) through the
driver, with every sector transfer retried up to five times by a wrapper of my own that
counts the failed tries:

| SPI clock | failed tries during one 28 MB save | notes |
|---|---|---|
| 12 MHz | 161 and about 230 (two saves) | a run of five failed reads in a row aborted a save; the file system then reported "directory not found" for a folder that was there |
| 6 MHz | 86 | the last failure shown was a write (`SD_ERROR_WRITE_FAILED`) |
| 6 MHz, 256 KB test write | 0 (three runs) | too short to show it |

A slower clock helped but did not cure it, and the failures come in bursts during long
runs of writes. Before I reformatted it, the same card at the driver's own 25 MHz gave
about eight times the failures of 12 MHz (46 against 6 in one 4 MB write).

## Cause

After the data of a write has been accepted, the card is busy while it programs the
block, and in SPI mode it signals that by holding MISO low. Two things in `sdcard.c`
let a long program time go wrong:

1. `sd_wait_ready()` waits by counting. It reads at most 10000 bytes
   (`uint32_t timeout = 10000`), so the time it waits depends on the SPI clock: at 25 MHz
   it is shorter than at 6 MHz. A write that takes longer than that, which happens when a
   card does internal housekeeping (the SD specification allows a write to take up to
   250 ms), times out.
2. The result of `sd_wait_ready()` is ignored in `sd_write_block()`, and no command
   waits for the card before it is sent. So the next command, a read or another write,
   goes to a card that is still busy and ignores it. A busy card's low data line reads as
   `0x00`, which `sd_send_command()` takes for an R1 of "no error". The read then times
   out waiting for its data token, or the write is refused at the data-response check
   (`!= 0x05`), and the caller sees `SD_ERROR_READ_FAILED` or `SD_ERROR_WRITE_FAILED` for a
   sector that was fine.

This also explains why lowering the clock helped (the count of 10000 bytes covers more
real time) without fixing it.

## Fix (`sdcard-busy-wait.patch`)

- `sd_wait_ready()` waits for up to 500 ms by `time_us_64()`, not by counting bytes.
- `sd_send_command()` calls it first for every command except CMD0, so a busy card is
  never sent a command.
- `sd_write_block()` returns `SD_ERROR_WRITE_FAILED` if the card is still busy after
  the wait, instead of ignoring it.

## A test that does not need a PicoCalc: `sd_busy_probe.c`

The file contains a small model of a card in SPI mode, with a simulated clock. After each
write the card holds MISO low for a chosen time, and it ignores everything clocked in
meanwhile. `sdcard.c` runs unchanged on top of it, with the Pico SDK calls replaced by
the functions in the same file (`stubs_sd/` has the two headers). The program writes 100
blocks and reads them back, for several busy times and clocks.

```
gcc -O1 -std=gnu99 -Istubs_sd -I<starter>/drivers sd_busy_probe.c <starter>/drivers/sdcard.c -o sd_busy_probe
./sd_busy_probe
```

Two things in it are assumptions and not measurements: how long a real card is busy, and
what one `spi_write_read_blocking()` call costs besides the bits on the wire (set to 2
microseconds, `SPI_CALL_US`). What it shows is the mechanism. Unmodified `sdcard.c`:

| busy time after each write | 25 MHz | 12 MHz | 6 MHz |
|---|---|---|---|
| 1, 10, 20 ms | no failures | no failures | no failures |
| 30 ms | 98 writes and 100 reads fail | 98 writes and 100 reads fail | no failures |
| 40 ms | 99 writes and 100 reads fail | 99 writes and 100 reads fail | 97 writes fail, 97 blocks wrong |
| 100 and 300 ms | 99 writes fail, 99 blocks wrong | 99 writes fail, 99 blocks wrong | 99 writes fail, 99 blocks wrong |

The threshold depends on the clock, as it did on the real card, because the driver waits
by counting. With `sdcard-busy-wait.patch` applied, every row and column shows no failed
writes, no failed reads and no wrong blocks.

## What I tested and what I did not

- Tested: the model above, with GCC 16.2 on Windows, original and patched `sdcard.c`.
- Tested on a real PicoCalc and card: a workaround in my own code, outside the driver. It
  waits for MISO to go high (up to 0.5 s, by the clock) before every sector transfer and
  after every write, which is what the patch does inside the driver. At 6 MHz, the same
  28 MB save that had 86 failed tries before needed none (the counter stayed at 0), and
  two short write tests also showed none. The save took 5 minutes, because of the slow clock.
- Not tested: the patch itself on a real PicoCalc, and the faster clocks with the wait in
  place (I have not yet tried 12 or 25 MHz with it).
- I did not look at the init path other than skipping the wait for CMD0, and I did not
  look at multi-block commands, which the driver does not use.

Files: `sd_busy_probe.c`, `stubs_sd/`, `sdcard-busy-wait.patch`.

Thomas Dzubin
