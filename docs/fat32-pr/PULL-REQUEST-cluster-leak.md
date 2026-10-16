`fat32_write()` leaves one cluster allocated and unreachable every time a write starts exactly at the end of a cluster. The space is wasted, nothing is corrupted: the file reads back correctly, but the FAT keeps a cluster in use that no file owns, and `fsck.fat` finds it as a lost chain.

## Cause

At the start of `fat32_write()` there are two places that allocate clusters:

1. The loop that walks the chain to the cluster of the write position. When the position is exactly at the end of the last cluster (a write that begins on a cluster boundary), the walk runs off the end of the chain, so it allocates a new cluster and links it (`allocate_and_link_cluster(cluster, ...)`).
2. The block that follows: it works out `current_clusters` from `file->file_size` (which does not include the cluster just added), finds the last cluster of the *old* chain, and allocates the clusters it thinks are missing, linking the new one after the old last cluster.

The second link overwrites the first, so the cluster allocated in step 1 is left in use with nothing pointing to it. Every cluster-aligned append loses one cluster.

## Effect

I noticed it on a real card: a 24 MB disc image and a 4 MB tape image, written with 8 KB writes on a card with 16 KB clusters, left 1723 clusters (28,229,632 bytes, the size of the two files) as lost chains, which `fsck.fat` then turned into `FSCK0000.REC` and so on. With 4 KB clusters and 4 KB writes the leak is as big as the file.

## Fix

Remove the walk. It did two things: it allocated (the bug), and it set `file->current_cluster`, which is overwritten after the allocation block anyway. The allocation block already allocates every cluster that is needed, and `seek_to_cluster()` is called after it to find the cluster to write to. Removing the walk also saves one pass along the chain for every write.

A write of zero bytes now returns `FAT32_OK` at once. Without that, a zero-length write at the end of a file that ends exactly on a cluster boundary would try to seek to a cluster that does not exist and fail (before this change the walk allocated an unneeded cluster, which hid that).

## Test: `host-tests/fat32-leak`

`fat32_leak_probe.c` formats a RAM block as FAT32 (4 KB clusters) and uses only the public API. On current `main` (a0fbba7), GCC 16.2, with and without this change:

| test | `main` | this PR |
|---|---|---|
| 8 writes of one cluster each: clusters used | FAIL: 15 used, 8 expected | PASS: 8 |
| a write of 0 bytes at the end of a file of exactly 2 clusters: succeeds, allocates nothing | FAIL: it allocates a cluster | PASS |
| 66,696 bytes in 16 pieces of many sizes (some on cluster boundaries), read back | PASS (the leak does not damage data) | PASS |

With an earlier version of this change (a `break` in the walk instead of removing it) I also wrote the same three files to a simulated card through the driver before and after and walked the FAT: 6909 clusters in use that belong to no file or folder before, 0 after, with every file size and chain length correct.

## What I tested and what I did not

- Tested: the host test above, original and changed; signed and unsigned `char`.
- The earlier `break` version ran in a vendored copy of the driver on a PicoCalc for a few hours of file copying without problems. The version in this PR (walk removed, zero-length guard) has been run only against the host test above.
- A seek past the end of a file followed by a write goes through the same allocation block, which already handled that case before; I did not add a test for it.

Thomas Dzubin

🤖 Generated with [Claude Code](https://claude.com/claude-code)
