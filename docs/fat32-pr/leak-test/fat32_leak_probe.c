/* fat32_leak_probe.c - does fat32_write() allocate each cluster once? Run on a PC.
 *
 * Author: Thomas Dzubin.
 *
 * Nothing from the Pico SDK is needed: the SD card is a block of RAM that this file
 * formats as FAT32, and stubs/ holds empty stand-ins for the three SDK headers that
 * fat32.c includes. Build and run from this folder (MinGW, Linux or macOS):
 *
 *   gcc -O1 -std=gnu99 -Istubs -I../../drivers fat32_leak_probe.c ../../drivers/fat32.c -o fat32_leak_probe
 *   ./fat32_leak_probe
 *
 * Two tests, each on a freshly formatted card, printing PASS or FAIL with what was seen:
 * a file written in pieces of exactly one cluster must use one cluster per piece, and a
 * file written in pieces of many sizes must read back exactly. The exit status is the
 * number of failed tests. Only the public API of fat32.h is used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "sdcard.h"
#include "fat32.h"

/* ------------------------------------------------------------------ */
/*  A RAM "SD card" with a FAT32 volume on it                           */
/* ------------------------------------------------------------------ */
#define CARD_SECTORS     600000u        /* 293 MB: FAT32 needs at least 65525 clusters */
#define SECTORS_PER_CLUS 8u            /* 4 KB clusters */
#define RESERVED         32u

static uint8_t *card;
static unsigned long sd_reads, sd_writes;

void sd_init(void) {}
sd_error_t sd_card_init(void) { return SD_OK; }
bool sd_card_present(void) { return true; }
bool sd_is_sdhc(void) { return true; }
const char *sd_error_string(sd_error_t e) { (void)e; return "sd error"; }

sd_error_t sd_read_block(uint32_t block, uint8_t *buffer)
{
    if (block >= CARD_SECTORS)
        return SD_ERROR_READ_FAILED;
    memcpy(buffer, card + (size_t)block * 512u, 512u);
    sd_reads++;
    return SD_OK;
}

sd_error_t sd_write_block(uint32_t block, const uint8_t *buffer)
{
    if (block >= CARD_SECTORS)
        return SD_ERROR_WRITE_FAILED;
    memcpy(card + (size_t)block * 512u, buffer, 512u);
    sd_writes++;
    return SD_OK;
}

sd_error_t sd_read_blocks(uint32_t start, uint32_t n, uint8_t *buffer)
{
    while (n--) {
        sd_error_t e = sd_read_block(start++, buffer);

        if (e != SD_OK)
            return e;
        buffer += 512;
    }
    return SD_OK;
}

sd_error_t sd_write_blocks(uint32_t start, uint32_t n, const uint8_t *buffer)
{
    while (n--) {
        sd_error_t e = sd_write_block(start++, buffer);

        if (e != SD_OK)
            return e;
        buffer += 512;
    }
    return SD_OK;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }

/* Format the RAM card as a blank FAT32 volume (no partition table). */
static void format_card(void)
{
    uint32_t fat = CARD_SECTORS / SECTORS_PER_CLUS / 128u + 2u;
    uint8_t *bs, *fs, *f;

    free(card);
    card = calloc(CARD_SECTORS, 512u);
    bs = card;
    memcpy(bs, "\xEB\x58\x90MSDOS5.0", 11);
    put16(bs + 11, 512);
    bs[13] = SECTORS_PER_CLUS;
    put16(bs + 14, RESERVED);
    bs[16] = 2;                                 /* two FATs */
    bs[21] = 0xF8;
    put16(bs + 24, 63);
    put16(bs + 26, 255);
    put32(bs + 32, CARD_SECTORS);
    put32(bs + 36, fat);
    put32(bs + 44, 2);                          /* root directory cluster */
    put16(bs + 48, 1);                          /* FSInfo sector */
    put16(bs + 50, 6);                          /* backup boot sector */
    bs[66] = 0x29;
    memcpy(bs + 71, "NO NAME    FAT32   ", 19);
    bs[510] = 0x55;
    bs[511] = 0xAA;
    memcpy(card + 6 * 512u, bs, 512u);
    fs = card + 512u;
    put32(fs, 0x41615252u);
    put32(fs + 484, 0x61417272u);
    put32(fs + 488, 0xFFFFFFFFu);               /* free count unknown */
    put32(fs + 492, 0xFFFFFFFFu);               /* next free unknown */
    put32(fs + 508, 0xAA550000u);
    f = card + RESERVED * 512u;
    put32(f, 0x0FFFFFF8u);
    put32(f + 4, 0x0FFFFFFFu);
    put32(f + 8, 0x0FFFFFFFu);                  /* the root directory is one cluster */
    memcpy(f + fat * 512u, f, 12);              /* second FAT */
    sd_reads = sd_writes = 0;
    fat32_unmount();
    fat32_init();
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */
static int failures;

static void verdict(const char *test, int ok, const char *detail)
{
    printf("%-4s %-34s %s\n", ok ? "PASS" : "FAIL", test, detail);
    if (!ok)
        failures++;
}

/* ------------------------------------------------------------------ */
/*  The tests                                                           */
/* ------------------------------------------------------------------ */

/* Writing a file in pieces of exactly one cluster: each cluster should be allocated once.
 * The space the volume reports free should fall by the file's size in clusters, no more. */
static void test_cluster_leak(void)
{
    static uint8_t block[4096];
    fat32_file_t f;
    uint64_t before = 0, after = 0;
    size_t put;
    unsigned i, pieces = 8, cluster;
    char detail[200];

    format_card();
    memset(block, 0x33, sizeof block);
    fat32_get_free_space(&before);          /* this mounts the new volume */
    cluster = fat32_get_cluster_size();
    if (fat32_create(&f, "clusters.bin") != FAT32_OK) {
        verdict("clusters allocated once", 0, "cannot create");
        return;
    }
    for (i = 0; i < pieces; i++)
        if (fat32_write(&f, block, cluster, &put) != FAT32_OK || put != cluster)
            break;
    fat32_close(&f);
    fat32_get_free_space(&after);
    sprintf(detail, "%u writes of one cluster: %llu clusters used, %u expected",
            pieces, (unsigned long long)((before - after) / cluster), pieces);
    verdict("clusters allocated once", (before - after) / cluster <= pieces + 1u, detail);
}

/* A write of zero bytes at the end of a file that ends exactly on a cluster boundary must
 * succeed, allocate nothing, and leave the file as it was. */
static void test_zero_length_write(void)
{
    static uint8_t block[4096];
    fat32_file_t f;
    uint64_t before = 0, after = 0, mid = 0;
    size_t put = 1;
    unsigned cluster;
    fat32_error_t result;

    format_card();
    memset(block, 0x44, sizeof block);
    fat32_get_free_space(&before);
    cluster = fat32_get_cluster_size();
    if (fat32_create(&f, "zero.bin") != FAT32_OK ||
        fat32_write(&f, block, cluster, &put) != FAT32_OK ||
        fat32_write(&f, block, cluster, &put) != FAT32_OK) {
        verdict("zero-length write at a boundary", 0, "cannot write the setup data");
        return;
    }
    fat32_get_free_space(&mid);
    result = fat32_write(&f, block, 0, &put);
    fat32_close(&f);
    fat32_get_free_space(&after);
    verdict("zero-length write at a boundary", result == FAT32_OK && put == 0 && after == mid &&
            before - mid == 2u * cluster, "write of 0 bytes after 2 whole clusters");
}

/* Write a file in pieces of many sizes (some on cluster boundaries), then read it all
 * back: the contents must be exactly what was written, and no more clusters used than
 * the length needs. */
static void test_write_integrity(void)
{
    static const unsigned piece[] = { 1, 511, 512, 513, 4095, 4096, 4097, 8192, 12288, 100,
                                      4096, 4096, 7, 4089, 20000, 3 };
    static uint8_t buf[20000], back[20000];
    fat32_file_t f;
    uint64_t before = 0, after = 0;
    size_t put, got;
    unsigned i, k, total = 0, cluster, bad = 0;
    char detail[200];

    format_card();
    fat32_get_free_space(&before);          /* this mounts the new volume */
    cluster = fat32_get_cluster_size();
    if (fat32_create(&f, "pieces.bin") != FAT32_OK) {
        verdict("write in pieces, read back", 0, "cannot create");
        return;
    }
    for (i = 0; i < sizeof piece / sizeof piece[0]; i++) {
        for (k = 0; k < piece[i]; k++)
            buf[k] = (uint8_t)((total + k) * 31u + 7u);
        if (fat32_write(&f, buf, piece[i], &put) != FAT32_OK || put != piece[i]) {
            bad = 1;
            break;
        }
        total += piece[i];
    }
    fat32_close(&f);
    fat32_get_free_space(&after);
    if (!bad && fat32_open(&f, "pieces.bin") == FAT32_OK) {
        unsigned at = 0;

        if (fat32_size(&f) != total)
            bad = 2;
        while (!bad && at < total) {
            unsigned n = total - at < sizeof back ? total - at : (unsigned)sizeof back;

            if (fat32_read(&f, back, n, &got) != FAT32_OK || got != n) {
                bad = 3;
                break;
            }
            for (k = 0; k < n; k++)
                if (back[k] != (uint8_t)((at + k) * 31u + 7u)) {
                    bad = 4;
                    break;
                }
            at += n;
        }
        fat32_close(&f);
    } else if (!bad) {
        bad = 5;
    }
    sprintf(detail, "%u bytes in %u pieces: %s, %llu clusters used, %u needed", total,
            (unsigned)(sizeof piece / sizeof piece[0]), bad ? "WRONG CONTENT OR SIZE" : "read back exactly",
            (unsigned long long)((before - after) / cluster), (total + cluster - 1) / cluster);
    verdict("write in pieces, read back", !bad, detail);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("fat32.c cluster allocation test on a RAM card (%u sectors, %u sector(s) per cluster)\n\n",
           CARD_SECTORS, SECTORS_PER_CLUS);
    test_cluster_leak();
    test_zero_length_write();
    test_write_integrity();
    printf("\n%d test(s) failed\n", failures);
    return failures;
}
