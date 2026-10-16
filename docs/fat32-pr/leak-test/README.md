# fat32 cluster allocation test

A PC-only test of how `fat32_write()` in `drivers/fat32.c` allocates clusters. It needs no
Pico SDK: `fat32_leak_probe.c` formats a block of RAM as a FAT32 volume, implements the
`sd_*` functions on it, and runs two tests through the public API. `stubs/` has empty
stand-ins for the three Pico SDK headers that `fat32.c` includes.

```
gcc -O1 -std=gnu99 -Istubs -I../../drivers fat32_leak_probe.c ../../drivers/fat32.c -o fat32_leak_probe
./fat32_leak_probe
```

1. A file is written in pieces of exactly one cluster. The space the volume reports free
   must fall by one cluster per piece.
2. A write of zero bytes at a cluster-aligned end of file succeeds and allocates nothing.
3. A file is written in pieces of many sizes (some on cluster boundaries) and read back:
   the contents must be exactly what was written.

The exit status is the number of failed tests.
