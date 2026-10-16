/* autotype.h - types answers on a terminal for the operator.
 *
 * Author: Thomas Dzubin.
 *
 * Portable. A script is a list of "when the terminal's output contains this text,
 * type that" steps. The program feeds every character the system prints to
 * autotype_see() and asks autotype_next() for the next key to type. Used for the
 * shutdown (SLEEP to tape) and for reloading the system from the SLEEP tape.
 */

#ifndef AUTOTYPE_H
#define AUTOTYPE_H

#include <stdint.h>

typedef struct {
    const char *expect;                 /* text to wait for (NULL ends the list) */
    const char *reply;                  /* text to type; NULL means finished */
} autotype_step_t;

typedef struct {
    const autotype_step_t *steps;
    unsigned    step;
    const char *reply;                  /* text being typed */
    char        tail[96];
    unsigned    length;
    int         finished;
} autotype_t;

/* Begin a script. first, if not NULL, is typed straight away. */
void autotype_start(autotype_t *a, const autotype_step_t *steps, const char *first);
void autotype_see(autotype_t *a, uint8_t c);
int  autotype_next(autotype_t *a);              /* a key to type, or -1 */
int  autotype_finished(const autotype_t *a);

#endif
