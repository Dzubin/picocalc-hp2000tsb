# Draft: fat32.c walks the whole cluster chain on every read and write

*Draft for Thomas Dzubin to review and post himself. Nothing has been sent.
The proposed change below is a sketch and has NOT been compiled or tested;
what was tested is the workaround described under "How it was found".*

Target: `drivers/fat32.c` in picocalc-text-starter (Blair Leduc).

## Summary

`fat32_read()` and `fat32_write()` find the cluster for the current file position
by following the FAT chain from the file's first cluster on every call. Each link
costs one SD sector read, and nothing is cached. The cost of one call therefore
grows with how far into the file the position is, and `fat32_write()` does the walk
three times. Programs that make many small reads or writes into a large file
(a disc image, a database, a log read back by record) become very slow.

## Where

- `fat32_read()`: `seek_to_cluster(file->start_cluster, position / bytes_per_cluster, ...)`
  starts at the first cluster each time. `file->current_cluster` is kept up to
  date but is not used as a starting point.
- `fat32_write()`:
  1. a loop from `start_cluster` up to the position's cluster (it also allocates),
  2. `find_last_cluster()` over the whole existing chain,
  3. `seek_to_cluster()` from `start_cluster` again.
- `read_cluster_fat_entry()` reads the FAT sector from the card for every link,
  although 128 consecutive links share one FAT sector.

For a file of N clusters and a position in cluster k, one write costs about
`2k + N` card reads before it transfers any data, plus the data sector read and
write and the directory entry update.

## How it was found

A 4.9 MB disc image (256 byte sectors read and written at random) on a PicoCalc.
Booting was tolerable. A reload that performs thousands of sector writes appeared
frozen. The same code ran in seconds on a PC.

Workaround that fixed it (in the application, not the driver): read the file's
cluster chain once when it is opened, keep it in RAM, and read and write sectors
directly. With that change the reload completed and normal use was much faster.
(The earlier fix to `get_next_free_cluster()` was the same kind of symptom, a
sequential write that looked hung, with a different cause.)

## Proposed change (sketch, untested)

1. Remember which cluster index `file->current_cluster` is. Add a field to
   `fat32_file_t`, for example `uint32_t current_index`.
2. Give `seek_to_cluster()` the file, and start from `current_cluster` when the
   wanted index is at or after `current_index`; start from `start_cluster` only
   when seeking backwards. Sequential access then costs one link per cluster
   boundary instead of a walk from the start.
3. In `fat32_write()`, do the walk once (step 1 above or step 3, not both), and
   find the last cluster only when the write extends the file (that is, when
   the end position is past the existing size), not on every call.
4. Keep the last FAT sector read in a small cache inside `read_cluster_fat_entry()`
   and `write_cluster_fat_entry()` (invalidate it on any FAT write to another
   sector and on unmount), so a chain walk reads each FAT sector once per 128
   links.

Steps 2 and 4 alone would remove most of the cost for sequential access. Random
access into a large file still needs the walk from the start (or an application
level chain map, as used in the workaround).

## Suggested test

Create a file of several MB, then time 1000 reads of 256 bytes at increasing
offsets and 1000 writes at random offsets inside the file, before and after the
change. The results of each read and write should be the same; only the time
should change.

## Notes for the author

- Behaviour and the public API do not need to change; the extra field in
  `fat32_file_t` is the only visible difference.
- The change should be checked on a card with small clusters (4 KB), where chains
  are longest.
