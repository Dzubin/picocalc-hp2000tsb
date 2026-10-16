/* hostfile_test.c - host test of hostfile.c (typing a file into a terminal, capturing output).
 *
 * Author: Thomas Dzubin.
 *
 * A fake terminal stands in for TSB: it takes the characters the importer types, and when
 * it sees a carriage return it answers with a line feed (and with ??? if the line starts
 * with BAD). The test checks what was typed, the counts, and that a capture appends and
 * ends at an exact length. Build and run from the project folder (MSYS2 UCRT64):
 *
 *   gcc -O1 -std=gnu99 -I. tests/hostfile_test.c hostfile.c storage_stdio.c -o hostfile_test
 *   ./hostfile_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "storage.h"
#include "hostfile.h"

const char *plat_data_dir(void)
{
    return "./";
}

/* platform.h functions the module does not use but the header declares */
static int failures;

static void check(const char *what, int ok)
{
    printf("%-4s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

static void put_file(const char *name, const char *text, size_t length)
{
    FILE *f = fopen(name, "wb");

    fwrite(text, 1, length, f);
    fclose(f);
}

static long file_size(const char *name)
{
    FILE *f = fopen(name, "rb");
    long n;

    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

/* Run an import to its end against the fake terminal; return what was typed. */
static void run_import(hostfile_import_t *h, char *typed, size_t size)
{
    uint32_t now = 0;
    size_t n = 0;
    int c, guard = 0;

    typed[0] = 0;
    while (h->active && guard++ < 1000000) {
        now += 1000;
        c = hostfile_import_next(h, now);
        if (c < 0)
            continue;
        if (n + 2 < size) {
            typed[n++] = c == HOSTFILE_CR ? '|' : (char)c;      /* | marks a carriage return */
            typed[n] = 0;
        }
        if (c == HOSTFILE_CR) {                                 /* the terminal answers */
            const char *lf = "\r\n";
            const char *bad = "???\r\n";
            const char *p;

            /* the typed line is what the importer just sent; look back for BAD */
            const char *start = typed + n - 1;

            while (start > typed && start[-1] != '|')
                start--;
            for (p = (strncmp(start, "BAD", 3) == 0 ? bad : lf); *p; p++)
                hostfile_import_see(h, (uint8_t)*p);
            if (strncmp(start, "BAD", 3) == 0)
                for (p = lf; *p; p++)
                    hostfile_import_see(h, (uint8_t)*p);
        }
    }
}

int main(void)
{
    static const char program[] =
        "10 PRINT \"HELLO\"\r\n"
        "\r\n"                                   /* an empty line: skipped */
        "20\tFOR I=1 TO 3   \r\n"                 /* a tab, and spaces at the end */
        "BAD LINE THAT TSB REFUSES\n"            /* a line without CR */
        "30 END";                                /* no line end at the end of the file */
    static char typed[1024];
    hostfile_import_t imp;
    hostfile_capture_t cap;
    uint32_t got;
    char big[300];

    printf("hostfile tests\n\n");

    /* ---- import */
    put_file("hf_in.txt", program, sizeof program - 1);
    check("import opens the file", hostfile_import_open(&imp, "hf_in.txt", 0) == 0);
    check("it counts 4 lines (empty line skipped)", imp.total == 4);
    run_import(&imp, typed, sizeof typed);
    printf("     typed: %s\n", typed);
    check("it typed the cleaned lines, each followed by CR",
          strcmp(typed, "10 PRINT \"HELLO\"|20 FOR I=1 TO 3|BAD LINE THAT TSB REFUSES|30 END|") == 0);
    check("it sent 4 lines and counted 1 refused", imp.sent == 4 && imp.errors == 1);
    hostfile_import_close(&imp);

    put_file("hf_empty.txt", "\r\n  \r\n", 6);
    check("a file with no lines is refused", hostfile_import_open(&imp, "hf_empty.txt", 0) == 2);
    check("a missing file is refused", hostfile_import_open(&imp, "hf_nothere.txt", 0) == 1);

    memset(big, 'A', sizeof big);
    big[299] = '\n';
    put_file("hf_long.txt", big, sizeof big);
    hostfile_import_open(&imp, "hf_long.txt", 0);
    run_import(&imp, typed, sizeof typed);
    check("a line longer than 119 characters is cut to 119", strlen(typed) == 119 + 1);
    hostfile_import_close(&imp);

    /* ---- capture */
    remove("hf_out.txt");
    check("capture opens a new file", hostfile_capture_open(&cap, "hf_out.txt") == 0);
    {
        const char *t = "LIST\r\n10 PRINT 1\r\nREADY\r\n";
        const char *p;

        for (p = t; *p; p++)
            hostfile_capture_put(&cap, (uint8_t)*p);
        hostfile_capture_put(&cap, 0);          /* dropped */
        hostfile_capture_put(&cap, 7);          /* dropped (bell) */
    }
    got = hostfile_capture_close(&cap);
    check("the first capture ends at its exact length", got == 25 && file_size("hf_out.txt") == 25);

    hostfile_capture_open(&cap, "hf_out.txt");
    hostfile_capture_put(&cap, 'X');
    hostfile_capture_put(&cap, 'Y');
    got = hostfile_capture_close(&cap);
    check("a second capture appends", got == 27 && file_size("hf_out.txt") == 27);

    /* a file left padded with zeros (a power cut) is appended after its text */
    {
        char padded[200];

        memset(padded, 0, sizeof padded);
        memcpy(padded, "SAVED\r\n", 7);
        put_file("hf_pad.txt", padded, sizeof padded);
    }
    hostfile_capture_open(&cap, "hf_pad.txt");
    hostfile_capture_put(&cap, 'Z');
    got = hostfile_capture_close(&cap);
    check("a zero-padded file is appended after its text", got == 8 && file_size("hf_pad.txt") == 8);

    /* more than one buffer of output */
    hostfile_capture_open(&cap, "hf_big.txt");
    {
        unsigned i;

        remove("hf_big.txt");
        hostfile_capture_open(&cap, "hf_big.txt");
        for (i = 0; i < 2000; i++)
            hostfile_capture_put(&cap, (uint8_t)('A' + i % 26));
    }
    got = hostfile_capture_close(&cap);
    check("2000 characters (several buffers) are all there", got == 2000 && file_size("hf_big.txt") == 2000);

    printf("\n%d failure(s)\n", failures);
    return failures;
}
