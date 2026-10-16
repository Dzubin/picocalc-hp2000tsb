/* autotype.c - types answers on a terminal for the operator (see autotype.h).
 *
 * Author: Thomas Dzubin.
 */

#include <string.h>
#include "autotype.h"

void autotype_start(autotype_t *a, const autotype_step_t *steps, const char *first)
{
    a->steps = steps;
    a->step = 0;
    a->reply = first;
    a->length = 0;
    a->tail[0] = 0;
    a->finished = (steps == NULL || steps[0].expect == NULL) && first == NULL;
}

void autotype_see(autotype_t *a, uint8_t c)
{
    const autotype_step_t *s;

    if (a->steps == NULL || a->finished)
        return;
    if (a->length >= sizeof a->tail - 1) {      /* keep the most recent output */
        memmove(a->tail, a->tail + sizeof a->tail / 2, a->length - sizeof a->tail / 2);
        a->length -= (unsigned)(sizeof a->tail / 2);
    }
    a->tail[a->length++] = (char)c;
    a->tail[a->length] = 0;
    s = &a->steps[a->step];
    if (s->expect == NULL || (a->reply != NULL && *a->reply != 0))
        return;                                 /* still typing the last answer */
    if (strstr(a->tail, s->expect) != NULL) {
        a->reply = s->reply;
        if (s->reply == NULL)
            a->finished = 1;
        a->step++;
        a->length = 0;
        a->tail[0] = 0;
    }
}

int autotype_next(autotype_t *a)
{
    if (a->reply != NULL && *a->reply != 0)
        return (unsigned char)*a->reply++;
    a->reply = NULL;
    return -1;
}

int autotype_finished(const autotype_t *a)
{
    return a->finished;
}
