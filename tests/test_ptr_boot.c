/* test_ptr_boot.c - host test: boot the paper tape loader and read the 2000E
 * disc bootstrap tape. Expected result (from the reference simulator):
 * halt with T = 102077 (HLT 77), P = 37716.
 *
 * Build from the project folder:
 *   gcc -std=c99 -Wall -Wextra -I. hp21xx.c hp21xx_fp.c dev_ptr.c \
 *       tests/test_ptr_boot.c -o test_ptr_boot
 * Run:  test_ptr_boot docs/kits/2000E/24286-60001_Rev-1438.abin [dumpfile [trace]]
 *
 * The optional trace file has one line per instruction executed, "P IR A B E O"
 * (octal, the state before the instruction), for comparing with the reference
 * simulator.
 */

#include <stdio.h>
#include <stdlib.h>
#include "hp21xx.h"
#include "dev_ptr.h"

static uint8_t *read_file(const char *name, uint32_t *length)
{
    FILE *f = fopen(name, "rb");
    uint8_t *buf;
    long n;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    *length = (uint32_t)n;
    return buf;
}

int main(int argc, char **argv)
{
    static dev_ptr_t ptr;
    uint32_t length;
    uint8_t *tape;
    int status;
    uint32_t i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s tape [dumpfile]\n", argv[0]);
        return 2;
    }
    tape = read_file(argv[1], &length);
    if (tape == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    hp_init(16384, 1);
    dev_ptr_init(&ptr, 013);
    dev_ptr_load_tape(&ptr, tape, length);
    hp.s = 0;
    hp.e = 1;                           /* the reference starts with E set */
    dev_ptr_boot(&ptr);
    if (argc > 3) {
        FILE *tr = fopen(argv[3], "w");
        uint32_t n;

        status = HP_RUN_COUNT;
        for (n = 0; n < 10000000 && status == HP_RUN_COUNT; n++) {
            hp_word ir = hp_read(hp.p);

            fprintf(tr, "%05o %06o %06o %06o %d %d\n", hp.p, ir, hp.a, hp.b,
                    hp.e, hp.o);
            status = hp_run(1);
        }
        fclose(tr);
    } else {
        status = hp_run(10000000);
    }

    printf("status %d, T %06o, P %05o, A %06o, B %06o, E %d, O %d, ticks %u\n",
           status, hp.t, hp.p, hp.a, hp.b, hp.e, hp.o, hp.ticks);

    if (argc > 2) {
        FILE *d = fopen(argv[2], "w");

        for (i = 0; i < hp.mem_size; i++)
            fprintf(d, "%05o: %06o\n", i, hp.mem[i]);
        fclose(d);
    }
    return status == HP_STOP_HALT && hp.t == 0102077 && hp.p == 037716 ? 0 : 1;
}
