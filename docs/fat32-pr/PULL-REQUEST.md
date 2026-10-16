# fat32: stop reading deleted long-name entries as live ones (out-of-bounds write), and compare the deleted-entry marker as `uint8_t`

To: Blair Leduc, `picocalc-text-starter`, `drivers/fat32.c`
From: Thomas Dzubin

Thank you for the starter, it has been a great base. While writing a program that
deletes and re-creates files on the SD card I ran into two problems in `fat32.c` and
one performance cost. I wrote a small test that needs nothing but `fat32.c` and a
RAM-based fake SD card, so you can see each one on a PC in a few seconds.
`fat32-fixes.patch` fixes the first two; the third is a report with numbers.

## 1. Deleted long-name entries are processed as live ones (out-of-bounds write, also on the Pico)

When a file with a long name is deleted, `unlink_entry()` marks every long-name entry
and the 8.3 entry with `0xE5`. `fat32_dir_read()` then tests the entries in this order:

1. end marker `0x00`
2. `entry->attr == FAT32_ATTR_LONG_NAME` (long-name part)
3. `entry->shortname[0] != FAT32_DIR_ENTRY_FREE` (a normal 8.3 entry)

A *deleted* long-name part still has `attr == 0x0F`, so it is taken by branch 2 as if
it were live. Its `seq` byte is now `0xE5`, so:

```c
int offset = ((lfn_entry->seq & 0x3F) - 1) * FAT32_DIR_LFN_PART_SIZE;   // (0x25 - 1) * 13 = 468
lfn_to_str(lfn_entry, filename + offset);                                // filename is char[256]
```

writes 13 bytes at `filename + 468` of a 256-byte stack buffer, 212 bytes past its end,
every time a directory that contains a deleted long-named file is read. That is every
`fat32_open()`, `fat32_create()`, `fat32_delete()` and `fat32_rename()` that has to
search such a directory, because they all go through `find_entry()` and
`fat32_dir_read()`. `(seq & 0x40)` is also true for `0xE5`, so the deleted part resets
the name being collected and replaces the saved checksum, which is wrong as well.

This happens with an unsigned `char` too (the Pico toolchain), so it is not only a PC
problem. I have seen odd behaviour on a real card after programs that delete and
re-create long-named files, including `FAT32_ERROR_DIR_NOT_FOUND` for a directory that
exists. I cannot prove from the card alone that this overflow is the cause, because what
it overwrites depends on the stack layout of the build, but writing 13 bytes into the
caller's frames is certainly capable of it.

**Fix** (in `fat32-fixes.patch`): handle a deleted entry first, whatever its kind, and
forget any partly collected long name; and refuse an `offset` that does not fit the buffer.

## 2. `shortname[0]` is a plain `char`, so `== 0xE5` is never true when `char` is signed

`fat32_dir_entry_t.shortname` is `char[11]`. On the Pico (GCC for ARM) `char` is
unsigned, so `shortname[0] == FAT32_DIR_ENTRY_FREE` works. On x86 and most desktop
targets `char` is signed, `0xE5` becomes `-27`, and the comparison with the int `229` is
never true. On a PC build the driver then:

- lists deleted entries as files (with a garbage first character, for example
  `?ong-fil.bin`), because `fat32_dir_read()` thinks they are not free;
- never reuses a deleted directory slot in `link_entry()`, so the directory only grows;
- gets confused after a delete followed by a create with the same name.

I mention it because anyone who builds the driver for a host test, as I did, sees
failures that do not exist on the device, and I lost some time believing they did.

**Fix:** compare `(uint8_t)entry->shortname[0]` in `fat32_dir_read()` and `link_entry()`.
Building with `-funsigned-char` also hides it, which is a quick way to see the device
behaviour on a PC.

## 3. Performance: every `fat32_write()` walks the cluster chain from the start, three times (report only)

`fat32_write()` follows the file's cluster chain from `start_cluster` to reach the write
position, then again in `find_last_cluster()`, then a third time in
`seek_to_cluster()`, and every step reads a FAT sector from the card. The cost of one
call therefore grows with the position in the file. On my RAM card with 4 KB clusters,
one 4 KB `fat32_write()` costs:

| position in the file | sector reads for that one call |
|---|---|
| 64 KB | 59 |
| 1 MB | 779 |

On a real card this is slow for any large file. Growing a 24 MB file through about 2900
driver calls cost 4.8 million sector reads in my simulator (4 KB clusters) and took
84 minutes on the PicoCalc; the same data copied after the clusters were allocated up
front needs only the 46 000 reads the data itself requires and took about 2 minutes.
`fat32_read()` walks the chain once per call (`seek_to_cluster()` from `start_cluster`),
so it has the same growth with position, at a third of the cost.

I did not include a patch for this one, since the right change (keep `current_cluster`
in `fat32_file_t` and continue from it when the position is sequential, and remember the
last cluster) touches the data structure and I would rather you chose the design. In my
own program I worked around it by allocating all clusters in one pass before copying.

## Reproducing: `fat32_probe.c`

`fat32_probe.c` is independent of any application. It formats a RAM block as a FAT32
volume (no partition table, 4 KB clusters), implements the `sd_*` functions on it, and
runs four tests through the public API only. `stubs/` holds empty stand-ins for the
three Pico SDK headers that `fat32.c` includes.

```
gcc -O1 -std=gnu99 -Istubs -I<starter>/drivers fat32_probe.c <starter>/drivers/fat32.c -o fat32_probe
./fat32_probe              # char as on your PC (signed on x86)
gcc ... -funsigned-char    # char as on the Pico
./fat32_probe -v           # also dumps the raw directory entries around the delete
```

Results with `fat32.c` as it is today, and with `fat32-fixes.patch` applied:

| test | today, signed `char` (PC) | today, unsigned `char` (Pico) | with the patch, either |
|---|---|---|---|
| delete then create the same long name (6 cycles, 3 names) | FAIL: 18 entries listed, 15 of them deleted, with unprintable names | PASS | PASS |
| rename a long-named file (twice) | FAIL: 4 entries listed instead of 2 | PASS | PASS |
| listing after deleting a long name | FAIL: the deleted file is still listed | PASS | PASS |
| cost of one 4 KB write at 1 MB | FAIL (779 reads) | FAIL (779 reads) | FAIL (779 reads, problem 3) |

The unsigned-`char` PASS lines hide problem 1, because the out-of-bounds write does not
make those tests fail by itself. To see it, `fat32-instrument.patch` adds a check and a
message in front of the `lfn_to_str()` call (it changes nothing else). Apply it to a
scratch copy, build with `-funsigned-char`, and run the probe:

```
[instrumented] lfn_to_str would write 13 bytes at filename+468 (buffer is 256 bytes), seq=E5
```

This line appears for every deleted long-name part in every directory read. With the fix
applied it does not appear.

## What I tested and what I did not

- Tested: the probe above, on a RAM card, with GCC 16.2 (MSYS2, Windows), signed and
  unsigned `char`, with and without the patch. I also ran my own program's file copying
  on a simulated SD card with the unsigned-`char` build.
- Not tested: the patch on a real Pico with a real card, and other compilers or
  sanitizers. If you have AddressSanitizer on Linux, running the probe against the
  unpatched driver should report the stack overflow in `lfn_to_str`, but I have not run it.
- `unlink_entry()` only looks for long-name parts within the sector of the 8.3 entry, so
  a long name whose parts cross a sector boundary would leave live-looking parts behind.
  I have not hit this and did not change it, but it might deserve a look.
- The patch is against `drivers/fat32.c` as I have it; my copy carries one unrelated
  change in `get_next_free_cluster()` (the search hint), which does not overlap these hunks.

Files: `fat32_probe.c`, `stubs/`, `fat32-fixes.patch`, `fat32-instrument.patch`.

Thomas Dzubin
